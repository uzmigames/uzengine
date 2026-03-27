/// UzEngine Viewport panel — WebviewPanel that drives the native render loop.
///
/// The native DX12 viewport renders on a dedicated render thread. Encoded H.264
/// NAL units are streamed from that thread to the webview via two callbacks
/// registered with `addon.startRenderLoop`:
///
///  - `onFrame`        — per-frame stats (fps, frameMs, drawCalls, …)
///  - `onEncodedFrame` — H.264 NAL Buffer; forwarded as binary to the WebView
///                       where `VideoDecoder` (WebCodecs) decodes and blits to
///                       a `<canvas>`.
///
/// Input forwarding: the WebView now sends `event.code` strings (e.g. `'KeyW'`)
/// rather than numeric keyCodes.  The extension host translates them to Win32
/// virtual-key codes via `ViewportPanel.KEY_MAP` before calling `addon.onKey`.
///
/// Render loop lifecycle:
///  - Panel hidden  → `addon.stopRenderLoop()`  (saves GPU)
///  - Panel visible → `addon.startRenderLoop(60, onFrame, onEncodedFrame)`

import * as vscode from 'vscode';
import type { UzEngineAddon } from '../uzengine';
import type { SceneTreeProvider } from './scene-tree-panel';

// ── Types ─────────────────────────────────────────────────────────────────────

/** Union of all messages sent from the WebView JS to the extension host. */
type WebViewMessage =
    | { type: 'ready' }
    | { type: 'mouseMove'; x: number; y: number }
    | { type: 'mouseButton'; button: number; pressed: boolean }
    /** `code` is the `KeyboardEvent.code` string, e.g. `'KeyW'`, `'ArrowUp'`. */
    | { type: 'key'; code: string; pressed: boolean }
    | { type: 'scroll'; delta: number };

// ── ViewportPanel ─────────────────────────────────────────────────────────────

/// Manages a `vscode.WebviewPanel` that hosts the UzEngine viewport.
///
/// Lifecycle:
///  1. Constructed → creates the WebviewPanel + starts the render loop once
///     the WebView signals `ready`.
///  2. Panel hidden → render loop paused (`addon.stopRenderLoop`).
///  3. Panel revealed → render loop resumed (`addon.startRenderLoop`).
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
        if (this.renderLoopActive || this.disposed) return;

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

        // Start the native render thread at 60 FPS.
        // onFrame    — receives per-frame stats from the render thread.
        // onEncoded  — receives a H.264 NAL Buffer; forwarded as binary to the
        //              WebView where VideoDecoder (WebCodecs) decodes it.
        this.addon.startRenderLoop(
            60,
            // onFrame callback
            (stats: {
                fps: number;
                frameMs: number;
                drawCalls: number;
                vertices: number;
                triangles: number;
            }) => {
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
            // onEncodedFrame callback
            (nalBuffer: Buffer) => {
                if (this.disposed) return;
                // Transfer ownership of the underlying ArrayBuffer to the WebView
                // via the transferable objects mechanism to avoid a copy.
                const uint8 = new Uint8Array(
                    nalBuffer.buffer,
                    nalBuffer.byteOffset,
                    nalBuffer.byteLength
                );
                void this.panel.webview.postMessage({ type: 'frame', data: uint8 });
            }
        );

        this.renderLoopActive = true;

        // Send SPS/PPS codec extradata so the WebView can configure VideoDecoder.
        try {
            const extradata = this.addon.getCodecExtradata();
            if (extradata) {
                void this.panel.webview.postMessage({
                    type: 'init',
                    extradata: Array.from(extradata as Uint8Array),
                });
            }
        } catch {
            // getCodecExtradata may not yet be available if the encoder is still
            // initialising; the WebView will request a re-init on next key frame.
        }
    }

    private stopRenderLoop(): void {
        if (this.renderLoopActive) {
            try {
                this.addon.stopRenderLoop();
            } catch {
                // Ignore — addon may be torn down during extension deactivation.
            }
            this.renderLoopActive = false;
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
            case 'key': {
                const vk = ViewportPanel.KEY_MAP[msg.code];
                if (vk !== undefined) {
                    this.addon.onKey(vk, msg.pressed);
                }
                break;
            }
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
    ///  - A `<canvas id="viewport-canvas">` that fills the viewport area and
    ///    receives decoded H.264 frames via the WebCodecs `VideoDecoder` API.
    ///  - A fallback `#viewport-overlay` shown while waiting for the first frame.
    ///  - Input forwarding for mouse, wheel, and keyboard events (`event.code`).
    ///  - Stats overlay driven by `postMessage` from the extension host.
    ///  - WebCodecs decoder: handles `init` (SPS/PPS config) and `frame` (NAL)
    ///    messages from the extension host.
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

    /* ── Viewport area ── */
    #viewport-container {
        position: relative;
        width: 100%;
        height: calc(100vh - 28px);
        background: #111111;
        overflow: hidden;
        outline: none;
    }

    /* Canvas fills the container and is the render target for decoded frames. */
    #viewport-canvas {
        display: block;
        width: 100%;
        height: 100%;
        /* Nearest-neighbour scaling keeps pixel-perfect look for non-native res. */
        image-rendering: pixelated;
    }

    /* Overlay shown while waiting for the first decoded video frame. */
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

<!-- Viewport: canvas receives H.264 frames; overlay fades once decoding starts. -->
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

    // ── Canvas / 2-D context ────────────────────────────────────────────────
    const canvas  = /** @type {HTMLCanvasElement} */ (document.getElementById('viewport-canvas'));
    const overlay = document.getElementById('viewport-overlay');
    const ctx2d   = canvas.getContext('2d');

    // Track whether we have ever drawn a frame (to hide the overlay once).
    var firstFrameDrawn = false;

    function onFirstFrame() {
        if (firstFrameDrawn) return;
        firstFrameDrawn = true;
        if (overlay) overlay.classList.add('hidden');
    }

    // ── WebCodecs VideoDecoder ───────────────────────────────────────────────
    // A null decoder means we are waiting for the 'init' message.
    var decoder = null;

    /**
     * Initialise (or re-initialise) the VideoDecoder with the SPS/PPS
     * extradata from the encoder.
     *
     * @param {Uint8Array} extradata  Raw AVCC/Annex-B SPS+PPS bytes.
     */
    function initDecoder(extradata) {
        // Close any previous decoder before creating a new one.
        if (decoder !== null) {
            try { decoder.close(); } catch (_) {}
            decoder = null;
        }

        // Size the canvas to match the encode resolution (1280x720 default).
        // The CSS makes it fill the container regardless of the intrinsic size.
        canvas.width  = 1280;
        canvas.height = 720;

        decoder = new VideoDecoder({
            output: function (frame) {
                // Draw the decoded VideoFrame onto the 2-D canvas.
                ctx2d.drawImage(frame, 0, 0, canvas.width, canvas.height);
                frame.close();
                onFirstFrame();
            },
            error: function (e) {
                console.error('[UzEngine] VideoDecoder error:', e);
                // On error, reset so the next 'init' message can recover.
                decoder = null;
            },
        });

        decoder.configure({
            codec: 'avc1.42E01F',   // H.264 Baseline Level 3.1
            codedWidth:  canvas.width,
            codedHeight: canvas.height,
            description: extradata, // SPS/PPS as AVCC extra data
            hardwareAcceleration: 'prefer-hardware',
            optimizeForLatency: true,
        });
    }

    // ── Toolbar gizmo mode buttons ──────────────────────────────────────────
    var gizmoGroup = ['btn-translate', 'btn-rotate', 'btn-scale'];
    var spaceGroup  = ['btn-local', 'btn-world'];

    function setActiveInGroup(groupIds, clickedId) {
        groupIds.forEach(function (id) {
            var el = document.getElementById(id);
            if (el) {
                el.classList.toggle('active', id === clickedId);
            }
        });
    }

    gizmoGroup.forEach(function (id) {
        var el = document.getElementById(id);
        if (el) {
            el.addEventListener('click', function () { setActiveInGroup(gizmoGroup, id); });
        }
    });
    spaceGroup.forEach(function (id) {
        var el = document.getElementById(id);
        if (el) {
            el.addEventListener('click', function () { setActiveInGroup(spaceGroup, id); });
        }
    });

    // ── Input forwarding ────────────────────────────────────────────────────
    var container = document.getElementById('viewport-container');

    container.addEventListener('mousemove', function (e) {
        vscode.postMessage({ type: 'mouseMove', x: e.offsetX, y: e.offsetY });
    });

    container.addEventListener('mousedown', function (e) {
        container.focus();
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: true });
        e.preventDefault();
    });

    container.addEventListener('mouseup', function (e) {
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: false });
    });

    container.addEventListener('contextmenu', function (e) {
        e.preventDefault();
    });

    container.addEventListener('wheel', function (e) {
        vscode.postMessage({ type: 'scroll', delta: e.deltaY > 0 ? -1 : 1 });
        e.preventDefault();
    }, { passive: false });

    // Use event.code (layout-independent) instead of keyCode.
    document.addEventListener('keydown', function (e) {
        vscode.postMessage({ type: 'key', code: e.code, pressed: true });
        e.preventDefault();
    });

    document.addEventListener('keyup', function (e) {
        vscode.postMessage({ type: 'key', code: e.code, pressed: false });
    });

    // ── Message handler ─────────────────────────────────────────────────────
    window.addEventListener('message', function (event) {
        var msg = event.data;

        if (msg.type === 'init') {
            // SPS/PPS extradata — configure the VideoDecoder.
            var extradataArray = new Uint8Array(msg.extradata);
            initDecoder(extradataArray);

        } else if (msg.type === 'frame') {
            // Encoded H.264 NAL unit — feed to the VideoDecoder.
            if (decoder === null || decoder.state === 'closed') return;
            try {
                // Determine chunk type: IDR NAL type (5) = key frame, else delta.
                var nalData = msg.data;
                // NAL unit type is in the lower 5 bits of the first byte.
                // For Annex-B streams, skip the start code (00 00 00 01 or 00 00 01).
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
                    timestamp: performance.now() * 1000, // microseconds
                    data: nalData,
                });
                decoder.decode(chunk);
            } catch (e) {
                console.error('[UzEngine] decode error:', e);
            }

        } else if (msg.type === 'stats') {
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
