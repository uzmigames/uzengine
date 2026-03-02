# UzEngine

An AI-native 3D game engine that runs inside VSCode. Scenes, shaders, and materials are plain-text files designed for LLM generation and editing.

```
[DX12 Render] → [GPU Readback] → [JPEG Encode] → [WebSocket :9002]
                                                          ↓
                                               [VSCode Webview Canvas]
```

> **Current state:** Phase 0 complete — DX12 pyramid renderer, JPEG streaming, IPC/MCP server working.

---

## Requirements

**C++ Engine**
- Windows 10/11 x64
- Visual Studio 2022 (MSVC v143)
- CMake 3.20+
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

1. Run `UzEngine.exe` — renders a spinning triangle at 60fps
2. Open VSCode
3. Press `Ctrl+Shift+P` → **UzEngine: Open Stream Viewer**
4. The viewport connects to `ws://localhost:9002` and displays the live render

**IPC / MCP control** (from Claude Code or any client):

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
├── CMakeLists.txt              # Build configuration
├── vcpkg.json                  # C++ dependencies
├── src/
│   ├── main.cpp                # Win32 entry, render loop, IPC server
│   ├── common/                 # Shared types, timer, ring buffer
│   ├── renderer/               # DX12 device, PSO, swap chain, GPU readback
│   ├── encoder/                # JPEG encoder (libjpeg-turbo)
│   └── network/                # WebSocket server (IXWebSocket)
├── vscode-uzengine/            # VSCode extension (TypeScript)
│   └── src/
│       ├── extension.ts        # Activation, command registration
│       ├── server-manager.ts   # UzEngine.exe process management
│       └── stream-panel.ts     # WebviewPanel + Canvas JPEG renderer
├── docs/                       # Full documentation suite (see below)
└── .rulebook/tasks/            # 80 implementation tasks (Rulebook format)
```

---

## Documentation

Full documentation is in [`docs/`](docs/README.md).

| Document | Description |
|----------|-------------|
| [docs/DEVELOPMENT-PLAN.md](docs/DEVELOPMENT-PLAN.md) | **Sequential implementation plan** — 80 tasks in phases and blocks with clear deliverables. Start here. |
| [docs/PRD.md](docs/PRD.md) | Product Requirements Document — vision, success metrics, functional/non-functional requirements |
| [docs/ROADMAP.md](docs/ROADMAP.md) | Full roadmap with dependencies, complexity estimates, definition of done |
| [docs/specs/](docs/specs/) | Architecture specs for all subsystems (51 documents, 9 domains) |
| [docs/engine/](docs/engine/) | C++ architecture, patterns, ECS, scripting, audio, physics, UI |
| [docs/guides/](docs/guides/) | Getting started, build, scene authoring, shader authoring, Lua scripting |
| [docs/reviews/](docs/reviews/) | AAA gap analysis — 137 gaps identified across 7 domains |

---

## Development Plan

80 tasks organized into 5 phases:

| Phase | Tasks | Focus |
|-------|-------|-------|
| **0** | — | DX12 renderer + JPEG stream + IPC/MCP ✅ Done |
| **0-PRE** | 5 | Architecture design (Render Graph, Data-Driven, Shared State, Async Streaming, Debug) |
| **1** | 23 | Editor, scene system, Lua scripting, ECS core, crash reporting |
| **2** | 24 | PBR rendering, glTF/FBX import, audio, UI system, asset pipeline |
| **3** | 20 | Skeletal mesh, animation, physics, particle system, save system, GI |
| **4** | 8 | Build pipeline, NavMesh, terrain, plugins, destructibles, cloth |

See [docs/DEVELOPMENT-PLAN.md](docs/DEVELOPMENT-PLAN.md) for the full step-by-step sequence.

---

## Architecture

The engine streams frames to VSCode via JPEG over WebSocket. Each frame is rendered with DX12, read back to CPU, compressed with libjpeg-turbo, and sent as a binary WebSocket message. The VSCode extension draws frames onto an HTML5 Canvas.

An IPC server on port 9003 handles JSON commands from Claude Code (via MCP) and the VSCode extension for scene control, screenshot capture, and engine state queries.

See [docs/guides/architecture.md](docs/guides/architecture.md) for full details.
