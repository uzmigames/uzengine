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
/** Data emitted when the viewport picks a node. */
export interface PickEvent {
  /** The picked node ID, or null if empty space was clicked. */
  readonly nodeId: number | null;
  /** Whether Ctrl was held during the pick. */
  readonly ctrlKey: boolean;
}

/** Data emitted when a box-select completes. */
export interface BoxPickEvent {
  /** Node IDs within the box region. */
  readonly nodeIds: number[];
  /** Whether Ctrl was held during the box drag. */
  readonly ctrlKey: boolean;
}

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

  // -- Selection events (extension host ← webview) --
  private readonly _onNodePicked = new vscode.EventEmitter<PickEvent>();
  readonly onNodePicked: vscode.Event<PickEvent> = this._onNodePicked.event;

  private readonly _onBoxPicked = new vscode.EventEmitter<BoxPickEvent>();
  readonly onBoxPicked: vscode.Event<BoxPickEvent> = this._onBoxPicked.event;

  /** Fired when the user clicks the viewport while in asset drop mode. */
  private readonly _onAssetDropped = new vscode.EventEmitter<{ x: number; y: number }>();
  readonly onAssetDropped: vscode.Event<{ x: number; y: number }> = this._onAssetDropped.event;

  /** Fired when the user presses Escape during asset drop mode. */
  private readonly _onDragCancelled = new vscode.EventEmitter<void>();
  readonly onDragCancelled: vscode.Event<void> = this._onDragCancelled.event;

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

    // Webview → extension host message handler (pick results, etc.)
    this._panel.webview.onDidReceiveMessage(
      (msg: unknown) => {
        const m = msg as Record<string, unknown>;
        if (m.type === 'pickResult') {
          this._onNodePicked.fire({
            nodeId: typeof m.nodeId === 'number' ? m.nodeId : null,
            ctrlKey: !!m.ctrlKey,
          });
        } else if (m.type === 'boxPickResult') {
          this._onBoxPicked.fire({
            nodeIds: Array.isArray(m.nodeIds) ? m.nodeIds as number[] : [],
            ctrlKey: !!m.ctrlKey,
          });
        } else if (m.type === 'assetDropped') {
          this._onAssetDropped.fire({
            x: typeof m.x === 'number' ? m.x : 0,
            y: typeof m.y === 'number' ? m.y : 0,
          });
        } else if (m.type === 'dragCancelled') {
          this._onDragCancelled.fire();
        }
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

  /**
   * Notify the webview of asset drag state changes.
   * When `active` is true, the viewport shows a green border and copy cursor.
   */
  postDragState(active: boolean): void {
    if (this._disposed) { return; }
    void this._panel.webview.postMessage({
      type: active ? 'dragStarted' : 'dragEnded',
    });
  }

  /**
   * Switch the WebSocket port used by the viewport stream.
   * Closes the current connection and reconnects to the new port.
   * Used when entering/exiting play mode.
   */
  switchPort(newPort: number): void {
    if (this._disposed) { return; }
    this._wsPort = newPort;
    void this._panel.webview.postMessage({
      type: 'switchPort',
      port: newPort,
    });
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

    this._onNodePicked.dispose();
    this._onBoxPicked.dispose();
    this._onAssetDropped.dispose();
    this._onDragCancelled.dispose();
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

    /* Drop-mode indicator (asset drag from Asset Browser) */
    #canvas-container.drop-active {
      outline: 2px solid #4caf50;
      outline-offset: -2px;
    }
    #canvas-container.drop-active::after {
      content: 'Drop to place asset';
      position: absolute;
      bottom: 8px;
      left: 50%;
      transform: translateX(-50%);
      color: #4caf50;
      font-size: 12px;
      pointer-events: none;
      z-index: 100;
    }
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
    <span id="snap-indicator" style="display:none;color:#f0c040;font-size:10px;margin-left:6px;">[Snap: 0.25m]</span>
    <span class="tb-sep"></span>
    <span id="mode-badge" class="tb active" style="pointer-events:none;font-size:10px;padding:1px 8px;">Editor</span>
  </div>
  <div id="canvas-container">
    <div id="overlay">Waiting for stream\u2026</div>
    <canvas id="viewport-canvas" tabindex="0"></canvas>
    <div id="box-select-rect" style="display:none;position:absolute;border:1px dashed rgba(255,255,255,0.8);background:rgba(255,255,255,0.08);pointer-events:none;box-sizing:border-box;"></div>
    <div id="dbg-hittest" style="position:absolute;top:2px;right:4px;color:#0f0;font-size:10px;pointer-events:none;font-family:monospace;z-index:999;white-space:pre;"></div>
    <div id="hover-label" style="display:none;position:absolute;background:rgba(30,30,30,0.85);color:#ddd;font-size:11px;padding:2px 8px;border-radius:3px;pointer-events:none;z-index:100;font-family:inherit;white-space:nowrap;border:1px solid #555;"></div>
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
      var canvasCont  = document.getElementById('canvas-container');
      var statusEl    = document.getElementById('status');
      var fpsEl       = document.getElementById('fps');
      var dbgSizeEl   = document.getElementById('dbg-size');
      var overlayEl   = document.getElementById('overlay');
      var modeBadgeEl = document.getElementById('mode-badge');
      var hoverLabel  = document.getElementById('hover-label');

      // -----------------------------------------------------------------------
      // State
      // -----------------------------------------------------------------------
      var ws             = /** @type {WebSocket|null} */ (null);
      var reconnectDelay = 1000;
      var MAX_RECONNECT  = 16000;
      var reconnectTimer = /** @type {ReturnType<typeof setTimeout>|null} */ (null);
      var isDisposed     = false;
      var dropModeActive = false;

      // Hover-pick state: throttled node name on cursor hover.
      var lastHoverPickTime = 0;
      var hoverPickPending  = false;
      var lastHoverX = 0;
      var lastHoverY = 0;

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
          'function decodeJpeg(data,offset,length){',
          '  var bytes=new Uint8Array(data,offset,length);',
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
          '  else if(m.type==="jpeg"){decodeJpeg(m.data,m.offset||0,m.length||m.data.byteLength);}',
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
          // Transfer the underlying ArrayBuffer to the worker (true zero-copy).
          // Pass byteOffset+byteLength so the worker can reconstruct the view.
          var ab = jpegBytes.buffer;
          renderWorker.postMessage(
            { type: 'jpeg', data: ab, offset: jpegBytes.byteOffset, length: jpegBytes.byteLength },
            [ab]
          );
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

      // Task 7.1-7.4: Multi-select state
      // selectedNodeIds: all box-picked node IDs ([] = nothing / single select)
      var selectedNodeIds = /** @type {number[]} */ ([]);
      // selectedNodeTransforms: map nodeId → {position, rotation, scale}
      var selectedNodeTransforms = /** @type {Object.<number, Object>} */ ({});
      // pendingTransformFetches: count of commitTransform requests in-flight for pivot setup
      var pendingTransformFetches = 0;
      // Simple undo stack for bulk transforms: [{type:'bulk', transforms:{nodeId: oldTransform}}]
      var undoStack = /** @type {Array} */ ([]);

      // Task 5.7 — Camera matrix cache (column-major, updated by editor.getCamera poll)
      var camViewMatrix  = null;   // 16-element array or null
      var camProjMatrix  = null;   // 16-element array or null
      var camWorldPos    = null;   // {x,y,z} or null
      // Drag start state (task 5.2 — needed to compute live deltas)
      var dragStartTransform = null;  // {position,rotation,scale} at beginDrag
      var dragStartHitPoint  = null;  // {x,y,z} world-space hit at beginDrag
      var dragConstraintAxis = null;  // 'x'|'y'|'z' for current drag
      // Rotation drag state
      var rotVStart          = null;  // {x,y,z} initial direction vector at drag start (on rotation plane)
      var rotVPrev           = null;  // {x,y,z} previous direction (for near-parallel freeze only)
      var rotTotalAngle      = 0;     // total angle from vStart (recomputed each frame, NOT accumulated)
      var rotDragMode        = null;  // 'axis' | 'free' | null
      var rotAxisWorld       = null;  // {x,y,z} locked world-space rotation axis for entire drag
      var rotPivotWorld      = null;  // {x,y,z} locked pivot world position for entire drag
      var rotArcballStart    = null;  // {x,y,z} arcball start vector (free rotation)
      var rotArcballPivotSX  = 0;     // projected pivot screen X (free rotation)
      var rotArcballPivotSY  = 0;     // projected pivot screen Y (free rotation)
      var rotArcballRadiusPx = 80;    // arcball sphere radius in pixels

      // -----------------------------------------------------------------------
      // Task 5.7 — Inline gizmo math (ported from gizmoController.ts)
      // -----------------------------------------------------------------------

      // Row-major 4x4 multiply: C = A * B, arr[r*4+c] = element(row r, col c)
      function mat4Mul16(a, b) {
        var out = new Array(16).fill(0);
        for (var r = 0; r < 4; r++) {
          for (var c = 0; c < 4; c++) {
            var sum = 0;
            for (var k = 0; k < 4; k++) { sum += a[r*4+k] * b[k*4+c]; }
            out[r*4+c] = sum;
          }
        }
        return out;
      }

      // Transpose a 4x4 matrix (swap row-major ↔ column-major)
      function mat4Transpose16(m) {
        return [
          m[0],m[4],m[8],m[12],
          m[1],m[5],m[9],m[13],
          m[2],m[6],m[10],m[14],
          m[3],m[7],m[11],m[15]
        ];
      }

      // Invert a 4x4 matrix stored in COLUMN-MAJOR order.
      function mat4Invert16_colmaj(m) {
        var inv = new Array(16).fill(0);
        inv[0]  =  m[5]*m[10]*m[15]-m[5]*m[11]*m[14]-m[9]*m[6]*m[15]+m[9]*m[7]*m[14]+m[13]*m[6]*m[11]-m[13]*m[7]*m[10];
        inv[4]  = -m[4]*m[10]*m[15]+m[4]*m[11]*m[14]+m[8]*m[6]*m[15]-m[8]*m[7]*m[14]-m[12]*m[6]*m[11]+m[12]*m[7]*m[10];
        inv[8]  =  m[4]*m[9]*m[15] -m[4]*m[11]*m[13]-m[8]*m[5]*m[15]+m[8]*m[7]*m[13]+m[12]*m[5]*m[11]-m[12]*m[7]*m[9];
        inv[12] = -m[4]*m[9]*m[14] +m[4]*m[10]*m[13]+m[8]*m[5]*m[14]-m[8]*m[6]*m[13]-m[12]*m[5]*m[10]+m[12]*m[6]*m[9];
        inv[1]  = -m[1]*m[10]*m[15]+m[1]*m[11]*m[14]+m[9]*m[2]*m[15]-m[9]*m[3]*m[14]-m[13]*m[2]*m[11]+m[13]*m[3]*m[10];
        inv[5]  =  m[0]*m[10]*m[15]-m[0]*m[11]*m[14]-m[8]*m[2]*m[15]+m[8]*m[3]*m[14]+m[12]*m[2]*m[11]-m[12]*m[3]*m[10];
        inv[9]  = -m[0]*m[9]*m[15] +m[0]*m[11]*m[13]+m[8]*m[1]*m[15]-m[8]*m[3]*m[13]-m[12]*m[1]*m[11]+m[12]*m[3]*m[9];
        inv[13] =  m[0]*m[9]*m[14] -m[0]*m[10]*m[13]-m[8]*m[1]*m[14]+m[8]*m[2]*m[13]+m[12]*m[1]*m[10]-m[12]*m[2]*m[9];
        inv[2]  =  m[1]*m[6]*m[15] -m[1]*m[7]*m[14] -m[5]*m[2]*m[15]+m[5]*m[3]*m[14]+m[13]*m[2]*m[7] -m[13]*m[3]*m[6];
        inv[6]  = -m[0]*m[6]*m[15] +m[0]*m[7]*m[14] +m[4]*m[2]*m[15]-m[4]*m[3]*m[14]-m[12]*m[2]*m[7] +m[12]*m[3]*m[6];
        inv[10] =  m[0]*m[5]*m[15] -m[0]*m[7]*m[13] -m[4]*m[1]*m[15]+m[4]*m[3]*m[13]+m[12]*m[1]*m[7] -m[12]*m[3]*m[5];
        inv[14] = -m[0]*m[5]*m[14] +m[0]*m[6]*m[13] +m[4]*m[1]*m[14]-m[4]*m[2]*m[13]-m[12]*m[1]*m[6] +m[12]*m[2]*m[5];
        inv[3]  = -m[1]*m[6]*m[11] +m[1]*m[7]*m[10] +m[5]*m[2]*m[11]-m[5]*m[3]*m[10]-m[9]*m[2]*m[7]  +m[9]*m[3]*m[6];
        inv[7]  =  m[0]*m[6]*m[11] -m[0]*m[7]*m[10] -m[4]*m[2]*m[11]+m[4]*m[3]*m[10]+m[8]*m[2]*m[7]  -m[8]*m[3]*m[6];
        inv[11] = -m[0]*m[5]*m[11] +m[0]*m[7]*m[9]  +m[4]*m[1]*m[11]-m[4]*m[3]*m[9] -m[8]*m[1]*m[7]  +m[8]*m[3]*m[5];
        inv[15] =  m[0]*m[5]*m[10] -m[0]*m[6]*m[9]  -m[4]*m[1]*m[10]+m[4]*m[2]*m[9] +m[8]*m[1]*m[6]  -m[8]*m[2]*m[5];
        var det = m[0]*inv[0]+m[1]*inv[4]+m[2]*inv[8]+m[3]*inv[12];
        if (Math.abs(det) < 1e-12) return null;
        var id = 1/det; for (var i=0;i<16;i++) inv[i]*=id; return inv;
      }

      // Matrices from C++ are DX row-major: arr[row*4+col].
      // DX convention: clip_row = pos_row * View * Proj.
      // VP = View * Proj (row-major multiply).

      // Unproject screen pixel → world-space ray {origin:{x,y,z}, dir:{x,y,z}}
      function screenToRay(px, py, vpW, vpH) {
        if (!camViewMatrix || !camProjMatrix) return null;
        var vp = mat4Mul16(camViewMatrix, camProjMatrix); // V * P row-major
        // Invert: transpose to col-major, invert, transpose back
        var vpColMaj = mat4Transpose16(vp);
        var vpInvCol = mat4Invert16_colmaj(vpColMaj);
        if (!vpInvCol) return null;
        var vpInv = mat4Transpose16(vpInvCol); // back to row-major
        // Unproject: ndc_row * VP^{-1} → world
        var ndcX = (px/vpW)*2-1, ndcY = 1-(py/vpH)*2;
        function unproj(ndcZ) {
          // world = ndc_row * vpInv  →  world[c] = Σ ndc[k] * vpInv[k*4+c]
          var cx=ndcX*vpInv[0]+ndcY*vpInv[4]+ndcZ*vpInv[8]+1*vpInv[12];
          var cy=ndcX*vpInv[1]+ndcY*vpInv[5]+ndcZ*vpInv[9]+1*vpInv[13];
          var cz=ndcX*vpInv[2]+ndcY*vpInv[6]+ndcZ*vpInv[10]+1*vpInv[14];
          var cw=ndcX*vpInv[3]+ndcY*vpInv[7]+ndcZ*vpInv[11]+1*vpInv[15];
          var wi=1/cw; return {x:cx*wi,y:cy*wi,z:cz*wi};
        }
        var near=unproj(0), far=unproj(1); // DX NDC Z range [0,1]
        var dx=far.x-near.x,dy=far.y-near.y,dz=far.z-near.z;
        var len=Math.sqrt(dx*dx+dy*dy+dz*dz);
        if(len<1e-12) return null;
        return {origin:near, dir:{x:dx/len,y:dy/len,z:dz/len}};
      }

      // Project world-space point to screen-space (px, py)
      // DX: clip = pos_row * VP  →  clip[c] = Σ pos[k] * VP[k*4+c]
      function worldToScreen(wp, vpW, vpH) {
        if (!camViewMatrix || !camProjMatrix) return null;
        var vp = mat4Mul16(camViewMatrix, camProjMatrix); // V * P row-major
        var cx=wp.x*vp[0]+wp.y*vp[4]+wp.z*vp[8]+vp[12];
        var cy=wp.x*vp[1]+wp.y*vp[5]+wp.z*vp[9]+vp[13];
        var cw=wp.x*vp[3]+wp.y*vp[7]+wp.z*vp[11]+vp[15];
        if(Math.abs(cw)<1e-12) return null;
        var ndcX=cx/cw, ndcY=cy/cw;
        return {sx:(ndcX*0.5+0.5)*vpW, sy:(1-(ndcY*0.5+0.5))*vpH};
      }

      // Dot product
      function dot3(a,b){return a.x*b.x+a.y*b.y+a.z*b.z;}
      function sub3(a,b){return {x:a.x-b.x,y:a.y-b.y,z:a.z-b.z};}
      function scale3(v,s){return {x:v.x*s,y:v.y*s,z:v.z*s};}
      function add3(a,b){return {x:a.x+b.x,y:a.y+b.y,z:a.z+b.z};}
      function len3(v){return Math.sqrt(v.x*v.x+v.y*v.y+v.z*v.z);}
      function cross3(a,b){return{x:a.y*b.z-a.z*b.y,y:a.z*b.x-a.x*b.z,z:a.x*b.y-a.y*b.x};}
      function norm3(v){var l=len3(v);if(l<1e-12)return{x:0,y:0,z:0};return scale3(v,1/l);}

      // Hit-test ray against axis cylinder — returns hit point or null
      function hitTestAxisRay(ray, origin, axisDir, axisLen, hitRadius) {
        var w0=sub3(ray.origin,origin);
        var a=dot3(ray.dir,ray.dir),b=dot3(ray.dir,axisDir);
        var c=dot3(axisDir,axisDir),d=dot3(ray.dir,w0),e=dot3(axisDir,w0);
        var denom=a*c-b*b;
        var t,s;
        if(Math.abs(denom)<1e-10){
          return null;
        }
        t=(b*e-c*d)/denom; s=(a*e-b*d)/denom;
        if(s<0||s>axisLen||t<0) return null;
        var closestRay=add3(ray.origin,scale3(ray.dir,t));
        var closestAxis=add3(origin,scale3(axisDir,s));
        var dist=len3(sub3(closestRay,closestAxis));
        if(dist>hitRadius) return null;
        return closestRay;
      }

      // Hit-test ray against plane — returns hit point or null
      function hitTestPlaneRay(ray, planeOrigin, planeNormal) {
        var denom=dot3(planeNormal,ray.dir);
        if(Math.abs(denom)<1e-10) return null;
        var t=dot3(planeNormal,sub3(planeOrigin,ray.origin))/denom;
        if(t<0) return null;
        return add3(ray.origin,scale3(ray.dir,t));
      }

      // Returns axis index 0/1/2 for hit, or -1 for no hit.
      // Uses the same scale factor as the renderer (task 1.5).
      function hitTestGizmoAxes(px, py) {
        if (!camWorldPos || !selectedNodeId) return -1;
        var vpW = canvas.width, vpH = canvas.height;
        var ray = screenToRay(px, py, vpW, vpH);
        if (!ray) return -1;

        // gizmoScale = max(dist * 0.1, 0.1) — matches renderer
        var nodePos = _gizmoNodePos;
        if (!nodePos) return -1;
        var dx=nodePos.x-camWorldPos.x, dy=nodePos.y-camWorldPos.y, dz=nodePos.z-camWorldPos.z;
        var dist=Math.sqrt(dx*dx+dy*dy+dz*dz);
        var gScale=Math.max(dist*0.1, 0.1);
        var axisLen=2.5*gScale, hitRadius=0.15*gScale;

        var axisDirs = [{x:1,y:0,z:0},{x:0,y:1,z:0},{x:0,y:0,z:1}];
        var origin = nodePos;

        if (currentGizmoMode === 'rotate') {
          // For rotate rings, test closest-approach to circle arc per axis
          var circleRad = 1.8*gScale;
          var kSegs = 32;
          var bestAxis = -1;
          var bestDist = Infinity;
          for (var a=0;a<3;a++) {
            for (var s=0;s<kSegs;s++) {
              var ang0=(s/kSegs)*Math.PI*2, ang1=((s+1)/kSegs)*Math.PI*2;
              var p0,p1;
              if(a===0){p0={x:origin.x,y:origin.y+Math.cos(ang0)*circleRad,z:origin.z+Math.sin(ang0)*circleRad};p1={x:origin.x,y:origin.y+Math.cos(ang1)*circleRad,z:origin.z+Math.sin(ang1)*circleRad};}
              else if(a===1){p0={x:origin.x+Math.cos(ang0)*circleRad,y:origin.y,z:origin.z+Math.sin(ang0)*circleRad};p1={x:origin.x+Math.cos(ang1)*circleRad,y:origin.y,z:origin.z+Math.sin(ang1)*circleRad};}
              else{p0={x:origin.x+Math.cos(ang0)*circleRad,y:origin.y+Math.sin(ang0)*circleRad,z:origin.z};p1={x:origin.x+Math.cos(ang1)*circleRad,y:origin.y+Math.sin(ang1)*circleRad,z:origin.z};}
              // Project ring segment endpoints to screen and test pixel distance
              var sp0 = worldToScreen(p0, vpW, vpH);
              var sp1 = worldToScreen(p1, vpW, vpH);
              if (!sp0 || !sp1) continue;
              // Point-to-segment distance in screen pixels
              var sdx = sp1.sx-sp0.sx, sdy = sp1.sy-sp0.sy;
              var slen2 = sdx*sdx+sdy*sdy;
              var t2 = slen2 > 0 ? Math.max(0, Math.min(1, ((px-sp0.sx)*sdx+(py-sp0.sy)*sdy)/slen2)) : 0;
              var cpx = sp0.sx+t2*sdx, cpy = sp0.sy+t2*sdy;
              var pxDist = Math.sqrt((px-cpx)*(px-cpx)+(py-cpy)*(py-cpy));
              if (pxDist < 10 && pxDist < bestDist) {
                bestDist = pxDist;
                bestAxis = a;
              }
            }
          }
          // Axis rings have priority over free ring
          if (bestAxis >= 0) return bestAxis;
          // Free ring: screen-space circle test (index 3)
          var scrPos = worldToScreen(origin, vpW, vpH);
          if (scrPos) {
            var freeRadPx = 2.0 * gScale; // world radius
            // Convert world free radius to screen pixels (approximate)
            var offsetPt = add3(origin, scale3(norm3(sub3(camWorldPos, origin)), freeRadPx));
            var scrOff = worldToScreen(offsetPt, vpW, vpH);
            if (scrOff) {
              var freeScreenRad = Math.sqrt(
                (scrOff.sx-scrPos.sx)*(scrOff.sx-scrPos.sx) +
                (scrOff.sy-scrPos.sy)*(scrOff.sy-scrPos.sy)
              );
              if (freeScreenRad < 10) freeScreenRad = 80; // fallback
              var cursorDist = Math.sqrt((px-scrPos.sx)*(px-scrPos.sx) + (py-scrPos.sy)*(py-scrPos.sy));
              if (Math.abs(cursorDist - freeScreenRad) < 10) return 3; // free ring
            }
          }
          return -1;
        }

        // Translate / scale — axis cylinders
        for (var ai=0;ai<3;ai++) {
          var hit2=hitTestAxisRay(ray,origin,axisDirs[ai],axisLen,hitRadius);
          if(hit2) return ai;
        }
        return -1;
      }

      // Current node world position and full transform (kept in sync with selection/transform)
      var _gizmoNodePos = null;
      // Full transform cache: {position:{x,y,z}, rotation:{w,x,y,z}, scale:{x,y,z}}
      var _selectedNodeTransform = null;

      // -----------------------------------------------------------------------
      // Task 5.2 — Additional math helpers for world-space drag
      // -----------------------------------------------------------------------

      // Get world-space axis direction for a constraint ('x'|'y'|'z')
      function getAxisDir(axis) {
        if (axis === 'x') return {x:1,y:0,z:0};
        if (axis === 'y') return {x:0,y:1,z:0};
        return {x:0,y:0,z:1};
      }

      // Snap a float to a grid
      function snapVal(v, grid) { return Math.round(v / grid) * grid; }
      function snapVec3(v, grid) { return {x:snapVal(v.x,grid),y:snapVal(v.y,grid),z:snapVal(v.z,grid)}; }

      // Quaternion helpers (w,x,y,z convention — matches GizmoController.ts)
      function quatFromAxisAngle(axis, angle) {
        var half=angle/2, s=Math.sin(half);
        return {w:Math.cos(half),x:axis.x*s,y:axis.y*s,z:axis.z*s};
      }
      function quatMul(q1, q2) {
        return {
          w: q1.w*q2.w - q1.x*q2.x - q1.y*q2.y - q1.z*q2.z,
          x: q1.w*q2.x + q1.x*q2.w + q1.y*q2.z - q1.z*q2.y,
          y: q1.w*q2.y - q1.x*q2.z + q1.y*q2.w + q1.z*q2.x,
          z: q1.w*q2.z + q1.x*q2.y - q1.y*q2.x + q1.z*q2.w
        };
      }
      function quatNorm(q) {
        var l=Math.sqrt(q.w*q.w+q.x*q.x+q.y*q.y+q.z*q.z);
        if(l<1e-12) return {w:1,x:0,y:0,z:0};
        return {w:q.w/l,x:q.x/l,y:q.y/l,z:q.z/l};
      }
      // Rotate a vec3 by a unit quaternion: v' = q * v * q^-1
      function quatRotateVec3(q, v) {
        // Optimized: t = 2 * cross(q.xyz, v); result = v + q.w * t + cross(q.xyz, t)
        var tx = 2*(q.y*v.z - q.z*v.y);
        var ty = 2*(q.z*v.x - q.x*v.z);
        var tz = 2*(q.x*v.y - q.y*v.x);
        return {
          x: v.x + q.w*tx + (q.y*tz - q.z*ty),
          y: v.y + q.w*ty + (q.z*tx - q.x*tz),
          z: v.z + q.w*tz + (q.x*ty - q.y*tx)
        };
      }

      // Convert a pixel radius to world-space radius at the depth of worldPos
      function pixelsToWorldRadius(worldPos, pixelRadius) {
        if (!camViewMatrix || !camProjMatrix) return 0.1;
        // Row-major VP: clip = pos * V * P → cw = pos_row * VP_col3
        var vp = mat4Mul16(camViewMatrix, camProjMatrix);
        var cw = worldPos.x*vp[3] + worldPos.y*vp[7] + worldPos.z*vp[11] + vp[15];
        if (Math.abs(cw) < 1e-12) return 0.1;
        // Row-major proj: element(0,0) = camProjMatrix[0]
        var focalX = camProjMatrix[0];
        if (Math.abs(focalX) < 1e-12) return 0.1;
        var vpW = canvas.width;
        var ndcRadius = pixelRadius / (vpW * 0.5);
        return ndcRadius * Math.abs(cw) / Math.abs(focalX);
      }

      // Signed angle from v0 to v1 around planeNormal (radians)
      function signedAngleOnPlane(v0, v1, planeNormal) {
        var c = cross3(v0, v1);
        var sinA = dot3(planeNormal, c);
        var cosA = dot3(v0, v1);
        return Math.atan2(sinA, cosA);
      }

      // Ray-plane intersection returning {t, point} or null
      function rayPlaneIntersect(ray, planeOrigin, planeNormal) {
        var denom = dot3(planeNormal, ray.dir);
        if (Math.abs(denom) < 1e-10) return null;
        var t = dot3(planeNormal, sub3(planeOrigin, ray.origin)) / denom;
        if (t < 0) return null;
        return {t: t, point: add3(ray.origin, scale3(ray.dir, t))};
      }

      // Map mouse pixel to arcball virtual sphere. Returns normalized {x,y,z}.
      function mapToArcball(mx, my, pivotSX, pivotSY, radiusPx) {
        var x = (mx - pivotSX) / radiusPx;
        var y = (my - pivotSY) / radiusPx;
        var s = x*x + y*y;
        if (s <= 1.0) {
          return {x: x, y: -y, z: Math.sqrt(1.0 - s)}; // -y: screen Y flipped
        } else {
          var invL = 1.0 / Math.sqrt(s);
          return {x: x * invL, y: -y * invL, z: 0};
        }
      }

      // Quaternion that rotates unit vector a to unit vector b
      function quatFromVectors(a, b) {
        var c = cross3(a, b);
        var d = dot3(a, b);
        // Handle near-180-degree case
        if (d < -0.999999) {
          // Pick arbitrary perpendicular axis
          var perp = Math.abs(a.x) < 0.9 ? cross3(a, {x:1,y:0,z:0}) : cross3(a, {x:0,y:1,z:0});
          perp = norm3(perp);
          return {w: 0, x: perp.x, y: perp.y, z: perp.z};
        }
        var w = 1.0 + d;
        var l = Math.sqrt(w*w + c.x*c.x + c.y*c.y + c.z*c.z);
        return {w: w/l, x: c.x/l, y: c.y/l, z: c.z/l};
      }

      // Build the drag-start state: compute hit point for the axis/plane at the current mouse pos.
      // Returns {hitPoint:{x,y,z}, planeNormal:{x,y,z}} or null.
      function computeDragStart(px, py) {
        if (!camWorldPos || !_gizmoNodePos || !dragConstraintAxis) return null;
        var ray = screenToRay(px, py, canvas.width, canvas.height);
        if (!ray) return null;
        var origin = _gizmoNodePos;
        var axisDir = getAxisDir(dragConstraintAxis);
        // For rotate: intersect ray with rotation plane, compute initial direction vector
        if (currentGizmoMode === 'rotate') {
          // Lock axis and pivot for the entire drag (WORLD mode: constant world axis)
          rotAxisWorld = axisDir;
          rotPivotWorld = {x: origin.x, y: origin.y, z: origin.z};
          var rpi = rayPlaneIntersect(ray, origin, axisDir);
          if (rpi) {
            var v0 = norm3(sub3(rpi.point, origin));
            if (len3(v0) > 1e-6) {
              rotVStart = v0;
              rotVPrev = v0;
              rotTotalAngle = 0;
              rotDragMode = 'axis';
            }
          }
          return {hitPoint: rpi ? rpi.point : origin, planeNormal: axisDir};
        }
        // For translate/scale: build a plane containing the axis that faces the camera
        var toCamera = norm3(sub3(camWorldPos, origin));
        var cross1 = cross3(axisDir, toCamera);
        var planeNormal = len3(cross1) > 1e-6 ? norm3(cross3(axisDir, cross1)) : toCamera;
        var hit2 = hitTestPlaneRay(ray, origin, planeNormal);
        return hit2 ? {hitPoint:hit2, planeNormal:planeNormal} : null;
      }

      // Cached drag-start plane normal (set at beginDrag, used throughout)
      var _dragPlaneNormal = null;

      function setGizmoMode(mode) {
        if (mode === currentGizmoMode) return;
        currentGizmoMode = mode;
        for (var k in modeButtons) {
          if (modeButtons[k]) modeButtons[k].classList.toggle('active', k === mode);
        }
        // Notify the extension host
        if (vscodeApi) vscodeApi.postMessage({ type: 'gizmo_mode', mode: mode });
        if (ws && ws.readyState === WebSocket.OPEN) {
          // Task 3.1: editor.setGizmo with active nodeId + mode
          if (selectedNodeId) {
            ws.send(JSON.stringify({ type: 'editor.setGizmo', nodeId: selectedNodeId, mode: mode }));
          }
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

        } else if (msg.type === 'switchPort') {
          // Switch WebSocket port (play mode ↔ editor mode).
          if (ws) {
            ws.close();
            ws = null;
          }
          WS_PORT = msg.port;
          WS_URL = 'ws://localhost:' + WS_PORT;
          setStatus('Reconnecting...', 'connecting');
          reconnectDelay = 1000;
          // Update mode badge (9003 = Play, otherwise Editor).
          if (modeBadgeEl) {
            var isPlay = WS_PORT === 9003;
            modeBadgeEl.textContent = isPlay ? 'Play' : 'Editor';
            modeBadgeEl.style.background = isPlay ? '#6a3d9a' : '';
            modeBadgeEl.style.borderColor = isPlay ? '#9b59b6' : '';
          }
          connect();

        } else if (msg.type === 'dragStarted') {
          dropModeActive = true;
          if (canvasCont) { canvasCont.classList.add('drop-active'); }
          canvas.style.cursor = 'copy';

        } else if (msg.type === 'dragEnded') {
          dropModeActive = false;
          if (canvasCont) { canvasCont.classList.remove('drop-active'); }
          canvas.style.cursor = '';
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
          // Binary frames (JPEG) get priority — process immediately.
          // Text messages (JSON) are deferred to avoid blocking frame rendering.
          if (typeof e.data !== 'string') {
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
            return;
          }

          // Defer JSON processing so it never blocks the current frame delivery.
          var textData = e.data;
          queueMicrotask(function () {
            try {
              var parsed = JSON.parse(textData);
              if (parsed.type === 'pickResult') {
                selectedNodeId = parsed.nodeId || null;
                selectedNodeIds = []; // clear multi-select on single pick
                selectedNodeTransforms = {};
                pendingTransformFetches = 0;
                if (!selectedNodeId) {
                  _gizmoNodePos = null;
                  _selectedNodeTransform = null;
                } else {
                  // Try to get position immediately from debug_projNodes
                  if (parsed.debug_projNodes) {
                    for (var pni = 0; pni < parsed.debug_projNodes.length; pni++) {
                      var pn = parsed.debug_projNodes[pni];
                      if (pn.id === selectedNodeId && pn.worldPos) {
                        _gizmoNodePos = {x: pn.worldPos[0], y: pn.worldPos[1], z: pn.worldPos[2]};
                        break;
                      }
                    }
                  }
                  // Also fetch full transform for rotation/scale operations
                  if (ws && ws.readyState === WebSocket.OPEN) {
                    ws.send(JSON.stringify({ type: 'editor.commitTransform', nodeId: selectedNodeId }));
                  }
                }
                if (vscodeApi) vscodeApi.postMessage({ type: 'pickResult', nodeId: parsed.nodeId, ctrlKey: ctrlHeld });
              } else if (parsed.type === 'selectionChanged') {
                selectedNodeId = parsed.nodeId || null;
                if (!selectedNodeId) {
                  _gizmoNodePos = null;
                } else if (ws && ws.readyState === WebSocket.OPEN) {
                  // Fetch initial node position for hit testing
                  ws.send(JSON.stringify({ type: 'editor.commitTransform', nodeId: selectedNodeId }));
                }
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
                // Transform applied by gizmo drag — update cached node position
                if (parsed.position && selectedNodeId) {
                  _gizmoNodePos = {x: parsed.position[0], y: parsed.position[1], z: parsed.position[2]};
                }
                if (vscodeApi) vscodeApi.postMessage({ type: 'transformUpdate', data: parsed });
              } else if (parsed.type === 'commitTransformResult') {
                // Authoritative transform — update cached node position and full transform
                if (parsed.transform) {
                  // Task 7.1: multi-select centroid accumulation
                  var respNodeId = parsed.nodeId || null;
                  if (pendingTransformFetches > 0 && respNodeId && selectedNodeIds.indexOf(respNodeId) >= 0) {
                    selectedNodeTransforms[respNodeId] = parsed.transform;
                    pendingTransformFetches--;
                    if (pendingTransformFetches <= 0) {
                      // All transforms received — compute centroid and set gizmo pivot
                      var cx = 0, cy = 0, cz = 0;
                      var cCount = 0;
                      for (var cId in selectedNodeTransforms) {
                        var cT = selectedNodeTransforms[cId];
                        if (cT && cT.position) {
                          cx += cT.position.x; cy += cT.position.y; cz += cT.position.z;
                          cCount++;
                        }
                      }
                      if (cCount > 0) {
                        cx /= cCount; cy /= cCount; cz /= cCount;
                        _gizmoNodePos = {x: cx, y: cy, z: cz};
                        // Task 7.2: send setGizmo with pivot centroid
                        if (ws && ws.readyState === WebSocket.OPEN) {
                          ws.send(JSON.stringify({
                            type: 'editor.setGizmo',
                            nodeId: selectedNodeIds[0],
                            mode: currentGizmoMode,
                            pivot: [cx, cy, cz]
                          }));
                        }
                      }
                    }
                  } else {
                    // Single-select: update primary cached transform
                    _selectedNodeTransform = parsed.transform;
                    if (parsed.transform.position) {
                      var pos2 = parsed.transform.position;
                      _gizmoNodePos = {x: pos2.x, y: pos2.y, z: pos2.z};
                    }
                  }
                }
                if (vscodeApi) vscodeApi.postMessage({ type: 'commitTransformResult', data: parsed });
              } else if (parsed.type === 'cameraMatrices') {
                // Task 5.7 — cache camera matrices for TS-side hit testing
                camViewMatrix = parsed.view || null;
                camProjMatrix = parsed.proj || null;
                if (parsed.position && parsed.position.length >= 3) {
                  camWorldPos = {x: parsed.position[0], y: parsed.position[1], z: parsed.position[2]};
                }
              } else if (parsed.type === 'boxPickResult') {
                // Task 5.5 / 7.1: box-pick result
                var boxNodeIds = parsed.nodeIds || [];
                if (vscodeApi) vscodeApi.postMessage({ type: 'boxPickResult', nodeIds: boxNodeIds, ctrlKey: ctrlHeld });
                console.log('[UzEngine] boxPickResult:', boxNodeIds);

                // Task 7.1: when 2+ nodes selected, fetch transforms to compute centroid
                if (boxNodeIds.length >= 2 && ws && ws.readyState === WebSocket.OPEN) {
                  selectedNodeIds = boxNodeIds.slice();
                  selectedNodeTransforms = {};
                  selectedNodeId = boxNodeIds[0]; // primary for gizmo mode/hover
                  pendingTransformFetches = boxNodeIds.length;
                  for (var bni = 0; bni < boxNodeIds.length; bni++) {
                    ws.send(JSON.stringify({ type: 'editor.commitTransform', nodeId: boxNodeIds[bni] }));
                  }
                } else if (boxNodeIds.length === 1) {
                  // Single node: treat as regular pick
                  selectedNodeIds = [];
                  selectedNodeId = boxNodeIds[0];
                  if (ws && ws.readyState === WebSocket.OPEN) {
                    ws.send(JSON.stringify({ type: 'editor.setGizmo', nodeId: boxNodeIds[0], mode: currentGizmoMode }));
                    ws.send(JSON.stringify({ type: 'editor.commitTransform', nodeId: boxNodeIds[0] }));
                  }
                } else {
                  // Empty: clear selection
                  selectedNodeIds = [];
                  selectedNodeId = null;
                  _gizmoNodePos = null;
                }
              } else if (parsed.type === 'cameraFocused') {
                console.log('[UzEngine] Camera focused on node', parsed.nodeId);
              } else if (parsed.type === 'gizmoHover') {
                gizmoHoverAxis = parsed.axis || null;
                if (canvasContainer) {
                  canvasContainer.style.cursor = gizmoHoverAxis ? 'grab' : 'crosshair';
                }
              } else if (parsed.type === 'hoverPickResult') {
                // Show node name tooltip near cursor.
                if (hoverLabel && hoverPickPending) {
                  hoverPickPending = false;
                  if (parsed.nodeName && parsed.nodeId) {
                    hoverLabel.textContent = parsed.nodeName;
                    hoverLabel.style.display = 'block';
                    // Position relative to canvas container.
                    if (canvasContainer) {
                      var hpRect = canvasContainer.getBoundingClientRect();
                      hoverLabel.style.left = (lastHoverX - hpRect.left + 14) + 'px';
                      hoverLabel.style.top  = (lastHoverY - hpRect.top  - 10) + 'px';
                    }
                  } else {
                    hoverLabel.style.display = 'none';
                  }
                }
              }
            } catch (ex) { console.warn('[UzEngine] JSON parse error:', ex); }
          });
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
      canvas.addEventListener('focus', function() {
        canvasFocused = true;
        if (vscodeApi) vscodeApi.postMessage({ type: 'viewport_focus', focused: true });
      });
      canvas.addEventListener('blur',  function() {
        canvasFocused = false;
        if (vscodeApi) vscodeApi.postMessage({ type: 'viewport_focus', focused: false });
      });

      // Gizmo hotkey map (only active when canvas is focused and NOT in right-click camera mode)
      var gizmoHotkeys = { w: 'translate', e: 'rotate', r: 'scale' };
      // Task 6.3: snap via Ctrl key
      var ctrlHeld = false;
      // Task 5.6: axis constraint hotkeys during drag
      var axisConstraintHotkeys = { x: 'x', y: 'y', z: 'z' };
      // Task 5.5: box-select state
      var boxSelecting = false;
      var boxStartSX = 0; var boxStartSY = 0;
      var boxSelectRect = document.getElementById('box-select-rect');

      function updateSnapIndicator() {
        var snapEl = document.getElementById('snap-indicator');
        if (snapEl) { snapEl.style.display = ctrlHeld ? 'inline' : 'none'; }
      }

      document.addEventListener('keydown', function(e) {
        var k = e.key.toLowerCase();
        if (e.key === 'Shift') { shiftDown = true; }
        if (e.key === 'Control') {
          ctrlHeld = true;
          updateSnapIndicator();
        }

        // Escape cancels asset drop mode.
        if (dropModeActive && e.key === 'Escape') {
          dropModeActive = false;
          if (canvasCont) { canvasCont.classList.remove('drop-active'); }
          canvas.style.cursor = '';
          if (vscodeApi) { vscodeApi.postMessage({ type: 'dragCancelled' }); }
          e.preventDefault();
          return;
        }

        if (rightMouseDown) {
          // Camera mode — WASD/QE for movement
          if (k in keys) { keys[k] = 1; e.preventDefault(); }
        } else if (canvasFocused) {
          // Task 5.6: X/Y/Z switch axis constraint mid-drag
          if (gizmoDragging && e.key === 'Escape') {
            // Cancel drag — revert to dragStartTransform
            if (dragStartTransform && ws && ws.readyState === WebSocket.OPEN) {
              if (selectedNodeIds.length >= 2) {
                for (var ci = 0; ci < selectedNodeIds.length; ci++) {
                  var cId = selectedNodeIds[ci];
                  var cOrig = selectedNodeTransforms[cId];
                  if (cOrig) {
                    ws.send(JSON.stringify({
                      type: 'editor.setNodeTransformLive', nodeId: cId,
                      transform: { position: cOrig.position, rotation: cOrig.rotation, scale: cOrig.scale }
                    }));
                  }
                }
              } else {
                ws.send(JSON.stringify({
                  type: 'editor.setNodeTransformLive', nodeId: selectedNodeId,
                  transform: dragStartTransform
                }));
              }
            }
            gizmoDragging = false;
            rotDragMode = null;
            rotVStart = null;
            rotVPrev = null;
            rotTotalAngle = 0;
            rotAxisWorld = null;
            rotPivotWorld = null;
            gizmoActiveAxis = null;
            if (canvasContainer) canvasContainer.style.cursor = 'crosshair';
            e.preventDefault();
          } else if (gizmoDragging && k in axisConstraintHotkeys) {
            dragConstraintAxis = axisConstraintHotkeys[k];
            gizmoActiveAxis = dragConstraintAxis;
            // Reset rotation state for new axis
            if (currentGizmoMode === 'rotate' && rotDragMode === 'axis') {
              // Commit current rotation delta (rot + position orbit) into dragStartTransform
              if (rotAxisWorld && rotPivotWorld && dragStartTransform && rotTotalAngle !== 0) {
                var switchDeltaQ = quatFromAxisAngle(rotAxisWorld, rotTotalAngle);
                var swOff = sub3(dragStartTransform.position, rotPivotWorld);
                var swRotOff = quatRotateVec3(switchDeltaQ, swOff);
                var swNewPos = add3(rotPivotWorld, swRotOff);
                dragStartTransform = {
                  position: swNewPos,
                  rotation: quatNorm(quatMul(switchDeltaQ, dragStartTransform.rotation)),
                  scale: dragStartTransform.scale
                };
              }
              var newAxis = getAxisDir(dragConstraintAxis);
              rotAxisWorld = newAxis;
              rotPivotWorld = _gizmoNodePos ? {x: _gizmoNodePos.x, y: _gizmoNodePos.y, z: _gizmoNodePos.z} : rotPivotWorld;
              rotVStart = null;
              rotVPrev = null;
              rotTotalAngle = 0;
              // Re-compute initial vector on new axis plane
              if (lastMouseEvent) {
                var acCoords = getCanvasCoords(lastMouseEvent);
                if (acCoords && rotPivotWorld) {
                  var acRay = screenToRay(acCoords.sx, acCoords.sy, canvas.width, canvas.height);
                  if (acRay) {
                    var acHit = rayPlaneIntersect(acRay, rotPivotWorld, newAxis);
                    if (acHit) {
                      var acV = norm3(sub3(acHit.point, rotPivotWorld));
                      if (len3(acV) > 1e-6) {
                        rotVStart = acV;
                        rotVPrev = acV;
                      }
                    }
                  }
                }
              }
            }
            if (ws && ws.readyState === WebSocket.OPEN) {
              ws.send(JSON.stringify({ type: 'editor.setGizmoHover', axis: dragConstraintAxis }));
            }
            e.preventDefault();
          } else if (!gizmoDragging) {
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
        }
      });
      document.addEventListener('keyup', function(e) {
        var k = e.key.toLowerCase();
        if (k in keys) { keys[k] = 0; }
        if (e.key === 'Shift') { shiftDown = false; }
        if (e.key === 'Control') { ctrlHeld = false; updateSnapIndicator(); }
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
        // Asset drop mode: left click places the asset.
        if (dropModeActive && e.button === 0 && vscodeApi) {
          e.preventDefault();
          e.stopPropagation();
          var rect = canvas.getBoundingClientRect();
          vscodeApi.postMessage({
            type: 'assetDropped',
            x: e.clientX - rect.left,
            y: e.clientY - rect.top
          });
          dropModeActive = false;
          if (canvasCont) { canvasCont.classList.remove('drop-active'); }
          canvas.style.cursor = '';
          return;
        }

        if (e.button === 2) {
          rightMouseDown = true;
          canvas.focus();
          canvas.requestPointerLock();
          e.preventDefault();
          return;
        }

        // Left click — hit-test gizmo axes (TS-side), then fall back to pick
        if (e.button === 0) {
          canvas.focus();
          lastMouseEvent = e;
          var coords = getCanvasCoords(e);
          if (!coords || !ws || ws.readyState !== WebSocket.OPEN) return;

          if (selectedNodeId && camViewMatrix) {
            // Task 5.1: TS-side hit testing
            var hitAxis = hitTestGizmoAxes(coords.sx, coords.sy);
            if (hitAxis >= 0) {
              var axisNames = ['x','y','z','free'];
              gizmoActiveAxis = axisNames[hitAxis];
              gizmoDragging = true;
              dragConstraintAxis = hitAxis < 3 ? gizmoActiveAxis : null;
              // Capture drag start transform and world-space hit point
              dragStartTransform = _selectedNodeTransform
                ? JSON.parse(JSON.stringify(_selectedNodeTransform))
                : null;
              if (hitAxis === 3 && currentGizmoMode === 'rotate') {
                // Free rotation: arcball init
                rotDragMode = 'free';
                var pivotScr = worldToScreen(_gizmoNodePos, canvas.width, canvas.height);
                if (pivotScr) {
                  rotArcballPivotSX = pivotScr.sx;
                  rotArcballPivotSY = pivotScr.sy;
                  // Estimate screen radius from gizmo scale
                  var gDist = len3(sub3(_gizmoNodePos, camWorldPos));
                  var gScl = Math.max(gDist * 0.1, 0.1);
                  var offsetPt2 = add3(_gizmoNodePos, scale3(norm3(sub3(camWorldPos, _gizmoNodePos)), 2.0 * gScl));
                  var scrOff2 = worldToScreen(offsetPt2, canvas.width, canvas.height);
                  rotArcballRadiusPx = scrOff2 ? Math.sqrt(
                    (scrOff2.sx-pivotScr.sx)*(scrOff2.sx-pivotScr.sx) +
                    (scrOff2.sy-pivotScr.sy)*(scrOff2.sy-pivotScr.sy)
                  ) : 80;
                  if (rotArcballRadiusPx < 10) rotArcballRadiusPx = 80;
                  rotArcballStart = mapToArcball(coords.sx, coords.sy, rotArcballPivotSX, rotArcballPivotSY, rotArcballRadiusPx);
                }
                rotTotalAngle = 0;
              } else {
                var dsResult = computeDragStart(coords.sx, coords.sy);
                dragStartHitPoint = dsResult ? dsResult.hitPoint : null;
                _dragPlaneNormal  = dsResult ? dsResult.planeNormal : null;
              }
              // Notify engine of mode + hover axis
              ws.send(JSON.stringify({ type: 'editor.setGizmo', nodeId: selectedNodeId, mode: currentGizmoMode }));
              ws.send(JSON.stringify({ type: 'editor.setGizmoHover', axis: hitAxis < 3 ? gizmoActiveAxis : null }));
              // Also initialize server drag state (needed for editor.gizmoDrag fallback)
              ws.send(JSON.stringify({ type: 'editor.gizmoBeginDrag', screen_x: coords.sx, screen_y: coords.sy }));
              canvasContainer.style.cursor = 'grabbing';
            } else {
              // No gizmo hit — box-select (Ctrl) or single pick
              if (ctrlHeld) {
                // Task 5.5: Ctrl+click on empty area starts box-select
                boxSelecting = true;
                boxStartSX = coords.sx; boxStartSY = coords.sy;
              } else {
                ws.send(JSON.stringify({ type: 'editor.pickNode', screen_x: coords.sx, screen_y: coords.sy }));
              }
            }
          } else if (selectedNodeId) {
            // No cached camera — send pickNode (GPU pick works without TS camera)
            ws.send(JSON.stringify({ type: 'editor.pickNode', screen_x: coords.sx, screen_y: coords.sy }));
          } else if (ctrlHeld) {
            // Task 5.5: Ctrl+click on empty canvas starts box-select
            boxSelecting = true;
            boxStartSX = coords.sx; boxStartSY = coords.sy;
          } else {
            ws.send(JSON.stringify({ type: 'editor.pickNode', screen_x: coords.sx, screen_y: coords.sy }));
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

        // End box-select on left mouse up
        if (e.button === 0 && boxSelecting) {
          boxSelecting = false;
          if (boxSelectRect) { boxSelectRect.style.display = 'none'; }
          var upCoords = getCanvasCoords(e);
          if (upCoords && ws && ws.readyState === WebSocket.OPEN) {
            var bx = Math.min(boxStartSX, upCoords.sx);
            var by = Math.min(boxStartSY, upCoords.sy);
            var bw = Math.abs(upCoords.sx - boxStartSX);
            var bh = Math.abs(upCoords.sy - boxStartSY);
            if (bw > 2 && bh > 2) {
              ws.send(JSON.stringify({ type: 'editor.boxPick', x: bx, y: by, width: bw, height: bh }));
            }
          }
        }

        // End gizmo drag on left mouse up
        if (e.button === 0 && gizmoDragging) {
          gizmoDragging = false;
          gizmoActiveAxis = null;
          dragConstraintAxis = null;
          rotDragMode = null;
          rotVStart = null;
          rotVPrev = null;
          rotTotalAngle = 0;
          rotAxisWorld = null;
          rotPivotWorld = null;
          rotArcballStart = null;
          justFinishedDrag = true;
          setTimeout(function() { justFinishedDrag = false; }, 50);
          if (ws && ws.readyState === WebSocket.OPEN) {
            // Task 7.4: push BulkPropertyCommand to undo stack for multi-select
            if (selectedNodeIds.length >= 2) {
              // Snapshot old transforms before committing (they were stored at drag start)
              var undoEntry = { type: 'bulk', transforms: {} };
              for (var uni = 0; uni < selectedNodeIds.length; uni++) {
                var unId = selectedNodeIds[uni];
                undoEntry.transforms[unId] = selectedNodeTransforms[unId]
                  ? JSON.parse(JSON.stringify(selectedNodeTransforms[unId]))
                  : null;
              }
              undoStack.push(undoEntry);
              // Commit all nodes and refresh their transforms
              for (var cni = 0; cni < selectedNodeIds.length; cni++) {
                ws.send(JSON.stringify({ type: 'editor.commitTransform', nodeId: selectedNodeIds[cni] }));
              }
              ws.send(JSON.stringify({ type: 'editor.setGizmoHover', axis: null }));
            } else {
              // Task 5.3: single-node commit transform + clear hover
              if (selectedNodeId) {
                ws.send(JSON.stringify({ type: 'editor.commitTransform', nodeId: selectedNodeId }));
                ws.send(JSON.stringify({ type: 'editor.setGizmoHover', axis: null }));
              }
            }
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

        // Task 5.5: update rubber-band rectangle during box-select
        if (boxSelecting && boxSelectRect && canvasContainer) {
          var bCoords = getCanvasCoords(e);
          if (bCoords) {
            var cRect = canvasContainer.getBoundingClientRect();
            var x0 = Math.min(boxStartSX, bCoords.sx);
            var y0 = Math.min(boxStartSY, bCoords.sy);
            var x1 = Math.max(boxStartSX, bCoords.sx);
            var y1 = Math.max(boxStartSY, bCoords.sy);
            // Convert back to container-relative display coords
            var scaleX = (cRect.width  / canvas.width)  || 1;
            var scaleY = (cRect.height / canvas.height) || 1;
            boxSelectRect.style.display = 'block';
            boxSelectRect.style.left   = (x0 * scaleX) + 'px';
            boxSelectRect.style.top    = (y0 * scaleY) + 'px';
            boxSelectRect.style.width  = ((x1 - x0) * scaleX) + 'px';
            boxSelectRect.style.height = ((y1 - y0) * scaleY) + 'px';
          }
          return;
        }

        // Task 5.2: Gizmo drag — world-space computation via setNodeTransformLive
        if (gizmoDragging && ws && ws.readyState === WebSocket.OPEN) {
          var now = performance.now();
          if (now - lastDragSendTime < 16) return;
          lastDragSendTime = now;
          var coords = getCanvasCoords(e);
          if (!coords) { return; }

          // Free rotation (arcball) — handle before axis-constrained check
          if (rotDragMode === 'free' && rotArcballStart && camViewMatrix && dragStartTransform) {
            var abCur = mapToArcball(coords.sx, coords.sy, rotArcballPivotSX, rotArcballPivotSY, rotArcballRadiusPx);
            // Compute quaternion from start to current in screen/camera space
            var abDeltaQ = quatFromVectors(rotArcballStart, abCur);
            // Transform from camera space to world space
            // Camera basis: right = viewMatrix column 0, up = column 1, fwd = -column 2
            // Row-major DX view: right=(m[0],m[4],m[8]), up=(m[1],m[5],m[9]), fwd=(m[2],m[6],m[10])
            var camRight = {x: camViewMatrix[0], y: camViewMatrix[4], z: camViewMatrix[8]};
            var camUp    = {x: camViewMatrix[1], y: camViewMatrix[5], z: camViewMatrix[9]};
            var camFwdAB = {x:-camViewMatrix[2], y:-camViewMatrix[6], z:-camViewMatrix[10]};
            // Transform arcball delta axis from camera space to world space
            // axis_world = axis.x * camRight + axis.y * camUp + axis.z * camFwd
            var abAxis = {
              x: abDeltaQ.x * camRight.x + abDeltaQ.y * camUp.x + abDeltaQ.z * camFwdAB.x,
              y: abDeltaQ.x * camRight.y + abDeltaQ.y * camUp.y + abDeltaQ.z * camFwdAB.y,
              z: abDeltaQ.x * camRight.z + abDeltaQ.y * camUp.z + abDeltaQ.z * camFwdAB.z
            };
            var worldDeltaQ = {w: abDeltaQ.w, x: abAxis.x, y: abAxis.y, z: abAxis.z};
            worldDeltaQ = quatNorm(worldDeltaQ);

            // Snapping for free rotation: extract angle, snap, recompose
            if (ctrlHeld) {
              var abAngle = 2 * Math.acos(Math.min(1, Math.abs(worldDeltaQ.w)));
              var snapStep = shiftDown ? Math.PI / 180 : Math.PI / 36;
              var snappedAngle = snapVal(abAngle, snapStep);
              if (abAngle > 1e-6) {
                var abAxisNorm = norm3({x: worldDeltaQ.x, y: worldDeltaQ.y, z: worldDeltaQ.z});
                worldDeltaQ = quatFromAxisAngle(abAxisNorm, snappedAngle);
              }
            }

            var freeNewRot = quatNorm(quatMul(worldDeltaQ, dragStartTransform.rotation));
            // World-pivot sandwich for arcball: pivot is object origin (_gizmoNodePos)
            var freePivot = _gizmoNodePos || dragStartTransform.position;
            if (selectedNodeIds.length >= 2) {
              for (var fni = 0; fni < selectedNodeIds.length; fni++) {
                var fnId = selectedNodeIds[fni];
                var fnOrig = selectedNodeTransforms[fnId];
                if (fnOrig && fnOrig.rotation) {
                  var fnNewRot = quatNorm(quatMul(worldDeltaQ, fnOrig.rotation));
                  var fnOff = sub3(fnOrig.position, freePivot);
                  var fnRotOff = quatRotateVec3(worldDeltaQ, fnOff);
                  var fnNewPos = add3(freePivot, fnRotOff);
                  ws.send(JSON.stringify({
                    type: 'editor.setNodeTransformLive', nodeId: fnId,
                    transform: { position: fnNewPos, rotation: fnNewRot, scale: fnOrig.scale }
                  }));
                }
              }
            } else {
              // Single object: pivot == object origin, so offset is 0 and position unchanged
              ws.send(JSON.stringify({
                type: 'editor.setNodeTransformLive', nodeId: selectedNodeId,
                transform: { position: dragStartTransform.position, rotation: freeNewRot, scale: dragStartTransform.scale }
              }));
            }
          }

          // Use world-space drag when we have camera + start transform + start hit
          else if (camViewMatrix && dragStartTransform && dragStartHitPoint && _dragPlaneNormal && dragConstraintAxis) {
            var dragRay = screenToRay(coords.sx, coords.sy, canvas.width, canvas.height);
            if (dragRay) {
              var dOrigin = dragStartTransform.position;
              var dAxis   = getAxisDir(dragConstraintAxis);
              var currentHit = hitTestPlaneRay(dragRay, dOrigin, _dragPlaneNormal);

              if (currentHit && currentGizmoMode === 'translate') {
                // Project start and current hit onto constraint axis → compute delta
                var startOffset   = dot3(sub3(dragStartHitPoint, dOrigin), dAxis);
                var currentOffset = dot3(sub3(currentHit, dOrigin), dAxis);
                var delta = currentOffset - startOffset;
                var newPos = {
                  x: dOrigin.x + dAxis.x * delta,
                  y: dOrigin.y + dAxis.y * delta,
                  z: dOrigin.z + dAxis.z * delta
                };
                if (ctrlHeld) { newPos = snapVec3(newPos, 0.25); }
                _gizmoNodePos = newPos;

                // Task 7.3: apply same delta to ALL selected nodes during multi-select drag
                if (selectedNodeIds.length >= 2) {
                  for (var mni = 0; mni < selectedNodeIds.length; mni++) {
                    var mnId = selectedNodeIds[mni];
                    var mnOrig = selectedNodeTransforms[mnId];
                    if (mnOrig && mnOrig.position) {
                      var mnPos = {
                        x: mnOrig.position.x + dAxis.x * delta,
                        y: mnOrig.position.y + dAxis.y * delta,
                        z: mnOrig.position.z + dAxis.z * delta
                      };
                      if (ctrlHeld) { mnPos = snapVec3(mnPos, 0.25); }
                      ws.send(JSON.stringify({
                        type: 'editor.setNodeTransformLive',
                        nodeId: mnId,
                        transform: { position: mnPos, rotation: mnOrig.rotation, scale: mnOrig.scale }
                      }));
                    }
                  }
                } else {
                  ws.send(JSON.stringify({
                    type: 'editor.setNodeTransformLive',
                    nodeId: selectedNodeId,
                    transform: {
                      position: newPos,
                      rotation: dragStartTransform.rotation,
                      scale:    dragStartTransform.scale
                    }
                  }));
                }

              } else if (currentGizmoMode === 'rotate' && rotDragMode === 'axis' && rotVStart && rotAxisWorld && rotPivotWorld) {
                // World-axis rotation: locked axis + absolute angle from vStart
                var rotPlaneNormal = rotAxisWorld; // constant for entire drag
                var rpi2 = rayPlaneIntersect(dragRay, rotPivotWorld, rotPlaneNormal);
                if (rpi2) {
                  var vCur = norm3(sub3(rpi2.point, rotPivotWorld));
                  if (len3(vCur) > 1e-6) {
                    // Near-parallel check: freeze if ray is nearly parallel to plane
                    var parallelCheck = Math.abs(dot3(dragRay.dir, rotPlaneNormal));
                    if (parallelCheck > 0.01) {
                      // Absolute angle from vStart to vCur (NO accumulation)
                      rotTotalAngle = signedAngleOnPlane(rotVStart, vCur, rotPlaneNormal);
                      // Precision mode: Shift reduces sensitivity 10x
                      if (shiftDown && !ctrlHeld) {
                        rotTotalAngle *= 0.1;
                      }
                      rotVPrev = vCur; // kept for compatibility, not used for angle
                    }
                  }
                }
                // Snapping: applied to totalAngle
                var appliedAngle = rotTotalAngle;
                if (ctrlHeld && shiftDown) {
                  appliedAngle = snapVal(rotTotalAngle, Math.PI / 180); // 1-deg snap
                } else if (ctrlHeld) {
                  appliedAngle = snapVal(rotTotalAngle, Math.PI / 36); // 5-deg snap
                }

                // Build delta quaternion around the LOCKED WORLD axis
                var deltaQuat = quatFromAxisAngle(rotAxisWorld, appliedAngle);

                // World-pivot sandwich: newPos = P + Q*(startPos - P), newRot = Q*startRot
                if (selectedNodeIds.length >= 2) {
                  for (var rni = 0; rni < selectedNodeIds.length; rni++) {
                    var rnId = selectedNodeIds[rni];
                    var rnOrig = selectedNodeTransforms[rnId];
                    if (rnOrig && rnOrig.rotation) {
                      var rnNewRot = quatNorm(quatMul(deltaQuat, rnOrig.rotation));
                      var rnOffset = sub3(rnOrig.position, rotPivotWorld);
                      var rnRotOffset = quatRotateVec3(deltaQuat, rnOffset);
                      var rnNewPos = add3(rotPivotWorld, rnRotOffset);
                      ws.send(JSON.stringify({
                        type: 'editor.setNodeTransformLive',
                        nodeId: rnId,
                        transform: { position: rnNewPos, rotation: rnNewRot, scale: rnOrig.scale }
                      }));
                    }
                  }
                } else {
                  var dRot = dragStartTransform.rotation;
                  var newRot = quatNorm(quatMul(deltaQuat, dRot));
                  // Orbit position around world pivot
                  var pivotOffset = sub3(dragStartTransform.position, rotPivotWorld);
                  var rotatedOffset = quatRotateVec3(deltaQuat, pivotOffset);
                  var newPos = add3(rotPivotWorld, rotatedOffset);
                  ws.send(JSON.stringify({
                    type: 'editor.setNodeTransformLive',
                    nodeId: selectedNodeId,
                    transform: {
                      position: newPos,
                      rotation: newRot,
                      scale:    dragStartTransform.scale
                    }
                  }));
                }

              } else if (currentHit && currentGizmoMode === 'scale') {
                // Project onto axis, compute ratio
                var startD   = dot3(sub3(dragStartHitPoint, dOrigin), dAxis);
                var currentD = dot3(sub3(currentHit, dOrigin), dAxis);
                var ratio = Math.abs(startD) > 1e-6 ? currentD / startD : 1.0;
                ratio = Math.max(0.01, ratio);
                var dScl = dragStartTransform.scale;
                var newScale = {
                  x: dragConstraintAxis === 'x' ? dScl.x * ratio : dScl.x,
                  y: dragConstraintAxis === 'y' ? dScl.y * ratio : dScl.y,
                  z: dragConstraintAxis === 'z' ? dScl.z * ratio : dScl.z
                };
                ws.send(JSON.stringify({
                  type: 'editor.setNodeTransformLive',
                  nodeId: selectedNodeId,
                  transform: {
                    position: dragStartTransform.position,
                    rotation: dragStartTransform.rotation,
                    scale:    newScale
                  }
                }));
              } else {
                // Plane miss — fallback to old system
                ws.send(JSON.stringify({ type: 'editor.gizmoDrag', screen_x: coords.sx, screen_y: coords.sy }));
              }
            }
          } else {
            // No camera/transform cache — fall back to old server-side drag
            ws.send(JSON.stringify({ type: 'editor.gizmoDrag', screen_x: coords.sx, screen_y: coords.sy }));
          }
          return;
        }

        // Hover pick — show node name tooltip (throttled ~5fps, 200ms)
        if (!gizmoDragging && !boxSelecting && ws && ws.readyState === WebSocket.OPEN) {
          var hpNow = performance.now();
          if (hpNow - lastHoverPickTime > 200) {
            lastHoverPickTime = hpNow;
            var hpCoords = getCanvasCoords(e);
            if (hpCoords) {
              lastHoverX = e.clientX;
              lastHoverY = e.clientY;
              hoverPickPending = true;
              ws.send(JSON.stringify({
                type: 'editor.hoverPick',
                screen_x: hpCoords.sx,
                screen_y: hpCoords.sy
              }));
            }
          }
        }

        // Task 5.4: Gizmo hover — TS-side hit test + editor.setGizmoHover (throttled ~15fps)
        if (!gizmoDragging && selectedNodeId && ws && ws.readyState === WebSocket.OPEN) {
          var now2 = performance.now();
          if (now2 - lastHoverSendTime < 66) return;
          lastHoverSendTime = now2;
          var hcoords = getCanvasCoords(e);
          if (hcoords) {
            if (camViewMatrix) {
              // TS-side hit test: send pre-computed axis
              var hAxis = hitTestGizmoAxes(hcoords.sx, hcoords.sy);
              var hAxisName = hAxis >= 0 && hAxis < 3 ? ['x','y','z'][hAxis] : (hAxis === 3 ? 'free' : null);
              // Debug overlay (temporary)
              var dbgEl = document.getElementById('dbg-hittest');
              if (dbgEl) {
                var dbgScr = _gizmoNodePos ? worldToScreen(_gizmoNodePos, canvas.width, canvas.height) : null;
                dbgEl.textContent = 'mode:' + currentGizmoMode + ' hit:' + hAxis
                  + ' scrPos:' + (dbgScr ? dbgScr.sx.toFixed(0)+','+dbgScr.sy.toFixed(0) : 'NULL')
                  + ' mouse:' + hcoords.sx + ',' + hcoords.sy;
              }
              if (hAxisName !== gizmoHoverAxis) {
                gizmoHoverAxis = hAxisName;
                ws.send(JSON.stringify({ type: 'editor.setGizmoHover', axis: hAxisName === 'free' ? null : hAxisName }));
                if (canvasContainer) {
                  canvasContainer.style.cursor = hAxisName ? 'grab' : 'crosshair';
                }
              }
            } else {
              // No cached camera — fall back to server-side hover
              ws.send(JSON.stringify({ type: 'editor.gizmoHover', screen_x: hcoords.sx, screen_y: hcoords.sy }));
            }
          }
        }
      });

      canvasContainer.addEventListener('mouseleave', function() {
        if (hoverLabel) hoverLabel.style.display = 'none';
        hoverPickPending = false;
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

      // Task 5.7: Poll camera matrices every 200ms for TS-side hit testing.
      setInterval(function() {
        if (ws && ws.readyState === WebSocket.OPEN) {
          ws.send(JSON.stringify({ type: 'editor.getCamera' }));
        }
      }, 200);

      // Send input state at ~30fps via WS text messages.
      // IMPORTANT: always send, even when all-zero.  The previous guard
      // (skip if no keys/mouse) caused "stuck key" bugs — after releasing
      // RMB the zero-state was never sent, so the engine kept moving.
      // Cost is negligible (~200 bytes/33ms vs 100KB+ JPEG frames at 60fps).
      setInterval(function() {
        if (!ws || ws.readyState !== WebSocket.OPEN) return;

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
