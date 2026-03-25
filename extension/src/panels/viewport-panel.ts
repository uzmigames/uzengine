/// UzEngine Viewport panel — WebviewPanel that drives the native render loop.
///
/// The native DX12 viewport renders into a STANDALONE top-level window
/// (HWND embedding into the WebView is Phase 2.1). The WebviewPanel acts as
/// the interaction surface: it captures keyboard / mouse / scroll events and
/// forwards them to the runtime via the addon, and receives per-frame stats
/// back via `postMessage` to display in the toolbar overlay.
///
/// Render loop: `setInterval` at ~60 FPS calls `addon.renderFrame()`.  When
/// the panel is hidden (`onDidChangeViewState`) the loop is paused to avoid
/// consuming GPU resources in the background.

import * as vscode from 'vscode';
import type { UzEngineAddon } from '../uzengine';
import type { SceneTreeProvider } from './scene-tree-panel';

// ── Types ─────────────────────────────────────────────────────────────────────

/** Union of all messages sent from the WebView JS to the extension host. */
type WebViewMessage =
    | { type: 'ready' }
    | { type: 'mouseMove'; x: number; y: number }
    | { type: 'mouseButton'; button: number; pressed: boolean }
    | { type: 'key'; keycode: number; pressed: boolean }
    | { type: 'scroll'; delta: number };

// ── ViewportPanel ─────────────────────────────────────────────────────────────

/// Manages a `vscode.WebviewPanel` that hosts the UzEngine viewport.
///
/// Lifecycle:
///  1. Constructed → creates the WebviewPanel + starts the render loop once
///     the WebView signals `ready`.
///  2. Panel hidden → render loop paused.
///  3. Panel revealed → render loop resumed.
///  4. `dispose()` → render loop stopped, native viewport destroyed, emitter fired.
export class ViewportPanel {
    private readonly panel: vscode.WebviewPanel;
    private renderTimer: ReturnType<typeof setInterval> | undefined;
    private disposed = false;
    private viewportCreated = false;

    private readonly _onDidDispose = new vscode.EventEmitter<void>();
    /// Fired when this ViewportPanel is disposed (panel closed or `dispose()` called).
    readonly onDidDispose: vscode.Event<void> = this._onDidDispose.event;

    constructor(
        extensionUri: vscode.Uri,
        private readonly addon: UzEngineAddon,
        private readonly sceneTree: SceneTreeProvider
    ) {
        this.panel = vscode.window.createWebviewPanel(
            'uzengine.viewport',
            'UzEngine Viewport',
            vscode.ViewColumn.One,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                // No local resource roots needed — HTML is fully inline.
                localResourceRoots: [extensionUri],
            }
        );

        this.panel.webview.html = this.buildHtml();

        // Dispose this wrapper when the panel is closed by the user.
        this.panel.onDidDispose(() => this.dispose());

        // Handle messages arriving from the WebView JavaScript.
        this.panel.webview.onDidReceiveMessage((msg: WebViewMessage) => {
            this.handleWebViewMessage(msg);
        });

        // Pause / resume the render loop when the panel visibility changes.
        this.panel.onDidChangeViewState(() => {
            if (this.panel.visible) {
                this.startRenderLoop();
            } else {
                this.stopRenderLoop();
            }
        });
    }

    // ── Public API ────────────────────────────────────────────────────────────

    /// Brings this panel to the foreground.
    reveal(): void {
        this.panel.reveal();
    }

    /// Destroys the panel, native viewport, and all held resources.
    ///
    /// Safe to call multiple times (idempotent after first call).
    dispose(): void {
        if (this.disposed) return;
        this.disposed = true;
        this.stopRenderLoop();
        if (this.viewportCreated) {
            try {
                this.addon.destroyViewport();
            } catch {
                // Ignore — the addon may already be torn down during extension deactivation.
            }
            this.viewportCreated = false;
        }
        this.panel.dispose();
        this._onDidDispose.fire();
        this._onDidDispose.dispose();
    }

    // ── Private — render loop ─────────────────────────────────────────────────

    private startRenderLoop(): void {
        if (this.renderTimer !== undefined || this.disposed) return;

        // Create the native viewport the first time the loop starts.
        if (!this.viewportCreated) {
            try {
                // Pass parentHwnd = 0 → standalone top-level window (Phase 2.1 will embed).
                this.addon.createViewport(0, 1280, 720);
                this.addon.setEditorMode(true);
                this.viewportCreated = true;
            } catch {
                // Viewport may already exist if createViewport was called elsewhere.
                // Treat it as created so we still drive the render loop.
                this.viewportCreated = true;
            }
        }

        // ~60 FPS — 16 ms interval.
        this.renderTimer = setInterval(() => {
            if (this.disposed) {
                this.stopRenderLoop();
                return;
            }
            try {
                const stats = this.addon.renderFrame();
                void this.panel.webview.postMessage({
                    type: 'stats',
                    fps: stats.fps,
                    frameMs: stats.frameMs,
                    drawCalls: stats.drawCalls,
                    vertices: stats.vertices,
                    triangles: stats.triangles,
                });
            } catch {
                // Render errors are transient (e.g. device lost recovery).
                // The loop continues — the next frame will attempt recovery.
            }
        }, 16);
    }

    private stopRenderLoop(): void {
        if (this.renderTimer !== undefined) {
            clearInterval(this.renderTimer);
            this.renderTimer = undefined;
        }
    }

    // ── Private — message handling ────────────────────────────────────────────

    private handleWebViewMessage(msg: WebViewMessage): void {
        switch (msg.type) {
            case 'ready':
                // WebView is fully initialised — safe to start rendering.
                this.startRenderLoop();
                break;
            case 'mouseMove':
                this.addon.onMouseMove(msg.x, msg.y);
                break;
            case 'mouseButton':
                this.addon.onMouseButton(msg.button, msg.pressed);
                break;
            case 'key':
                this.addon.onKey(msg.keycode, msg.pressed);
                break;
            case 'scroll':
                this.addon.onScroll(msg.delta);
                break;
        }
    }

    // ── Private — HTML ────────────────────────────────────────────────────────

    /// Returns the complete inline HTML for the WebviewPanel.
    ///
    /// The page contains:
    ///  - A toolbar with gizmo mode buttons (Translate / Rotate / Scale) and
    ///    a space-mode toggle (Local / World).
    ///  - A placeholder `#viewport` div with an explanatory message (the real
    ///    render output is in the native window).
    ///  - Input forwarding for mouse, wheel, and keyboard events.
    ///  - Stats overlay driven by `postMessage` from the extension host.
    private buildHtml(): string {
        // Content Security Policy: only inline scripts, no external resources.
        const csp = [
            "default-src 'none'",
            "style-src 'unsafe-inline'",
            "script-src 'unsafe-inline'",
        ].join('; ');

        return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="${csp}">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>UzEngine Viewport</title>
<style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    body {
        overflow: hidden;
        background: #1e1e1e;
        color: #cccccc;
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        font-size: 12px;
        user-select: none;
    }

    /* ── Toolbar ── */
    #toolbar {
        display: flex;
        align-items: center;
        gap: 6px;
        height: 28px;
        padding: 0 8px;
        background: #252526;
        border-bottom: 1px solid #3c3c3c;
        flex-shrink: 0;
    }

    #toolbar .sep {
        width: 1px;
        height: 16px;
        background: #555;
        margin: 0 2px;
    }

    #toolbar button {
        background: #3c3c3c;
        color: #cccccc;
        border: 1px solid #555555;
        padding: 2px 10px;
        cursor: pointer;
        border-radius: 3px;
        font-size: 11px;
        line-height: 18px;
        transition: background 0.1s;
    }

    #toolbar button:hover {
        background: #4c4c4c;
    }

    #toolbar button.active {
        background: #0e639c;
        border-color: #1177bb;
        color: #ffffff;
    }

    #stats {
        margin-left: auto;
        font-size: 11px;
        color: #888888;
        white-space: nowrap;
        font-variant-numeric: tabular-nums;
    }

    /* ── Viewport placeholder ── */
    #viewport {
        width: 100%;
        height: calc(100vh - 28px);
        background: #111111;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 12px;
        color: #555555;
    }

    #viewport-icon {
        font-size: 48px;
        line-height: 1;
        opacity: 0.4;
    }

    #viewport-msg {
        font-size: 14px;
        font-weight: 500;
        color: #666666;
    }

    #viewport-sub {
        font-size: 11px;
        color: #444444;
    }
</style>
</head>
<body>
<div id="toolbar">
    <button id="btn-translate" class="active" title="Translate (W)">T</button>
    <button id="btn-rotate"    title="Rotate (E)">R</button>
    <button id="btn-scale"     title="Scale (R)">S</button>
    <div class="sep"></div>
    <button id="btn-local"  title="Local Space">Local</button>
    <button id="btn-world"  class="active" title="World Space">World</button>
    <span id="stats">-- FPS</span>
</div>
<div id="viewport" tabindex="0">
    <div id="viewport-icon">&#x1F5A5;</div>
    <div id="viewport-msg">Rendering in native viewport window</div>
    <div id="viewport-sub">Keyboard &amp; mouse input is captured here and forwarded to the engine</div>
</div>
<script>
(function () {
    'use strict';

    const vscode = acquireVsCodeApi();

    // ── Toolbar gizmo mode buttons ──────────────────────────────────────────
    const gizmoGroup = ['btn-translate', 'btn-rotate', 'btn-scale'];
    const spaceGroup  = ['btn-local', 'btn-world'];

    function setActiveInGroup(groupIds, clickedId) {
        groupIds.forEach(function (id) {
            const el = document.getElementById(id);
            if (el) {
                el.classList.toggle('active', id === clickedId);
            }
        });
    }

    gizmoGroup.forEach(function (id) {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('click', function () { setActiveInGroup(gizmoGroup, id); });
        }
    });
    spaceGroup.forEach(function (id) {
        const el = document.getElementById(id);
        if (el) {
            el.addEventListener('click', function () { setActiveInGroup(spaceGroup, id); });
        }
    });

    // ── Input forwarding ────────────────────────────────────────────────────
    const viewport = document.getElementById('viewport');

    viewport.addEventListener('mousemove', function (e) {
        vscode.postMessage({ type: 'mouseMove', x: e.offsetX, y: e.offsetY });
    });

    viewport.addEventListener('mousedown', function (e) {
        viewport.focus();
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: true });
        e.preventDefault();
    });

    viewport.addEventListener('mouseup', function (e) {
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: false });
    });

    viewport.addEventListener('contextmenu', function (e) {
        e.preventDefault();
    });

    viewport.addEventListener('wheel', function (e) {
        vscode.postMessage({ type: 'scroll', delta: e.deltaY > 0 ? -1 : 1 });
        e.preventDefault();
    }, { passive: false });

    document.addEventListener('keydown', function (e) {
        vscode.postMessage({ type: 'key', keycode: e.keyCode, pressed: true });
    });

    document.addEventListener('keyup', function (e) {
        vscode.postMessage({ type: 'key', keycode: e.keyCode, pressed: false });
    });

    // ── Stats update ────────────────────────────────────────────────────────
    window.addEventListener('message', function (e) {
        var msg = e.data;
        if (msg.type === 'stats') {
            var statsEl = document.getElementById('stats');
            if (statsEl) {
                statsEl.textContent =
                    msg.fps.toFixed(0) + ' FPS\u2002|\u2002' +
                    msg.frameMs.toFixed(1) + ' ms\u2002|\u2002' +
                    msg.drawCalls + ' draw calls\u2002|\u2002' +
                    msg.triangles + ' tris';
            }
        }
    });

    // Signal that the WebView is ready to receive render-loop messages.
    vscode.postMessage({ type: 'ready' });
}());
</script>
</body>
</html>`;
    }
}
