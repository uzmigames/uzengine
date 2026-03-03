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

import * as vscode from 'vscode';
import { randomBytes } from 'crypto';
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
     * The frame is transferred to the webview as a transferable ArrayBuffer to
     * avoid a copy through the IPC serialisation boundary.
     *
     * @param jpeg  Raw JPEG bytes (viewport_id prefix already stripped).
     */
    receiveFrame(jpeg: Uint8Array): void {
        if (this._disposed || !this._panel.visible) { return; }
        // Post as ArrayBuffer for zero-copy transfer into the webview context.
        void this._panel.webview.postMessage({
            type: 'frame',
            // Slice to own the buffer (the input Uint8Array may be a view into
            // a larger ArrayBuffer that we cannot transfer).
            data: Array.from(jpeg),
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
        const nonce = this._nonce;
        const title = this.config.title;
        const wsPort = this._wsPort;
        const viewportId = this._viewportId;

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
                 script-src 'nonce-${nonce}';
                 style-src 'unsafe-inline';">
  <title>${safeTitle}</title>
  <style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    body {
      background: #000;
      display: flex;
      flex-direction: column;
      height: 100vh;
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

    #canvas-container {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      background: #000;
      overflow: hidden;
      position: relative;
    }

    #viewport-canvas {
      max-width: 100%;
      max-height: 100%;
      display: block;
      image-rendering: pixelated;
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
    <span id="vp-id">vp#${safeViewportId}</span>
  </div>
  <div id="canvas-container">
    <div id="overlay">Waiting for stream\u2026</div>
    <canvas id="viewport-canvas"></canvas>
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
      var ctx2d       = /** @type {CanvasRenderingContext2D} */ (canvas.getContext('2d'));
      var statusEl    = document.getElementById('status');
      var fpsEl       = document.getElementById('fps');
      var overlayEl   = document.getElementById('overlay');

      // -----------------------------------------------------------------------
      // State
      // -----------------------------------------------------------------------
      var ws             = /** @type {WebSocket|null} */ (null);
      var reconnectDelay = 1000;
      var MAX_RECONNECT  = 16000;
      var reconnectTimer = /** @type {ReturnType<typeof setTimeout>|null} */ (null);
      var isDisposed     = false;

      // Frame pipeline state
      var pendingBitmap  = /** @type {ImageBitmap|null} */ (null);
      var rafScheduled   = false;

      // FPS counter
      var frameCount    = 0;
      var lastFpsTick   = Date.now();

      // -----------------------------------------------------------------------
      // Status helpers
      // -----------------------------------------------------------------------
      function setStatus(text, cls) {
        statusEl.textContent = text;
        statusEl.className = cls;
      }

      // -----------------------------------------------------------------------
      // Render loop (rAF-driven, 60 fps cap from browser)
      // -----------------------------------------------------------------------
      function scheduleRender() {
        if (rafScheduled) { return; }
        rafScheduled = true;
        requestAnimationFrame(renderFrame);
      }

      function renderFrame() {
        rafScheduled = false;
        if (!pendingBitmap) { return; }

        var bmp = pendingBitmap;
        pendingBitmap = null;

        // Resize canvas to match bitmap on first frame or size change.
        if (canvas.width !== bmp.width || canvas.height !== bmp.height) {
          canvas.width  = bmp.width;
          canvas.height = bmp.height;
        }

        ctx2d.drawImage(bmp, 0, 0);
        bmp.close(); // Release GPU memory immediately.

        // FPS display (updated once per second).
        frameCount++;
        var now = Date.now();
        if (now - lastFpsTick >= 1000) {
          fpsEl.textContent = Math.round(frameCount * 1000 / (now - lastFpsTick)) + ' fps';
          frameCount  = 0;
          lastFpsTick = now;
        }
      }

      // -----------------------------------------------------------------------
      // Frame ingestion from the extension host (postMessage path)
      // -----------------------------------------------------------------------
      window.addEventListener('message', function (event) {
        var msg = event.data;
        if (!msg || typeof msg !== 'object') { return; }

        if (msg.type === 'frame' && Array.isArray(msg.data)) {
          // Reconstruct the Uint8Array from the number[] (avoids structured-clone
          // limitations on transferable ArrayBuffers across VS Code's process boundary).
          var bytes = new Uint8Array(msg.data);
          var blob  = new Blob([bytes], { type: 'image/jpeg' });

          createImageBitmap(blob).then(function (bmp) {
            if (overlayEl && !overlayEl.classList.contains('hidden')) {
              overlayEl.classList.add('hidden');
            }
            // Drop the previous pending frame if the rAF hasn't fired yet
            // (frame rate from engine may exceed 60 fps briefly).
            if (pendingBitmap) { pendingBitmap.close(); }
            pendingBitmap = bmp;
            scheduleRender();
          }).catch(function (err) {
            console.warn('[UzEngine] createImageBitmap failed:', err);
          });

        } else if (msg.type === 'suspend') {
          setStatus('Suspended', 'suspended');
          fpsEl.textContent = '-- fps';

        } else if (msg.type === 'resume') {
          setStatus('Active', 'connected');
        }
      });

      // -----------------------------------------------------------------------
      // WebSocket connection (direct stream — extension host also routes frames
      // via postMessage for the common single-WS path; both paths are supported).
      // -----------------------------------------------------------------------
      function connect() {
        if (isDisposed) { return; }
        setStatus('Connecting\u2026', 'connecting');

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
        });

        ws.addEventListener('message', function (e) {
          if (!(e.data instanceof ArrayBuffer)) { return; }
          if (e.data.byteLength < 4) { return; }

          // Parse 4-byte little-endian viewport_id prefix.
          var view = new DataView(e.data);
          var vpId = view.getUint32(0, true);
          if (vpId !== VIEWPORT_ID) { return; } // Not for this panel.

          var jpegBytes = new Uint8Array(e.data, 4);
          var blob = new Blob([jpegBytes], { type: 'image/jpeg' });

          createImageBitmap(blob).then(function (bmp) {
            if (overlayEl && !overlayEl.classList.contains('hidden')) {
              overlayEl.classList.add('hidden');
            }
            if (pendingBitmap) { pendingBitmap.close(); }
            pendingBitmap = bmp;
            scheduleRender();
          }).catch(function (err) {
            console.warn('[UzEngine] createImageBitmap failed:', err);
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

    }());
  </script>
</body>
</html>`;
    }
}
