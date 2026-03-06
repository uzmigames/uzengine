# UzEngine — Module Structure

> This document defines the six top-level modules of the UzEngine project, their responsibilities, boundaries, allowed dependencies, source layout, and CMake targets.

---

## 1. The Six Modules

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│  MODULE MAP                                                                     │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  EDITOR  (vscode-uzengine/ + src/editor/)                                │  │
│  │  VSCode extension panels: Scene Tree, Inspector, Asset Browser,          │  │
│  │  Shader editor, Play controls, Console. TypeScript + WebView UI.         │  │
│  │  Deps: communicates with Engine and Renderer via IPC/WebSocket only.     │  │
│  └──────────────────────┬───────────────────────────────────────────────────┘  │
│           IPC (JSON)    │  WebSocket (JPEG frames)                             │
│  ┌──────────────────────▼───────────────────────────────────────────────────┐  │
│  │  RUNTIME  (src/runtime/)                                                 │  │
│  │  The full game loop: Renderer + game.dll scripts + UI overlay.           │  │
│  │  Activated when clicking ▶ Play in the editor.                           │  │
│  │  Two modes: --editor (preview, streams frames) / --play (shipping game). │  │
│  │  Deps: Renderer + Engine + UI                                            │  │
│  └───────────┬──────────────────────────────────────────────────────────────┘  │
│              │                                                                  │
│  ┌───────────▼──────────────────────────────────────────────────────────────┐  │
│  │  RENDERER  (src/renderer/)                                               │  │
│  │  Produces rendered frames. DX12 pipeline, shader compiler (DXC),        │  │
│  │  render graph, GPU resources, material system, JPEG readback.            │  │
│  │  Used by: Runtime (game), Editor (preview), Build (offline cook).       │  │
│  │  Deps: Engine only                                                       │  │
│  └───────────┬──────────────────────────────────────────────────────────────┘  │
│              │                                                                  │
│  ┌───────────▼──────────────────────────────────────────────────────────────┐  │
│  │  ENGINE  (src/engine/)                                                   │  │
│  │  C++ framework: scene graph, ECS, asset manager, physics, audio,        │  │
│  │  input, animation, memory (arenas), math, file I/O, logging.            │  │
│  │  No rendering. No GPU. Pure logic and data.                              │  │
│  │  Deps: none (core/platform only)                                        │  │
│  └───────────┬──────────────────────────────────────────────────────────────┘  │
│              │                                                                  │
│  ┌───────────▼──────────────────────────────────────────────────────────────┐  │
│  │  UI  (src/ui/)                                                           │  │
│  │  2D overlay rendered on top of the game frame.                          │  │
│  │  Buttons, menus, text, images, video, clickable areas.                  │  │
│  │  Driven by .uzui files (plain text, INI-like). No game logic inside.    │  │
│  │  Deps: Renderer (for draw calls), Engine (for asset loading)            │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
│                                                                                 │
│  ┌──────────────────────────────────────────────────────────────────────────┐  │
│  │  BUILD  (src/build/ + tools/)                                           │  │
│  │  Offline pipeline: cook assets, compile shaders, pack .uzpak,           │  │
│  │  stage shipping binary. Runs headless (no window, no IPC server).       │  │
│  │  Deps: Engine + Renderer (shader compiler only, no window)              │  │
│  └──────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### New Module: uz_ui_html.dll (Standalone)

```
uz_ui_html.dll (C ABI library — no uz_engine.lib dependency)
├── HTML+CSS document parser and renderer
├── Webview-style layout engine (flexbox)
├── Widget types: Button, Label, Input, Panel, etc.
├── Stable C ABI: ui_load_document, ui_set_property, ui_tick, ui_get_draw_list, ui_deliver_input
└── Used by: render.exe (editor panels) + game process (game UI)
```

This module is **completely standalone** and has no dependency on uz_engine.lib. It can be used independently for UI rendering in any application.

---

## 2. Module Responsibilities

### 2.1 Engine (`src/engine/`)

The **framework** that all other C++ modules build on. Contains everything needed to represent and simulate a game world — without producing any pixels.

**Owns:**
- Memory: `Arena`, `Handle<T>`, `Pool<T>`
- Math: `Vec2/3/4`, `Mat3/4`, `Quat`, `Transform3D`
- Scene graph: `Node`, `NodeType`, `Scene`, `SceneManager`, `.uzscene` I/O
- ECS: `entt::registry` wrapper, all component structs, all system free functions
- Asset manager: `ResourceManager`, `ResourceHandle<T>`, `.import/` pipeline
- Physics (Phase 3): Jolt Physics integration, `CRigidBody`, `CCollider`
- Audio (Phase 3): miniaudio integration, `CAudioSource`
- Animation (Phase 3): AnimClip sampler, state machine, IK
- Input (Phase 3): `InputAction`, `InputMappingContext`, raw device polling
- Logging: `log_info/warn/error` (sink-agnostic — editor uses OutputChannel, shipping uses file)
- IPC glue: `IpcRegistry`, `IpcPoller`, `SceneCommand` queue, `FileWatcher`

**Does NOT own:**
- Any GPU resource or D3D12 type
- Any window or HWND
- Any WebSocket or network code
- Any UI layout or draw call

**Key rule:** Engine code compiles and runs on a machine with no GPU. It is fully unit-testable headless.

---

### 2.2 Renderer (`src/renderer/`)

The **frame production** module. Takes data from the ECS world and turns it into pixels. Shared across Runtime, Editor preview, and the Build tool's offline shader compiler.

**Owns:**
- DX12 device, command queue, swap chain, descriptor heaps
- `RenderGraph`: pass registration, barrier management, execution
- `GpuBuffer`, `GpuTexture`: RAII wrappers for all GPU resources
- `ShaderCompiler`: DXC wrapper — HLSL → DXIL, error diagnostics
- `Material`: shader handle + parameter block (cbuffer layout)
- `MeshBuffer`: GPU-resident vertex/index buffers
- PSO cache: keyed by shader + vertex layout + blend/depth state
- Readback pipeline: double-buffered `ID3D12Resource` + fence
- `JpegEncoder`: RGBA readback → JPEG (libjpeg-turbo)
- `WsServer`: frame broadcast to VSCode (used in editor/preview mode only)

**Reads from Engine (never writes):**
- ECS world: `CTransform`, `CMeshRenderer`, `CDirLight`, `CCamera`, `CSkinnedMesh`
- `ResourceHandle<Mesh>`, `ResourceHandle<Material>` — resolved via ResourceManager

**Does NOT own:**
- Scene graph structure or node hierarchy
- Game logic, scripts, physics
- UI layout (UI module has its own render pass that runs after the 3D pass)

**Key rule:** Renderer reads ECS as a read-only snapshot. It never modifies game state.

---

### 2.3 Runtime (`src/runtime/`)

The **executable entry point** that wires Engine + Renderer + UI + game logic into a running game. It is the process that runs when you click ▶ Play.

Runtime has two operating modes, selected via CLI flag:

| Mode | Flag | Used by | Description |
|------|------|---------|-------------|
| **Editor preview** | `--editor` | VSCode extension | Streams JPEG frames via WebSocket. IPC server active. Accepts scene/shader commands. |
| **Shipping game** | `--play` | End user | Fullscreen window. No IPC. No streaming. Mounts `game.uzpak`. No DXC. No importers. |

**Owns:**
- `WinMain` / entry point
- Thread creation and lifecycle (Main, Encode, Game Logic, IPC Poller, File Watcher)
- `GameContext` construction and passing to `game.dll`
- `HotReloadManager`: shadow-copy DLL, `save_state`/`load_state` protocol
- Frame limiter (QPC spin-sleep)
- Win32 message loop

**Wires together:**
- Engine subsystem init/shutdown order
- Render loop ↔ Game loop synchronization (semaphores)
- SPSC ring buffer (Main → Encode thread)

**Does NOT own:**
- Any rendering detail (delegates to Renderer)
- Any scene structure (delegates to Engine)
- Any UI layout (delegates to UI module)

---

### 2.4 Build (`src/build/` + `tools/`)

The **offline pipeline** that transforms raw project files into a self-contained, shippable game package.

**Stages in order:**

```
1. Cook       — import sources into engine formats
               .glb / .fbx  → .uzmesh + .uzskel + .uzanim
               .png / .hdr  → .dds (BC7/BC5/BC6H via DirectXTex)
               .hlsl / .glsl → .dxil / .spv (pre-compiled shaders)
               .wav          → .ogg (libvorbis)

2. Pack       — assemble all cooked outputs into game.uzpak
               per-file zstd compression
               xxhash3 directory (O(log n) lookup by path hash)
               footer-at-end layout (memory-mappable, no seek)

3. Compile    — compile render.exe in Shipping configuration
               UZ_SHIPPING=1: strips IPC, DXC, importers, asset browser
               links against game.uzpak VFS, not filesystem directly

4. Stage      — assemble the release directory
               UzGame.exe + game.uzpak + redistributables (MSVC runtime)
               SHA256 manifest for update diffing

5. Package    — (optional) NSIS installer or zip archive
```

**Owns:**
- `CookPipeline`: file discovery, dependency tracking, incremental (hash-based)
- `PakWriter`: `.uzpak` assembler
- `ShaderCook`: headless DXC compilation (no window, no D3D12 device required — uses DXC API directly)
- `ManifestWriter`: SHA256 file list
- `CPack` config for NSIS packaging

**Uses from Renderer:** `ShaderCompiler` only (no window, no swap chain).
**Uses from Engine:** `ResourceManager` for format knowledge, `Arena` for temp allocations.

**Does NOT run at game launch.** Build is a standalone CLI tool: `UzBuild.exe --project ./myproject --output ./dist`

---

### 2.5 Editor (`vscode-uzengine/` + `src/editor/`)

The **authoring environment** built on top of VSCode. Has two parts:

**TypeScript side (`vscode-uzengine/src/`):**
- VSCode extension activation, command registration
- `ServerManager`: spawns/kills `render.exe --editor`, health-check, reconnect
- `StreamPanel`: WebviewPanel displaying JPEG frames from WebSocket
- `SceneTreeProvider`: `TreeDataProvider` for the Scene Tree panel (native VSCode)
- `InspectorPanel`: Webview (Webview UI Toolkit) for selected node properties
- `AssetBrowserProvider`: `TreeDataProvider` for project assets
- `PlayControls`: status bar items ▶ / ⏸ / ■
- `ShaderDiagnostics`: maps DXC error output to VSCode inline squiggles
- `ConsoleOutput`: `OutputChannel` receiving engine log lines via IPC

**C++ side (`src/editor/`):**
- IPC handlers for editor-specific commands: `editor.getSceneTree`, `editor.selectNode`, `editor.getNodeProperties`, `editor.setNodeProperty`
- These handlers are compiled in **only when `UZ_EDITOR=1`** — stripped from shipping build

**Does NOT own:**
- Any game logic
- Any rendering — it just displays what the Runtime streams
- Any `.uzscene` parsing — it delegates to Engine via IPC

---

### 2.6 UI (`src/ui/`)

The **2D overlay** rendered on top of the game's 3D frame. Used for HUDs, menus, loading screens, subtitles, inventory screens — anything the player interacts with via the game UI.

**Owns:**
- `.uzui` file format (plain text, INI-like): layout, widget tree, style properties
- `UiDocument`: parsed in-memory representation of a `.uzui` file
- `UiRenderer`: 2D render pass that executes after the 3D pass (orthographic, no depth test)
- Widget types: `Button`, `Label`, `Image`, `Video`, `Panel`, `ScrollArea`, `ProgressBar`
- Layout engine: anchor-based (top/left/right/bottom/center) + flex-row/flex-column
- Hit testing: maps screen pixel → widget → triggers `on_click`, `on_hover` events in game.dll
- Font rendering: SDF atlas (pre-baked via msdf-atlas-gen)
- Video playback: (Phase 3) software-decoded via pl_mpeg or minimp4

**Does NOT own:**
- Game logic triggered by UI events (that lives in game.dll)
- Any 3D rendering
- Editor panels (those are in the Editor module, rendered by VSCode, not by UiRenderer)

**File format (`.uzui`):**

```ini
[uidocument version=1 name="MainMenu"]

[panel id=1 anchor=center width=400 height=300]
background = [0.1, 0.1, 0.1, 0.9]

[label id=2 parent=1 anchor=top_center y=20]
text    = "UzEngine Demo"
font    = "res://fonts/roboto.ttf"
size    = 32
color   = [1.0, 1.0, 1.0, 1.0]

[button id=3 parent=1 anchor=center y=20]
text     = "Start Game"
on_click = "game.on_start_clicked"   ← dispatched to game.dll

[button id=4 parent=1 anchor=center y=80]
text     = "Quit"
on_click = "game.on_quit_clicked"
```

---

## 3. Dependency Rules

```
        game.dll
            │
            ▼
         RUNTIME ──────────────────────────────────┐
            │                                      │
            ├──► RENDERER ──► ENGINE               │
            │                                      │
            ├──► UI ─────────┤                     │
            │                └──► ENGINE           │
            │                                      │
            └──► UI_HTML.DLL (standalone)         │
                                                   │
         EDITOR (TypeScript) ──► IPC/WS ──────────►┘
                                                   │
         BUILD ──► RENDERER (ShaderCompiler only)  │
               └──► ENGINE                         │
```

**Strict rules:**

| Module | May depend on | Must NOT depend on |
|--------|---------------|--------------------|
| `engine` | `core`, platform OS headers | `renderer`, `ui`, `ui_html`, `runtime`, `editor`, `build` |
| `renderer` | `engine` | `ui`, `ui_html`, `runtime`, `editor`, `build`, `game.dll` |
| `ui` | `engine`, `renderer` | `ui_html`, `runtime`, `editor`, `build`, `game.dll` internals |
| `ui_html` | Nothing (standalone C ABI) | Any C++ module |
| `runtime` | `engine`, `renderer`, `ui`, `ui_html` | `editor` C++ code, `build` |
| `editor` (TS) | IPC/WebSocket only | Any C++ module directly |
| `editor` (C++) | `engine`, `renderer` | `ui`, `ui_html`, `runtime`, `build` |
| `build` | `engine`, `renderer` (ShaderCompiler) | `ui`, `ui_html`, `runtime`, `editor` |
| `game.dll` | `core/game_api.h` only | Everything else |

---

## 4. Source Tree

```
e:\UzEngine\
│
├── src/
│   ├── engine/                  ← ENGINE module
│   │   ├── core/                   arena, handle, result, string_id, math, containers
│   │   ├── scene/                  node, scene, scene_manager, .uzscene I/O
│   │   ├── ecs/                    components, systems, world (entt)
│   │   ├── assets/                 resource_manager, formats, importers
│   │   ├── physics/                jolt integration (Phase 3)
│   │   ├── audio/                  miniaudio integration (Phase 3)
│   │   ├── animation/              clip sampler, state machine, IK (Phase 3)
│   │   ├── input/                  InputAction, MappingContext, XInput (Phase 3)
│   │   └── platform/               file_watcher, Win32 OS abstraction
│   │
│   ├── renderer/                ← RENDERER module
│   │   ├── dx12/                   device, command queue, swap chain, heaps
│   │   ├── render_graph/           pass registration, barrier management
│   │   ├── gpu_resources/          GpuBuffer, GpuTexture RAII
│   │   ├── shader_compiler/        DXC wrapper, PSO cache
│   │   ├── material/               parameter blocks, cbuffer layout
│   │   ├── mesh_buffer/            GPU vertex/index buffers
│   │   ├── readback/               double-buffered readback + fence
│   │   ├── streaming/              JpegEncoder + WsServer (editor mode only)
│   │   └── passes/                 ShadowPass, GBufferPass, LightPass, PostProcess
│   │
│   ├── ui/                      ← UI module
│   │   ├── document/               UiDocument, .uzui parser
│   │   ├── widgets/                Button, Label, Image, Panel, etc.
│   │   ├── layout/                 anchor + flex layout engine
│   │   ├── renderer/               UiRenderer (2D pass, SDF fonts, hit test)
│   │   └── video/                  video widget, software decode (Phase 3)
│   │
│   ├── runtime/                 ← RUNTIME module
│   │   ├── main.cpp                WinMain, thread creation, frame limiter
│   │   ├── game_host.h/cpp         GameContext construction, game.dll loader
│   │   ├── hot_reload.h/cpp        shadow copy, save/load state protocol
│   │   └── ipc/                    IpcRegistry + IpcPoller + all handlers
│   │
│   ├── editor/                  ← EDITOR C++ handlers (UZ_EDITOR=1 only)
│   │   └── ipc_handlers/           editor.getSceneTree, editor.selectNode, etc.
│   │
│   └── build/                   ← BUILD module
│       ├── cook/                   CookPipeline, asset importers
│       ├── pack/                   PakWriter, .uzpak assembler
│       ├── shader_cook/            headless DXC compilation
│       └── manifest/               SHA256 manifest writer
│
├── vscode-uzengine/             ← EDITOR TypeScript
│   └── src/
│       ├── extension.ts
│       ├── server-manager.ts
│       ├── stream-panel.ts
│       ├── scene-tree-provider.ts
│       ├── inspector-panel.ts
│       ├── asset-browser-provider.ts
│       └── play-controls.ts
│
├── tools/                       ← standalone CLI tools
│   └── UzBuild/                    Build pipeline CLI
│
└── tests/                       ← unit tests (headless, no GPU)
    ├── engine/
    └── ui/
```

---

## 5. Executables & Libraries

| Output | Type | Modules | When used |
|--------|------|---------|-----------|
| `render.exe` | Executable | Runtime + Renderer + Engine + UI + Editor handlers | Editor preview + shipping game |
| `UzBuild.exe` | Executable | Build + Renderer (ShaderCompiler) + Engine | CI/CD, packaging |
| `game.dll` | Shared lib | Game logic only (C ABI) | Hot-reloaded by render.exe |
| `uz_ui_html.dll` | Shared lib | Standalone UI HTML/CSS renderer (C ABI) | Loaded by render.exe + game for UI |
| `uz_engine.lib` | Static lib | Engine module | Linked by render.exe + UzBuild.exe |
| `uz_renderer.lib` | Static lib | Renderer module | Linked by render.exe + UzBuild.exe |
| `uz_ui.lib` | Static lib | UI module | Linked by render.exe |

---

## 6. Preprocessor Flags Per Configuration

| Flag | Set when | Effect |
|------|----------|--------|
| `UZ_EDITOR=1` | Debug + RelWithDebInfo | Enables IPC server, editor IPC handlers, DXC runtime, asset importers, streaming |
| `UZ_SHIPPING=1` | Shipping config | Strips IPC, DXC, importers, streaming. Mounts `game.uzpak` only. No window chrome. |
| `UZ_DEBUG=1` | Debug | ASAN, DX12 debug device, validation layers, verbose logging |
| `UZ_DEV=1` | RelWithDebInfo | Profiling enabled (Tracy), no validation overhead |

```cmake
# Runtime target — editor vs shipping
if(UZ_SHIPPING)
    target_compile_definitions(UzEngine PRIVATE UZ_SHIPPING=1)
else()
    target_compile_definitions(UzEngine PRIVATE UZ_EDITOR=1)
endif()
```

---

## 7. Third-Party Dependency Map

| Library | Module | Notes |
|---------|--------|-------|
| `glm` | Engine (math) | Header-only, math types |
| `entt` | Engine (ecs) | Header-only, ECS registry |
| `nlohmann-json` | Runtime (ipc) | JSON parse for IPC |
| `ixwebsocket` | Renderer (streaming) | WebSocket server (editor mode only) |
| `libjpeg-turbo` | Renderer (streaming) | JPEG encode (editor mode only) |
| `d3d12`, `dxgi`, `dxguid` | Renderer | Windows SDK |
| `dxcompiler` (DXC) | Renderer (shader_compiler) | Runtime `LoadLibraryW` — not linked at compile time |
| `DirectXTex` | Build (cook) | BC7/BC5/BC6H texture compression |
| `fastgltf` | Engine (assets/import) | Phase 2 — GLTF import |
| `ufbx` | Engine (assets/import) | Phase 2 — FBX import |
| `meshoptimizer` | Engine (assets/import) | Phase 2 — LOD simplification |
| `jolt` | Engine (physics) | Phase 3 — rigid body, ragdoll |
| `miniaudio` | Engine (audio) | Phase 3 — single-header audio |
| `msdf-atlas-gen` | UI (fonts) | SDF font atlas baking |
| `pl_mpeg` / `minimp4` | UI (video) | Phase 3 — video widget decode |
| `zstd` | Build (pack) | Per-file compression in .uzpak |
| `xxhash` | Build (pack) | Directory hash for .uzpak lookup |
| `doctest` | tests/ | Unit test framework |
