/// UzEngine Scene Editor — CustomTextEditorProvider for .uzscene files.
///
/// Pipeline (H.264 hardware-accelerated frame streaming):
///   DX12 render → GPU readback → H.264 encode (FFmpeg libavcodec HW) → IPC Buffer
///   → postMessage(Uint8Array, [transfer]) → WebCodecs VideoDecoder → VideoFrame → canvas
///
/// Architecture: **push model** — the runtime drives its own render thread at
/// the requested FPS and pushes encoded frames + stats via N-API callbacks that
/// are forwarded over IPC from the worker process.
///
/// The WebView contains a full-size <canvas> where each decoded frame is drawn.
/// Toolbar (scene name, gizmo buttons, stats) overlays the canvas.
/// Input (mouse, keyboard, scroll) is captured on the canvas and forwarded
/// to the runtime via RuntimeClient (fire-and-forget IPC commands).

import * as vscode from 'vscode';
import type { RuntimeClient } from '../runtime-client';
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

    private renderLoopActive = false;
    private viewportCreated = false;
    private readyInProgress = false;
    private pendingWidth = 1280;
    private pendingHeight = 720;
    private lastKeyframeRequest = 0;
    private lastMouseMove = 0;

    /// Tracks the active webview panel session. Each call to
    /// `resolveCustomTextEditor` increments this counter so that stale
    /// `handleReady` invocations from the previous session are ignored.
    private sessionId = 0;

    /// Lazy getter for the RuntimeClient. Invoked only when a scene editor tab
    /// is actually opened, not at extension activation time.
    private readonly getClient: () => RuntimeClient;

    private readonly log: vscode.LogOutputChannel;

    constructor(
        clientGetter: () => RuntimeClient,
        private readonly sceneTree: SceneTreeProvider,
        log: vscode.LogOutputChannel
    ) {
        this.getClient = clientGetter;
        this.log = log;
    }

    resolveCustomTextEditor(
        document: vscode.TextDocument,
        webviewPanel: vscode.WebviewPanel,
        _token: vscode.CancellationToken
    ): void {
        // Reset all per-session state and restart the worker process.
        // The SceneEditorProvider is a singleton — state carries over from a
        // previous session. The worker may have a DEVICE REMOVED GPU state
        // from the previous session, so we kill it and start fresh.
        this.sessionId++;
        const mySession = this.sessionId;
        this.viewportCreated = false;
        this.renderLoopActive = false;
        this.readyInProgress = false;
        try { this.getClient().restart(); } catch { /* no client yet */ }

        webviewPanel.webview.options = { enableScripts: true };
        webviewPanel.webview.html = this.buildHtml(document);

        webviewPanel.webview.onDidReceiveMessage((msg: WebViewMessage) => {
            // Ignore messages from stale sessions (e.g. the old webview still
            // sending after resolveCustomTextEditor was called again).
            if (mySession !== this.sessionId) return;

            if (!this.viewportCreated) {
                // Only accept 'ready' and 'resize' before viewport is created.
                if (msg.type === 'ready') {
                    void this.handleReady(document, webviewPanel, mySession);
                } else if (msg.type === 'resize' && msg.width > 0 && msg.height > 0) {
                    this.pendingWidth = msg.width;
                    this.pendingHeight = msg.height;
                }
                return;
            }

            const client = this.getClient();
            switch (msg.type) {
                case 'ready':
                    // Webview reloaded (e.g. after tab switch with retainContextWhenHidden=false).
                    // Viewport already exists — just restart the render loop.
                    if (!this.renderLoopActive) {
                        void this.startRenderLoop(webviewPanel);
                    }
                    break;
                case 'mouseMove': {
                    const now = Date.now();
                    if (now - this.lastMouseMove >= 16) {
                        this.lastMouseMove = now;
                        client.mouseMove(msg.x, msg.y);
                    }
                    break;
                }
                case 'mouseButton':
                    client.mouseButton(msg.button, msg.pressed);
                    break;
                case 'key':
                    client.key(msg.keycode, msg.pressed);
                    break;
                case 'scroll':
                    client.scroll(msg.delta);
                    break;
                case 'decoderError':
                    // Decoder will recover on next in-band keyframe. No action needed.
                    break;
                case 'resize':
                    if (msg.width > 0 && msg.height > 0) {
                        void client.resizeViewport(msg.width, msg.height).catch(() => {});
                    }
                    break;
            }
        });

        webviewPanel.onDidChangeViewState(() => {
            if (!this.viewportCreated) return;
            if (webviewPanel.visible) {
                // Tab became visible — resume render loop.
                if (!this.renderLoopActive) {
                    void this.startRenderLoop(webviewPanel);
                }
            } else {
                // Tab hidden — pause render loop to save GPU.
                void this.stopRenderLoop();
            }
        });

        webviewPanel.onDidDispose(() => {
            // Full cleanup: stop render, destroy viewport.
            void this.stopRenderLoop();
            if (this.viewportCreated) {
                void this.getClient().destroyViewport().catch(() => {});
                this.viewportCreated = false;
            }
        });
    }

    // ── Private — engine lifecycle ────────────────────────────────────────────

    /// Full initialization — runs ONCE per document. Creates viewport, loads
    /// scene, starts render loop. Subsequent 'ready' messages just resume.
    ///
    /// @param session  The session counter value captured when this editor was
    ///   opened. If `this.sessionId` has advanced past `session` by the time
    ///   this async function resumes after any `await`, the initialization is
    ///   for a stale session and must be aborted.
    private async handleReady(
        document: vscode.TextDocument,
        webviewPanel: vscode.WebviewPanel,
        session: number
    ): Promise<void> {
        if (this.readyInProgress || this.viewportCreated) return;
        this.readyInProgress = true;

        try {
            this.log.info('[SceneEditor] handleReady: full initialization');

            // Set content root BEFORE init() and createViewport() so the VFS is
            // initialized with the correct CWD during uz_runtime_init() /
            // uz_create_viewport(). The pending root is consumed inside the native
            // layer when the viewport is created.
            const scenePath = document.uri.fsPath;
            const contentDir = scenePath.replace(/[\\/]scenes[\\/][^\\/]+$/, '');
            const client = this.getClient();
            if (contentDir !== scenePath) {
                await client.setContentRoot(contentDir);
                this.log.info(`[SceneEditor] setContentRoot: "${contentDir}" (before init+viewport)`);
            } else {
                this.log.warn('[SceneEditor] handleReady: no /scenes/ segment in path — setContentRoot skipped');
            }

            // After every await, guard against a newer session taking over.
            if (session !== this.sessionId) {
                this.log.info('[SceneEditor] handleReady: stale session — aborting');
                return;
            }

            await this.ensureViewport();

            if (session !== this.sessionId) {
                this.log.info('[SceneEditor] handleReady: stale session after ensureViewport — aborting');
                return;
            }

            const sceneLoaded = await this.loadScene(document);
            if (!sceneLoaded) {
                this.log.error('[SceneEditor] Scene load failed — render loop NOT started');
                return;
            }

            if (session !== this.sessionId) {
                this.log.info('[SceneEditor] handleReady: stale session after loadScene — aborting');
                return;
            }

            await this.startRenderLoop(webviewPanel);
            this.log.info('[SceneEditor] handleReady: complete');
        } catch (e: unknown) {
            const msg = e instanceof Error ? e.message : String(e);
            this.log.error(`[SceneEditor] handleReady failed: ${msg}`);
        } finally {
            this.readyInProgress = false;
        }
    }

    private async ensureViewport(): Promise<void> {
        if (this.viewportCreated) return;
        const client = this.getClient();

        // init() is idempotent — safe to call multiple times.
        try {
            await client.init();
            this.log.info('[SceneEditor] client.init() OK');
        } catch { /* already initialized — OK */ }

        // Destroy any stale viewport from a previous session before creating
        // a new one. uz_create_viewport refuses to create a second viewport.
        try { await client.destroyViewport(); } catch { /* none exists — OK */ }

        try {
            await client.createViewport(0, this.pendingWidth, this.pendingHeight);
            await client.setEditorMode(true);
            this.viewportCreated = true;
            this.log.info(`[SceneEditor] createViewport(${this.pendingWidth}x${this.pendingHeight}) OK`);
        } catch (e: unknown) {
            const msg = e instanceof Error ? e.message : String(e);
            this.log.error(`[SceneEditor] createViewport FAILED: ${msg}`);
            void vscode.window.showErrorMessage(`UzEngine viewport creation failed: ${msg}`);
            throw e;
        }
    }

    private async loadScene(document: vscode.TextDocument): Promise<boolean> {
        const client = this.getClient();
        const scenePath = document.uri.fsPath;
        this.log.info(`[SceneEditor] loadScene: path="${scenePath}"`);

        // setContentRoot was already called in handleReady before viewport
        // creation. Re-apply it here in case the content dir changed (e.g. the
        // user loaded a scene from a different project). This is idempotent.
        const contentDir = scenePath.replace(/[\\/]scenes[\\/][^\\/]+$/, '');
        if (contentDir !== scenePath) {
            try {
                await client.setContentRoot(contentDir);
                this.log.info(`[SceneEditor] setContentRoot (re-apply): "${contentDir}" OK`);
            } catch (e: unknown) {
                const msg = e instanceof Error ? e.message : String(e);
                this.log.error(`[SceneEditor] setContentRoot FAILED: ${msg}`);
                void vscode.window.showWarningMessage(`UzEngine setContentRoot failed: ${msg}`);
                return false;
            }
        }

        try {
            await client.loadScene(scenePath);
            this.log.info('[SceneEditor] loadScene OK');
            this.sceneTree.refresh();
            return true;
        } catch (e: unknown) {
            const msg = e instanceof Error ? e.message : String(e);
            this.log.error(`[SceneEditor] loadScene FAILED: ${msg}`);
            void vscode.window.showErrorMessage(`UzEngine scene load failed: ${msg}`);
            return false;
        }
    }

    /// Sends SPS/PPS extradata to the webview so VideoDecoder can be configured.
    private async sendCodecExtradata(panel: vscode.WebviewPanel): Promise<boolean> {
        try {
            const extradata = await this.getClient().getCodecExtradata();
            if (extradata && extradata.length > 0) {
                this.log.info(`[SceneEditor] sendCodecExtradata: ${extradata.length} bytes`);
                void panel.webview.postMessage({
                    type: 'codecExtradata',
                    data: Array.from(new Uint8Array(extradata)),
                });
                return true;
            }
            this.log.debug('[SceneEditor] sendCodecExtradata: no extradata yet');
            return false;
        } catch (e: unknown) {
            this.log.error(`[SceneEditor] sendCodecExtradata FAILED: ${e instanceof Error ? e.message : String(e)}`);
            return false;
        }
    }

    // ── Private — render loop (push model via worker IPC) ─────────────────────

    private async startRenderLoop(panel: vscode.WebviewPanel): Promise<void> {
        if (this.renderLoopActive) return;

        const client = this.getClient();
        const log = this.log;

        log.info('[SceneEditor] startRenderLoop: launching native render thread @ 30 FPS');
        let frameCount = 0;

        try {
            await client.startRenderLoop(
                30,
                (stats) => {
                    frameCount++;
                    if (frameCount === 1) {
                        log.info(`[SceneEditor] first frame — fps=${stats.fps.toFixed(0)} drawCalls=${stats.drawCalls} tris=${stats.triangles}`);
                    } else if (frameCount === 30) {
                        log.info(`[SceneEditor] 30 frames — fps=${stats.fps.toFixed(0)} drawCalls=${stats.drawCalls} tris=${stats.triangles}`);
                    }
                    void panel.webview.postMessage({
                        type: 'stats',
                        fps: stats.fps,
                        frameMs: stats.frameMs,
                        drawCalls: stats.drawCalls,
                        vertices: stats.vertices,
                        triangles: stats.triangles,
                    });
                },
                (nalBase64: string) => {
                    if (frameCount <= 1) {
                        log.info(`[SceneEditor] first encoded frame — ${nalBase64.length} chars base64`);
                    }
                    // Pass base64 string directly to webview. Typed arrays don't
                    // survive VSCode's postMessage serialization reliably.
                    void panel.webview.postMessage({ type: 'frame', data: nalBase64 });
                }
            );

            this.renderLoopActive = true;
            log.info('[SceneEditor] render loop active');
        } catch (e: unknown) {
            const msg = e instanceof Error ? e.message : String(e);
            log.error(`[SceneEditor] startRenderLoop FAILED: ${msg}`);
        }
    }

    private async stopRenderLoop(): Promise<void> {
        if (!this.renderLoopActive) return;
        this.log.info('[SceneEditor] stopRenderLoop');
        try {
            await this.getClient().stopRenderLoop();
            this.log.info('[SceneEditor] stopRenderLoop OK');
        } catch (e: unknown) {
            this.log.warn(`[SceneEditor] stopRenderLoop: ${e instanceof Error ? e.message : String(e)}`);
        }
        this.renderLoopActive = false;
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
        if (canvas.width !== lastResizeW || canvas.height !== lastResizeH) {
            lastResizeW = canvas.width;
            lastResizeH = canvas.height;
            vscode.postMessage({ type: 'resize', width: canvas.width, height: canvas.height });
        }
    }
    resizeCanvas();
    window.addEventListener('resize', resizeCanvas);

    var framesReceived = 0;
    var framesDecoded = 0;
    var gotFirstKeyframe = false;

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
                framesDecoded++;
                ctx.drawImage(frame, 0, 0, canvas.width, canvas.height);
                frame.close();
            },
            error: function (err) {
                // Decoder will auto-recover on next keyframe (in-band SPS/PPS).
                // Reset gotFirstKeyframe so we wait for the next IDR.
                gotFirstKeyframe = false;
            }
        });

        // Configure for Annex-B format (start codes 00 00 00 01). Do NOT pass
        // 'description' — the encoder produces Annex-B with in-band SPS/PPS,
        // not avcC (length-prefixed). Passing Annex-B bytes as avcC 'description'
        // causes "Failed to parse avcC" errors.
        var config = {
            codec: 'avc1.640028',  // High profile L4.0 — permissive
            codedWidth: canvas.width || 1280,
            codedHeight: canvas.height || 720,
            hardwareAcceleration: 'prefer-hardware',
            optimizeForLatency: true
        };

        try {
            decoder.configure(config);
            decoderReady = true;
            frameTimestamp = 0;
        } catch (cfgErr) {
            decoderReady = false;
        }
    }

    // ── NAL unit keyframe detection ───────────────────────────────────────
    function isKeyframe(data) {
        for (var i = 0; i < data.length - 4; i++) {
            if (data[i] === 0 && data[i+1] === 0 &&
                (data[i+2] === 1 || (data[i+2] === 0 && data[i+3] === 1))) {
                var offset = (data[i+2] === 1) ? i + 3 : i + 4;
                if (offset < data.length) {
                    var nalType = data[offset] & 0x1F;
                    // 5=IDR slice, 7=SPS, 8=PPS — all indicate a keyframe bundle
                    if (nalType === 5 || nalType === 7 || nalType === 8) return true;
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
    var viewportFocused = false;
    // Virtual mouse position — accumulates pointer-lock deltas so the
    // runtime's delta calculation (mouseX - prevMouseX) works correctly.
    var virtualMouseX = 0, virtualMouseY = 0;
    canvas.addEventListener('focus', function () { viewportFocused = true; });
    canvas.addEventListener('blur',  function () { viewportFocused = false; });

    canvas.addEventListener('mousedown', function (e) {
        if (e.button === 2) {
            virtualMouseX = e.offsetX;
            virtualMouseY = e.offsetY;
            canvas.requestPointerLock();
        }
        canvas.focus();
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: true });
        e.preventDefault();
    });
    canvas.addEventListener('mouseup', function (e) {
        if (e.button === 2 && document.pointerLockElement === canvas) {
            document.exitPointerLock();
        }
        vscode.postMessage({ type: 'mouseButton', button: e.button, pressed: false });
    });
    document.addEventListener('mousemove', function (e) {
        if (document.pointerLockElement === canvas) {
            // Accumulate deltas into virtual position so the runtime's
            // per-frame delta = (mouseX - prevMouseX) produces the correct value.
            virtualMouseX += e.movementX;
            virtualMouseY += e.movementY;
            vscode.postMessage({ type: 'mouseMove', x: virtualMouseX, y: virtualMouseY });
        } else if (viewportFocused) {
            vscode.postMessage({ type: 'mouseMove', x: e.offsetX, y: e.offsetY });
        }
    });
    canvas.addEventListener('contextmenu', function (e) { e.preventDefault(); });
    canvas.addEventListener('wheel', function (e) {
        vscode.postMessage({ type: 'scroll', delta: e.deltaY > 0 ? -1 : 1 });
        e.preventDefault();
    }, { passive: false });
    document.addEventListener('keydown', function (e) {
        if (!viewportFocused) return;
        vscode.postMessage({ type: 'key', keycode: e.keyCode, pressed: true });
        e.preventDefault();
    });
    document.addEventListener('keyup', function (e) {
        if (!viewportFocused) return;
        vscode.postMessage({ type: 'key', keycode: e.keyCode, pressed: false });
    });

    // ── Message handler ───────────────────────────────────────────────────
    window.addEventListener('message', function (e) {
        var msg = e.data;

        if (msg.type === 'codecExtradata') {
            // In Annex-B mode we don't pass description to VideoDecoder.
            // Just init the decoder without extradata.
            codecExtradata = msg.data;
            if (typeof VideoDecoder !== 'undefined') {
                initDecoder(null);
            }
        } else if (msg.type === 'frame') {
            // H.264 NAL unit as base64 string from the extension host.
            if (typeof VideoDecoder === 'undefined') {
                diag('VideoDecoder not available!');
                return;
            }
            if (!decoderReady) {
                initDecoder(null);
            }
            try {
                // Decode base64 → Uint8Array
                var binaryStr = atob(msg.data);
                var nalData = new Uint8Array(binaryStr.length);
                for (var j = 0; j < binaryStr.length; j++) {
                    nalData[j] = binaryStr.charCodeAt(j);
                }

                framesReceived++;
                var kf = isKeyframe(nalData);

                // Skip delta frames until we've received our first keyframe.
                if (!gotFirstKeyframe) {
                    if (!kf) return;
                    gotFirstKeyframe = true;
                }

                var chunk = new EncodedVideoChunk({
                    type: kf ? 'key' : 'delta',
                    timestamp: frameTimestamp,
                    data: nalData
                });
                frameTimestamp += 33333;  // ~30 FPS in microseconds
                decoder.decode(chunk);
            } catch (err) {
                // Silently ignore — decoder recovers on next keyframe.
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
