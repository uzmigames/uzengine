# Viewport Integration — H.264 Frame Streaming, Input Routing, and Toolbar

> **Status**: Infrastructure ready (crash protection, hidden HWND, library mode). H.264 encoder pending (phase2-2.1.0).
> **Target Architecture**: H.264 Media Foundation → WebCodecs VideoDecoder → Canvas @ 120 FPS (validated by POC 0.2)
> **Source**: `extension/src/panels/scene-editor-provider.ts`, `napi/src/napi_viewport.cpp`, `runtime/engine/app/runtime.cpp`
> **References**: `docs/engine/29-design-decisions/02-poc-lessons.md`, `docs/engine/28-ipc-protocol/03-frame-streaming.md`

## Purpose

The Viewport Integration layer renders the 3D scene inside a VSCode editor tab as a webview `<canvas>`. The runtime encodes frames as H.264 via Media Foundation hardware encoder, streams the bitstream to the webview via N-API typed arrays, and the webview decodes with WebCodecs `VideoDecoder` at 120 FPS with <5ms latency. This is the architecture validated by POC 0.2.

## Target Architecture (H.264 — POC 0.2 Validated)

```
Runtime (.node addon, in-process)
  ├─ DX12 render to offscreen RT (hidden HWND, no visible window)
  ├─ GPU readback to CPU (triple-buffered)
  ├─ H.264 encode via Media Foundation (HW accelerated, 2-4ms)
  └─ N-API typed array (zero-copy Uint8Array)
       │
       ▼
VSCode Webview (Canvas)
  ├─ WebCodecs VideoDecoder (HW accelerated, 1-2ms)
  ├─ VideoFrame → Canvas drawImage
  └─ 120 FPS @ 854x480, 80-100 FPS @ 1080p
```

### POC 0.2 Benchmarks

| Operation | Latency |
|-----------|---------|
| N-API function call | <1 µs |
| H.264 frame encode (GPU) | 2-4 ms |
| WebCodecs decode | 1-2 ms |
| Canvas render | <1 ms |
| **Total latency** | **3-7 ms** |

| Resolution | Bitrate | FPS |
|------------|---------|-----|
| 854x480 | 5 Mbps | 120 FPS |
| 1280x720 | 8 Mbps | 120 FPS |
| 1920x1080 | 15 Mbps | 80-100 FPS |

See `docs/engine/29-design-decisions/02-poc-lessons.md` for full POC data.

## Implementation Status

### Done: Infrastructure (v0.2.52)

- **Hidden HWND**: `uz_create_viewport(0, w, h)` creates HWND for DX12 swapchain but never shows it. No standalone window.
- **Library mode**: `PlatformWindow::SetLibraryMode(true)` — no `PostQuitMessage`, scoped `PeekMessage`.
- **Crash handler (VEH)**: writes minidump + crash reports to `native/crashes/`, propagates to `__try/__except`.
- **N-API SEH wrappers**: `uz_seh_render_frame()`, `uz_seh_create_viewport()`, etc. — crash → JS exception.
- **Extension crash detection**: stops render loop on SEH exception, shows VSCode error notification.
- **Temporary PNG fallback**: `captureScreenshot()` → base64 PNG → webview canvas @ 30 FPS (placeholder until H.264).

### Pending: H.264 Pipeline (phase2-2.1.0)

Full task list in `.rulebook/tasks/phase2-2.1.0-viewport-hwnd-embedding/tasks.md`:

- **Phase A**: `H264Encoder` class — Media Foundation, Baseline profile, low-latency CBR, triple-buffered readback, encoder thread
- **Phase B**: C API (`uz_encode_frame`) + N-API bridge (`encodeFrame(): Buffer`)
- **Phase C**: WebView `VideoDecoder` — `avc1.42001e`, HW-accelerated, `optimizeForLatency: true`
- **Phase D**: Input forwarding verification on `<canvas>`
- **Phase E**: Lifecycle (panel hide/reveal/resize), adaptive bitrate, error recovery, JPEG fallback

## Library Mode (Crash Protection)

When running inside VSCode/Electron, the runtime enables library mode:

1. **PlatformWindow::SetLibraryMode(true)** — prevents host interference:
   - `WM_DESTROY` skips `PostQuitMessage(0)` (would kill Electron)
   - `PumpMessages()` scopes `PeekMessageW` to our HWND only

2. **CrashHandler library mode (VEH)** — crash reports without termination:
   - `AddVectoredExceptionHandler` writes minidump + txt + json
   - Returns `EXCEPTION_CONTINUE_SEARCH` → inner `__try/__except` catches it
   - Crash files: `native/crashes/{YYYY-MM-DD_HH-MM-SS}/`

3. **N-API SEH wrappers** — structured exception → JS exception:
   - `uz_seh_render_frame()`, `uz_seh_create_viewport()`, etc.
   - `UzNapiCheckSeh()` converts to `Napi::Error`

4. **Extension crash detection** — consecutive error counting:
   - On SEH exception or 5+ consecutive failures: stops render loop
   - Shows VSCode error notification with crash details

## Current SceneEditorProvider (Temporary PNG Fallback)

Until H.264 encoder is implemented, frames are streamed as PNG base64 at ~30 FPS.
This will be replaced by H.264 `encodeFrame()` → `postMessage(Uint8Array)` → `VideoDecoder`.

```typescript
// TEMPORARY: PNG fallback. Target: H.264 MF + WebCodecs @ 120 FPS
class SceneEditorProvider implements vscode.CustomTextEditorProvider {
    resolveCustomTextEditor(document, webviewPanel, _token) {
        this.ensureViewport();       // addon.init() + addon.createViewport(0, 1280, 720)
        this.loadScene(document);    // addon.loadScene(path)
        this.startRenderLoop(panel); // 30 FPS PNG (will become 120 FPS H.264)
    }
}

    /// Get webview HTML that embeds the HWND and provides controls.
    private getWebviewContent(): string {
        return `
            <!DOCTYPE html>
            <html>
            <head>
                <style>
                    * { margin: 0; padding: 0; box-sizing: border-box; }
                    body {
                        display: flex;
                        flex-direction: column;
                        height: 100vh;
                        background: #1a1a1a;
                        color: #e0e0e0;
                        font-family: system-ui, -apple-system, sans-serif;
                    }

                    #toolbar {
                        display: flex;
                        gap: 10px;
                        padding: 8px;
                        background: #252525;
                        border-bottom: 1px solid #3a3a3a;
                        align-items: center;
                    }

                    .toolbar-group {
                        display: flex;
                        gap: 5px;
                        align-items: center;
                    }

                    button {
                        padding: 6px 12px;
                        background: #3a3a3a;
                        color: #e0e0e0;
                        border: none;
                        border-radius: 4px;
                        cursor: pointer;
                        font-size: 12px;
                    }

                    button:hover {
                        background: #4a4a4a;
                    }

                    button.active {
                        background: #0078d4;
                        color: white;
                    }

                    #viewport-container {
                        flex: 1;
                        position: relative;
                        overflow: hidden;
                    }

                    #hwnd-container {
                        width: 100%;
                        height: 100%;
                        /* HWND is embedded here via native code */
                    }

                    .stats {
                        position: absolute;
                        top: 10px;
                        right: 10px;
                        background: rgba(0, 0, 0, 0.7);
                        padding: 10px;
                        font-family: monospace;
                        font-size: 12px;
                        color: #0f0;
                        pointer-events: none;
                    }
                </style>
            </head>
            <body>
                <div id="toolbar">
                    <div class="toolbar-group">
                        <button id="play" title="Play (P)">▶ Play</button>
                        <button id="pause" title="Pause (Space)">⏸ Pause</button>
                        <button id="stop" title="Stop (Escape)">⏹ Stop</button>
                    </div>

                    <div style="width: 1px; background: #3a3a3a; height: 24px;"></div>

                    <div class="toolbar-group">
                        <button id="translate" title="Translate (W)">W</button>
                        <button id="rotate" title="Rotate (E)">E</button>
                        <button id="scale" title="Scale (R)">R</button>
                    </div>

                    <div style="width: 1px; background: #3a3a3a; height: 24px;"></div>

                    <div class="toolbar-group">
                        <button id="local" title="Local space (L)">Local</button>
                        <button id="world" class="active" title="World space">World</button>
                    </div>

                    <div style="width: 1px; background: #3a3a3a; height: 24px;"></div>

                    <div class="toolbar-group">
                        <label style="display: flex; align-items: center; gap: 5px;">
                            <input type="checkbox" id="snap-toggle" checked>
                            Snap
                        </label>
                    </div>

                    <div style="flex: 1;"></div>

                    <div class="toolbar-group">
                        <button id="camera-ortho" title="Orthographic view">Ortho</button>
                        <button id="camera-persp" class="active" title="Perspective view">Persp</button>
                    </div>

                    <div class="toolbar-group">
                        <button id="shading-lit" class="active" title="Lit (L)">Lit</button>
                        <button id="shading-unlit" title="Unlit (U)">Unlit</button>
                        <button id="shading-normals" title="Normals (N)">Normals</button>
                    </div>
                </div>

                <div id="viewport-container">
                    <div id="hwnd-container"></div>
                    <div class="stats">
                        <div>FPS: <span id="fps">0</span></div>
                        <div>Frame: <span id="frame">0</span>ms</div>
                        <div>Tris: <span id="tris">0</span></div>
                    </div>
                </div>

                <script>
                    const vscode = acquireVsCodeApi();

                    // Toolbar button handlers
                    document.getElementById("play").onclick = () =>
                        vscode.postMessage({ type: "play" });
                    document.getElementById("pause").onclick = () =>
                        vscode.postMessage({ type: "pause" });
                    document.getElementById("stop").onclick = () =>
                        vscode.postMessage({ type: "stop" });

                    document.getElementById("translate").onclick = () =>
                        vscode.postMessage({ type: "gizmo_mode", mode: "translate" });
                    document.getElementById("rotate").onclick = () =>
                        vscode.postMessage({ type: "gizmo_mode", mode: "rotate" });
                    document.getElementById("scale").onclick = () =>
                        vscode.postMessage({ type: "gizmo_mode", mode: "scale" });

                    document.getElementById("snap-toggle").onchange = (e) =>
                        vscode.postMessage({ type: "snap_enable", enabled: e.target.checked });

                    // Forward mouse/keyboard to engine
                    const container = document.getElementById("hwnd-container");
                    container.onmousemove = (e) => vscode.postMessage({
                        type: "mouse_move",
                        x: e.clientX,
                        y: e.clientY,
                    });
                    container.onmousedown = (e) => vscode.postMessage({
                        type: "mouse_down",
                        button: e.button,
                    });
                    container.onmouseup = (e) => vscode.postMessage({
                        type: "mouse_up",
                        button: e.button,
                    });
                    container.onwheel = (e) => vscode.postMessage({
                        type: "mouse_wheel",
                        delta: e.deltaY,
                    });

                    document.onkeydown = (e) => {
                        if (e.target === document.body) {
                            vscode.postMessage({ type: "key_down", key: e.key });
                        }
                    };
                    document.onkeyup = (e) => {
                        if (e.target === document.body) {
                            vscode.postMessage({ type: "key_up", key: e.key });
                        }
                    };

                    // Listen for messages from extension
                    window.addEventListener("message", (e) => {
                        const msg = e.data;
                        if (msg.type === "stats") {
                            document.getElementById("fps").textContent = msg.fps;
                            document.getElementById("frame").textContent = msg.frameMs;
                            document.getElementById("tris").textContent = msg.triangles;
                        }
                    });
                </script>
            </body>
            </html>
        `;
    }

    /// Set up input event forwarding to the engine.
    private setupInputRouting(): void {
        this.viewportPanel.webview.onDidReceiveMessage((msg) => {
            switch (msg.type) {
                case "mouse_move":
                    this.nativeModule.onMouseMove(msg.x, msg.y);
                    break;
                case "mouse_down":
                    this.nativeModule.onMouseDown(msg.button);
                    break;
                case "mouse_up":
                    this.nativeModule.onMouseUp(msg.button);
                    break;
                case "mouse_wheel":
                    this.nativeModule.onMouseWheel(msg.delta);
                    break;
                case "key_down":
                    this.nativeModule.onKeyDown(msg.key);
                    break;
                case "key_up":
                    this.nativeModule.onKeyUp(msg.key);
                    break;
                case "gizmo_mode":
                    vscode.commands.executeCommand("uz-engine.setGizmoMode", msg.mode);
                    break;
                case "snap_enable":
                    vscode.commands.executeCommand("uz-engine.enableSnap", msg.enabled);
                    break;
                case "play":
                    vscode.commands.executeCommand("uz-engine.play");
                    break;
                case "pause":
                    vscode.commands.executeCommand("uz-engine.pause");
                    break;
                case "stop":
                    vscode.commands.executeCommand("uz-engine.stop");
                    break;
            }
        });
    }

    /// Set up toolbar with gizmo/camera/shading controls.
    private setupToolbar(): void {
        // Toolbar is embedded in HTML above
        // Additional logic can be added here for state management
    }
}
```

## Input Routing

Forward viewport input to the engine's input dispatcher:

```cpp
// editor/viewport/input_dispatcher.h

/// Dispatch input event to gizmo system, camera, or entity.
class InputDispatcher {
public:
    /// Route mouse position to orbit camera or gizmo.
    void OnMouseMove(int x, int y);

    /// Route mouse button to gizmo or selection.
    void OnMouseDown(int x, int y, MouseButton button);
    void OnMouseUp(MouseButton button);

    /// Route keyboard to gizmo constraints or shortcuts.
    void OnKeyDown(Key key);
    void OnKeyUp(Key key);

private:
    enum InputMode {
        OrbitCamera,  // Right-mouse drag to orbit
        GizmoManip,   // Gizmo is being dragged
        Selection,    // Click to select
    };

    InputMode input_mode_ = InputMode::OrbitCamera;
};
```

## Toolbar Controls

### Play/Pause/Stop
Controls game loop and simulation:

| Button | Action | Keyboard | Description |
|---|---|---|---|
| **Play** | Start simulation | P | Run physics, animations, scripts |
| **Pause** | Pause simulation | Space | Freeze frame, allow camera control |
| **Stop** | Reset to start | Escape | Stop simulation, restore initial state |

### Gizmo Mode (W, E, R)
Select transform gizmo:

| Button | Gizmo | Keyboard | Description |
|---|---|---|---|
| **W** | Translate | W | Move entity along X/Y/Z |
| **E** | Rotate | E | Rotate around X/Y/Z |
| **R** | Scale | R | Scale along X/Y/Z |

### Transform Space (L, Space)
Toggle between local and world space:

| Mode | Keyboard | Description |
|---|---|---|
| **Local** | L | Gizmo axes align to entity's orientation |
| **World** | (Space) | Gizmo axes fixed to world XYZ |

### Snapping
Enable/disable grid snapping:

| Control | Description |
|---|---|
| **Snap toggle** | Checkbox to enable/disable grid snapping |
| **Snap size** | Configured in preferences (e.g., 0.25m grid) |

### Camera Mode
Switch between orthographic and perspective:

| Mode | Description |
|---|---|
| **Perspective** | Standard 3D view (camera has FOV) |
| **Orthographic** | Flattened view (useful for 2D layout) |

### Shading Mode
Choose rendering visualization:

| Mode | Description |
|---|---|
| **Lit** | Full PBR rendering with lights |
| **Unlit** | Albedo only (flat colors, no lighting) |
| **Normals** | Surface normals as colors (debug) |

## Performance Stats

Display real-time engine stats overlay:

```typescript
/// Send stats to viewport every frame.
function updateViewportStats(fps: number, frameMs: number, triangleCount: number) {
    viewportPanel.webview.postMessage({
        type: "stats",
        fps: Math.round(fps),
        frameMs: frameMs.toFixed(2),
        triangles: triangleCount.toLocaleString(),
    });
}
```

## Multi-Viewport (Phase 4)

Support split view (2x2 quad) for simultaneous top/front/right/perspective:

```typescript
interface ViewportSplit {
    /// Number of views: 1 (single), 2 (horizontal), 4 (quad)
    layout: "single" | "horizontal" | "vertical" | "quad";

    /// Active viewport for gizmo interaction
    activeTile: number;
}
```

Each tile renders a different camera view but receives input from a single gizmo.

## Related Docs

- **[Overview (§41.01)](01-overview.md)** — Architecture and layout switching
- **[Panel Management (§41.03)](03-panel-management.md)** — Panel lifecycle for viewport
- **[IDE Compatibility (§41.04)](04-compatibility.md)** — Input routing across IDEs
- **[Editor Framework (§40)](../40-editor-framework/01-overview.md)** — Viewport client (C++) and gizmo system

## Crash Report Format

Crash files are written to `native/crashes/{YYYY-MM-DD_HH-MM-SS}/`:

| File | Purpose |
|------|---------|
| `crash.dmp` | Windows minidump — open in Visual Studio for full stack trace with symbols |
| `crash.txt` | Human-readable: exception code, address, frame number, scene, RHI op, GPU adapter, stack trace |
| `crash.json` | Machine-parseable: same data as txt, structured for automated analysis or CI integration |

### crash.json Schema

```json
{
  "engine": "UzEngine",
  "timestamp": "2026-03-25_14-30-00",
  "exception": {
    "code": "0xC0000005",
    "name": "EXCEPTION_ACCESS_VIOLATION",
    "address": "0x00007FF6A1234567"
  },
  "context": {
    "frameNumber": 142,
    "sceneName": "scenes/main.uzscene",
    "lastRhiOp": "DrawIndexedInstanced",
    "gpuAdapter": "NVIDIA GeForce RTX 4090",
    "gpuDeviceRemoved": false
  },
  "stackTrace": [
    {"index": 0, "address": "0x00007FF6A1234567"},
    {"index": 1, "address": "0x00007FF6A1234568"}
  ]
}
```
