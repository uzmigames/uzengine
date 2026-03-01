import * as vscode from 'vscode';
import { randomBytes } from 'crypto';

/**
 * Manages the singleton WebviewPanel that renders the UzEngine JPEG stream.
 *
 * Design decisions:
 * - Singleton pattern via `StreamPanel.currentPanel` so re-invoking the
 *   command reveals the existing panel rather than opening a duplicate.
 * - The canvas/WebSocket pipeline lives entirely in the inlined webview
 *   script — this avoids needing a separate webview bundle and keeps the
 *   CSP tight (no external scripts).
 * - Each WebSocket message is one complete JPEG frame; `createImageBitmap`
 *   decodes it off the main thread and `ctx.drawImage` blits it to the canvas.
 * - The server URL is read from configuration and injected into the HTML at
 *   render time; configuration changes take effect on the next panel open.
 */
export class StreamPanel {
    /** The currently active panel, or `undefined` if none is open. */
    public static currentPanel: StreamPanel | undefined;

    private static readonly VIEW_TYPE = 'uzengineStreamViewer';

    private readonly _panel: vscode.WebviewPanel;
    private _disposables: vscode.Disposable[] = [];

    // -------------------------------------------------------------------------
    // Static factory
    // -------------------------------------------------------------------------

    /**
     * Creates a new panel or reveals the existing one.
     * @param extensionUri - The URI of the extension's install directory,
     *                       used to resolve local resources if needed.
     */
    public static createOrShow(extensionUri: vscode.Uri): void {
        const column =
            vscode.window.activeTextEditor
                ? vscode.window.activeTextEditor.viewColumn
                : undefined;

        // Reveal existing panel in its current column.
        if (StreamPanel.currentPanel) {
            StreamPanel.currentPanel._panel.reveal(column);
            return;
        }

        // Create a new panel.
        const panel = vscode.window.createWebviewPanel(
            StreamPanel.VIEW_TYPE,
            'UzEngine Stream',
            column ?? vscode.ViewColumn.One,
            StreamPanel._webviewOptions()
        );

        StreamPanel.currentPanel = new StreamPanel(panel, extensionUri);
    }

    // -------------------------------------------------------------------------
    // Constructor (private — use createOrShow)
    // -------------------------------------------------------------------------

    private constructor(panel: vscode.WebviewPanel, _extensionUri: vscode.Uri) {
        this._panel = panel;

        this._update();

        // Clean up when the panel is closed by the user.
        this._panel.onDidDispose(
            () => this.dispose(),
            null,
            this._disposables
        );

        // When the panel becomes visible again (e.g. tab switch), send a resume
        // message so the webview can react if needed. The canvas pipeline does
        // not require an explicit play() call, but the handler is kept for
        // forward compatibility. The webview context is preserved via
        // retainContextWhenHidden, so the WebSocket stays alive across tab switches.
        this._panel.onDidChangeViewState(
            () => {
                if (this._panel.visible) {
                    this._panel.webview.postMessage({ type: 'resume' });
                }
            },
            null,
            this._disposables
        );
    }

    // -------------------------------------------------------------------------
    // Disposal
    // -------------------------------------------------------------------------

    /** Dispose the panel and all registered disposables. */
    public dispose(): void {
        StreamPanel.currentPanel = undefined;
        this._panel.dispose();

        while (this._disposables.length) {
            const disposable = this._disposables.pop();
            disposable?.dispose();
        }
    }

    // -------------------------------------------------------------------------
    // Private helpers
    // -------------------------------------------------------------------------

    /** Re-renders the webview content. */
    private _update(): void {
        const serverUrl = this._getServerUrl();
        this._panel.webview.html = this._getHtmlForWebview(
            this._panel.webview,
            serverUrl
        );
    }

    /**
     * Reads the configured WebSocket URL from VSCode settings.
     * Falls back to the default `ws://localhost:9002` if not set.
     */
    private _getServerUrl(): string {
        const config = vscode.workspace.getConfiguration('uzengine');
        return config.get<string>('serverUrl', 'ws://localhost:9002');
    }

    /** Webview panel options — enable scripts, restrict local resource roots. */
    private static _webviewOptions(): vscode.WebviewOptions & vscode.WebviewPanelOptions {
        return {
            enableScripts: true,
            retainContextWhenHidden: true,
            // No local resource roots needed — all content is inlined.
            localResourceRoots: [],
        };
    }

    /**
     * Generates a cryptographically random nonce string suitable for use in
     * a CSP `script-src 'nonce-...'` directive.
     */
    private _getNonce(): string {
        return randomBytes(16).toString('base64');
    }

    /**
     * Builds the full HTML document for the webview, with the canvas/WebSocket
     * pipeline inlined as a `<script nonce="...">` block.
     *
     * @param _webview - The webview instance (reserved for future URI generation).
     * @param serverUrl - The WebSocket URL to connect to.
     */
    private _getHtmlForWebview(
        _webview: vscode.Webview,
        serverUrl: string
    ): string {
        const nonce = this._getNonce();

        // Escape the URL for safe embedding inside a JS string literal.
        const safeUrl = serverUrl.replace(/\\/g, '\\\\').replace(/'/g, "\\'");

        return /* html */ `<!DOCTYPE html>
<html lang="en">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <meta http-equiv="Content-Security-Policy"
        content="default-src 'none';
                 connect-src ws://localhost:* wss://localhost:*;
                 img-src blob: data: vscode-webview:;
                 script-src 'nonce-${nonce}';
                 style-src 'unsafe-inline';">
  <title>UzEngine Stream</title>
  <style>
    *, *::before, *::after {
      box-sizing: border-box;
      margin: 0;
      padding: 0;
    }

    body {
      background: #1e1e1e;
      color: #ccc;
      font-family: 'Consolas', 'Courier New', monospace;
      display: flex;
      flex-direction: column;
      height: 100vh;
      overflow: hidden;
    }

    #toolbar {
      display: flex;
      align-items: center;
      gap: 16px;
      padding: 6px 12px;
      background: #252526;
      border-bottom: 1px solid #3c3c3c;
      font-size: 12px;
      flex-shrink: 0;
      user-select: none;
    }

    #toolbar .label {
      color: #569cd6;
      font-weight: bold;
      letter-spacing: 0.05em;
    }

    #status {
      font-weight: bold;
      transition: color 0.2s;
    }

    #status.connected    { color: #4ec9b0; }
    #status.disconnected { color: #f48771; }
    #status.connecting   { color: #dcdcaa; }

    #fps {
      color: #888;
      min-width: 60px;
    }

    #server-url {
      color: #9cdcfe;
      font-size: 11px;
      opacity: 0.75;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
      max-width: 300px;
    }

    #stream-container {
      flex: 1;
      display: flex;
      align-items: center;
      justify-content: center;
      background: #000;
      overflow: hidden;
    }

    #stream {
      max-width: 100%;
      max-height: 100%;
      object-fit: contain;
      display: block;
    }
  </style>
</head>
<body>
  <div id="toolbar">
    <span class="label">UzEngine</span>
    <span id="status" class="disconnected">Disconnected</span>
    <span id="fps">-- fps</span>
    <span id="server-url" title="${safeUrl}">${safeUrl}</span>
  </div>
  <div id="stream-container">
    <div id="diag" style="position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);color:#ff0;font-size:18px;z-index:10;text-align:center;pointer-events:none">
      Webview OK — aguardando stream...
    </div>
    <img id="stream" alt="" style="position:relative;z-index:1">
  </div>

  <script nonce="${nonce}">
    (function () {
      'use strict';

      // -----------------------------------------------------------------------
      // DOM references
      // -----------------------------------------------------------------------
      var img       = /** @type {HTMLImageElement} */ (document.getElementById('stream'));
      var statusEl  = /** @type {HTMLElement} */ (document.getElementById('status'));
      var fpsEl     = /** @type {HTMLElement} */ (document.getElementById('fps'));
      var diagEl    = /** @type {HTMLElement} */ (document.getElementById('diag'));

      console.log('[UzEngine] Webview script loaded');

      // -----------------------------------------------------------------------
      // State
      // -----------------------------------------------------------------------
      var ws             = /** @type {WebSocket|null} */ (null);

      var lastFpsTick    = Date.now();
      var frameCount     = 0;

      var reconnectDelay = 1000;
      var MAX_RECONNECT  = 16000;
      var reconnectTimer = /** @type {ReturnType<typeof setTimeout>|null} */ (null);
      var isDisposed     = false;

      // The server URL is injected by the extension host at render time.
      var SERVER_URL = '${safeUrl}';

      // -----------------------------------------------------------------------
      // Status helpers
      // -----------------------------------------------------------------------
      function setStatus(text, cls) {
        statusEl.textContent = text;
        statusEl.className = cls;
      }

      // -----------------------------------------------------------------------
      // WebSocket connection with exponential back-off
      // -----------------------------------------------------------------------
      function connect() {
        if (isDisposed) { return; }

        setStatus('Connecting\u2026', 'connecting');

        try {
          ws = new WebSocket(SERVER_URL);
        } catch (err) {
          console.error('[UzEngine] WebSocket construction failed:', err);
          scheduleReconnect();
          return;
        }

        ws.binaryType = 'arraybuffer';

        ws.addEventListener('open', function () {
          console.log('[UzEngine] WebSocket connected to', SERVER_URL);
          setStatus('Connected', 'connected');
          reconnectDelay = 1000; // reset back-off on successful connection
        });

        ws.addEventListener('message', function (e) {
          if (!(e.data instanceof ArrayBuffer)) {
            console.warn('[UzEngine] Received non-binary message:', typeof e.data);
            return;
          }

          // Convert ArrayBuffer to base64 data URL.
          var bytes = new Uint8Array(e.data);
          var binary = '';
          for (var i = 0; i < bytes.byteLength; i++) {
            binary += String.fromCharCode(bytes[i]);
          }
          var dataUrl = 'data:image/jpeg;base64,' + btoa(binary);

          if (frameCount === 0) {
            console.log('[UzEngine] First frame: ' + e.data.byteLength + ' bytes, setting img.src');
            diagEl.style.display = 'none';
          }

          img.src = dataUrl;

          // FPS counter: count frames, update display once per second.
          frameCount++;
          var now = Date.now();
          if (now - lastFpsTick >= 1000) {
            fpsEl.textContent = Math.round(frameCount * 1000 / (now - lastFpsTick)) + ' fps';
            frameCount  = 0;
            lastFpsTick = now;
          }
        });

        ws.addEventListener('close', function (e) {
          console.log('[UzEngine] WebSocket closed. Code:', e.code, 'Reason:', e.reason);
          setStatus('Reconnecting\u2026', 'disconnected');
          scheduleReconnect();
        });

        ws.addEventListener('error', function (e) {
          console.error('[UzEngine] WebSocket error:', e);
          // The 'close' event fires immediately after; reconnect is handled there.
        });
      }

      function scheduleReconnect() {
        if (isDisposed) { return; }
        var delay = reconnectDelay;
        setStatus('Reconnecting in ' + (delay / 1000).toFixed(1) + 's\u2026', 'disconnected');
        fpsEl.textContent = '-- fps';

        reconnectTimer = setTimeout(function () {
          reconnectTimer = null;
          // Double the delay, capped at MAX_RECONNECT.
          reconnectDelay = Math.min(reconnectDelay * 2, MAX_RECONNECT);
          connect();
        }, delay);
      }

      function disconnect() {
        isDisposed = true;
        if (reconnectTimer !== null) {
          clearTimeout(reconnectTimer);
          reconnectTimer = null;
        }
        if (ws) {
          ws.close();
          ws = null;
        }
      }

      // -----------------------------------------------------------------------
      // Handle messages from the extension host (e.g. configuration updates).
      // -----------------------------------------------------------------------
      window.addEventListener('message', function (event) {
        var message = event.data;
        if (!message || typeof message !== 'object') { return; }

        if (message.type === 'updateServerUrl' && typeof message.url === 'string') {
          if (message.url !== SERVER_URL) {
            console.log('[UzEngine] Server URL changed to', message.url);
            SERVER_URL = message.url;
            disconnect();
            isDisposed = false;
            reconnectDelay = 1000;
            connect();
          }
        }

        if (message.type === 'resume') {
          // Canvas does not need an explicit play() call — the WebSocket
          // pipeline is always running while connected. This handler is kept
          // as a no-op for forward compatibility with the extension host.
        }
      });

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
