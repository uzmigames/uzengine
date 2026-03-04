# UzEngine

An AI-native 3D game engine that runs inside VSCode. Scenes, shaders, and materials are plain-text files designed for LLM generation and editing.

```
[DX12 Render] → [GPU Readback] → [JPEG Encode ×2] → [WebSocket]
                                                          ↓
                                              [VSCode Webview Canvas]
                                              · Scene viewport
                                              · Fly camera (WASD+RMB)
                                              · Transform gizmos
                                              · Scene tree + Inspector
```

> **Current state:** v0.3.0 — Component-based renderer, scene system with persistence, multi-instance engine, gizmo system, editor fly camera, 144fps JPEG streaming with dual-threaded encoding.

---

## Requirements

**C++ Engine**
- Windows 10/11 x64
- Visual Studio 2022 (MSVC v143)
- CMake 3.20+
- Ninja (recommended build system)
- vcpkg (auto-installs dependencies)
- DirectX 12 (included in Windows SDK)

**VSCode Extension**
- Node.js 20+
- VSCode 1.85+

---

## Build

```bash
# Configure (vcpkg installs all dependencies automatically)
cmake -B build -G "Visual Studio 17 2022" -A x64

# Build
cmake --build build --config Release

# Run
build/Release/UzEngine.exe
```

**VSCode Extension**

```bash
cd vscode-uzengine
npm install
npm run compile
```

Press **F5** inside `vscode-uzengine/` to launch the Extension Development Host.

---

## Usage

1. Open any `.uzscene` file — the engine starts automatically and streams the scene viewport
2. Use **RMB + WASD/EQ** to fly-navigate the scene (UE5-style)
3. Select nodes in the **Scene Tree** panel (sidebar) and edit properties in **Inspector**
4. Use **W/E/R** hotkeys or toolbar buttons to switch between **Translate / Rotate / Scale** gizmos
5. **Play/Pause/Stop** controls for runtime preview

**Command Palette** (`Ctrl+Shift+P`):
- `UzEngine: Open Scene Viewport` — open a standalone viewport
- `UzEngine: Start Server` / `Stop Server` — manual engine control

**AI / MCP control** (from Claude Code or any MCP client):

```json
{ "fn": "engine.ping" }
{ "fn": "scene.setRotation", "args": { "speed": 2.0 } }
{ "fn": "scene.setBackground", "args": { "r": 0.1, "g": 0.1, "b": 0.2 } }
{ "fn": "engine.screenshot" }
```

---

## Project Structure

```
UzEngine/
├── CMakeLists.txt
├── vcpkg.json                        # C++ dependencies (ixwebsocket, libjpeg-turbo, nlohmann-json, zlib)
├── src/
│   ├── main.cpp                      # Win32 entry, render loop, IPC server, scene handlers
│   ├── common/                       # Shared types, timer, ring buffer, IPC registry
│   ├── engine/
│   │   ├── core/                     # Arena allocator, component registry, module manager,
│   │   │                             #   crash reporter, file watcher, platform, math, string IDs
│   │   ├── scene/                    # Scene graph, node transforms, component store,
│   │   │                             #   scene loader/saver (.uzscene), scene manager
│   │   ├── editor/                   # Editor-specific logic
│   │   └── debug/                    # Debug utilities
│   ├── renderer/
│   │   ├── renderer.cpp/h            # DX12 device, PSO, swap chain, GPU readback, resize
│   │   ├── scene_params.h            # Thread-safe scene state (atomics), camera input
│   │   ├── fps_overlay.cpp/h         # FPS counter stamped into render output
│   │   ├── components/               # Camera, editor, light render components
│   │   └── gizmos/                   # Transform, camera, light gizmos + registry
│   ├── encoder/                      # JPEG encoder (libjpeg-turbo), dual-thread pipeline
│   └── network/                      # WebSocket server (IXWebSocket)
├── vscode-uzengine/                  # VSCode extension (TypeScript)
│   └── src/
│       ├── extension.ts              # Activation, commands, keybindings
│       ├── viewport-panel.ts         # Webview viewport with canvas, camera, picking, gizmos
│       ├── engine-instance-manager.ts # Multi-instance UzEngine.exe lifecycle
│       ├── engine-instance.ts        # Per-scene engine process management
│       ├── scene-tree-provider.ts    # Tree view of scene nodes
│       ├── inspector-panel.ts        # Property inspector (webview sidebar)
│       ├── console-panel.ts          # Engine console output
│       ├── play-mode-controller.ts   # Play/Pause/Stop mode management
│       ├── uzscene-editor.ts         # Custom editor for .uzscene files
│       ├── ipc-client.ts             # File-based IPC communication
│       └── engine-client.ts          # WebSocket engine client
├── tests/                            # C++ test suites (core, renderer, editor, debug)
├── tools/mcp-uzengine/               # MCP server for AI interaction
└── docs/                             # Full documentation suite
```

---

## Key Features

### Renderer
- **DirectX 12** renderer with 144fps target, depth buffer, multiple geometry types
- **Dual JPEG encoder threads** — round-robin dispatch, ~143fps throughput
- **Dynamic viewport resize** — render target adapts to panel size in real-time
- **GPU readback with backpressure** — conditional readback skips when encoders are busy
- **FPS overlay** stamped directly into the streamed frame
- **Headless mode** — `UZENGINE_HEADLESS=1` for zero-UI operation

### Scene System
- **Data-oriented scene graph** with flat-array node management
- **Component store** with type-erased pools and O(1) access
- **JSON-based `.uzscene` persistence** — load, save, and auto-restore state
- **Scene manager** handling multiple scenes with state preservation

### Gizmos
- **Transform gizmos** — Translate, Rotate, Scale with W/E/R hotkeys
- **Component gizmos** — Camera frustum wireframe, directional/point light visualization
- **Gizmo registry** for extensible gizmo types

### Editor
- **Fly camera** — RMB + WASD/EQ with scroll-wheel speed, Shift boost (UE5-style)
- **Object picking** — click-to-select nodes in the viewport
- **Scene Tree** sidebar with add, delete, rename, duplicate node operations
- **Inspector panel** with per-node property editing
- **Console panel** for engine output
- **Play/Pause/Stop** mode with state snapshot and restore
- **Multi-instance engine** — each scene preview spawns its own `UzEngine.exe`

### AI Integration
- **MCP server** (`tools/mcp-uzengine/`) with 5 tools: `uz_call`, `uz_screenshot`, `uz_status`, `uz_read_file`, `uz_write_file`
- **File-based IPC** — 12+ registered handlers for scene control, screenshots, file operations
- Claude Code sees rendered frames directly via `uz_screenshot` (base64 PNG)

---

## Documentation

Full documentation is in [`docs/`](docs/README.md).

| Document | Description |
|----------|-------------|
| [CHANGELOG.md](CHANGELOG.md) | Version history with detailed changes |
| [docs/DEVELOPMENT-PLAN.md](docs/DEVELOPMENT-PLAN.md) | Sequential implementation plan — 80 tasks in phases |
| [docs/PRD.md](docs/PRD.md) | Product Requirements Document |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Full roadmap with dependencies and complexity estimates |
| [docs/specs/](docs/specs/) | Architecture specs for all subsystems |
| [docs/engine/](docs/engine/) | C++ architecture, patterns, ECS, scripting |
| [docs/guides/](docs/guides/) | Getting started, build, scene and shader authoring |

---

## Architecture

Each `.uzscene` file opened in VSCode spawns an independent `UzEngine.exe` process. The engine renders frames with DX12, compresses them via dual JPEG threads, and streams them over WebSocket. The extension draws frames on a Canvas and sends user input back.

```
┌─ UzEngine.exe (per scene) ─────────────────────────────────────────────────┐
│                                                                            │
│   Scene Graph ──→ Component Store ──→ Gizmo Registry                       │
│       │                                    │                               │
│       ▼                                    ▼                               │
│   ┌────────────────────────────────────────────────┐                       │
│   │           DX12 Renderer (144fps)               │                       │
│   │   Geometry  ·  Gizmos  ·  Grid  ·  FPS Overlay │                       │
│   └──────────────────┬─────────────────────────────┘                       │
│                      │ GPU Readback (conditional)                          │
│                      ▼                                                     │
│              ┌───────────────┐                                             │
│              │  JPEG Encode  │ ×2 threads, round-robin                     │
│              └───────┬───────┘                                             │
│                      │ binary frames                                       │
│   ┌──────────────────▼────────────────┐  ┌──────────────────────────────┐  │
│   │  WebSocket :9002+                 │  │  IPC (file-based, 50ms poll) │  │
│   │  · JPEG frames → extension       │  │  · command.json → response   │  │
│   │  · resize/input ← extension      │  │  · 12+ handlers             │  │
│   └──────────────────┬────────────────┘  └─────────────┬────────────────┘  │
│                      │                                 │                   │
└──────────────────────┼─────────────────────────────────┼───────────────────┘
                       │                                 │
         ──────────────┼─────────────────────────────────┼──────────
                       │                                 │
┌──────────────────────┼─────────────────────────────────┼───────────────────┐
│                      │     VSCode Extension            │                   │
│   ┌──────────────────▼──────────────┐  ┌───────────────▼────────────────┐  │
│   │  Viewport (Webview Canvas)      │  │  Scene Tree · Inspector       │  │
│   │  · JPEG → Canvas draw           │  │  · Add/Delete/Rename nodes    │  │
│   │  · Fly camera (RMB+WASD)       │  │  · Property editing           │  │
│   │  · Object picking (click)      │  │  · Drag & drop                │  │
│   │  · Gizmo interaction (W/E/R)   │  ├────────────────────────────────┤  │
│   │  · Viewport resize             │  │  Console · Play/Pause/Stop    │  │
│   └─────────────────────────────────┘  └────────────────────────────────┘  │
│                                                                            │
│   Engine Instance Manager — spawns one UzEngine.exe per .uzscene           │
│                                                                            │
└────────────────────────────────────────────────────────────────────────────┘
```

See [docs/guides/architecture.md](docs/guides/architecture.md) for full details.
