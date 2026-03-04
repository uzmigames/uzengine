/**
 * viewport-panel.ts — ViewportPanel class.
 *
 * Wraps a single vscode.WebviewPanel and binds it to a ViewportId assigned by
 * the engine. One ViewportPanel exists per open editor tab that streams a
 * rendered JPEG feed from the engine.
 *
 * Responsibilities:
 *  - Create the VSCode WebviewPanel with a full-page canvas + WebSocket client.
 *  - Track the viewport lifecycle state (inactive → active ↔ suspended → destroyed).
 *  - Notify the ViewportManager when the tab is shown, hidden, or closed so the
 *    manager can issue the appropriate IPC calls to the engine.
 *  - Route incoming JPEG frames (demultiplexed by ViewportManager) to the webview.
 *
 * The webview HTML renders frames via:
 *   1. Receive a binary message containing a 4-byte viewport_id prefix + JPEG body.
 *   2. Slice off the prefix (already done by the TypeScript side via `receiveFrame`).
 *   3. Decode the JPEG with `createImageBitmap` (off the main thread).
 *   4. Draw to a full-page `<canvas>` in an rAF loop at 60 fps.
 *
 * This class does NOT issue IPC calls directly — all IPC is delegated to the
 * ViewportManager so the singleton controls sequencing and error handling.
 */

import { randomBytes } from 'crypto';
import * as vscode from 'vscode';
import {
  VIEWPORT_ID_INVALID,
  ViewportConfig,
  ViewportId,
  ViewportState,
  ViewportType,
} from './viewport-types';

// Re-export for convenience — callers that only need ViewportPanel can import
// everything from this module.
export type { ViewportConfig, ViewportId, ViewportState, ViewportType };

// ---------------------------------------------------------------------------
// ViewportPanel
// ---------------------------------------------------------------------------

/**
 * Callback interface the ViewportPanel uses to communicate lifecycle events
 * upward to the ViewportManager. Defined as an interface (not a class import)
 * to avoid a circular module dependency.
 */
export interface ViewportPanelHost {
  /** Called when the panel's visibility changes. */
  onPanelVisibilityChanged(panel: ViewportPanel, visible: boolean): void;
  /** Called when the panel is disposed (tab closed by user). */
  onPanelDisposed(panel: ViewportPanel): void;
}

/**
 * One ViewportPanel per VSCode editor tab.
 *
 * Lifecycle: construct → `initialize()` → show → [suspend/resume] → dispose.
 */
export class ViewportPanel implements vscode.Disposable {
  /** The engine-assigned ID. VIEWPORT_ID_INVALID until `initialize()` succeeds. */
  private _viewportId: ViewportId = VIEWPORT_ID_INVALID;

  /** Current lifecycle state. */
  private _state: ViewportState = 'inactive';

  /** Whether `dispose()` has been called (prevents double-dispose). */
  private _disposed = false;

  /** The underlying VSCode webview panel. */
  private readonly _panel: vscode.WebviewPanel;

  /** Disposables registered during construction, cleaned up on dispose. */
  private readonly _disposables: vscode.Disposable[] = [];

  /** The nonce used for this panel's CSP. Generated once per panel instance. */
  private readonly _nonce: string;

  /** The WebSocket port to connect to (set after initialize()). */
  private _wsPort = 9002;

  // -------------------------------------------------------------------------
  // Construction
  // -------------------------------------------------------------------------

  /**
   * Creates a new VSCode WebviewPanel for a viewport.
   *
   * @param config  Viewport configuration (type, title, dimensions, fps, etc.)
   * @param host    Callback interface for lifecycle events (the ViewportManager).
   * @param context Extension context for resource scoping.
   */
  constructor(
    public readonly config: ViewportConfig,
    private readonly _host: ViewportPanelHost,
    context: vscode.ExtensionContext
  ) {
    this._nonce = randomBytes(16).toString('base64');

    // Create the webview panel. retainContextWhenHidden keeps the canvas
    // and WebSocket alive across tab switches so the stream resumes instantly.
    this._panel = vscode.window.createWebviewPanel(
      `uzengine.viewport.${config.type}`,
      config.title,
      vscode.ViewColumn.One,
      {
        enableScripts: true,
        retainContextWhenHidden: true,
        localResourceRoots: [],
      }
    );

    // Set a placeholder HTML immediately so the tab has visible content
    // while we wait for the IPC viewport.create response.
    this._panel.webview.html = this._buildLoadingHtml();

    // Tab visibility → suspend/resume.
    this._panel.onDidChangeViewState(
      (e: vscode.WebviewPanelOnDidChangeViewStateEvent) => {
        this._host.onPanelVisibilityChanged(this, e.webviewPanel.visible);
      },
      null,
      this._disposables
    );

    // Tab closed → destroy viewport + free GPU resources.
    this._panel.onDidDispose(
      () => {
        this._state = 'destroyed';
        this._host.onPanelDisposed(this);
      },
      null,
      this._disposables
    );

    // Register with context so VSCode disposes us on extension deactivation.
    context.subscriptions.push(this);
  }

  // -------------------------------------------------------------------------
  // Accessors
  // -------------------------------------------------------------------------

  /** The engine-assigned viewport ID. VIEWPORT_ID_INVALID before initialize(). */
  get viewportId(): ViewportId {
    return this._viewportId;
  }

  /** Current lifecycle state. */
  get state(): ViewportState {
    return this._state;
  }

  /** Whether the underlying VSCode panel is currently visible. */
  get isVisible(): boolean {
    return this._panel.visible;
  }

  /** Whether this panel has been disposed. */
  get isDisposed(): boolean {
    return this._disposed;
  }

  // -------------------------------------------------------------------------
  // Lifecycle — called by ViewportManager
  // -------------------------------------------------------------------------

  /**
   * Finalises the panel after the engine has assigned a ViewportId.
   * Replaces the loading HTML with the live JPEG canvas stream.
   *
   * @param viewportId  The ID returned by the `viewport.create` IPC call.
   * @param wsPort      WebSocket port returned by the `viewport.create` response.
   */
  initialize(viewportId: ViewportId, wsPort: number): void {
    if (this._disposed) { return; }
    this._viewportId = viewportId;
    this._wsPort = wsPort;
    // State stays 'inactive' until the engine sends the first frame / we
    // issue a viewport.setActive call. The webview itself connects the WS
    // immediately on load — the engine won't stream until setActive(true).
    this._panel.webview.html = this._buildStreamHtml();
  }

  /**
   * Transitions the panel state to 'active'. Called by ViewportManager after
   * the `viewport.setActive(true)` IPC call succeeds.
   */
  markActive(): void {
    if (this._disposed) { return; }
    this._state = 'active';
  }

  /**
   * Transitions the panel state to 'suspended'. Called by ViewportManager after
   * the `viewport.setActive(false)` IPC call succeeds.
   */
  markSuspended(): void {
    if (this._disposed) { return; }
    this._state = 'suspended';
  }

  /**
   * Routes a decoded JPEG frame to the webview canvas.
   * Called by ViewportManager.routeFrame() after demultiplexing the viewport_id prefix.
   *
   * The frame is sent to the webview as a Uint8Array (backed by its own
   * ArrayBuffer) so VSCode's postMessage can transfer it efficiently.
   * Previously this used `Array.from(jpeg)` which created an O(n) JS number
   * array — extremely slow for 100KB+ JPEG frames.
   *
   * @param jpeg  Raw JPEG bytes (viewport_id prefix already stripped).
   */
  receiveFrame(jpeg: Uint8Array): void {
    if (this._disposed || !this._panel.visible) { return; }
    // Slice to own the buffer (the input may be a view into a larger
    // ArrayBuffer that we cannot transfer).
    const owned = jpeg.slice();
    void this._panel.webview.postMessage({
      type: 'frame',
      data: owned,
    });
  }

  /**
   * Reveals the underlying VSCode panel (brings the tab to the foreground).
   */
  reveal(column?: vscode.ViewColumn): void {
    this._panel.reveal(column);
  }

  // -------------------------------------------------------------------------
  // Disposal
  // -------------------------------------------------------------------------

  /**
   * Disposes the VSCode WebviewPanel and all registered disposables.
   * Called by ViewportManager.dispose() or by VSCode on extension deactivation.
   * The `onDidDispose` callback fires as a side-effect, which in turn calls
   * `host.onPanelDisposed` — but we guard against double-work with `_disposed`.
   */
  dispose(): void {
    if (this._disposed) { return; }
    this._disposed = true;
    this._state = 'destroyed';

    this._panel.dispose();

    for (const d of this._disposables) {
      d.dispose();
    }
    this._disposables.length = 0;
  }

  // -------------------------------------------------------------------------
  // HTML generation
  // -------------------------------------------------------------------------

  /**
   * Returns minimal placeholder HTML shown while waiting for the IPC response.
   */
  private _buildLoadingHtml(): string {
    const nonce = this._nonce;
    const typeLabel = this.config.type;
    const title = this.config.title;

    return /* html */ `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta http-equiv="Content-Security-Policy"
        content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';">
  <title>${title}</title>
  <style>
    * { box-sizing: border-box; margin: 0; padding: 0; }
    body {
      background: #1e1e1e; color: #9cdcfe;
      font-family: 'Consolas', 'Courier New', monospace;
      display: flex; align-items: center; justify-content: center;
      height: 100vh; overflow: hidden; font-size: 14px;
    }
    .msg { text-align: center; }
    .label { color: #569cd6; font-weight: bold; margin-bottom: 8px; font-size: 16px; }
    .sub { color: #888; font-size: 12px; margin-top: 4px; }
  </style>
</head>
<body>
  <div class="msg">
    <div class="label">UzEngine</div>
    <div>${title}</div>
    <div class="sub">Initialising ${typeLabel} viewport\u2026</div>
  </div>
  <script nonce="${nonce}">/* placeholder */</script>
</body>
</html>`;
  }

  /**
   * Builds the full-page canvas HTML that receives JPEG frames from the
   * extension host and renders them at 60 fps.
   *
   * Frame delivery path:
   *   Engine → WebSocket → ViewportManager.routeFrame() → receiveFrame()
   *   → postMessage({ type: 'frame', data: number[] }) → webview script
   *   → Uint8Array → Blob → createImageBitmap → canvas.drawImage (in rAF)
   *
   * The webview also connects directly to the WebSocket for the JPEG stream.
   * The viewport_id prefix is parsed here so the webview can ignore frames
   * for other viewports (belt-and-suspenders — routing normally happens in TS).
   */
  private _buildStreamHtml(): string {
    return buildViewportStreamHtml({
      nonce: this._nonce,
      title: this.config.title,
      wsPort: this._wsPort,
      viewportId: this._viewportId,
    });
  }
}

// ---------------------------------------------------------------------------
// Exported pure-function HTML builder — reusable by other providers
// ---------------------------------------------------------------------------

/**
 * Builds the full-page canvas HTML for a JPEG WebSocket viewport stream.
 * Exported so other providers (e.g. UzSceneEditorProvider) can embed the
 * same viewport canvas without creating a full ViewportPanel instance.
 *
 * @param opts.nonce       CSP nonce for the inline script.
 * @param opts.title       Panel title shown in the toolbar.
 * @param opts.wsPort      WebSocket port (default 9002).
 * @param opts.viewportId  Engine viewport ID. Use 0 for legacy raw-JPEG mode
 *                         (engine sends no 4-byte prefix).
 */
export function buildViewportStreamHtml(opts: {
  nonce: string;
  title: string;
  wsPort: number;
  viewportId: number;
}): string {
  const { nonce, title, wsPort, viewportId } = opts;

  // Safe embedding of integer values — no string escaping needed.
  const safeWsPort = String(wsPort);
  const safeViewportId = String(viewportId);
  const safeTitle = title
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');

  return /* html */ `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta http-equiv="Content-Security-Policy"
        content="default-src 'none';
                 connect-src ws://localhost:* wss://localhost:*;
                 img-src blob: data:;
                 worker-src blob:;
                 script-src 'nonce-${nonce}';
                 style-src 'unsafe-inline';">
  <title>${safeTitle}</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    html, body {
      width: 100%;
      height: 100%;
    }

    body {
      background: #000;
      display: flex;
      flex-direction: column;
      overflow: hidden;
      font-family: 'Consolas', 'Courier New', monospace;
      color: #ccc;
    }

    #toolbar {
      display: flex;
      align-items: center;
      gap: 12px;
      padding: 4px 10px;
      background: #252526;
      border-bottom: 1px solid #3c3c3c;
      font-size: 11px;
      flex-shrink: 0;
      user-select: none;
    }

    #toolbar .label { color: #569cd6; font-weight: bold; }

    #status { font-weight: bold; transition: color 0.15s; }
    #status.connected    { color: #4ec9b0; }
    #status.disconnected { color: #f48771; }
    #status.connecting   { color: #dcdcaa; }
    #status.suspended    { color: #888; }

    #fps { color: #888; min-width: 55px; }
    #vp-id { color: #888; font-size: 10px; }

    /* ── Gizmo toolbar buttons ── */
    .tb-sep {
      width: 1px; height: 16px;
      background: #555; margin: 0 2px;
      flex-shrink: 0;
    }
    .tb {
      display: inline-flex; align-items: center; gap: 3px;
      padding: 2px 7px; border: 1px solid transparent;
      border-radius: 3px; background: transparent;
      color: #bbb; font-size: 11px; cursor: pointer;
      font-family: inherit; transition: all 0.12s;
      user-select: none; line-height: 1;
    }
    .tb:hover { background: #3c3c3c; color: #e0e0e0; }
    .tb.active {
      background: #264f78; border-color: #569cd6;
      color: #fff;
    }
    .tb .hk {
      font-size: 9px; color: #888;
      border: 1px solid #555; border-radius: 2px;
      padding: 0 3px; margin-left: 1px;
    }
    .tb.active .hk { color: #9cdcfe; border-color: #569cd6; }

    #canvas-container {
      flex: 1;
      position: relative;
      background: #000;
      overflow: hidden;
      min-height: 0;
    }

    #viewport-canvas {
      position: absolute;
      top: 0;
      left: 0;
      width: 100%;
      height: 100%;
      display: block;
      image-rendering: auto;
      outline: none;
    }

    #overlay {
      position: absolute;
      top: 50%; left: 50%;
      transform: translate(-50%, -50%);
      color: #555;
      font-size: 13px;
      text-align: center;
      pointer-events: none;
      transition: opacity 0.3s;
    }
    #overlay.hidden { opacity: 0; }
  </style>
</head>
<body>
  <div id="toolbar">
    <span class="label">UzEngine</span>
    <span id="status" class="connecting">Connecting\u2026</span>
    <span id="fps">-- fps</span>
    <span id="dbg-size" style="color:#666;font-size:10px;"></span>
    <span id="vp-id">vp#${safeViewportId}</span>
    <span class="tb-sep"></span>
    <button id="btn-translate" class="tb active" data-mode="translate" title="Translate (W)">\u229E Move <span class="hk">W</span></button>
    <button id="btn-rotate" class="tb" data-mode="rotate" title="Rotate (E)">\u27F3 Rotate <span class="hk">E</span></button>
    <button id="btn-scale" class="tb" data-mode="scale" title="Scale (R)">\u2922 Scale <span class="hk">R</span></button>
    <span class="tb-sep"></span>
    <button id="btn-world" class="tb active" data-space="world" title="World Space">World</button>
    <button id="btn-local" class="tb" data-space="local" title="Local Space">Local</button>
    <span class="tb-sep"></span>
    <button id="btn-focus" class="tb" title="Focus on selected (F)" style="background:#238636;color:#fff;border-color:#2ea043;">\u2295 Focus</button>
  </div>
  <div id="canvas-container">
    <div id="overlay">Waiting for stream\u2026</div>
    <canvas id="viewport-canvas" tabindex="0"></canvas>
  </div>

  <script nonce="${nonce}">
    (function () {
      'use strict';

      // -----------------------------------------------------------------------
      // Constants injected by the extension host
      // -----------------------------------------------------------------------
      var WS_PORT     = ${safeWsPort};
      var VIEWPORT_ID = ${safeViewportId};
      var WS_URL      = 'ws://localhost:' + WS_PORT;

      // -----------------------------------------------------------------------
      // DOM refs
      // -----------------------------------------------------------------------
      var canvas      = /** @type {HTMLCanvasElement} */ (document.getElementById('viewport-canvas'));
      var statusEl    = document.getElementById('status');
      var fpsEl       = document.getElementById('fps');
      var dbgSizeEl   = document.getElementById('dbg-size');
      var overlayEl   = document.getElementById('overlay');

      // -----------------------------------------------------------------------
      // State
      // -----------------------------------------------------------------------
      var ws             = /** @type {WebSocket|null} */ (null);
      var reconnectDelay = 1000;
      var MAX_RECONNECT  = 16000;
      var reconnectTimer = /** @type {ReturnType<typeof setTimeout>|null} */ (null);
      var isDisposed     = false;

      // -----------------------------------------------------------------------
      // Status helpers
      // -----------------------------------------------------------------------
      function setStatus(text, cls) {
        statusEl.textContent = text;
        statusEl.className = cls;
      }

      // -----------------------------------------------------------------------
      // Render Worker — decode + draw on a separate thread via OffscreenCanvas.
      // The main thread is fully free for input handling (camera controls).
      //
      // Architecture:
      //   Main thread:  WS/postMessage → transfer JPEG bytes → Worker
      //   Worker:       Decode JPEG → drawImage on OffscreenCanvas → postMessage fps
      //   Main thread:  Update FPS display from worker messages
      //
      // Uses an inline Blob URL worker because VSCode webview CSP blocks
      // external worker script URLs.
      // -----------------------------------------------------------------------
      var supportsOffscreen = typeof canvas.transferControlToOffscreen === 'function';
      var renderWorker = null;

      if (supportsOffscreen) {
        // -- Worker source (runs in a separate thread) --
        // Built as an array of strings joined with newlines because this code
        // lives inside an outer template literal (backticks can't nest).
        var workerSource = [
          'var offscreen=null,ctx=null,pendingBitmap=null,rafScheduled=false;',
          'var frameCount=0,lastFpsTick=Date.now();',
          'var hasImageDecoder=typeof ImageDecoder!=="undefined";',
          'function scheduleRender(){',
          '  if(rafScheduled)return;rafScheduled=true;',
          '  requestAnimationFrame(renderFrame);',
          '}',
          'function renderFrame(){',
          '  rafScheduled=false;',
          '  if(!pendingBitmap)return;',
          '  var bmp=pendingBitmap;pendingBitmap=null;',
          '  var bmpW=bmp.displayWidth||bmp.width;',
          '  var bmpH=bmp.displayHeight||bmp.height;',
          '  if(offscreen.width!==bmpW||offscreen.height!==bmpH){',
          '    offscreen.width=bmpW;offscreen.height=bmpH;',
          '  }',
          '  ctx.drawImage(bmp,0,0);bmp.close();',
          '  frameCount++;',
          '  var now=Date.now();',
          '  if(now-lastFpsTick>=1000){',
          '    self.postMessage({type:"fps",fps:Math.round(frameCount*1000/(now-lastFpsTick))});',
          '    frameCount=0;lastFpsTick=now;',
          '  }',
          '}',
          'function decodeJpeg(data){',
          '  var bytes=new Uint8Array(data);',
          '  if(hasImageDecoder){',
          '    var d=new ImageDecoder({type:"image/jpeg",data:bytes});',
          '    d.decode().then(function(r){onDecoded(r.image);d.close();}).catch(function(){',
          '      d.close();hasImageDecoder=false;decodeFB(bytes);',
          '    });',
          '  }else{decodeFB(bytes);}',
          '}',
          'function decodeFB(bytes){',
          '  createImageBitmap(new Blob([bytes],{type:"image/jpeg"})).then(function(b){',
          '    onDecoded(b);',
          '  }).catch(function(){});',
          '}',
          'function onDecoded(img){',
          '  self.postMessage({type:"firstFrame"});',
          '  if(pendingBitmap)pendingBitmap.close();',
          '  pendingBitmap=img;scheduleRender();',
          '}',
          'self.onmessage=function(e){',
          '  var m=e.data;',
          '  if(m.type==="init"){offscreen=m.canvas;ctx=offscreen.getContext("2d");}',
          '  else if(m.type==="jpeg"){decodeJpeg(m.data);}',
          '};'
        ].join('\\n');

        var wBlob = new Blob([workerSource], { type: 'application/javascript' });
        var blobUrl = URL.createObjectURL(wBlob);
        renderWorker = new Worker(blobUrl);
        URL.revokeObjectURL(blobUrl);

        // Transfer canvas control to the worker.
        var offscreen = canvas.transferControlToOffscreen();
        renderWorker.postMessage({ type: 'init', canvas: offscreen }, [offscreen]);

        // Listen for worker messages (FPS updates, first frame).
        var firstFrameReceived = false;
        renderWorker.onmessage = function(e) {
          var msg = e.data;
          if (msg.type === 'fps') {
            fpsEl.textContent = msg.fps + ' fps';
          } else if (msg.type === 'firstFrame' && !firstFrameReceived) {
            firstFrameReceived = true;
            if (overlayEl) overlayEl.classList.add('hidden');
          }
        };
      }

      // -----------------------------------------------------------------------
      // Fallback: main-thread decode+draw (if OffscreenCanvas unavailable).
      // -----------------------------------------------------------------------
      var ctx2d = supportsOffscreen ? null : canvas.getContext('2d');
      var pendingBitmap = null;
      var rafScheduled = false;
      var frameCount = 0;
      var lastFpsTick = Date.now();

      function scheduleRender() {
        if (rafScheduled) return;
        rafScheduled = true;
        requestAnimationFrame(renderFrame);
      }

      function renderFrame() {
        rafScheduled = false;
        if (!pendingBitmap) return;
        var bmp = pendingBitmap;
        pendingBitmap = null;
        var bmpW = bmp.displayWidth || bmp.width;
        var bmpH = bmp.displayHeight || bmp.height;
        if (canvas.width !== bmpW || canvas.height !== bmpH) {
          canvas.width = bmpW;
          canvas.height = bmpH;
        }
        ctx2d.drawImage(bmp, 0, 0);
        bmp.close();
        frameCount++;
        var now = Date.now();
        if (now - lastFpsTick >= 1000) {
          fpsEl.textContent = Math.round(frameCount * 1000 / (now - lastFpsTick)) + ' fps';
          frameCount = 0;
          lastFpsTick = now;
        }
      }

      var hasImageDecoder = supportsOffscreen ? false : (typeof ImageDecoder !== 'undefined');

      function decodeJpegFallback(jpegBytes) {
        if (hasImageDecoder) {
          var decoder = new ImageDecoder({ type: 'image/jpeg', data: jpegBytes });
          decoder.decode().then(function(result) {
            onFrameDecoded(result.image);
            decoder.close();
          }).catch(function() {
            decoder.close();
            hasImageDecoder = false;
            decodeViaBlob(jpegBytes);
          });
        } else {
          decodeViaBlob(jpegBytes);
        }
      }

      function decodeViaBlob(jpegBytes) {
        createImageBitmap(new Blob([jpegBytes], { type: 'image/jpeg' })).then(function(bmp) {
          onFrameDecoded(bmp);
        }).catch(function() {});
      }

      function onFrameDecoded(imageSource) {
        if (overlayEl && !overlayEl.classList.contains('hidden')) {
          overlayEl.classList.add('hidden');
        }
        if (pendingBitmap) pendingBitmap.close();
        pendingBitmap = imageSource;
        scheduleRender();
      }

      // -----------------------------------------------------------------------
      // Unified frame dispatch — route JPEG bytes to worker or fallback.
      // -----------------------------------------------------------------------
      function handleJpegFrame(jpegBytes) {
        if (renderWorker) {
          // Transfer the ArrayBuffer to the worker (zero-copy).
          var buf = jpegBytes.buffer.slice(
            jpegBytes.byteOffset,
            jpegBytes.byteOffset + jpegBytes.byteLength
          );
          renderWorker.postMessage({ type: 'jpeg', data: buf }, [buf]);
        } else {
          decodeJpegFallback(jpegBytes);
        }
      }

      // -----------------------------------------------------------------------
      // Gizmo toolbar — mode & space buttons
      // -----------------------------------------------------------------------
      var vscodeApi = (typeof acquireVsCodeApi === 'function') ? acquireVsCodeApi() : null;

      var modeButtons = {
        translate: document.getElementById('btn-translate'),
        rotate:    document.getElementById('btn-rotate'),
        scale:     document.getElementById('btn-scale')
      };
      var spaceButtons = {
        world: document.getElementById('btn-world'),
        local: document.getElementById('btn-local')
      };
      var currentGizmoMode = 'translate';
      var currentGizmoSpace = 'world';

      // Gizmo interaction state
      var selectedNodeId = null;
      var gizmoDragging = false;
      var gizmoActiveAxis = null;
      var gizmoHoverAxis = null;
      var justFinishedDrag = false;
      var lastDragSendTime = 0;
      var lastHoverSendTime = 0;

      function setGizmoMode(mode) {
        if (mode === currentGizmoMode) return;
        currentGizmoMode = mode;
        for (var k in modeButtons) {
          if (modeButtons[k]) modeButtons[k].classList.toggle('active', k === mode);
        }
        // Notify the extension host
        if (vscodeApi) vscodeApi.postMessage({ type: 'gizmo_mode', mode: mode });
        // Also send via WS for engine-direct IPC
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify({ type: 'gizmo_mode', mode: mode }));
        }
      }

      function setGizmoSpace(space) {
        if (space === currentGizmoSpace) return;
        currentGizmoSpace = space;
        for (var k in spaceButtons) {
          if (spaceButtons[k]) spaceButtons[k].classList.toggle('active', k === space);
        }
        if (vscodeApi) vscodeApi.postMessage({ type: 'gizmo_space', space: space });
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify({ type: 'gizmo_space', space: space }));
        }
      }

      // Button click handlers
      for (var m in modeButtons) {
        (function(mode) {
          if (modeButtons[mode]) {
            modeButtons[mode].addEventListener('click', function() { setGizmoMode(mode); });
          }
        })(m);
      }
      for (var s in spaceButtons) {
        (function(space) {
          if (spaceButtons[space]) {
            spaceButtons[space].addEventListener('click', function() { setGizmoSpace(space); });
          }
        })(s);
      }

      // Focus on selected node
      function focusOnSelected() {
        if (!ws || ws.readyState !== WebSocket.OPEN) return;
        if (!selectedNodeId) return;
        ws.send(JSON.stringify({ type: 'editor.focusNode', nodeId: selectedNodeId }));
      }
      var btnFocus = document.getElementById('btn-focus');
      if (btnFocus) btnFocus.addEventListener('click', focusOnSelected);

      // -----------------------------------------------------------------------
      // Frame ingestion from the extension host (postMessage path)
      // -----------------------------------------------------------------------
      window.addEventListener('message', function (event) {
        var msg = event.data;
        if (!msg || typeof msg !== 'object') { return; }

        if (msg.type === 'frame' && msg.data) {
          var bytes = msg.data instanceof Uint8Array
            ? msg.data
            : new Uint8Array(msg.data);
          handleJpegFrame(bytes);

        } else if (msg.type === 'suspend') {
          setStatus('Suspended', 'suspended');
          fpsEl.textContent = '-- fps';

        } else if (msg.type === 'resume') {
          setStatus('Active', 'connected');

        } else if (msg.type === 'set_gizmo_mode' && msg.mode) {
          // Sync from extension host (e.g. VSCode keybinding)
          currentGizmoMode = '';
          setGizmoMode(msg.mode);

        } else if (msg.type === 'set_gizmo_space' && msg.space) {
          currentGizmoSpace = '';
          setGizmoSpace(msg.space);
        }
      });

      // -----------------------------------------------------------------------
      // WebSocket connection (direct stream — extension host also routes frames
      // via postMessage for the common single-WS path; both paths are supported).
      // -----------------------------------------------------------------------
      function connect() {
        if (isDisposed) { return; }
        setStatus('Connecting\\u2026', 'connecting');

        try {
          ws = new WebSocket(WS_URL);
        } catch (err) {
          console.error('[UzEngine] WS construction error:', err);
          scheduleReconnect();
          return;
        }

        ws.binaryType = 'arraybuffer';

        ws.addEventListener('open', function () {
          setStatus('Connected', 'connected');
          reconnectDelay = 1000;
          lastSentWidth = 0;
          lastSentHeight = 0;
        });

        ws.addEventListener('message', function (e) {
          // Handle text messages (JSON responses from engine)
          if (typeof e.data === 'string') {
            try {
              var parsed = JSON.parse(e.data);
              if (parsed.type === 'pickResult') {
                selectedNodeId = parsed.nodeId || null;
                if (vscodeApi) vscodeApi.postMessage({ type: 'pickResult', nodeId: parsed.nodeId });
              } else if (parsed.type === 'selectionChanged') {
                selectedNodeId = parsed.nodeId || null;
              } else if (parsed.type === 'gizmoState') {
                if (parsed.axis != null) {
                  gizmoDragging = true;
                  gizmoActiveAxis = parsed.axis;
                } else {
                  // No axis hit — if we were trying to begin drag, fall back to pick
                  if (!gizmoDragging && selectedNodeId) {
                    // beginDrag returned no axis — do a pickNode instead
                    var fallbackCoords = getCanvasCoords(lastMouseEvent);
                    if (fallbackCoords) {
                      ws.send(JSON.stringify({
                        type: 'editor.pickNode',
                        screen_x: fallbackCoords.sx, screen_y: fallbackCoords.sy
                      }));
                    }
                  }
                  gizmoDragging = false;
                  gizmoActiveAxis = null;
                }
              } else if (parsed.type === 'transformUpdate') {
                // Transform applied by gizmo drag — could update properties panel
                if (vscodeApi) vscodeApi.postMessage({ type: 'transformUpdate', data: parsed });
              } else if (parsed.type === 'cameraFocused') {
                console.log('[UzEngine] Camera focused on node', parsed.nodeId);
              } else if (parsed.type === 'gizmoHover') {
                gizmoHoverAxis = parsed.axis || null;
                if (canvasContainer) {
                  canvasContainer.style.cursor = gizmoHoverAxis ? 'grab' : 'crosshair';
                }
              }
            } catch (ex) { console.warn('[UzEngine] JSON parse error:', ex); }
            return;
          }

          if (!(e.data instanceof ArrayBuffer)) { return; }

          var jpegBytes;
          if (VIEWPORT_ID === 0) {
            if (e.data.byteLength < 2) { return; }
            jpegBytes = new Uint8Array(e.data);
          } else {
            if (e.data.byteLength < 4) { return; }
            var view = new DataView(e.data);
            var vpId = view.getUint32(0, true);
            if (vpId !== VIEWPORT_ID) { return; }
            jpegBytes = new Uint8Array(e.data, 4);
          }

          handleJpegFrame(jpegBytes);
        });

        ws.addEventListener('close', function (e) {
          console.log('[UzEngine] WS closed. Code:', e.code, 'Reason:', e.reason);
          setStatus('Reconnecting\u2026', 'disconnected');
          scheduleReconnect();
        });

        ws.addEventListener('error', function () {
          // The close event fires after error — reconnect handled there.
        });
      }

      function scheduleReconnect() {
        if (isDisposed) { return; }
        var delay = reconnectDelay;
        reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT);
        reconnectTimer = setTimeout(function () {
          reconnectTimer = null;
          connect();
        }, delay);
      }

      // -----------------------------------------------------------------------
      // Start
      // -----------------------------------------------------------------------
      connect();

      // -----------------------------------------------------------------------
      // Camera input capture (UE5-style fly camera)
      // -----------------------------------------------------------------------
      var keys = { w:0, a:0, s:0, d:0, q:0, e:0 };
      var shiftDown = false;
      var rightMouseDown = false;
      var mouseDeltaX = 0;
      var mouseDeltaY = 0;
      var scrollDelta = 0;

      // Track canvas focus — gizmo hotkeys only fire when the viewport
      // canvas is focused, so they don't intercept typing in VSCode's
      // chat, search bar, or other input fields.
      var canvasFocused = false;
      canvas.addEventListener('focus', function() { canvasFocused = true; });
      canvas.addEventListener('blur',  function() { canvasFocused = false; });

      // Gizmo hotkey map (only active when canvas is focused and NOT in right-click camera mode)
      var gizmoHotkeys = { w: 'translate', e: 'rotate', r: 'scale' };

      document.addEventListener('keydown', function(e) {
        var k = e.key.toLowerCase();
        if (e.key === 'Shift') { shiftDown = true; }

        if (rightMouseDown) {
          // Camera mode — WASD/QE for movement
          if (k in keys) { keys[k] = 1; e.preventDefault(); }
        } else if (canvasFocused) {
          // Editor mode — W/E/R for gizmo mode switching, F for focus
          if (k in gizmoHotkeys) {
            setGizmoMode(gizmoHotkeys[k]);
            e.preventDefault();
          }
          if (k === 'f') {
            focusOnSelected();
            e.preventDefault();
          }
        }
      });
      document.addEventListener('keyup', function(e) {
        var k = e.key.toLowerCase();
        if (k in keys) { keys[k] = 0; }
        if (e.key === 'Shift') { shiftDown = false; }
      });

      var canvasContainer = document.getElementById('canvas-container');
      var lastMouseEvent = null;

      // Helper: convert mouse event to engine screen coordinates
      function getCanvasCoords(e) {
        if (!e) return null;
        var rect = canvasContainer.getBoundingClientRect();
        var scaleX = canvas.width / rect.width;
        var scaleY = canvas.height / rect.height;
        return {
          sx: Math.round((e.clientX - rect.left) * scaleX),
          sy: Math.round((e.clientY - rect.top) * scaleY)
        };
      }

      canvasContainer.addEventListener('mousedown', function(e) {
        if (e.button === 2) {
          rightMouseDown = true;
          canvas.focus();
          canvas.requestPointerLock();
          e.preventDefault();
          return;
        }

        // Left click — try gizmo drag first, then fall back to pick
        if (e.button === 0) {
          canvas.focus();
          lastMouseEvent = e;
          var coords = getCanvasCoords(e);
          if (!coords || !ws || ws.readyState !== WebSocket.OPEN) return;

          if (selectedNodeId) {
            // Try to grab a gizmo axis
            ws.send(JSON.stringify({
              type: 'editor.gizmoBeginDrag',
              screen_x: coords.sx, screen_y: coords.sy
            }));
          } else {
            // No node selected — pick
            ws.send(JSON.stringify({
              type: 'editor.pickNode',
              screen_x: coords.sx, screen_y: coords.sy
            }));
          }
        }
      });

      document.addEventListener('mouseup', function(e) {
        if (e.button === 2) {
          rightMouseDown = false;
          if (document.pointerLockElement === canvas) {
            document.exitPointerLock();
          }
          keys = { w:0, a:0, s:0, d:0, q:0, e:0 };
          shiftDown = false;
        }

        // End gizmo drag on left mouse up
        if (e.button === 0 && gizmoDragging) {
          gizmoDragging = false;
          gizmoActiveAxis = null;
          justFinishedDrag = true;
          setTimeout(function() { justFinishedDrag = false; }, 50);
          if (ws && ws.readyState === WebSocket.OPEN) {
            ws.send(JSON.stringify({ type: 'editor.gizmoEndDrag' }));
          }
          canvasContainer.style.cursor = 'crosshair';
        }
      });

      canvasContainer.addEventListener('contextmenu', function(e) {
        e.preventDefault();
      });

      document.addEventListener('mousemove', function(e) {
        if (rightMouseDown && document.pointerLockElement === canvas) {
          mouseDeltaX += e.movementX;
          mouseDeltaY += e.movementY;
          return;
        }

        // Gizmo drag
        if (gizmoDragging && ws && ws.readyState === WebSocket.OPEN) {
          var now = performance.now();
          if (now - lastDragSendTime < 16) return;
          lastDragSendTime = now;
          var coords = getCanvasCoords(e);
          if (coords) {
            ws.send(JSON.stringify({
              type: 'editor.gizmoDrag',
              screen_x: coords.sx, screen_y: coords.sy
            }));
          }
          return;
        }

        // Gizmo hover (throttled to ~15fps)
        if (!gizmoDragging && selectedNodeId && ws && ws.readyState === WebSocket.OPEN) {
          var now2 = performance.now();
          if (now2 - lastHoverSendTime < 66) return;
          lastHoverSendTime = now2;
          var hcoords = getCanvasCoords(e);
          if (hcoords) {
            ws.send(JSON.stringify({
              type: 'editor.gizmoHover',
              screen_x: hcoords.sx, screen_y: hcoords.sy
            }));
          }
        }
      });

      canvasContainer.addEventListener('wheel', function(e) {
        var delta = -e.deltaY;
        if (e.deltaMode === 0) { delta /= 100; }
        else if (e.deltaMode === 1) { delta /= 3; }
        scrollDelta += delta;
        e.preventDefault();
      }, { passive: false });

      // -----------------------------------------------------------------------
      // Left-click to select (only when NOT finishing a gizmo drag)
      // -----------------------------------------------------------------------
      canvasContainer.addEventListener('click', function(e) {
        if (e.button !== 0) return;
        if (gizmoDragging) return;
        if (justFinishedDrag) return;

        canvas.focus();
        var coords = getCanvasCoords(e);
        if (!coords || !ws || ws.readyState !== WebSocket.OPEN) return;

        // Only send pickNode if no node is selected (gizmoBeginDrag handles the selected case)
        if (!selectedNodeId) {
          ws.send(JSON.stringify({
            type: 'editor.pickNode',
            screen_x: coords.sx, screen_y: coords.sy
          }));
        }
      });

      document.addEventListener('pointerlockchange', function() {
        if (document.pointerLockElement !== canvas) {
          rightMouseDown = false;
          keys = { w:0, a:0, s:0, d:0, q:0, e:0 };
          shiftDown = false;
        }
      });

      // Send input state at ~30fps via WS text messages.
      setInterval(function() {
        if (!ws || ws.readyState !== WebSocket.OPEN) return;
        var hasInput = rightMouseDown || mouseDeltaX !== 0 || mouseDeltaY !== 0 || scrollDelta !== 0;
        if (!hasInput && !keys.w && !keys.s && !keys.a && !keys.d && !keys.q && !keys.e) return;

        ws.send(JSON.stringify({
          type: 'camera_input',
          w: keys.w, s: keys.s, a: keys.a, d: keys.d, q: keys.q, e: keys.e,
          dx: mouseDeltaX, dy: mouseDeltaY,
          rmb: rightMouseDown, shift: shiftDown,
          scroll: scrollDelta
        }));

        mouseDeltaX = 0;
        mouseDeltaY = 0;
        scrollDelta = 0;
      }, 33);

      // -----------------------------------------------------------------------
      // Viewport resize — notify engine of actual panel dimensions
      // -----------------------------------------------------------------------
      var lastSentWidth = 0;
      var lastSentHeight = 0;

      function sendResize(w, h) {
        // Round to even numbers (GPU alignment).
        w = Math.max(64, w & ~1);
        h = Math.max(64, h & ~1);
        if (w === lastSentWidth && h === lastSentHeight) return;
        // Only mark as sent AFTER successful WS send.
        // If WS is closed, the next periodic check will retry.
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify({ type: 'viewport_resize', width: w, height: h }));
          lastSentWidth = w;
          lastSentHeight = h;
        }
      }

      var resizeTimer = /** @type {ReturnType<typeof setTimeout>|null} */ (null);

      function checkResize() {
        var cr = canvasContainer.getBoundingClientRect();
        if (cr.width > 0 && cr.height > 0) {
          var dpr = window.devicePixelRatio || 1;
          var pw = Math.round(cr.width * dpr);
          var ph = Math.round(cr.height * dpr);
          sendResize(pw, ph);
          // Debug: show css size, dpr, physical size sent, canvas buffer size.
          dbgSizeEl.textContent =
            'css:' + Math.round(cr.width) + 'x' + Math.round(cr.height) +
            ' dpr:' + dpr.toFixed(2) +
            ' sent:' + lastSentWidth + 'x' + lastSentHeight +
            ' buf:' + canvas.width + 'x' + canvas.height;
        }
      }

      // Debounced resize — wait 500ms after the LAST size change before
      // sending to the engine.  The DX12 resize is very expensive (full GPU
      // drain + buffer recreation), so we only fire when the user stops
      // dragging.  CSS stretches the canvas during the drag for visual feedback.
      function scheduleResize() {
        if (resizeTimer) { clearTimeout(resizeTimer); }
        resizeTimer = setTimeout(checkResize, 500);
      }

      // ResizeObserver fires on every layout change — debounced.
      var resizeObserver = new ResizeObserver(function() { scheduleResize(); });
      resizeObserver.observe(canvasContainer);

      // Fallback: periodic check every 3s (covers edge cases where
      // ResizeObserver doesn't fire in VSCode webviews).
      setInterval(function() { scheduleResize(); }, 3000);

    }());
  </script>
</body>
</html>`;
}
