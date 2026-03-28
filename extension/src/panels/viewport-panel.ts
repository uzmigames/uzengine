/// UzEngine Viewport panel — WebviewPanel that drives the native render loop.
///
/// The native DX12 viewport renders in the isolated worker process. Encoded
/// H.264 NAL units are streamed from the worker to the extension host via IPC,
/// then forwarded to the webview via `postMessage`. The webview uses WebCodecs
/// `VideoDecoder` to decode and blit frames onto a `<canvas>`.
///
/// Input forwarding: the WebView sends `event.code` strings (e.g. `'KeyW'`)
/// rather than numeric keyCodes.  The extension host translates them to Win32
/// virtual-key codes via `ViewportPanel.KEY_MAP` before calling
/// `client.keyEx()`.
///
/// Render loop lifecycle:
///  - Panel hidden  → `client.stopRenderLoop()`  (saves GPU)
///  - Panel visible → `client.startRenderLoop(60, onFrame, onEncodedFrame)`

import * as vscode from 'vscode';
import type { RuntimeClient } from '../runtime-client';
import type { SceneTreeProvider } from './scene-tree-panel';

// ── Types ─────────────────────────────────────────────────────────────────────

/** Union of all messages sent from the WebView JS to the extension host. */
type WebViewMessage =
    | { type: 'ready' }
    | { type: 'mouseMove'; x: number; y: number }
    /**
     * Sent while the pointer is locked (right-click camera orbit).
     * `dx`/`dy` are the raw `MouseEvent.movementX/Y` deltas in pixels.
     */
    | { type: 'mouseDelta'; dx: number; dy: number }
    | { type: 'mouseButton'; button: number; pressed: boolean }
    /** `code` is the `KeyboardEvent.code` string, e.g. `'KeyW'`, `'ArrowUp'`. */
    | { type: 'key'; code: string; pressed: boolean; modifiers: number }
    | { type: 'scroll'; delta: number };

// ── ViewportPanel ─────────────────────────────────────────────────────────────

/// Manages a `vscode.WebviewPanel` that hosts the UzEngine viewport.
///
/// Lifecycle:
///  1. Constructed → creates the WebviewPanel + starts the render loop once
///     the WebView signals `ready`.
///  2. Panel hidden → render loop paused (`client.stopRenderLoop`).
///  3. Panel revealed → render loop resumed (`client.startRenderLoop`).
///  4. `dispose()` → render loop stopped, native viewport destroyed, emitter fired.
export class ViewportPanel {
    private readonly panel: vscode.WebviewPanel;
    private renderLoopActive = false;
    private disposed = false;
    private viewportCreated = false;

    /**
     * Translation table from `KeyboardEvent.code` (DOM standard) to Win32
     * virtual-key codes.  Any code not present in this map is silently ignored.
     */
    private static readonly KEY_MAP: Readonly<Record<string, number>> = {
        'KeyW': 0x57, 'KeyA': 0x41, 'KeyS': 0x53, 'KeyD': 0x44,
        'KeyQ': 0x51, 'KeyE': 0x45, 'KeyF': 0x46, 'KeyG': 0x47,
        'KeyR': 0x52, 'KeyT': 0x54, 'KeyZ': 0x5A, 'KeyX': 0x58,
        'KeyC': 0x43, 'KeyV': 0x56,
        'Space':        0x20,
        'ShiftLeft':    0x10, 'ShiftRight':   0x10,
        'ControlLeft':  0x11, 'ControlRight':  0x11,
        'AltLeft':      0x12, 'AltRight':      0x12,
        'ArrowUp':      0x26, 'ArrowDown':     0x28,
        'ArrowLeft':    0x25, 'ArrowRight':    0x27,
        'F1':  0x70, 'F2':  0x71, 'F3':  0x72, 'F4':  0x73,
        'F5':  0x74, 'F6':  0x75, 'F7':  0x76, 'F8':  0x77,
        'F9':  0x78, 'F10': 0x79, 'F11': 0x7A, 'F12': 0x7B,
        'Escape':    0x1B, 'Tab':    0x09, 'Enter':    0x0D, 'Backspace': 0x08,
        'Delete':    0x2E, 'Insert': 0x2D, 'Home':     0x24, 'End':       0x23,
        'PageUp':    0x21, 'PageDown': 0x22,
        'Digit0': 0x30, 'Digit1': 0x31, 'Digit2': 0x32, 'Digit3': 0x33,
        'Digit4': 0x34, 'Digit5': 0x35, 'Digit6': 0x36, 'Digit7': 0x37,
        'Digit8': 0x38, 'Digit9': 0x39,
    };

    private readonly _onDidDispose = new vscode.EventEmitter<void>();
    /// Fired when this ViewportPanel is disposed (panel closed or `dispose()` called).
    readonly onDidDispose: vscode.Event<void> = this._onDidDispose.event;

    /// The RuntimeClient used to communicate with the isolated worker process.
    private readonly client: RuntimeClient;

    constructor(
        extensionUri: vscode.Uri,
        clientOrGetter: RuntimeClient | (() => RuntimeClient),
        private readonly sceneTree: SceneTreeProvider
    ) {
        // Accept either a client instance or a lazy getter for backward compat.
        this.client = typeof clientOrGetter === 'function'
            ? clientOrGetter()
            : clientOrGetter;

        this.panel = vscode.window.createWebviewPanel(
            'uzengine.viewport',
            'UzEngine Viewport',
            vscode.ViewColumn.One,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                localResourceRoots: [extensionUri],
            }
        );

        this.panel.webview.html = this.buildHtml();

        // Dispose this wrapper when the panel is closed by the user.
        this.panel.onDidDispose(() => this.dispose());

        // Handle messages arriving from the WebView JavaScript.
        this.panel.webview.onDidReceiveMessage((msg: WebViewMessage) => {
            void this.handleWebViewMessage(msg);
        });

        // Pause / resume the render loop when the panel visibility changes.
        this.panel.onDidChangeViewState(() => {
            if (this.panel.visible) {
                void this.startRenderLoop();
            } else {
                void this.stopRenderLoop();
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
        void this.stopRenderLoop().then(() => {
            if (this.viewportCreated) {
                void this.client.destroyViewport().catch(() => {
                    // Ignore — the worker may already be torn down.
                });
                this.viewportCreated = false;
            }
        });
        this.panel.dispose();
        this._onDidDispose.fire();
        this._onDidDispose.dispose();
    }

    // ── Private — render loop ─────────────────────────────────────────────────

    private async startRenderLoop(): Promise<void> {
        if (this.renderLoopActive || this.disposed) return;

        // Create the native viewport the first time the loop starts.
        if (!this.viewportCreated) {
            try {
                await this.client.createViewport(0, 1280, 720);
                await this.client.setEditorMode(true);
                this.viewportCreated = true;

                // Auto-load project scene if a .uzproject is found in the workspace.
                await this.autoLoadProjectScene();
            } catch {
                // Viewport may already exist. Treat as created so we still drive
                // the render loop.
                this.viewportCreated = true;
            }
        }

        // Start the native render thread at 60 FPS.
        await this.client.startRenderLoop(
            60,
            // onFrame callback — per-frame stats
            (stats) => {
                if (this.disposed) return;
                void this.panel.webview.postMessage({
                    type: 'stats',
                    fps: stats.fps,
                    frameMs: stats.frameMs,
                    drawCalls: stats.drawCalls,
                    vertices: stats.vertices,
                    triangles: stats.triangles,
                });
            },
            // onEncodedFrame callback — H.264 NAL as base64 string
            (nalBase64: string) => {
                if (this.disposed) return;
                void this.panel.webview.postMessage({ type: 'frame', data: nalBase64 });
            }
        );

        this.renderLoopActive = true;

        // Send SPS/PPS codec extradata so the WebView can configure VideoDecoder.
        try {
            const extradata = await this.client.getCodecExtradata();
            if (extradata) {
                void this.panel.webview.postMessage({
                    type: 'init',
                    extradata: Array.from(extradata as Uint8Array),
                });
            }
        } catch {
            // getCodecExtradata may not yet be available if the encoder is
            // still initialising; the WebView will request a re-init on the
            // next key frame.
        }
    }

    private async stopRenderLoop(): Promise<void> {
        if (!this.renderLoopActive) return;
        try {
            await this.client.stopRenderLoop();
        } catch {
            // Ignore — worker may be torn down during extension deactivation.
        }
        this.renderLoopActive = false;
    }

    // ── Private — message handling ────────────────────────────────────────────

    private async handleWebViewMessage(msg: WebViewMessage): Promise<void> {
        switch (msg.type) {
            case 'ready':
                await this.startRenderLoop();
                break;
            case 'mouseMove':
                this.client.mouseMove(msg.x, msg.y);
                break;
            case 'mouseDelta':
                this.client.mouseMove(msg.dx, msg.dy);
                break;
            case 'mouseButton':
                this.client.mouseButton(msg.button, msg.pressed);
                break;
            case 'key': {
                const vk = ViewportPanel.KEY_MAP[msg.code];
                if (vk !== undefined) {
                    this.client.keyEx(vk, msg.pressed, msg.modifiers ?? 0);
                }
                break;
            }
            case 'scroll':
                this.client.scroll(msg.delta);
                break;
        }
    }

    // ── Private — auto scene load ─────────────────────────────────────────────

    /// Searches the workspace for a .uzproject file, reads its default_scene,
    /// sets the content root, and loads the scene automatically.
    private async autoLoadProjectScene(): Promise<void> {
        try {
            const workspaceFolders = vscode.workspace.workspaceFolders;
            if (!workspaceFolders) return;

            // eslint-disable-next-line @typescript-eslint/no-require-imports
            const fs = require('fs') as typeof import('fs');
            // eslint-disable-next-line @typescript-eslint/no-require-imports
            const nodePath = require('path') as typeof import('path');

            for (const folder of workspaceFolders) {
                const root = folder.uri.fsPath;

                const candidates = [
                    nodePath.join(root, 'sample', 'projects', 'pbr_showcase.uzproject'),
                    ...this.findFiles(root, '.uzproject', 3),
                ];

                for (const projectPath of candidates) {
                    if (!fs.existsSync(projectPath)) continue;

                    let projectJson: { default_scene?: string; content_root?: string };
                    try {
                        projectJson = JSON.parse(fs.readFileSync(projectPath, 'utf8')) as typeof projectJson;
                    } catch {
                        continue;
                    }

                    const defaultScene = projectJson.default_scene;
                    const contentRoot = projectJson.content_root ?? '../content/';

                    if (!defaultScene) continue;

                    const projectDir = nodePath.dirname(projectPath);
                    const resolvedContentRoot = nodePath.resolve(projectDir, contentRoot);
                    const scenePath = nodePath.join(resolvedContentRoot, defaultScene);

                    if (!fs.existsSync(scenePath)) continue;

                    await this.client.setContentRoot(resolvedContentRoot);
                    await this.client.loadScene(scenePath);
                    this.sceneTree.refresh();
                    return;
                }
            }
        } catch {
            // Auto-load is best-effort; ignore all errors.
        }
    }

    /// Recursively finds files with a given extension up to maxDepth.
    private findFiles(dir: string, ext: string, maxDepth: number): string[] {
        if (maxDepth <= 0) return [];
        // eslint-disable-next-line @typescript-eslint/no-require-imports
        const fs = require('fs') as typeof import('fs');
        // eslint-disable-next-line @typescript-eslint/no-require-imports
        const nodePath = require('path') as typeof import('path');
        const results: string[] = [];
        try {
            for (const entry of fs.readdirSync(dir, { withFileTypes: true })) {
                const full = nodePath.join(dir, entry.name);
                if (entry.isFile() && entry.name.endsWith(ext)) {
                    results.push(full);
                } else if (
                    entry.isDirectory() &&
                    !entry.name.startsWith('.') &&
                    entry.name !== 'node_modules'
                ) {
                    results.push(...this.findFiles(full, ext, maxDepth - 1));
                }
            }
        } catch { /* permission denied etc. */ }
        return results;
    }

    // ── Private — HTML ────────────────────────────────────────────────────────

    /// Returns the complete inline HTML for the WebviewPanel.
    private buildHtml(): string {
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

    /* ── Viewport area ── */
    #viewport-container {
        position: relative;
        width: 100%;
        height: calc(100vh - 28px);
        background: #111111;
        overflow: hidden;
        outline: none;
    }

    #viewport-canvas {
        display: block;
        width: 100%;
        height: 100%;
        image-rendering: pixelated;
    }

    #viewport-overlay {
        position: absolute;
        inset: 0;
        display: flex;
        flex-direction: column;
        align-items: center;
        justify-content: center;
        gap: 12px;
        color: #555555;
        pointer-events: none;
        transition: opacity 0.3s;
    }

    #viewport-overlay.hidden {
        opacity: 0;
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

<div id="viewport-container" tabindex="0">
    <canvas id="viewport-canvas"></canvas>
    <div id="viewport-overlay">
        <div id="viewport-icon">&#x1F5A5;</div>
        <div id="viewport-msg">Waiting for render stream&hellip;</div>
        <div id="viewport-sub">H.264 stream will appear here once the engine is ready</div>
    </div>
</div>

<script>
(function () {
    'use strict';

    const vscode = acquireVsCodeApi();

    const canvas  = /** @type {HTMLCanvasElement} */ (document.getElementById('viewport-canvas'));
    const overlay = document.getElementById('viewport-overlay');
    const ctx2d   = canvas.getContext('2d');

    var firstFrameDrawn = false;

    function onFirstFrame() {
        if (firstFrameDrawn) return;
        firstFrameDrawn = true;
        if (overlay) overlay.classList.add('hidden');
    }

    var hasWebCodecs = (typeof VideoDecoder !== 'undefined');
    var usingJpegFallback = !hasWebCodecs;
    var decoder = null;

    function initDecoder(extradata) {
        if (usingJpegFallback) return;

        if (decoder !== null) {
            try { decoder.close(); } catch (_) {}
            decoder = null;
        }

        canvas.width  = 1280;
        canvas.height = 720;

        decoder = new VideoDecoder({
            output: function (frame) {
                ctx2d.drawImage(frame, 0, 0, canvas.width, canvas.height);
                frame.close();
                onFirstFrame();
            },
            error: function (e) {
                console.error('[UzEngine] VideoDecoder error:', e);
                decoder = null;
            },
        });

        try {
            decoder.configure({
                codec: 'avc1.42E01F',
                codedWidth:  canvas.width,
                codedHeight: canvas.height,
                description: extradata,
                hardwareAcceleration: 'prefer-hardware',
                optimizeForLatency: true,
            });
        } catch (configErr) {
            console.warn('[UzEngine] VideoDecoder.configure() failed, switching to JPEG fallback:', configErr);
            try { decoder.close(); } catch (_) {}
            decoder = null;
            usingJpegFallback = true;
        }
    }

    var jpegImg = new Image();
    jpegImg.onload = function () {
        ctx2d.drawImage(jpegImg, 0, 0, canvas.width, canvas.height);
        onFirstFrame();
    };

    var gizmoGroup = ['btn-translate', 'btn-rotate', 'btn-scale'];
    var spaceGroup  = ['btn-local', 'btn-world'];

    function setActiveInGroup(groupIds, clickedId) {
        groupIds.forEach(function (id) {
            var el = document.getElementById(id);
            if (el) el.classList.toggle('active', id === clickedId);
        });
    }

    gizmoGroup.forEach(function (id) {
        var el = document.getElementById(id);
        if (el) el.addEventListener('click', function () { setActiveInGroup(gizmoGroup, id); });
    });
    spaceGroup.forEach(function (id) {
        var el = document.getElementById(id);
        if (el) el.addEventListener('click', function () { setActiveInGroup(spaceGroup, id); });
    });

    var container = document.getElementById('viewport-container');
    var viewportFocused = false;

    container.addEventListener('focus', function () { viewportFocused = true; });
    container.addEventListener('blur',  function () { viewportFocused = false; });

    canvas.addEventListener('mousedown', function (e) {
        if (e.button === 2) canvas.requestPointerLock();
        container.focus();
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: true });
        e.preventDefault();
    });

    canvas.addEventListener('mouseup', function (e) {
        if (e.button === 2 && document.pointerLockElement === canvas) {
            document.exitPointerLock();
        }
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: false });
    });

    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });

    document.addEventListener('mousemove', function (e) {
        if (document.pointerLockElement === canvas) {
            vscode.postMessage({ type: 'mouseDelta', dx: e.movementX, dy: e.movementY });
        } else if (viewportFocused) {
            vscode.postMessage({ type: 'mouseMove', x: e.offsetX, y: e.offsetY });
        }
    });

    container.addEventListener('wheel', function (e) {
        vscode.postMessage({ type: 'scroll', delta: e.deltaY > 0 ? -1 : 1 });
        e.preventDefault();
    }, { passive: false });

    document.addEventListener('keydown', function (e) {
        if (!viewportFocused) return;
        vscode.postMessage({
            type: 'key',
            code: e.code,
            pressed: true,
            modifiers: (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0)
        });
        e.preventDefault();
    });

    document.addEventListener('keyup', function (e) {
        if (!viewportFocused) return;
        vscode.postMessage({
            type: 'key',
            code: e.code,
            pressed: false,
            modifiers: (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0) | (e.metaKey ? 8 : 0)
        });
    });

    window.addEventListener('message', function (event) {
        var msg = event.data;

        if (msg.type === 'init') {
            if (!usingJpegFallback) {
                var extradataArray = new Uint8Array(msg.extradata);
                initDecoder(extradataArray);
            }
        } else if (msg.type === 'frame') {
            if (usingJpegFallback) return;
            if (decoder === null || decoder.state === 'closed') return;
            try {
                var nalData = msg.data;
                var startOffset = 0;
                if (nalData[0] === 0 && nalData[1] === 0 && nalData[2] === 0 && nalData[3] === 1) {
                    startOffset = 4;
                } else if (nalData[0] === 0 && nalData[1] === 0 && nalData[2] === 1) {
                    startOffset = 3;
                }
                var nalType = nalData[startOffset] & 0x1F;
                var isKeyFrame = (nalType === 5 || nalType === 7 || nalType === 8);

                var chunk = new EncodedVideoChunk({
                    type: isKeyFrame ? 'key' : 'delta',
                    timestamp: performance.now() * 1000,
                    data: nalData,
                });
                decoder.decode(chunk);
            } catch (e) {
                console.error('[UzEngine] decode error:', e);
            }
        } else if (msg.type === 'jpeg') {
            if (!usingJpegFallback) return;
            if (msg.width && msg.height) {
                canvas.width  = msg.width;
                canvas.height = msg.height;
            } else if (canvas.width === 0 || canvas.height === 0) {
                canvas.width  = 1280;
                canvas.height = 720;
            }
            jpegImg.src = 'data:image/jpeg;base64,' + msg.data;
        } else if (msg.type === 'stats') {
            var statsEl = document.getElementById('stats');
            if (statsEl) {
                var parts = [
                    msg.fps.toFixed(0) + ' FPS',
                    msg.frameMs.toFixed(1) + ' ms',
                    msg.drawCalls + ' draw calls',
                    msg.triangles + ' tris',
                ];
                if (msg.encoder)     parts.push(String(msg.encoder));
                if (msg.resolution)  parts.push(String(msg.resolution));
                else if (canvas.width > 0 && canvas.height > 0) {
                    parts.push(canvas.width + '\u00D7' + canvas.height);
                }
                statsEl.textContent = parts.join('\u2002|\u2002');
            }
        }
    });

    vscode.postMessage({ type: 'ready' });
}());
</script>
</body>
</html>`;
    }
}
