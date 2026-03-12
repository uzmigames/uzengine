# Viewport Integration — Embedding, Input Routing, and Toolbar

> **Status**: Planned (Phase 2–3)
> **Source**: `editor/viewport/viewport_embedding.ts`, `editor/viewport/viewport_client.cpp`
> **References**: VSCode WebviewPanel containment, Win32 HWND parenting, message routing for cross-process input

## Purpose

The Viewport Integration layer manages the 3D HWND viewport within Engine Mode. The runtime N-API module creates a native window and D3D12 swapchain; the VSCode extension must embed this HWND into the UI, route input events, and provide toolbar controls for camera, rendering mode, and snap settings.

## Viewport Architecture

```
VSCode Extension (TypeScript)
    │
    ├── viewport_embedding.ts               — Webview container for HWND
    ├── viewport_toolbar.ts                 — Camera, shading, snap controls
    └── input_router.ts                     — Forward keyboard/mouse to engine
            │
            └──► N-API Runtime (C++)
                 │
                 ├── viewport_client.h/cpp   — HWND creation, input handling
                 ├── editor_viewport.h/cpp   — Window messages (WM_MOUSEMOVE, etc.)
                 └── input_dispatcher.h/cpp  — Route input to gizmo/camera/orbit
```

## HWND Embedding Strategy

The runtime creates an HWND child window. The extension embeds it into a webview container.

```typescript
class ViewportEmbedding {
    private viewportPanel: vscode.WebviewPanel;
    private viewportHwnd: number | null = null;  // Win32 HWND handle
    private nativeModule: any;  // N-API module

    async create(viewColumn: vscode.ViewColumn): Promise<void> {
        // 1. Create webview panel to hold viewport
        this.viewportPanel = vscode.window.createWebviewPanel(
            "uz-viewport",
            "Viewport",
            viewColumn,
            {
                enableScripts: true,
                retainContextWhenHidden: true,
                portMapping: [],  // No port mapping needed (HWND-based)
            }
        );

        // 2. Initialize N-API runtime and create HWND
        this.nativeModule = require("uz_engine.node");
        const result = this.nativeModule.createViewportWindow({
            width: 1920,
            height: 1080,
            parentHwnd: 0,  // Will be set after webview container is created
        });

        if (!result.success) {
            throw new Error(`Failed to create viewport: ${result.error}`);
        }

        this.viewportHwnd = result.hwnd;

        // 3. Provide viewport HTML with drag/drop and input routing
        this.viewportPanel.webview.html = this.getWebviewContent();

        // 4. Set up input routing
        this.setupInputRouting();

        // 5. Set up toolbar
        this.setupToolbar();
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
