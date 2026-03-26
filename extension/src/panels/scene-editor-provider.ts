/// UzEngine Scene Editor — CustomTextEditorProvider for .uzscene files.
///
/// Pipeline (H.264 hardware-accelerated frame streaming):
///   DX12 render → GPU readback → H.264 encode (FFmpeg libavcodec HW) → N-API Buffer
///   → postMessage(Uint8Array, [transfer]) → WebCodecs VideoDecoder → VideoFrame → canvas
///
/// Target: 120 FPS @ 1280x720, <5ms total latency (validated by POC 0.2).
///
/// The WebView contains a full-size <canvas> where each decoded frame is drawn.
/// Toolbar (scene name, gizmo buttons, stats) overlays the canvas.
/// Input (mouse, keyboard, scroll) is captured on the canvas and forwarded
/// to the runtime via N-API.

import * as vscode from 'vscode';
import type { UzEngineAddon } from '../uzengine';
import type { SceneTreeProvider } from './scene-tree-panel';

// ── Types ─────────────────────────────────────────────────────────────────────

type WebViewMessage =
    | { type: 'ready' }
    | { type: 'mouseMove'; x: number; y: number }
    | { type: 'mouseButton'; button: number; pressed: boolean }
    | { type: 'key'; keycode: number; pressed: boolean }
    | { type: 'scroll'; delta: number }
    | { type: 'decoderError'; message: string }
    | { type: 'resize'; width: number; height: number };

// ── SceneEditorProvider ───────────────────────────────────────────────────────

export class SceneEditorProvider implements vscode.CustomTextEditorProvider {
    public static readonly viewType = 'uzengine.sceneEditor';

    private renderTimer: ReturnType<typeof setInterval> | undefined;
    private viewportCreated = false;
    private consecutiveErrors = 0;

    constructor(
        private readonly addon: UzEngineAddon,
        private readonly sceneTree: SceneTreeProvider
    ) {}

    resolveCustomTextEditor(
        document: vscode.TextDocument,
        webviewPanel: vscode.WebviewPanel,
        _token: vscode.CancellationToken
    ): void {
        webviewPanel.webview.options = { enableScripts: true };
        webviewPanel.webview.html = this.buildHtml(document);

        webviewPanel.webview.onDidReceiveMessage((msg: WebViewMessage) => {
            switch (msg.type) {
                case 'ready':
                    this.ensureViewport();
                    this.loadScene(document);
                    this.sendCodecExtradata(webviewPanel);
                    this.startRenderLoop(webviewPanel);
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
                case 'decoderError':
                    // WebCodecs decoder hit an error — send a keyframe to recover.
                    this.addon.requestKeyframe();
                    break;
                case 'resize':
                    // Webview canvas resized — resize the viewport + reinit encoder.
                    if (msg.width > 0 && msg.height > 0) {
                        this.addon.resizeViewport(msg.width, msg.height);
                        // Encoder is reinited in C++ on resize. Send new extradata.
                        this.sendCodecExtradata(webviewPanel);
                        this.addon.requestKeyframe();
                    }
                    break;
            }
        });

        webviewPanel.onDidChangeViewState(() => {
            if (webviewPanel.visible) {
                // Request keyframe so decoder has a clean reference after being hidden.
                this.addon.requestKeyframe();
                this.startRenderLoop(webviewPanel);
            } else {
                this.stopRenderLoop();
            }
        });

        webviewPanel.onDidDispose(() => {
            this.stopRenderLoop();
        });
    }

    // ── Private — engine lifecycle ────────────────────────────────────────────

    private ensureViewport(): void {
        if (this.viewportCreated) return;
        try { this.addon.init(); } catch { /* already initialized */ }
        try {
            this.addon.createViewport(0, 1280, 720);
            this.addon.setEditorMode(true);
            this.viewportCreated = true;
        } catch {
            this.viewportCreated = true;
        }
    }

    private loadScene(document: vscode.TextDocument): void {
        const scenePath = document.uri.fsPath;
        const contentDir = scenePath.replace(/[\\/]scenes[\\/][^\\/]+$/, '');
        if (contentDir !== scenePath) {
            try { this.addon.setContentRoot(contentDir); } catch { /* ignore */ }
        }

        try {
            this.addon.loadScene(scenePath);
            this.sceneTree.refresh();
        } catch { /* scene load failure — canvas stays black */ }
    }

    /// Sends SPS/PPS extradata to the webview so VideoDecoder can be configured.
    private sendCodecExtradata(panel: vscode.WebviewPanel): void {
        try {
            const extradata = this.addon.getCodecExtradata();
            if (extradata && extradata.length > 0) {
                const arr = new Uint8Array(extradata);
                void panel.webview.postMessage({
                    type: 'codecExtradata',
                    data: Array.from(arr)
                });
            }
        } catch { /* encoder not ready yet — decoder will use in-band params */ }
    }

    // ── Private — render loop (H.264 frame streaming) ─────────────────────────

    private startRenderLoop(panel: vscode.WebviewPanel): void {
        if (this.renderTimer !== undefined) return;

        this.consecutiveErrors = 0;

        // ~120 FPS target — 8ms interval. The actual frame rate is bounded by
        // the encoder (2-4ms) + GPU render time. If a frame isn't ready,
        // encodeFrame() returns null and we skip that tick.
        this.renderTimer = setInterval(() => {
            try {
                const stats = this.addon.renderFrame();

                // Try H.264 encode first; fall back to PNG screenshot.
                const nalBuf = this.addon.encodeFrame();
                if (nalBuf && nalBuf.length > 0) {
                    const arr = new Uint8Array(nalBuf);
                    void panel.webview.postMessage(
                        { type: 'frame', data: Array.from(arr) }
                    );
                } else {
                    // PNG fallback — encoder not available or buffering.
                    const pngBuf = this.addon.captureScreenshot();
                    if (pngBuf && pngBuf.length > 0) {
                        const b64 = pngBuf.toString('base64');
                        void panel.webview.postMessage(
                            { type: 'pngFrame', data: b64 }
                        );
                    }
                }

                void panel.webview.postMessage({
                    type: 'stats',
                    fps: stats.fps,
                    frameMs: stats.frameMs,
                    drawCalls: stats.drawCalls,
                    vertices: stats.vertices,
                    triangles: stats.triangles,
                });

                this.consecutiveErrors = 0;
            } catch (e: unknown) {
                this.consecutiveErrors++;
                const msg = e instanceof Error ? e.message : String(e);

                if (msg.includes('SEH exception') || this.consecutiveErrors >= 5) {
                    this.stopRenderLoop();
                    void vscode.window.showErrorMessage(
                        `UzEngine runtime crashed: ${msg}. Check native/crashes/ for details.`
                    );
                }
            }
        }, 8);
    }

    private stopRenderLoop(): void {
        if (this.renderTimer !== undefined) {
            clearInterval(this.renderTimer);
            this.renderTimer = undefined;
        }
    }

    // ── Private — HTML ────────────────────────────────────────────────────────

    private buildHtml(document: vscode.TextDocument): string {
        let sceneName = 'Unknown Scene';
        try {
            const json = JSON.parse(document.getText());
            if (json.name) sceneName = json.name;
        } catch { /* invalid JSON */ }

        const csp = [
            "default-src 'none'",
            "style-src 'unsafe-inline'",
            "script-src 'unsafe-inline'",
            "img-src data: blob:",
        ].join('; ');

        return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta http-equiv="Content-Security-Policy" content="${csp}">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>${sceneName}</title>
<style>
    *, *::before, *::after { box-sizing: border-box; margin: 0; padding: 0; }

    body {
        overflow: hidden;
        background: #111;
        color: #ccc;
        font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
        font-size: 12px;
        user-select: none;
    }

    #toolbar {
        position: absolute;
        top: 0; left: 0; right: 0;
        display: flex;
        align-items: center;
        gap: 6px;
        height: 28px;
        padding: 0 8px;
        background: rgba(37, 37, 38, 0.85);
        border-bottom: 1px solid #3c3c3c;
        z-index: 10;
    }

    #toolbar .scene-name { font-weight: 600; color: #e0e0e0; margin-right: 8px; }
    #toolbar .sep { width: 1px; height: 16px; background: #555; margin: 0 2px; }

    #toolbar button {
        background: #3c3c3c; color: #ccc; border: 1px solid #555;
        padding: 2px 10px; cursor: pointer; border-radius: 3px;
        font-size: 11px; line-height: 18px; transition: background 0.1s;
    }
    #toolbar button:hover { background: #4c4c4c; }
    #toolbar button.active { background: #0e639c; border-color: #1177bb; color: #fff; }

    #stats {
        margin-left: auto; font-size: 11px; color: #888;
        white-space: nowrap; font-variant-numeric: tabular-nums;
    }

    canvas {
        position: absolute;
        top: 0; left: 0;
        width: 100vw;
        height: 100vh;
        display: block;
    }
</style>
</head>
<body>
<canvas id="viewport" tabindex="0"></canvas>
<div id="toolbar">
    <span class="scene-name">${sceneName}</span>
    <div class="sep"></div>
    <button id="btn-translate" class="active" title="Translate (W)">T</button>
    <button id="btn-rotate"    title="Rotate (E)">R</button>
    <button id="btn-scale"     title="Scale (R)">S</button>
    <div class="sep"></div>
    <button id="btn-local"  title="Local Space">Local</button>
    <button id="btn-world"  class="active" title="World Space">World</button>
    <span id="stats">-- FPS</span>
</div>
<script>
(function () {
    'use strict';
    var vscode = acquireVsCodeApi();
    var canvas = document.getElementById('viewport');
    var ctx = canvas.getContext('2d');

    // ── Canvas sizing ─────────────────────────────────────────────────────
    var lastResizeW = 0, lastResizeH = 0;
    function resizeCanvas() {
        canvas.width = window.innerWidth;
        canvas.height = window.innerHeight;
        // Notify extension of new dimensions so viewport + encoder resize.
        if (canvas.width !== lastResizeW || canvas.height !== lastResizeH) {
            lastResizeW = canvas.width;
            lastResizeH = canvas.height;
            vscode.postMessage({ type: 'resize', width: canvas.width, height: canvas.height });
        }
    }
    resizeCanvas();
    window.addEventListener('resize', resizeCanvas);

    // ── H.264 WebCodecs VideoDecoder ──────────────────────────────────────
    var decoder = null;
    var decoderReady = false;
    var codecExtradata = null;
    var frameTimestamp = 0;

    function initDecoder(extradata) {
        if (decoder) {
            try { decoder.close(); } catch (e) { /* ignore */ }
        }

        decoder = new VideoDecoder({
            output: function (frame) {
                ctx.drawImage(frame, 0, 0, canvas.width, canvas.height);
                frame.close();
            },
            error: function (err) {
                console.error('VideoDecoder error:', err);
                decoderReady = false;
                vscode.postMessage({ type: 'decoderError', message: err.message });
            }
        });

        var config = {
            codec: 'avc1.42001e',
            codedWidth: canvas.width || 1280,
            codedHeight: canvas.height || 720,
            hardwareAcceleration: 'prefer-hardware',
            optimizeForLatency: true
        };

        if (extradata && extradata.length > 0) {
            config.description = new Uint8Array(extradata);
        }

        decoder.configure(config);
        decoderReady = true;
        frameTimestamp = 0;
    }

    // ── PNG fallback (if WebCodecs unavailable) ───────────────────────────
    var usePngFallback = (typeof VideoDecoder === 'undefined');
    var frameImg = new Image();
    var framePending = false;

    frameImg.onload = function () {
        ctx.drawImage(frameImg, 0, 0, canvas.width, canvas.height);
        framePending = false;
    };

    // ── NAL unit keyframe detection ───────────────────────────────────────
    // Scan for NAL unit type 5 (IDR) in Annex-B bitstream.
    function isKeyframe(data) {
        for (var i = 0; i < data.length - 4; i++) {
            if (data[i] === 0 && data[i+1] === 0 &&
                (data[i+2] === 1 || (data[i+2] === 0 && data[i+3] === 1))) {
                var offset = (data[i+2] === 1) ? i + 3 : i + 4;
                if (offset < data.length) {
                    var nalType = data[offset] & 0x1F;
                    if (nalType === 5) return true;
                }
            }
        }
        return false;
    }

    // ── Toolbar gizmo buttons ─────────────────────────────────────────────
    var gizmoGroup = ['btn-translate', 'btn-rotate', 'btn-scale'];
    var spaceGroup  = ['btn-local', 'btn-world'];

    function setActiveInGroup(ids, clicked) {
        ids.forEach(function (id) {
            var el = document.getElementById(id);
            if (el) el.classList.toggle('active', id === clicked);
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

    // ── Input forwarding on canvas ────────────────────────────────────────
    canvas.addEventListener('mousemove', function (e) {
        vscode.postMessage({ type: 'mouseMove', x: e.offsetX, y: e.offsetY });
    });
    canvas.addEventListener('mousedown', function (e) {
        canvas.focus();
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: true });
        e.preventDefault();
    });
    canvas.addEventListener('mouseup', function (e) {
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: false });
    });
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    canvas.addEventListener('wheel', function (e) {
        vscode.postMessage({ type: 'scroll', delta: e.deltaY > 0 ? -1 : 1 });
        e.preventDefault();
    }, { passive: false });
    document.addEventListener('keydown', function (e) {
        vscode.postMessage({ type: 'key', keycode: e.keyCode, pressed: true });
    });
    document.addEventListener('keyup', function (e) {
        vscode.postMessage({ type: 'key', keycode: e.keyCode, pressed: false });
    });

    // ── Message handler ───────────────────────────────────────────────────
    window.addEventListener('message', function (e) {
        var msg = e.data;

        if (msg.type === 'codecExtradata') {
            codecExtradata = msg.data;
            if (!usePngFallback) {
                initDecoder(codecExtradata);
            }
        } else if (msg.type === 'frame') {
            if (usePngFallback) {
                // PNG fallback path (for environments without WebCodecs).
                if (!framePending) {
                    framePending = true;
                    var bytes = new Uint8Array(msg.data);
                    var blob = new Blob([bytes], { type: 'image/png' });
                    frameImg.src = URL.createObjectURL(blob);
                }
            } else {
                // H.264 WebCodecs path.
                if (!decoderReady) {
                    initDecoder(codecExtradata);
                }
                try {
                    var nalData = new Uint8Array(msg.data);
                    var chunk = new EncodedVideoChunk({
                        type: isKeyframe(nalData) ? 'key' : 'delta',
                        timestamp: frameTimestamp,
                        data: nalData
                    });
                    frameTimestamp += 8333; // ~120 FPS in microseconds
                    decoder.decode(chunk);
                } catch (err) {
                    console.error('Decode error:', err);
                }
            }
        } else if (msg.type === 'pngFrame') {
            // PNG fallback from extension (encoder unavailable).
            if (!framePending) {
                framePending = true;
                frameImg.src = 'data:image/png;base64,' + msg.data;
            }
        } else if (msg.type === 'stats') {
            var s = document.getElementById('stats');
            if (s) {
                s.textContent =
                    msg.fps.toFixed(0) + ' FPS\\u2002|\\u2002' +
                    msg.frameMs.toFixed(1) + ' ms\\u2002|\\u2002' +
                    msg.drawCalls + ' draw calls\\u2002|\\u2002' +
                    msg.triangles + ' tris';
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
