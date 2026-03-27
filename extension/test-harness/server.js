#!/usr/bin/env node
/// UzEngine Test Harness — WebSocket Bridge Server
///
/// Loads the N-API addon directly, starts the render loop, and bridges
/// frames + stats + input to the browser test harness via WebSocket.
///
/// Usage:
///   node server.js [--port 9090] [--fps 60]
///
/// The server:
///   1. Loads uzengine.node from extension/native/
///   2. Creates a viewport (offscreen)
///   3. Starts WebSocket server on :9090
///   4. On client connect: starts render loop, streams H.264 NAL + stats
///   5. Receives input events from client, forwards to engine

const path = require('path');
const http = require('http');

// Parse CLI args
const args = process.argv.slice(2);
let port = 9090;
let targetFps = 60;
for (let i = 0; i < args.length; i++) {
    if (args[i] === '--port' && args[i + 1]) port = parseInt(args[++i]);
    if (args[i] === '--fps' && args[i + 1]) targetFps = parseInt(args[++i]);
}

// ── Load N-API addon ─────────────────────────────────────────────────────

const nativePath = path.resolve(__dirname, '..', 'native');
const addonPath = path.join(nativePath, 'uzengine.node');

console.log(`[Server] Loading addon from: ${addonPath}`);

let addon;
try {
    addon = require(addonPath);
} catch (e) {
    console.error(`[Server] Failed to load addon: ${e.message}`);
    console.error(`[Server] Make sure to build and deploy: cd napi && npx cmake-js compile && powershell scripts/deploy-napi.ps1`);
    process.exit(1);
}

// ── Initialize engine ────────────────────────────────────────────────────

console.log(`[Server] Initializing engine...`);

// Set content root so engine finds DLLs
addon.setContentRoot(nativePath);

// Set log callback
addon.setLogCallback((level, category, message) => {
    const prefix = level >= 3 ? 'ERR' : level >= 2 ? 'WRN' : 'INF';
    console.log(`[Engine/${prefix}] [${category}] ${message}`);
});

// Initialize runtime
try {
    addon.runtimeInit();
    console.log(`[Server] Runtime initialized`);
} catch (e) {
    console.error(`[Server] Runtime init failed: ${e.message}`);
    process.exit(1);
}

// Create viewport (offscreen — parentHwnd = 0)
try {
    addon.createViewport(0, 1280, 720);
    addon.setEditorMode(true);
    console.log(`[Server] Viewport created (1280x720 offscreen)`);
} catch (e) {
    console.error(`[Server] Viewport creation failed: ${e.message}`);
}

// ── WebSocket server (minimal, no dependency) ────────────────────────────

const { WebSocketServer } = (() => {
    try {
        return require('ws');
    } catch {
        console.error('[Server] "ws" package not found. Installing...');
        require('child_process').execSync('npm install ws', { cwd: __dirname, stdio: 'inherit' });
        return require('ws');
    }
})();

const httpServer = http.createServer((req, res) => {
    // Serve the test harness HTML
    if (req.url === '/' || req.url === '/index.html') {
        const fs = require('fs');
        const html = fs.readFileSync(path.join(__dirname, 'index.html'), 'utf8');
        res.writeHead(200, { 'Content-Type': 'text/html' });
        res.end(html);
    } else {
        res.writeHead(404);
        res.end('Not found');
    }
});

const wss = new WebSocketServer({ server: httpServer });

let activeClient = null;
let renderLoopActive = false;

wss.on('connection', (ws) => {
    console.log(`[Server] Client connected`);
    activeClient = ws;

    ws.on('message', (data) => {
        try {
            const msg = JSON.parse(data.toString());
            handleClientMessage(ws, msg);
        } catch (e) {
            console.error(`[Server] Invalid message: ${e.message}`);
        }
    });

    ws.on('close', () => {
        console.log(`[Server] Client disconnected`);
        stopStreaming();
        activeClient = null;
    });
});

function handleClientMessage(ws, msg) {
    switch (msg.type) {
        case 'init':
            startStreaming(ws, msg);
            break;
        case 'stop':
            stopStreaming();
            break;
        case 'requestKeyframe':
            try { addon.requestKeyframe(); } catch {}
            break;
        case 'key':
            try { addon.onKeyEx(msg.vk, msg.pressed, msg.modifiers || 0); } catch {}
            break;
        case 'mouseMove':
            try { addon.onMouseMove(msg.x, msg.y); } catch {}
            break;
        case 'mouseDelta':
            try { addon.onMouseMove(msg.dx, msg.dy); } catch {}
            break;
        case 'mouseButton':
            try { addon.onMouseButton(msg.button, msg.pressed); } catch {}
            break;
        case 'scroll':
            try { addon.onScroll(msg.delta); } catch {}
            break;
    }
}

function startStreaming(ws, initMsg) {
    if (renderLoopActive) return;

    const fps = initMsg.fps || targetFps;
    const mode = initMsg.mode || 'h264';

    console.log(`[Server] Starting streaming: ${mode} @ ${fps}fps`);

    // Send codec extradata for H.264 init
    if (mode === 'h264') {
        try {
            const extradata = addon.getCodecExtradata();
            if (extradata) {
                ws.send(JSON.stringify({
                    type: 'init',
                    extradata: Array.from(extradata),
                }));
                console.log(`[Server] Sent extradata: ${extradata.length} bytes`);
            }
        } catch (e) {
            console.log(`[Server] No extradata available: ${e.message}`);
        }
    }

    // Start render loop
    try {
        addon.startRenderLoop(
            fps,
            // onFrame — stats callback
            (stats) => {
                if (!activeClient || activeClient.readyState !== 1) return;
                try {
                    activeClient.send(JSON.stringify({
                        type: 'stats',
                        fps: stats.fps,
                        frameMs: stats.frameMs,
                        drawCalls: stats.drawCalls,
                        vertices: stats.vertices,
                        triangles: stats.triangles,
                    }));
                } catch {}
            },
            // onEncodedFrame — NAL callback
            (nalBuffer) => {
                if (!activeClient || activeClient.readyState !== 1) return;
                try {
                    if (mode === 'h264') {
                        // Send binary NAL data
                        activeClient.send(nalBuffer, { binary: true });
                    }
                } catch {}
            }
        );
        renderLoopActive = true;
        console.log(`[Server] Render loop started`);
    } catch (e) {
        console.error(`[Server] Failed to start render loop: ${e.message}`);
        // Fallback to sync render + JPEG
        startJpegFallback(ws, fps);
    }
}

function startJpegFallback(ws, fps) {
    console.log(`[Server] Using JPEG fallback at ${Math.min(fps, 15)}fps`);
    const interval = Math.max(1000 / Math.min(fps, 15), 66);

    const timer = setInterval(() => {
        if (!activeClient || activeClient.readyState !== 1) {
            clearInterval(timer);
            return;
        }
        try {
            const stats = addon.renderFrame();
            ws.send(JSON.stringify({ type: 'stats', ...stats }));

            const screenshot = addon.captureScreenshot();
            if (screenshot) {
                ws.send(JSON.stringify({
                    type: 'jpeg',
                    data: screenshot.toString('base64'),
                }));
            }
        } catch {}
    }, interval);

    renderLoopActive = true;
}

function stopStreaming() {
    if (!renderLoopActive) return;
    try { addon.stopRenderLoop(); } catch {}
    renderLoopActive = false;
    console.log(`[Server] Render loop stopped`);
}

// ── Start server ─────────────────────────────────────────────────────────

httpServer.listen(port, () => {
    console.log(`\n╔══════════════════════════════════════════════════╗`);
    console.log(`║  UzEngine Test Harness Server                    ║`);
    console.log(`║  http://localhost:${port}/                          ║`);
    console.log(`║  WebSocket: ws://localhost:${port}                   ║`);
    console.log(`║  Target FPS: ${targetFps}                                ║`);
    console.log(`╚══════════════════════════════════════════════════╝\n`);
    console.log(`Open http://localhost:${port}/ in Chrome and click Connect.`);
});

// Graceful shutdown
process.on('SIGINT', () => {
    console.log('\n[Server] Shutting down...');
    stopStreaming();
    try { addon.destroyViewport(); } catch {}
    try { addon.runtimeShutdown(); } catch {}
    process.exit(0);
});
