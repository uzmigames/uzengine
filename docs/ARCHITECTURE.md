# UzEngine 0.2 - Architecture

## Vision
AAA-quality game engine (visual parity with Unity, Godot, Unreal) fully embedded in VSCode, with open file formats designed for LLM manipulation. **The first engine built for autonomous AI-driven development.**

## Core Principles
- **AAA visual quality** — rendering math and techniques based on Unreal Engine 5.6 (clean-room reimplementation, zero copied code)
- **Open formats** — all scene/asset files are human-readable and LLM-friendly
- **Highly configurable** — every subsystem is pluggable and data-driven
- **Multi-backend rendering** — RHI abstraction layer supporting multiple graphics APIs
- **Runtime-first development** — build and validate the runtime engine before extension/MCP integration
- **Reference-driven development** — never brute-force implementations; always consult proven engine sources first
- **No garbage collector** — deterministic memory via Rust-inspired ownership and borrow checking in C++

## Engineering Principles

### Clean Code
This engine is too large for sloppy code — every subsystem must be readable, testable, and maintainable by both humans and AI agents.

- **Meaningful names** — names reveal intent. No `tmp`, `buf`, `data`, `mgr`. Prefer `meshVertexBuffer` over `vb`.
- **Small functions** — one function = one thing, max ~40 lines. If a block needs a comment, extract it.
- **No side effects** — `GetX()` reads, `SetX()` writes. Never mix.
- **No magic numbers** — `constexpr float kGravity = -9.81f;`, not inline literals.
- **Fail early** — validate at system boundaries. `UZ_CHECK` for invariants, `UZ_ENSURE` for soft errors.
- **Delete dead code** — git has history. No commented-out code.
- **Max 1500 lines per file** — split proactively at ~1000. One file = one responsibility.

### SOLID (adapted for C++ game engine)
- **Single Responsibility** — `InputSystem` owns input. `Config` owns config. Not a god class.
- **Open/Closed** — new RHI backends, new components, new importers extend via new types, not by modifying existing code.
- **Liskov Substitution** — any `rhi_backend_t` (DX12, Vulkan, Metal) is interchangeable at the vtable level.
- **Interface Segregation** — focused interfaces: `IRenderable`, `IUpdatable`, `ISerializable`. No fat base classes.
- **Dependency Inversion** — Renderer depends on RHI abstraction, not DX12 directly. Inject, don't hardcode.

### Observability
An engine without observability is undebuggable. Every subsystem must be transparent.

- **Structured logging** — `UZ_LOG(Category, Level, fmt, ...)` at every init/shutdown, external call, and error path. Log includes category, level, and formatted context.
- **CPU profiling** — `UZ_PROFILE_SCOPE("name")` on every function that touches GPU, disk, or runs per-frame. Tracy integration for visual analysis.
- **Assertions** — `UZ_CHECK(expr)` for invariants (fatal in debug), `UZ_ENSURE(expr)` for recoverable violations (returns bool), `UZ_VERIFY(expr)` for side-effect expressions (never stripped).
- **Stat counters** — `StatCounter` tracks allocations, draw calls, frame time, cache hits per group (Rendering, RHI, Physics, Scene, Memory, IO).
- **Error context** — always log what, why, and the input: `"Failed to load texture: path='{}', reason='{}'"`.

## Development Directives

### Reference Sources
When implementing any subsystem, **always consult reference engine sources** before writing code. Never guess or brute-force an implementation.

| Priority | Engine | Path | Usage |
|----------|--------|------|-------|
| 1 (Primary) | **Unreal Engine 5.6** | `W:\UE_5.6_Source` | Parameters, math, algorithms, architecture patterns |
| 2 (Secondary) | **Godot** | `E:\godot` | Alternative approaches, simpler implementations for comparison |

### Clean-Room Rules
- **Study the approach, understand the math, reimplement from scratch** — same parameters and calculations, original code
- **NEVER copy source code** from reference engines
- **NEVER reference engine names in code comments** — no legal exposure
- Only deviate from the reference architecture when a more efficient alternative exists, and document why

## Lessons from POC 0.1
- Native C++ + DirectX rendering streamed via RPC/WebSocket JPG to VSCode extension: **works well**
- Problem: monolithic architecture with no clear layer separation
- Problem: shaders were extremely hard to get right — migrating to SM6.6 as baseline
- Problem: **Webview FPS bottleneck** — runtime rendered at 244 FPS but webview couldn't keep up. Streaming JPG frames through webview is inherently limited. (POC 0.2 solved this with H.264 MF + WebCodecs: 120 FPS sustained)
- Decision: restart with well-defined layers and documentation from day one

## Lessons from POC 0.2 (N-API Stress Test)
- **N-API addon inside Electron/VSCode is viable** — DX12 renderer compiled as `.node` module, loaded in-process
- **Key fix:** must include `${CMAKE_JS_SRC}` (delay-load hook) for Windows/Electron — without it, `require()` segfaults because the addon tries to resolve symbols from `node.exe` but Electron's executable is `Code.exe`
- **Key fix:** `NAPI_CPP_EXCEPTIONS` crashes on Electron/Windows ([electron#22799](https://github.com/electron/electron/issues/22799)) — must use `NAPI_DISABLE_CPP_EXCEPTIONS`
- **Key fix:** must compile with `--runtime=electron --runtime-version=<version>` to match Electron's Node ABI

### Frame Delivery Evolution (encoding benchmarks at 854x480)

| Method | Encode | Decode | Payload | FPS | Notes |
|--------|--------|--------|---------|-----|-------|
| Raw RGBA via postMessage | 0ms | 0ms | ~1.6MB | 2-7 FPS | postMessage serialization kills performance |
| JPEG (stb_image_write) | 2.5ms | ~2ms | ~23KB | 138 FPS | Good, but per-frame compression wastes bandwidth |
| WebP (libwebp) | **42.8ms** | ~3ms | ~58KB | 24 FPS | CPU-only VP8 encoding, way too slow |
| **H.264 (Media Foundation) + WebCodecs** | **2.2ms** | **3.3ms** | ~54KB | **120 FPS** | **Winner. Hardware-accelerated encode + decode** |
| H.264 + WebCodecs + TrackGenerator | 2.2ms | ~3ms | ~54KB | TBD | VideoFrame → MediaStreamTrackGenerator → `<video>` (native compositor) |
| WebRTC (local RTCPeerConnection) | TBD | TBD | TBD | TBD | Next test: eliminates postMessage, native video pipeline end-to-end |

### H.264 Streaming Architecture (final approach for webview panels)
The best approach for streaming rendered frames to webview panels (e.g. preview, remote headless) is **H.264 video encoding via Media Foundation + WebCodecs VideoDecoder**:

- **C++ side:** Media Foundation H.264 encoder (hardware-accelerated via GPU ASIC)
  - Baseline profile, low-latency mode, no B-frames, CBR 20Mbps
  - GOP = 15 frames (keyframe every ~250ms to minimize ghosting)
  - RGBA→NV12 color conversion + encode on dedicated thread (triple-buffered)
  - `CoInitializeEx(COINIT_MULTITHREADED)` required before MF calls
  - Enumerate hardware encoders first (`MFT_ENUM_FLAG_HARDWARE`), fallback to software
  - Set `CODECAPI_AVLowLatencyMode`, `CODECAPI_AVEncMPVDefaultBPictureCount=0`
- **Webview side:** WebCodecs `VideoDecoder` for hardware-accelerated H.264 decode
  - Codec string `avc1.42001e` (Baseline profile Level 3.0), `optimizeForLatency: true`
  - Feed `EncodedVideoChunk` with type `key`/`delta` based on encoder keyframe flag
  - Single-slot pattern: newest frame wins, older frames dropped
  - Output `VideoFrame` drawn directly to canvas via `drawImage()`
- **Transport:** Node Buffer via `postMessage` (same as JPEG/WebP approach)

**Key insight:** Video codecs exploit temporal redundancy between frames — when the viewport changes little between frames, H.264 delta frames are tiny compared to per-frame JPEG. Even in worst case (50K spinning cubes = maximum motion), H.264 matches JPEG speed while providing better quality at similar bandwidth. For typical editor use (mostly static scenes with occasional movement), H.264 will be dramatically more efficient.

- **Conclusion:** N-API in-process model is the correct architecture. Frame delivery for editor viewport should use shared GPU surfaces (no readback/encoding needed) in the final engine. H.264 streaming is the correct approach for webview preview panels and remote/headless scenarios.

---

## Project Structure

```
0.2/
├── runtime/                     # Engine core (C++, compiled with Zig)
│   ├── build.zig                # Zig build system — compiles C++ via zig cc
│   ├── build.zig.zon            # Zig package manifest
│   └── src/
│       ├── uz_runtime.h         # Public C API (extern "C")
│       └── uz_runtime.cpp       # Engine implementation
├── napi/                        # N-API bridge (C++, compiled with cmake-js/MSVC)
│   ├── CMakeLists.txt           # cmake-js build (links runtime.lib + node.lib + delay-load)
│   ├── package.json             # @uzmigames/napi — node-addon-api + cmake-js deps
│   └── src/
│       └── addon.cpp            # N-API bindings wrapping runtime C API
├── extension/                   # VSCode extension (TypeScript)
│   ├── package.json             # @uzmigames/extension — extension manifest
│   ├── tsconfig.json
│   ├── native/                  # .node binary copied here by build pipeline
│   └── src/
│       └── extension.ts         # Extension entry, loads native/uzengine.node
├── .vscode/                     # Launch configs (F5 debug)
│   ├── launch.json
│   └── tasks.json
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DAG.md
│   ├── PDR.md
│   ├── ROADMAP.md
│   └── specs/                   # Per-milestone specifications
├── poc-napi/                    # POC 0.2 backup (reference only, not built)
└── package.json                 # @uzmigames/uzengine — orchestrates full build pipeline
```

### Build Pipeline

```
npm run build
  │
  ├─ 1. build:runtime    zig build -Doptimize=ReleaseFast
  │                       → runtime/zig-out/lib/uzengine_runtime.lib
  │
  ├─ 2. build:napi       cmake-js compile (MSVC, Electron 39.6.0 headers)
  │                       links runtime.lib + node.lib + delay-load hook
  │                       → napi/build/Release/uzengine.node
  │
  ├─ 3. copy:node        → extension/native/uzengine.node
  │
  └─ 4. build:extension  tsc -p .
                          → extension/out/extension.js
```

### Why three layers

- **runtime/** is pure C++ with zero Node/N-API dependency. It compiles as a static library and can be consumed by CLI tools, test harnesses, or a standalone game executable. Zig compiles it.
- **napi/** is a thin C++ bridge that wraps the runtime's C API for Node/Electron. cmake-js compiles it with MSVC to get proper delay-load support for Electron. It links `uzengine_runtime.lib` and produces `uzengine.node`.
- **extension/** is pure TypeScript. It `require()`s `uzengine.node` from `native/` (copied there by the build pipeline). No native compilation happens here.

---

## Development Phases

### Phase 1: Runtime Render Foundation (CURRENT)
Build the core rendering pipeline as a standalone runtime with test harness. No extension or MCP until render is solid.

### Phase 2: Editor Extension
VSCode extension with full editor UI, connected to the validated runtime.

**Primary approach: Runtime as native N-API addon (.node)**
The runtime compiles as a native Node addon loaded directly by the extension — no IPC, no WebSocket, no frame streaming. Direct in-process function calls and shared GPU surfaces. Webview used only for non-viewport editor panels (inspector, hierarchy, etc.).

### Phase 1.5: Build Pipeline & Packager (EARLY — don't defer)
Game build system that compiles a project into a self-contained, cross-platform executable.

**Why early:** POC never had this and it's critical. The build pipeline is where real optimization happens — texture compression, shader compilation, asset packing. Building it early ensures every feature is designed with shipping in mind from the start.

**Build Pipeline Steps:**
```
Project Source
     │
     ├─ Asset Processing
     │   ├── Texture compression (BC7, ASTC, ETC2 per platform)
     │   ├── Mesh optimization (LOD generation, vertex cache)
     │   ├── Audio encoding (platform-specific codecs)
     │   └── Material baking
     │
     ├─ Shader Compilation
     │   ├── HLSL → DXBC/DXIL (DX11/DX12)
     │   ├── HLSL → SPIR-V (Vulkan)
     │   ├── HLSL → MSL (Metal via SPIRV-Cross)
     │   └── Permutation management (feature flags, quality levels)
     │
     ├─ Scene Packaging
     │   ├── Strip editor-only data
     │   ├── Resolve all asset references
     │   └── Serialize to binary format
     │
     ├─ Asset Packing
     │   ├── Pack into archive files (.uzpak)
     │   ├── Compress (zlib/lz4)
     │   └── Generate asset manifest
     │
     └─ Executable Generation
         ├── Link runtime (editor module stripped)
         ├── Embed/bundle asset packs
         └── Per-platform output (Windows .exe, Linux, macOS, Android .apk)
```

**Output:** Self-contained executable + asset packs, no engine installation required.

### Phase 3: MCP Integration
LLM integration layer for autonomous development workflows.

---

## Runtime Execution Model — SINGLE DECISION, NO AMBIGUITY

> **The runtime is an N-API native addon (.node) loaded in-process by the VSCode extension host. It is NOT a separate process.**

This is the single most important architectural decision. Everything else follows from it.

| Aspect | In-Process N-API (.node) | ~~Separate Process (REJECTED)~~ |
|--------|--------------------------|----------------------------------|
| **Frame delivery** | Shared GPU textures (viewport HWND) / H.264 MF+WebCodecs at 120 FPS (webview panels) | WebSocket JPEG streaming (bottleneck) |
| **Function calls** | Direct C++ ↔ JS binding (~0 overhead) | IPC serialization (~1-5ms per call) |
| **Memory** | Shared address space, zero-copy | Separate heaps, must serialize everything |
| **Crash isolation** | Addon crash kills extension host (Electron restarts it in ~1s) | Process crash is isolated |
| **Debugging** | Attach debugger to extension host process | Attach to separate process |
| **Lifecycle** | Loads on `require()`, unloads on extension deactivate | Must spawn, manage, heartbeat, kill |
| **Input** | Runtime creates own HWND for raw input (no webview interception) | Same, but must relay results via IPC |
| **Build complexity** | cmake-js with `--runtime=electron` | Simpler (standalone exe) |

**Why not separate process:** POC 0.1 proved that frame streaming is a hard bottleneck (244 FPS render → ~60 FPS display). The engine needs to render at display refresh rate with zero transfer overhead. In-process shared textures achieve this. The crash isolation tradeoff is acceptable — the extension host restarts cleanly, and the runtime will have SEH handlers + minidump generation for crash diagnostics.

**Viewport rendering (not a webview):** The runtime creates its own borderless HWND (child of the VSCode window or floating) for the 3D viewport. This gives direct DX12 swapchain presentation — no readback, no encoding, no postMessage. The webview is used only for 2D editor panels (inspector, hierarchy, asset browser, etc.).

### Multi-Scene Runtime Pool

**Lesson from POC 0.1:** Opening a scene requires loading the full runtime (DX12 device, physics world, asset dependencies). As the engine grew, this cold-start took several seconds — unacceptable when the user opens multiple scenes in VSCode tabs. The solution was a **pre-warmed process pool**.

#### Problem
1. **Cold-start latency** — runtime initialization (GPU device, subsystems, asset dependencies) takes seconds
2. **Multiple scenes** — user may open 3-4 scenes simultaneously, each needing an independent render pipeline
3. **Build conflicts** — during development, recompiling the runtime while pool processes are running causes `EBUSY` / link errors because the `.node` binary (or `.exe` in POC 0.1) is locked by running processes

#### Architecture

```
┌─ Extension Host ────────────────────────────────────────────┐
│                                                              │
│  ┌─ Runtime Pool Manager ─────────────────────────────────┐ │
│  │                                                         │ │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐             │ │
│  │  │ Runtime 0 │  │ Runtime 1 │  │ Runtime 2 │  (warm)    │ │
│  │  │ scene: A  │  │ scene: B  │  │ (idle)    │             │ │
│  │  │ rendering │  │ rendering │  │ pre-warmed│             │ │
│  │  └──────────┘  └──────────┘  └──────────┘             │ │
│  │                                                         │ │
│  │  Pool config: min_warm=2, max_total=8                   │ │
│  └─────────────────────────────────────────────────────────┘ │
│                                                              │
│  User opens Scene C → Pool assigns Runtime 2 → loads scene  │
│  Pool spawns new Runtime 3 (pre-warm) to maintain min_warm  │
└──────────────────────────────────────────────────────────────┘
```

#### Design

| Aspect | Decision |
|--------|----------|
| **Pool location** | Inside the runtime itself: `uzengine-runtime --pool` starts the pool manager as a lightweight coordinator |
| **Pre-warming** | Idle runtimes initialize DX12 device + core subsystems but load NO scene — ready to accept `--scene` command |
| **Scene assignment** | Extension requests a runtime from pool → pool assigns an idle pre-warmed instance → sends scene path → runtime loads scene and begins rendering |
| **Pool sizing** | `min_warm` (default: 2) idle runtimes always available. `max_total` (default: 8) hard cap. Configurable via `.uzproject` |
| **Graceful shutdown** | On recompile, pool manager sends shutdown signal to all runtimes → waits for graceful exit (GPU flush, resource release) → unlocks binary → recompile succeeds |
| **Build-safe teardown** | `uzengine-runtime --pool-shutdown` CLI command kills pool cleanly. Build scripts call this before compilation. Extension also calls on deactivate. |
| **Crash recovery** | If a runtime crashes (scene bug, GPU TDR), pool detects via process exit code → creates minidump → spawns replacement → notifies extension |

#### CLI Interface

```bash
# Start pool manager (spawns min_warm idle runtimes)
uzengine-runtime --pool --min-warm 2 --max-total 8

# Assign scene to a pooled runtime (called by extension via N-API)
# Pool internally routes to an available pre-warmed instance
uzengine-runtime --scene "content/scenes/level1.uzscene"

# Graceful shutdown (call before recompile)
uzengine-runtime --pool-shutdown

# Standalone (no pool, single scene, for debugging/CI)
uzengine-runtime --scene "content/scenes/test.uzscene" --no-pool
```

#### N-API vs Separate Process for Pool

In POC 0.1 (separate process architecture), the pool spawned OS processes. With N-API (in-process), the pool manages **multiple runtime instances within the same extension host process** — each instance has its own DX12 device, swapchain, and scene state but shares the same address space. This is simpler (no IPC) but means a crash in any runtime instance kills all of them (extension host restarts).

**Alternative under evaluation**: If in-process multi-instance proves unstable, fall back to a hybrid model — pool manager runs in-process via N-API, but each scene runtime is a child process communicating via the `.node` addon's IPC bridge. This provides crash isolation per scene at the cost of IPC overhead (acceptable for editor, since viewport uses native HWND regardless).

---

## Layers

### 1. VSCode Extension — `uzengine` (TypeScript)
Main editor interface. All editor screens live here. *(Phase 2)*

**Responsibilities:**
- Editor UI (scene hierarchy, inspector, asset browser, viewport)
- Project/scene management
- Direct communication with Runtime Engine via N-API function calls (in-process, no protocol)
- Webview host for editor panels (inspector, hierarchy, asset browser — NOT viewport)

**Tech:** TypeScript, VSCode Extension API, Webviews

---

### 2. Runtime Engine — `uzengine-runtime` (C++)
High-performance native core. **Runs in-process as an N-API addon (.node)** loaded directly by the extension host.

> **Decision (validated by POC 0.2):** The runtime is a native `.node` module, not a separate process. This eliminates IPC, WebSocket, and frame streaming overhead entirely. The POC proved N-API + DX12 works inside Electron at 138+ FPS with JPEG streaming, and direct buffer sharing will be even faster. A crash in the .node addon crashes the extension host (not VSCode itself — Electron restarts it), which is acceptable for the massive performance gain.

**Responsibilities:**
- RHI (Rendering Hardware Interface) abstraction
- Render pipeline (deferred/forward, PBR, post-processing)
- Crash report system (SEH handler + minidump before extension host dies)
- Physics, ECS, Audio, Input
- Direct buffer/texture sharing with extension host via N-API

**Tech:** C++17/20, HLSL (SM5/SM6.6), Win32, N-API (node-addon-api)

**What the runtime is NOT:**
- NOT a separate process — no spawn/kill lifecycle, no IPC protocol
- NOT a WebSocket server — no frame streaming, no JSON serialization
- NOT headless by default — creates DX12 device for offscreen rendering, no Win32 window

---

### 3. MCP Server — `uzengine-mcp` (Node.js)
Integration bridge with LLMs (Claude, etc). *(Phase 3)*

**Responsibilities:**
- Expose MCP tools for engine manipulation (create objects, modify scenes, generate code)
- Translate LLM commands into engine actions
- Access to project/scene state

**Tech:** Node.js, TypeScript, MCP SDK

---

## Component System — Lazy, Scene-Driven Architecture

**Critical lesson from POC 0.1:** the component system must be fully agnostic and lazy. Nothing is loaded or instantiated until a scene is opened.

### Rules
1. **No eager loading** — the runtime starts empty, no components are registered or instantiated at boot
2. **Scene drives everything** — when a scene is opened, only the components present in that scene are registered and instantiated
3. **Pre-warming allowed, instantiation forbidden** — scripts can be compiled/cached ahead of time, but no component instances exist until the scene requests them
4. **Agnostic registration** — the runtime does not hardcode which components exist; components self-register when their scene data is encountered
5. **Unload on scene close** — when a scene is closed, its components are destroyed and unregistered (unless shared with another open scene)

### Flow
```
Engine Boot → Empty runtime (zero components)
     │
Open Scene → Parse scene file → Discover component types used
     │
Register  → Only register component types found in scene
     │
Instantiate → Create component instances per scene entity
     │
Close Scene → Destroy instances → Unregister unused types
```

This ensures minimal memory footprint, fast startup, and clean separation between engine and game logic.

---

## Input System — Runtime-Owned

**Critical lesson from POC 0.1:** letting VSCode manage hotkeys and input caused constant conflicts and limitations. The input system must live entirely in the runtime.

### Design
- **All input is captured and processed by the runtime** — keyboard, mouse, gamepad
- **Mouse picking is runtime-side** — raycasting against scene geometry, not DOM hit-testing
- **Input mapping is data-driven** — action maps defined in config files (rebindable)
- **Editor and game share the same input system** — editor just has its own action map layer on top

### Input Stack (priority high → low)
```
1. Editor shortcuts (when in editor mode)  — gizmo manipulation, camera orbit, etc.
2. Game input (when in play mode)           — player controls, game actions
3. System input                             — screenshot, profiler toggle, console open
```

### API Design
```cpp
// Action map definition (data-driven, loaded from config)
UZ_INPUT_ACTION("Editor.TranslateGizmo", Key::W);
UZ_INPUT_ACTION("Editor.RotateGizmo", Key::E);
UZ_INPUT_ACTION("Editor.ScaleGizmo", Key::R);
UZ_INPUT_ACTION("Game.Jump", Key::Space);

// Polling
if (Input::IsActionPressed("Editor.TranslateGizmo")) { ... }
if (Input::IsActionJustPressed("Game.Jump")) { ... }

// Mouse picking (runtime raycasting)
HitResult hit = Input::PickAtScreenPos(mouseX, mouseY, camera, scene);
if (hit.entity.IsValid()) { SelectEntity(hit.entity); }

// Raw input
FVector2 mouseDelta = Input::GetMouseDelta();
float scrollDelta = Input::GetScrollDelta();
bool isDown = Input::IsKeyDown(Key::LeftShift);
```

### Features
- Raw input capture via platform layer (Win32 `WM_INPUT`, etc.)
- Action maps with layers, priority, and consume-on-handle
- Mouse picking via GPU or CPU raycasting (not DOM)
- Gamepad support (XInput/DirectInput)
- Input recording/playback for testing
- Same system works in shipped game — zero editor dependency

### N-API Integration (Phase 2)
When running inside VSCode, the runtime's native window receives input directly. The extension does NOT intercept keyboard/mouse for viewport interaction — it forwards raw events to the runtime via N-API, or the runtime captures from its own surface.

---

## Visual Feedback Loop — LLM Vision Integration

**The most critical workflow feature.** Game engines are inherently visual — code alone can't tell you if a shader looks right. A thin CLI wrapper (`uzengine-cli`) loads the runtime `.node` addon via Node.js and exposes headless rendering commands for LLM/CI workflows.

### CLI Commands (via `uzengine-cli` — Node.js script that loads the .node addon)
```bash
# Capture first rendered frame and save as PNG
uzengine-runtime --screenshot output.png

# Capture with specific scene
uzengine-runtime --screenshot output.png --scene "scenes/test.uzscene"

# Capture at specific resolution
uzengine-runtime --screenshot output.png --width 1920 --height 1080

# Capture with debug overlays (wireframe, normals, depth, etc.)
uzengine-runtime --screenshot output.png --overlay wireframe
uzengine-runtime --screenshot output.png --overlay normals
uzengine-runtime --screenshot output.png --overlay depth
uzengine-runtime --screenshot output.png --overlay gbuffer

# Capture multiple frames (e.g., animation test)
uzengine-runtime --screenshot output.png --frame 60

# Capture and exit (headless mode for CI/LLM workflow)
uzengine-runtime --headless --screenshot output.png --exit-after-frame 1
```

### LLM Development Workflow
```
1. LLM writes/modifies shader or scene code
2. Build runtime
3. Run: uzengine-runtime --headless --screenshot frame.png --exit-after-frame 1
4. LLM reads frame.png with vision
5. LLM evaluates: "The specular highlight is too bright, the normal map looks inverted"
6. LLM fixes code → goto 2
```

### Implementation Requirements
- **Headless rendering mode** — create GPU device and render without showing a window
- **Deterministic first frame** — same scene + same code = same screenshot (no random, fixed time)
- **GPU readback** — copy render target to CPU memory → encode PNG
- **Debug overlays** — switchable visualization modes (wireframe, normals, depth, albedo, metallic, roughness, AO)
- **GBuffer dump** — export individual GBuffer channels as separate PNGs for debugging
- **Exit codes** — return non-zero on render failure (shader compile error, device lost) with error in stdout
- **Fast startup** — minimize time from launch to screenshot (skip unnecessary subsystems)

### Future: Automated Visual Regression
- Store reference screenshots per scene
- Compare new render against reference (perceptual diff)
- Flag visual regressions in CI pipeline
- LLM can be given both "expected" and "actual" images for targeted debugging

---

## Runtime Executable & CLI

The runtime compiles as both a **static library** (for N-API addon and test harnesses) and a **standalone executable** for CLI usage, debugging, CI, and shipped games. The same codebase produces both — the executable is a thin `main()` that links the runtime library.

### Executable Modes

```bash
# Interactive windowed mode (default)
uzengine-runtime --scene "scenes/level1.uzscene"

# Headless screenshot (CI/LLM workflow)
uzengine-runtime --headless --scene "scenes/test.uzscene" --screenshot output.png --exit-after-frame 1

# Backend selection via CLI
uzengine-runtime --scene "scenes/test.uzscene" --rhi dx12        # DX12 SM6.6 (default)
uzengine-runtime --scene "scenes/test.uzscene" --rhi dx12-sm5    # DX12 SM5 fallback
uzengine-runtime --scene "scenes/test.uzscene" --rhi vulkan      # Vulkan/SPIRV
uzengine-runtime --scene "scenes/test.uzscene" --rhi metal       # Metal/MSL (macOS)

# Debug overlays
uzengine-runtime --scene "scenes/test.uzscene" --overlay wireframe
uzengine-runtime --scene "scenes/test.uzscene" --overlay normals --overlay depth

# Shader model override (for testing fallback paths)
uzengine-runtime --scene "scenes/test.uzscene" --shader-model sm5
uzengine-runtime --scene "scenes/test.uzscene" --shader-model sm6.6

# Validation / profiling
uzengine-runtime --scene "scenes/test.uzscene" --gpu-validation --gpu-breadcrumbs
uzengine-runtime --scene "scenes/test.uzscene" --profile --frames 1000
```

### Backend Selection Priority

The RHI backend is resolved in this order:
1. **CLI flag** (`--rhi dx12`) — highest priority, overrides everything
2. **Project config** (`.uzproject` → `"rhi": "dx12"`) — per-project setting
3. **User config** (`~/.uzengine/config.json` → `"default_rhi": "dx12"`) — global preference
4. **Auto-detect** — probe GPU capabilities, pick best available backend

```json
// .uzproject
{
  "engine": {
    "rhi": "dx12",
    "shader_model": "sm6.6",
    "fallback_rhi": "dx12-sm5"
  }
}
```

When the user changes the RHI backend (e.g., from `dx12` to `vulkan`), the shader cache is checked for the target backend. If shaders haven't been compiled for that backend yet, **post-switch shader compilation** runs automatically before the first frame.

### Shipped Game Executable

The build pipeline (Spec 1.5) produces a standalone game executable that links the runtime statically. The shipped exe:
- Embeds compiled shaders for all target backends (configurable in build settings)
- Loads assets from `.uzpak` archives
- Has no editor, no debug UI, no validation overhead
- Supports the same `--rhi` and `--shader-model` flags for runtime backend switching

---

## Shader Compilation Pipeline

> Full specification: [1.5.0-shader-pipeline.md](specs/1.5.0-shader-pipeline.md)

Shaders are authored in **HLSL** and compiled via DXC to backend-specific bytecode (DXIL, SPIRV, MSL). The pipeline uses two distinct modes to avoid the stale-cache and file-copy problems from POC 1:

- **`UZ_SHADER_DEV`**: Load `.hlsl` from disk via ShaderVFS (virtual filesystem with mount points). Content-hash cache (xxHash64) with FileWatcher + reverse-dependency graph for surgical hot-reload. Lazy recompilation on next use (dirty flag pattern, inspired by Godot).
- **`UZ_SHADER_SHIP`**: All bytecode precompiled into `shaders.uzpak` archive, indexed by hash. Zero disk I/O for source files. No DXC, no VFS, no FileWatcher in the binary.

### Key Design Decisions

1. **Never embed shaders as C header strings** — POC 1 proved this causes stale cache during rapid iteration. Dev loads from disk; shipping uses precompiled archive.
2. **Content-addressed cache** — `xxHash64(source + includes + defines + target + compiler_version)`. No timestamps. Changing any included file produces a different hash automatically.
3. **Reverse-dependency graph** — when `common.hlsli` changes, instantly find every shader that includes it (transitively). Mark all dependents dirty, recompile lazily.
4. **HLSL is the single source language** — DXC compiles to DXIL (DX12) and SPIRV (Vulkan). SPIRV-Cross transpiles to MSL (Metal) / GLSL (GLES).
5. **Reflection-driven binding** — SPIRV-Reflect extracts resource bindings → root signature (DX12) and descriptor set layouts (Vulkan) auto-generated.
6. **Post-switch compilation** — when `--rhi` changes, compile missing shaders before first frame (DDC model).
7. **Permutation cap** — max 256 variants per shader to prevent UE5-style permutation explosion.

### Dependencies

| Library | Purpose | Location |
|---------|---------|----------|
| DXC | HLSL → DXIL, HLSL → SPIRV | Dynamic load (`dxcompiler.dll`) |
| FXC | HLSL → DXBC (SM5 legacy) | Dynamic load (`d3dcompiler_47.dll`) |
| SPIRV-Cross | SPIRV → MSL, SPIRV → GLSL | TBD |
| SPIRV-Reflect | Binding layout extraction | `thirdparty/spirv-reflect/` |
| xxHash | Content hash for cache keys | `thirdparty/xxhash/` |

---

## RHI — Rendering Hardware Interface

The first major challenge. A clean abstraction layer that lets the renderer work across multiple graphics backends without leaking API-specific details.

### Backends (by priority)

| # | Backend | Shader Model | Priority | Status |
|---|---------|-------------|----------|--------|
| 1 | **DirectX 12** | SM6.6 | **Primary — develop first** | **Done** (clear screen validated) |
| 2 | DirectX 12 | SM5 | Fallback for older DX12 GPUs | Planned |
| 3 | DirectX 11 | SM5 | Legacy GPU support | Planned |
| 4 | Vulkan | SPIR-V | Cross-platform (Linux/Steam Deck) | Planned |
| 5 | Metal | MSL | macOS/iOS | Planned |

### Why SM6.6 first?
- Modern shader features: dynamic resources, wave intrinsics, mesh shaders
- Enables Unreal 5.6-quality rendering techniques from day one
- Easier to write clean shaders and then create SM5 fallback paths
- POC proved that starting with SM5 and trying to upgrade was painful

### Backend Selection at Runtime

The RHI backend is selected once at startup and cannot change during a session (GPU device, descriptor heaps, and command queues are backend-specific). The selection follows the priority chain described in "Runtime Executable & CLI" above.

```cpp
// Pseudocode for backend initialization
RHIBackend backend = ResolveBackend(cliArgs, projectConfig, userConfig);
IRHIDevice* device = CreateRHIDevice({
    .backend = backend,
    .enableValidation = isDebugBuild,
    .shaderModel = ResolveShaderModel(cliArgs, projectConfig),
});
```

If the requested backend is not available (e.g., Vulkan on a system without drivers), the system falls back to the next priority backend and logs a warning.

### Shader Model Negotiation

The device probes GPU capabilities at init time:
1. Check SM6.6 support (Resource Binding Tier 3)
2. If SM6.6 not available, fall back to SM6.0 → SM5.1 → SM5.0
3. Load shader bytecode matching the negotiated shader model
4. If bytecode for the negotiated SM is not in the cache, trigger post-switch compilation

### RHI Core Abstractions
```
RHIDevice           — GPU device creation, feature queries, backend selection
RHICommandList      — Command recording (draw, dispatch, barriers)
RHISwapChain        — Present to window
RHIPipelineState    — Shader + render state, PSO cache
RHIBuffer           — Vertex/Index/Constant/Structured buffers
RHITexture          — 2D/3D/Cube textures, render targets
RHIShader           — Compiled shader bytecode (DXIL/SPIRV/MSL)
RHIRootSignature    — Resource binding layout (auto-generated from reflection)
RHISampler          — Texture sampling state
RHIFence            — GPU/CPU synchronization
ShaderCompiler      — HLSL compilation, cross-compilation, caching
```

---

## Observability — Logging, Profiling & Crash Reporting

Built from day one into every subsystem. No code should exist without proper instrumentation.

### Logging System

Category-based logging with compile-time verbosity filtering.

**Verbosity Levels:**
```
Fatal → Error → Warning → Info → Debug → Trace
```

**API Design:**
```cpp
// Category declaration (header)
UZ_DECLARE_LOG_CATEGORY(LogRHI);
UZ_DECLARE_LOG_CATEGORY(LogRenderer);
UZ_DECLARE_LOG_CATEGORY(LogShader);

// Category definition (cpp)
UZ_DEFINE_LOG_CATEGORY(LogRHI);

// Usage
UZ_LOG(LogRHI, Info, "Device created: {}", adapterName);
UZ_LOG(LogRHI, Error, "Failed to create swap chain: {}", hr);
UZ_LOG(LogRenderer, Debug, "Draw calls this frame: {}", drawCallCount);
```

**Features:**
- Compile-time verbosity filtering (strip Debug/Trace in shipping builds)
- Pluggable output backends (console, file with rotation, debugger output)
- Thread-safe with minimal locking
- Structured format args for future trace integration
- Every RHI call, resource creation/destruction, and state transition must be logged

### CPU Profiling System

RAII scope-based profiling with hierarchical stat groups.

**API Design:**
```cpp
// Stat group declaration
UZ_DECLARE_STAT_GROUP(Rendering);
UZ_DECLARE_STAT_GROUP(RHI);
UZ_DECLARE_STAT_GROUP(Physics);

// Scope timing — automatic RAII
UZ_PROFILE_SCOPE(Rendering, DrawOpaquePass);
UZ_PROFILE_SCOPE(RHI, CommandListExecute);

// Counters
UZ_STAT_INCREMENT(Rendering, DrawCalls);
UZ_STAT_SET(Rendering, TriangleCount, numTris);

// Frame boundary marker
UZ_PROFILE_FRAME_MARK();
```

**Features:**
- RAII scoped counters (zero-cost when disabled)
- Stat groups: Rendering, RHI, Physics, Scene, Memory, IO
- Per-frame stats with clear-every-frame semantics
- Memory allocation tracking per subsystem
- Compile-time strippable via `UZ_PROFILING_ENABLED`
- Two tiers: full profiling (dev builds) and lightweight named events (shipping)

### GPU Profiling System

Async GPU timestamp queries with multi-frame buffering.

**API Design:**
```cpp
// Insert GPU timestamp
UZ_GPU_TIMESTAMP(commandList, "ShadowPass");
UZ_GPU_TIMESTAMP(commandList, "GBufferPass");
UZ_GPU_TIMESTAMP(commandList, "LightingPass");

// Frame boundary
UZ_GPU_FRAME_BEGIN(commandList);
UZ_GPU_FRAME_END(commandList);
```

**Features:**
- Triple-buffered GPU query heap (avoid CPU stalls on readback)
- Per-queue profiling: Graphics, Compute, Copy
- Hierarchical breadcrumb system for GPU crash debugging
- GPU timestamps converted to CPU time domain for unified timeline
- Integrated with RHI layer — every backend implements the same profiling interface

### Crash Reporting System

Capture full engine state on crash, including GPU context.

**Crash Context Types:**
- CPU crash (access violation, stack overflow)
- GPU crash (device removed/lost, TDR)
- Assert failure
- Out of memory
- Hang detection (watchdog timeout)

**Captured Data:**
- Call stack with symbol resolution
- Thread state (all threads)
- GPU breadcrumb trail (last N GPU operations)
- Active resource state (buffers, textures in flight)
- Log buffer (last N log entries)
- System info (GPU adapter, driver version, OS, memory)
- Frame number, timestamp, active scene

**API Design:**
```cpp
// Scoped crash context — adds info to crash report if crash happens within scope
UZ_CRASH_CONTEXT("Loading shader: {}", shaderPath);
UZ_CRASH_CONTEXT("Rendering frame {}", frameNumber);

// GPU breadcrumbs — RHI-level markers for GPU crash debugging
UZ_GPU_BREADCRUMB(commandList, "DrawMesh: {}", meshName);

// Assert with crash report
UZ_ASSERT(device != nullptr, "RHI device is null");
UZ_VERIFY_HR(hr, "Failed to create resource");
```

### Profiler Data Output

**Phase 1 (Runtime):**
- Console overlay with real-time stats (frame time, GPU time, draw calls, memory)
- CSV export for offline analysis
- Log file output with rotation

**Phase 2+ (Editor):**
- Full profiler UI in VSCode extension (timeline view, flame graph)
- Live stat streaming to editor panels
- GPU profiler with per-pass breakdown

---

## File Formats — JSON-Based, Schema-Validated

**All engine file formats use JSON.** This is a universal standard across the entire engine — project config, scenes, materials, input maps, everything. No binary formats for authored data (binary is only for compiled/cooked assets).

### Why JSON everywhere
- LLMs are trained on massive amounts of JSON — best structured format for AI manipulation
- Native parsing in every language (C++, TypeScript, Node.js)
- Human-readable, diffable in version control
- VSCode has built-in JSON/JSONC support with intellisense
- Nested structures map naturally to engine data hierarchies

### File Extensions & Formats

| Extension | Purpose | Format |
|-----------|---------|--------|
| `.uzproject` | Project configuration | JSON |
| `.uzscene` | Scene definition (entities, components, hierarchy) | JSON |
| `.uzmaterial` | Material definition (shader, parameters, textures) | JSON |
| `.uzprefab` | Prefab / entity template | JSON |
| `.uzinput` | Input action map | JSON |
| `.uzconfig` | Engine/editor settings | JSON |
| `.uzpak` | Cooked asset pack (build output) | **Binary** (not authored) |

### JSON Schema Validation
Every file format has a corresponding JSON Schema that:
- Documents every field, type, default value, and valid range
- Enables VSCode intellisense and auto-completion for `.uz*` files
- Validates files on load with clear error messages
- Serves as the single source of truth for the format spec

Schemas are stored in `runtime/schemas/` and published to the VSCode extension for editor support.

---

## Project Structure & Configuration (.uzproject)

**Zero hardcoded values.** Every engine setting has a default but can be overridden via the project file. The `.uzproject` file is the root of a game project and defines all configuration.

### Game Project Directory Structure
```
MyGame/
├── MyGame.uzproject                 # Project root config (JSON)
├── content/                         # All game assets
│   ├── scenes/                      # Scene files (.uzscene)
│   ├── textures/                    # Source textures
│   ├── meshes/                      # 3D models
│   ├── materials/                   # Material definitions
│   ├── audio/                       # Sound files
│   ├── fonts/                       # Font files
│   └── ui/                          # UI layouts
├── source/                          # Game-specific C++ or script code
├── plugins/                         # Project-specific engine plugins
├── binaries/                        # Build output (generated)
│   ├── win64/
│   ├── linux/
│   └── android/
└── cache/                           # Intermediate files (generated, git-ignored)
    ├── shaders/                     # Compiled shader cache
    ├── textures/                    # Compressed texture cache
    └── cooked/                      # Cooked asset cache
```

### .uzproject File Format (JSON — LLM-friendly)
```json
{
  "name": "MyGame",
  "version": "1.0.0",
  "engine_version": "0.2",

  "rendering": {
    "backend": "dx12",
    "shader_model": "6.6",
    "resolution": [1920, 1080],
    "vsync": true,
    "max_fps": 0,
    "hdr": false,
    "render_scale": 1.0,
    "shadow_quality": "high",
    "shadow_resolution": 2048,
    "ao_method": "gtao",
    "anti_aliasing": "taa",
    "tonemap": "aces"
  },

  "physics": {
    "gravity": [0, -9.81, 0],
    "fixed_timestep": 0.01666,
    "max_substeps": 4
  },

  "audio": {
    "sample_rate": 48000,
    "max_channels": 64
  },

  "input": {
    "action_map": "config/input.json"
  },

  "build": {
    "platforms": ["win64", "linux"],
    "compression": "lz4",
    "texture_format": {
      "win64": "bc7",
      "linux": "bc7",
      "android": "astc"
    },
    "output_dir": "binaries"
  },

  "scenes": {
    "startup": "content/scenes/main_menu.uzscene"
  },

  "paths": {
    "content": "content",
    "source": "source",
    "plugins": "plugins",
    "cache": "cache"
  }
}
```

### Configuration Principles
1. **No hardcoded values** — every parameter has a default in the engine, overridable by `.uzproject`
2. **JSON format** — human-readable, LLM-friendly, easy to diff in version control
3. **Cascading config** — engine defaults → project config → scene overrides → runtime CLI flags
4. **Hot-reloadable** — config changes apply without restart where possible (Phase 2)
5. **Validated on load** — unknown keys warn, missing keys use defaults, invalid values error with clear message

### Config Resolution Order (highest priority wins)
```
CLI flags (--width, --backend, etc.)
  ↓
Scene overrides (.uzscene per-scene settings)
  ↓
Project config (.uzproject)
  ↓
Engine defaults (compiled into runtime)
```

---

## Rendering Architecture — Deferred Pipeline

### Pipeline
- **Deferred rendering** as primary pipeline
- Forward pass for transparents and special materials
- GBuffer layout: Albedo, Normal, Metallic/Roughness, Emissive, Depth

### Global Illumination (multi-tier)
| Tier | Method | Use Case |
|------|--------|----------|
| 1 | **Software ray-traced GI** (Lumen-style) | Default — works on all DX12 hardware |
| 2 | **Light probes** | Fallback for lower-end / mobile |
| 3 | **Hardware ray tracing (DXR)** | Opt-in for RTX/RDNA2+ GPUs |
| 4 | **Path tracing** | Reference / cinematic quality |

All tiers configurable via `.uzproject` rendering settings. Reference implementation from UE 5.6 Lumen (clean-room).

### Post-Processing Stack
- Tone mapping (ACES, configurable)
- TAA / FXAA
- GTAO / SSAO
- Bloom, DOF, Motion Blur
- Color grading / LUT
- All passes are modular and configurable

---

## Scripting — C++ & Lua

### C++ (Primary)
- Game logic written in C++ compiled against engine headers
- Shipped as DLL/SO for hot-reload in editor, statically linked in shipping builds
- Same language as engine internals — zero bridging cost

### Lua (Secondary)
- Embedded Lua VM for rapid prototyping and mod support
- Bound to engine API via lightweight C++ bindings
- Scripts live in project `source/` directory
- Hot-reloadable at runtime without recompilation

### LLM Optimization
Both C++ and Lua are well-represented in LLM training data. JSON scene files reference scripts by path — LLM can generate and modify game logic directly.

---

## Asset Import Pipeline

Source assets (FBX, glTF, PNG, WAV, etc.) are **imported and split** into engine-native open formats. The original source file is never used at runtime.

### Import Flow
```
Source Asset (.fbx, .gltf, .blend, etc.)
     │
     ▼
Asset Importer (build_pipeline/asset_processor)
     │
     ├── Meshes        → .uzmesh (JSON metadata + binary vertex data)
     ├── Skeleton       → .uzskeleton (JSON)
     ├── Animations     → .uzanim (JSON metadata + binary keyframes)
     ├── Materials      → .uzmaterial (JSON — shader graph or instance)
     ├── Textures       → .uztexture (JSON metadata, source PNG/EXR kept)
     └── Prefab         → .uzprefab (JSON — assembled entity with components)
```

### Principles
- **One source → many engine assets** — FBX splits into mesh, skeleton, animations, materials, textures
- **Source files preserved** in content/ alongside generated engine assets
- **Re-import on source change** — detected by file watcher (editor) or CLI
- **Open formats** — all generated assets are JSON (metadata) + optional binary blobs (vertex data, keyframes)
- **Import settings** stored per-asset as JSON sidecar files

---

## Material System — Shader Graphs & Instances

### Architecture
```
Shader Graph (.uzmaterial, type: "graph")
     │
     ▼
Material Instance (.uzmaterial, type: "instance")
     │  references a graph, overrides parameters
     ▼
Shader Permutations (compiled by build pipeline)
```

### Shader Graphs
- Node-based material definition stored as JSON
- Nodes: math ops, texture samples, PBR outputs, custom HLSL blocks
- Compiles to HLSL at build time → shader permutations via build pipeline
- Editable via node editor in VSCode extension (Phase 2)
- LLM can generate and modify graphs directly (JSON format)

### Material Instances
- Reference a parent shader graph
- Override specific parameters (colors, textures, scalars) without recompiling
- Lightweight — just a JSON file with parameter overrides
- Support parameter inheritance chains

### Example Material Instance
```json
{
  "type": "instance",
  "parent": "content/materials/pbr_standard.uzmaterial",
  "parameters": {
    "base_color": [0.8, 0.2, 0.1, 1.0],
    "metallic": 0.0,
    "roughness": 0.7,
    "normal_map": "content/textures/brick_normal.png",
    "ao_map": "content/textures/brick_ao.png"
  }
}
```

---

## Memory Model — Ownership & Borrow Checking

**No garbage collector. Deterministic memory management via Rust-inspired ownership semantics in C++.**

Applies to both client and dedicated server — identical memory model everywhere.

### Principles
- **Single owner** — every resource has exactly one owner at any time
- **Move semantics by default** — ownership transfers explicitly, no implicit copies
- **Borrow checking** — references are either one mutable (`&mut`) OR many immutable (`&`), never both
- **RAII everywhere** — resources freed deterministically when owner goes out of scope
- **No shared_ptr spaghetti** — `unique_ptr` as the ownership primitive, raw pointers only for non-owning observation

### Implementation Strategy
```cpp
// Owning handle — single owner, non-copyable, movable
template<typename T>
class Own {
    // unique_ptr semantics + custom deleters per subsystem
    // Move-only: transfer ownership explicitly
};

// Borrowed reference — non-owning, scope-validated in debug
template<typename T>
class Ref {
    // Raw pointer in release, lifetime-checked in debug builds
    // Cannot outlive the owner
};

// Mutable borrow — exclusive access, debug-validated
template<typename T>
class RefMut {
    // Debug: asserts no other borrows active
    // Release: zero-cost raw pointer
};
```

### Debug Validation
- In debug builds, borrow checker tracks active borrows and asserts on violations
- Use-after-free detection via poisoned memory patterns
- Double-free prevention via owner state tracking
- Zero overhead in release builds — all checks compile out

### Why Not GC
- **Deterministic** — no GC pauses, predictable frame times for AAA
- **Cache-friendly** — no object header overhead, tighter memory layouts
- **GPU sync** — GPU resource lifetimes must be explicit (fence-based), GC cannot manage this correctly
- **Server performance** — dedicated servers need predictable latency, GC spikes are unacceptable

---

## Hot Reload System

**Modeled after Godot's approach** — the best hot-reload of the three major engines. Changes apply instantly without losing editor/game state.

### What hot-reloads
| Asset Type | Method | State Preserved |
|-----------|--------|----------------|
| **Shaders/Materials** | File watcher → recompile → swap pipeline state | Yes |
| **Textures** | File watcher → re-import → upload to GPU | Yes |
| **Scenes** | Manual reload or file watcher | Partial (transforms preserved) |
| **Lua scripts** | File watcher → reload VM module | Yes (state reset per-script) |
| **C++ game DLL** | Serialize state → unload DLL → rebuild → reload → deserialize | Yes |
| **JSON configs** | File watcher → re-parse → apply | Yes |

### Implementation
- File watcher on content/ and source/ directories
- Asset dependency graph tracks what needs to reload when a file changes
- Serialize/deserialize pattern for C++ hot-reload (like Godot's `_get_property_list`)
- GPU resources swapped atomically — no visual glitch during reload

### Future: Live Compilation
Investigate real-time C++ compilation (like Unreal's Live Coding) for faster iteration. Deferred — hot-reload via DLL swap is sufficient initially.

---

## UI System — HTML/CSS Reactive

### Architecture
Game UI is built with **HTML/CSS** rendered via an embedded browser layer, using a **reactive framework** inspired by Vue.js patterns.

```
┌─────────────────────────────────────────┐
│  Game UI Layer (HTML/CSS/JS)            │
│                                         │
│  ┌──────────┐   data binding   ┌──────┐│
│  │ Reactive  │◄───────────────►│ C++  ││
│  │ Templates │   events        │ State││
│  │ (Vue-style)│◄───────────────►│ Lua  ││
│  └──────────┘                  │ State││
│                                └──────┘│
└─────────────────────────────────────────┘
```

### Design
- **Reactive data binding** — C++ and Lua expose observable state to UI; UI updates automatically on change
- **Event system** — UI emits events consumed by C++ or Lua (button clicks, slider changes, input fields)
- **CSS styling** — standard CSS for layout, animations, transitions — designers can style without touching engine code
- **Component-based** — reusable UI components (health bar, inventory slot, dialog box) as HTML templates
- **LLM-friendly** — HTML/CSS is well-understood by LLMs, easy to generate and modify
- **Debug UI** — imgui for engine/editor debug overlays (separate from game UI)

### Binding API (C++)
```cpp
// Expose C++ state to UI
ui->Bind("player.health", &playerHealth);  // auto-updates UI on change
ui->Bind("inventory", &inventoryList);     // reactive list binding

// Listen for UI events
ui->On("buy_item", [](const UIEvent& e) {
    auto itemId = e.Get<std::string>("id");
    // handle purchase
});
```

### Binding API (Lua)
```lua
-- Expose Lua state to UI
ui:bind("score", score_value)
ui:bind("quest_log", quest_entries)

-- Listen for UI events
ui:on("dialog_choice", function(event)
    local choice = event.choice_id
    -- handle dialog
end)
```

---

## Upscaling — DLSS / FSR / XeSS

### Supported Technologies
| Technology | Vendor | Integration |
|-----------|--------|-------------|
| **DLSS** (Deep Learning Super Sampling) | NVIDIA | DLSS SDK, requires RTX GPU |
| **FSR** (FidelityFX Super Resolution) | AMD | Open source, works on any GPU |
| **Intel XeSS** (Xe Super Sampling) | Intel | XeSS SDK, optimized for Arc but works broadly |

### Architecture
- Render at lower internal resolution → upscale to display resolution
- Motion vectors and depth buffer fed to upscaler
- Integrated as a **post-processing pass** in the render pipeline, after TAA but before final output
- Selectable at runtime via project settings or in-game menu
- Automatic fallback: DLSS → XeSS → FSR (based on GPU vendor detection)
- Quality presets: Ultra Performance, Performance, Balanced, Quality, Native (pass-through)

### Configuration
```json
{
  "rendering": {
    "upscaling": {
      "method": "auto",
      "quality": "balanced"
    }
  }
}
```

---

## Networking — Dedicated Server & Replication

### Architecture
Lightweight dedicated server model inspired by Unreal's replication system, but leaner and more optimized.

```
┌─────────────┐       state sync        ┌─────────────┐
│  Client 1   │◄───────────────────────►│  Dedicated   │
│  (full engine)│                        │  Server      │
└─────────────┘                         │  (headless)  │
┌─────────────┐                         │              │
│  Client N   │◄───────────────────────►│  Authoritative│
└─────────────┘                         └─────────────┘
```

### Design
- **Authoritative server** — server owns game state, clients predict and reconcile
- **Property replication** — components mark properties as replicated with conditions (owner-only, initial-only, always)
- **RPCs** — Client→Server (validated), Server→Client (reliable/unreliable), Multicast
- **Relevancy** — only replicate actors relevant to each client (distance, area, custom rules)
- **Delta compression** — only changed properties sent, bitpacked for minimal bandwidth
- **Server export** — game project builds as dedicated server executable (headless, no renderer)
- **Listen server** — client can also host (for small games / LAN)
- **Replay system** — record/playback network state for debugging and game replays

### Replication Example (C++)
```cpp
class UZ_REPLICATED PlayerState : public Component {
    REPLICATE(health, ReplicateCondition::OwnerOnly);
    REPLICATE(position, ReplicateCondition::Always);
    REPLICATE(score, ReplicateCondition::Always);

    SERVER_RPC(TakeDamage, float amount);
    CLIENT_RPC(OnDamageReceived, float newHealth);
    MULTICAST_RPC(PlayHitEffect, Vec3 location);
};
```

---

## Build Toolchain

### Compiler Support
| Compiler | Status | Notes |
|----------|--------|-------|
| **Zig** (cc) | Primary (experimental) | Cross-compilation, fast builds, C/C++ interop |
| **MSVC** | Supported | Windows primary, best DirectX debugging |
| **Clang** | Supported | Cross-platform, excellent diagnostics |

- Zig as build system and C/C++ compiler (via `zig cc` / `zig c++`) — testing viability for cross-compilation and faster iteration
- MSVC always supported as fallback and for Windows-specific tooling (PIX, GPU debugging)
- Clang for cross-platform builds and sanitizers (ASAN, UBSAN, TSAN)

### Shader Compilation
- **DXC** (DirectX Shader Compiler) — HLSL → DXIL (DX12), SPIR-V (Vulkan)
- Offline compilation during build pipeline for shipping
- Runtime compilation for development (hot-reload shaders)
- Shader permutation system — feature flags generate variants at build time
- Shader cache — compiled shaders cached by content hash, avoid recompilation

### Third-Party Dependencies Strategy

**POC lesson**: compiling libs like zlib from source triggered antivirus false positives (unsigned DLLs flagged as trojans). Two-track approach:

| Track | When to use | Location |
|-------|-------------|----------|
| **vcpkg** | Libs that produce DLLs needing code signing (zlib, openssl, etc.) — vcpkg provides pre-signed binaries | `thirdparty/vcpkg/` |
| **Source** | Header-only or static libs with no AV issues (stb, imgui, json, etc.) | `thirdparty/source/` |

- `vcpkg.json` manifest at `thirdparty/` root declares required packages
- **Setup script** (`scripts/setup.ps1` / `setup.sh`) — installs vcpkg if not present, runs `vcpkg install`, validates environment
- Setup script also handles GitHub-hosted deps that aren't in vcpkg
- All thirdparty headers isolated — no engine headers leak into thirdparty, no thirdparty headers leak into engine public API

### Third-Party Library Selection

Curated from analysis of Unreal 5.6 (147 libs) and Godot (68 libs). Prioritizing quality, performance, and minimal dependency footprint.

#### Core / Foundation
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **zlib** (1.3+) | General-purpose compression | zlib | vcpkg | Both |
| **zstd** | Fast lossless compression (assets, network) | Facebook | vcpkg | Both |
| **mimalloc** | High-performance memory allocator (replaces default) | Microsoft | source | Both |
| **fmt** | Fast string formatting (logging, debug) | {fmt} | source (header-only) | Both |
| **xxhash** | Extremely fast hashing (asset IDs, caching) | Cyan4973 | source (header-only) | Both |
| **ICU** | Unicode, localization, plural rules | Unicode.org | vcpkg | Both |
| **RapidJSON** | Fast JSON parse/serialize (all .uz* files) | Tencent | source (header-only) | Both |
| **CLI11** | Command-line argument parser | CLI11 | source (header-only) | Both |

#### Graphics / Rendering
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **D3D12MemoryAllocator** | DX12 GPU memory management | AMD GPUOpen | source | Godot |
| **DirectX Headers** | DX12/DX11 API headers | Microsoft | source | — |
| **Vulkan Headers + VMA** | Vulkan API + memory allocator | Khronos / AMD | source | Both |
| **metal-cpp** | Metal C++ bindings (macOS/iOS) | Apple | source | Godot |
| **DXC** (DirectXShaderCompiler) | HLSL → DXIL/SPIR-V compilation | Microsoft | vcpkg | — |
| **SPIRV-Cross** | SPIR-V → GLSL/MSL translation | Khronos | source | Both |
| **SPIRV-Reflect** | Shader reflection (binding layout extraction) | Khronos | source | Both |
| **meshoptimizer** | Mesh LOD, simplification, vertex cache opt | zeux | source | Godot |
| **astcenc** | ASTC texture compression | ARM | source | Both |
| **etcpak** | Fast ETC2/DXT compression | wolfpld | source | Godot |
| **nvTextureTools** | NVIDIA BC6H/BC7 texture compression | NVIDIA | vcpkg | Unreal |
| **SMAA** | Subpixel morphological antialiasing | iryoku | source (header-only) | Godot |
| **tinyexr** | EXR/HDR image format | syoyo | source (header-only) | Godot |

#### Physics & Collision
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **Jolt Physics** | 3D rigid body physics engine | jrouwe | source | Godot |
| **VHACD** | Convex decomposition for collision meshes | kmammou | source | Both |

#### Audio
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **miniaudio** | Cross-platform audio playback/capture | mackron | source (header-only) | — |
| **libvorbis + libogg** | Ogg Vorbis audio codec | Xiph.org | vcpkg | Both |
| **libopus** | Low-latency audio codec (voice chat) | Xiph.org | vcpkg | Unreal |
| **dr_libs** | WAV/MP3/FLAC single-header decoders | mackron | source (header-only) | Both |
| **Kiss FFT** | FFT for audio analysis/effects | Mark Borgerding | source | Unreal |

#### Image Formats
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **libpng** | PNG read/write | libpng.org | vcpkg | Both |
| **libjpeg-turbo** | Fast JPEG decode/encode | libjpeg-turbo | vcpkg | Both |
| **libwebp** | WebP format (UI textures, lightweight) | Google | vcpkg | Godot |
| **stb_image / stb_image_resize** | Quick image load/resize (tools, fallback) | nothings | source (header-only) | Both |

#### Text & Font
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **FreeType** | Font rasterization | FreeType.org | vcpkg | Both |
| **HarfBuzz** | Complex text shaping (Arabic, Devanagari, etc.) | HarfBuzz | vcpkg | Both |
| **msdfgen** | SDF font atlas generation | Chlumsky | source | Both |

#### Networking & Crypto
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **enet** | Reliable UDP (game networking) | lsalzman | source | Both |
| **mbedTLS** | TLS/SSL (platform services, HTTPS) | Mbed | vcpkg | Godot |
| **libcurl** | HTTP client (REST APIs, downloads) | curl | vcpkg | Unreal |
| **nghttp2** | HTTP/2 (asset downloads, streaming) | tatsuhiro-t | vcpkg | Unreal |

#### Asset Import
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **ufbx** | FBX file import (no Autodesk SDK dependency) | ufbx | source | Godot |
| **cgltf** | glTF 2.0 parser | jkuhlmann | source (header-only) | — |
| **MikkTSpace** | Tangent space generation (DCC compatibility) | mmikk | source | Both |
| **xatlas** | UV atlas generation (lightmaps) | jpcy | source | Godot |

#### AI & Navigation
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **Recast/Detour** | Navmesh generation + pathfinding | recastnavigation | source | Both |
| **RVO2** | Crowd collision avoidance | snape | source | Godot |

#### Math & Geometry
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **DirectXMath** | SIMD math (vectors, matrices) | Microsoft | source (header-only) | Unreal |
| **nanoflann** | KD-tree nearest neighbor search | jlblancoc | source (header-only) | Unreal |
| **clipper2** | 2D polygon clipping/offsetting | AngusJohnson | source | Godot |
| **FastNoiseLite** | Procedural noise generation | Auburn | source (header-only) | Godot |

#### Debug & Profiling
| Library | Purpose | Source | Track | Used By |
|---------|---------|--------|-------|---------|
| **imgui** | Debug UI overlays (editor/dev only) | ocornut | source | — |
| **RenderDoc API** | GPU frame capture integration | Baldur Karlsson | source (header-only) | Unreal |
| **libbacktrace** | Stack traces for crash reports | GNU | source | Godot |

#### Platform SDKs (installed separately, not in thirdparty/)
| SDK | Purpose | Notes |
|-----|---------|-------|
| **Steamworks** | Steam platform services | NDA, separate install |
| **Epic Online Services** | EOS platform services | Separate install |
| **DLSS SDK** | NVIDIA upscaling | Separate install, NDA |
| **FSR SDK** | AMD upscaling | Open source, separate |
| **XeSS SDK** | Intel upscaling | Separate install |
| **OpenXR** | VR/AR support | vcpkg or separate |

#### Considered and Rejected
| Library | Reason for Rejection |
|---------|---------------------|
| **PhysX** | Heavier than Jolt, NVIDIA dependency; Jolt is MIT, faster, and simpler |
| **Boost** | Too large; use specific standalone libs (fmt, asio) instead |
| **FBX SDK** (Autodesk) | Proprietary, large binary; ufbx is MIT and lighter |
| **OpenAL** | Legacy API; miniaudio is simpler and more modern |
| **Eigen** | Overkill for game math; DirectXMath or custom SIMD is leaner |
| **CEF** (Chromium) | Too heavy for game UI; HTML/CSS via lightweight embedded renderer |
| **SDL** | We handle windowing/input directly via platform layer |

---

## Localization System

**Modeled after Unreal's localization pipeline** — text IDs, string tables, culture-aware formatting.

### Architecture
- **String tables** — JSON files mapping text IDs to localized strings per culture
- **Text IDs** everywhere — no hardcoded user-facing strings in code
- **Culture system** — language + region (e.g., `pt-BR`, `en-US`, `ja-JP`)
- **Plural rules** — ICU-style plural categories (zero, one, two, few, many, other)
- **Format strings** — culture-aware number, date, currency formatting
- **Fallback chain** — `pt-BR` → `pt` → `en` (default)

### String Table Format
```json
{
  "namespace": "ui.main_menu",
  "culture": "pt-BR",
  "strings": {
    "MENU_PLAY": "Jogar",
    "MENU_SETTINGS": "Configurações",
    "ITEMS_COUNT": "{count, plural, =0{Nenhum item} one{# item} other{# itens}}"
  }
}
```

### Usage (C++)
```cpp
// Simple text lookup
auto text = UZ_TEXT("ui.main_menu", "MENU_PLAY");

// Formatted with arguments
auto text = UZ_TEXT_FMT("ui.main_menu", "ITEMS_COUNT", {{"count", 5}});
// → "5 itens" (pt-BR) / "5 items" (en-US)
```

### Usage (Lua)
```lua
local text = uz.text("ui.main_menu", "MENU_PLAY")
local text = uz.text_fmt("ui.main_menu", "ITEMS_COUNT", { count = 5 })
```

### Pipeline
- Source strings extracted from code (`UZ_TEXT` macros) and assets
- Translation files exported as JSON for translators or LLM translation
- Import translations → compile to binary lookup tables for shipping
- Editor shows text IDs with preview of current culture

---

## AAA Feature Set (Full Scope)

The engine targets feature parity with major AAA engines.

### Runtime Systems

| System | Approach |
|--------|----------|
| **Physics** | Jolt Physics — rigidbodies, colliders, raycasts, triggers, joints, cloth, destruction. |
| **Animation** | Skeletal animation, blend trees, animation state machines, control rig (procedural), IK (FABRIK, CCD), root motion, montages, additive blending, animation sequences. |
| **Particles / VFX** | Niagara-style modular VFX system (fully open JSON format). GPU particles, emitter/system graphs, forces, collisions, ribbons, mesh particles, events, data interfaces. Designed for 100% AI-driven VFX creation. |
| **Terrain** | Heightmap-based terrain, virtual texturing, multi-layer material painting, foliage instancing, LOD. |
| **Virtual Texturing** | Streaming virtual textures for terrain and large surfaces — tile-based, GPU feedback buffer, on-demand page loading. |
| **Raytracing** | Native DXR support — RT reflections, RT shadows, RT ambient occlusion, RT global illumination. |
| **Lumen GI** | Software-based global illumination (screen-space + surface cache + radiance cache), hardware RT acceleration path. |
| **Lighting** | Cascaded shadow maps, virtual shadow maps, volumetric fog/lighting, sky/atmosphere model, reflection probes. |
| **Camera** | Camera stack, post-process volumes (bloom, DOF, motion blur, color grading, vignette, chromatic aberration, film grain). |
| **Level Streaming** | World partition, async level loading/unloading, streaming volumes, seamless transitions. |
| **LOD** | Automatic mesh LOD generation, HLOD for large scenes, distance-based material simplification. |
| **Occlusion Culling** | GPU-driven occlusion, hierarchical Z-buffer, software rasterizer fallback. |
| **Audio** | Spatial audio, reverb zones, music system, voice management (decision pending: engine choice). |
| **AI** | Navigation mesh, pathfinding (A*), behavior trees, EQS-style queries, perception system. |

### Editor Tools (Phase 2 — VSCode Extension)

All editors run as webview panels in VSCode, built with HTML/CSS (reactive). Data stored as JSON — fully LLM-readable and generatable.

| Editor | Purpose | Data Format |
|--------|---------|-------------|
| **Shader Graph Editor** | Node-based material creation, PBR outputs, custom HLSL blocks | `.uzmaterial` (JSON graph) |
| **Animation Controller** | State machine for animation blending, transitions, layers | `.uzanimcontroller` (JSON) |
| **Control Rig** | Procedural animation: IK solvers, bone constraints, runtime corrections | `.uzcontrolrig` (JSON) |
| **Animation Sequence Editor** | Timeline editing, keyframes, curves, events, additive layers | `.uzanimsequence` (JSON + binary curves) |
| **UI Editor** | WYSIWYG HTML/CSS game UI design, data binding setup, event wiring | `.uzui` (HTML/CSS/JSON) |
| **Visual Script Editor** | Blueprint-style node graph for gameplay logic (C++/Lua code generation) | `.uzscript` (JSON graph) |
| **Behavior Tree Editor** | AI behavior trees: tasks, decorators, services, blackboard | `.uzbehavior` (JSON) |
| **Localization Editor** | String table management, culture overview, missing translation tracker | `.uzlocale` (JSON) |
| **Animation Editor** | Preview, retarget, blend space visualization, montage assembly | Uses `.uzanimsequence` |
| **VFX Graph Editor** | GPU particle emitter graphs, force fields, spawn rules | `.uzvfx` (JSON graph) |
| **Level Editor** | Viewport with gizmos, entity hierarchy, property inspector | `.uzscene` (JSON) |

### Editor Architecture
```
┌─ VSCode Extension ──────────────────────────────────────┐
│                                                          │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  │
│  │ Shader Graph  │  │ Anim Control │  │ Behavior Tree│  │
│  │ (webview)     │  │ (webview)    │  │ (webview)    │  │
│  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  │
│         │                 │                  │           │
│         └────────────┬────┘──────────────────┘           │
│                      │ IPC (postMessage)                 │
│                      ▼                                   │
│              ┌───────────────┐                           │
│              │ Extension Host │                           │
│              │ (TypeScript)   │                           │
│              └───────┬───────┘                           │
│                      │ N-API                             │
│                      ▼                                   │
│              ┌───────────────┐                           │
│              │ Runtime (.node)│                           │
│              │ live preview   │                           │
│              └───────────────┘                           │
└──────────────────────────────────────────────────────────┘
```

- Each editor is a **webview panel** with its own HTML/CSS/JS
- All editors share a common **node graph renderer** component (reused by shader, visual script, behavior tree, VFX, anim controller)
- Editors read/write JSON files directly — LLM can generate/modify without the editor
- **Live preview** — editors request runtime to render preview (material preview sphere, animation playback, particle preview)

---

## VFX System — Niagara-Style (Open Format)

**Modeled after Unreal's Niagara** — modular, stackable, data-driven — but fully open (JSON) and designed from day one for AI-generated content.

### Architecture
```
VFX System (.uzvfx, JSON)
├── System                     # Top-level container
│   ├── Emitter 1              # Independent particle emitter
│   │   ├── Spawn Module       # How/when particles are born
│   │   ├── Update Module      # Per-frame particle logic (forces, drag, noise)
│   │   ├── Render Module      # Sprite, ribbon, mesh, light renderer
│   │   └── Event Handlers     # React to collisions, death, custom events
│   ├── Emitter 2
│   └── ...
└── Data Interfaces            # External data (meshes, textures, scene queries)
```

### Key Differences from Niagara
- **Fully open JSON** — no proprietary binary, LLM can read/write/generate entire VFX systems
- **Module stack is JSON nodes** — same node graph format as shader graphs and visual scripts
- **No compilation step** — interpreted at runtime (GPU compute), hot-reloadable
- **AI-first API** — every parameter has semantic metadata (description, range, units) so LLMs understand what each knob does

### Example VFX System
```json
{
  "type": "vfx_system",
  "emitters": [
    {
      "name": "fire_core",
      "spawn": { "rate": 500, "burst": false },
      "modules": [
        { "type": "initial_velocity", "min": [0,2,0], "max": [0.5,4,0.5] },
        { "type": "lifetime", "min": 0.3, "max": 0.8 },
        { "type": "gravity", "force": [0, -1, 0] },
        { "type": "size_over_life", "curve": [[0,0.5],[0.5,1.0],[1.0,0.0]] },
        { "type": "color_over_life", "gradient": [
          [0, [1,0.9,0.3,1]], [0.5, [1,0.3,0,0.8]], [1, [0.2,0,0,0]]
        ]}
      ],
      "renderer": { "type": "sprite", "material": "content/materials/fire_particle.uzmaterial" }
    }
  ]
}
```

---

## AI Content Pipeline

**Core vision**: the engine is built for AI-driven development. Every asset format is open and LLM-friendly, and the pipeline integrates a **configurable multi-AI system** for end-to-end content generation — each category can use multiple providers, swappable via project config.

### AI-Assisted Workflows

```
┌─ AI Content Generation ─────────────────────────────────────┐
│                                                              │
│  Code ──────► Claude / GPT / DeepSeek ──► .cpp / .lua        │
│  Textures ──► Nano Banana / FLUX / Scenario ──► .png ──► import │
│  3D Models ──► Hunyuan / Meshy / Tripo ──► .fbx/.glb ──► import │
│  Audio ─────► Stable Audio / Suno / AIVA ──► .ogg ──► import │
│  SFX ───────► ElevenLabs SFX ──► .wav ──► import             │
│  Voice ─────► ElevenLabs / PlayHT ──► .wav ──► import        │
│  Music ─────► Suno / AIVA / Mubert ──► .ogg ──► import       │
│  Video ref ──► Kling / Veo / Sora ──► .mp4 (reference)       │
│  Animation ──► DeepMotion / Plask ──► .fbx ──► import        │
│  Icons/UI ──► Scenario / Leonardo ──► .png ──► import        │
│  VFX ───────► LLM ──► .uzvfx (JSON)                         │
│  Materials ──► LLM ──► .uzmaterial (JSON)                    │
│  Shaders ──► LLM ──► .uzmaterial shader graph (JSON)         │
│  UI ────────► LLM ──► .uzui (HTML/CSS/JSON)                  │
│  Scenes ────► LLM ──► .uzscene (JSON)                        │
│  AI Behavior ► LLM ──► .uzbehavior (JSON)                    │
│                                                              │
└──────────────────────────────────────────────────────────────┘
```

### Integrated AI Tools — Full Catalog

#### Code / LLM
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **Claude Opus 4.5** | Best complex refactoring, SWE-bench leader | Anthropic API | $15/$75 per 1M tokens |
| **Claude Sonnet 4.6** | Best speed/quality balance for daily coding | Anthropic API | Lower than Opus |
| **GPT-5.3 Codex** | Strong all-rounder | OpenAI API | Competitive |
| **Gemini 2.5 Pro** | 1M context, large codebase analysis | Google AI API | Competitive |
| **DeepSeek R1** | Budget leader ($0.55/$2.19 per 1M tokens) | DeepSeek API | Cheapest |
| **Codestral** | Code-specialized autocomplete | Mistral API | Budget-friendly |

#### Image / Texture Generation
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **Nano Banana** | Game texture generation (diffusion) | API | Pay-per-image |
| **FLUX 1.1 Pro** | Best realistic textures, lighting, materials | BFL / Replicate / fal.ai | Pay-per-image |
| **Stable Diffusion 3.5** | Full control, LoRA, ControlNet, self-hostable | Stability API + self-host | Free (self) / Paid (API) |
| **Scenario** | Game-specific: custom model training, sprites, icons | REST API | Freemium |
| **Leonardo AI** | Game textures, icons, environment art | API | Freemium |
| **DALL-E 3** | Best prompt adherence | OpenAI API | Pay-per-image |
| **Midjourney v6** | Best artistic aesthetics | No public API | $10-60/mo |

#### 3D Model Generation
| Tool | Strengths | API | Export Formats | Pricing |
|------|-----------|-----|----------------|---------|
| **Meshy 6** | Most mature API, best docs, enterprise | REST API | FBX, GLB, OBJ, STL, USDZ | Freemium |
| **Tripo AI** | Best overall quality, fast | REST API | OBJ, FBX, GLB, STL | Freemium |
| **Hunyuan3D 3.1** | Text/image-to-3D, LowPoly mode | API | Multiple | Varies |
| **Rodin (Hyper3D)** | Detailed, auto texturing, mesh optimization | API | OBJ, GLB | Paid |

#### Audio / Music
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **Suno V5** | Best overall quality, studio vocals | Third-party wrappers | Free tier / Pro $10/mo |
| **Stable Audio** | Industry standard commercial API | Stability API | Pay-per-gen |
| **AIVA** | Orchestral/cinematic game soundtracks | API | Freemium |
| **Mubert** | Most mature music API, streaming-ready | REST API | Pay-per-use |
| **Udio** | Most human-sounding vocals | Limited API | Paid |

#### Sound Effects
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **ElevenLabs SFX** | Sound effect generation from text description | REST API | Included in voice plans |
| **Stable Audio** | SFX + music in one API | Stability API | Pay-per-gen |

#### Voice / TTS (NPC Dialogue)
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **ElevenLabs** | 1200+ voices, 29 languages, voice cloning, NPC tools | REST + WebSocket | Free tier / Starter $5/mo |
| **PlayHT 2.0** | Conversational, 142 languages, custom pronunciation | REST API | Freemium |
| **MiniMax Speech** | Budget leader, excellent English | API | Budget-friendly |

#### Animation / Motion Capture
| Tool | Strengths | API | Export | Pricing |
|------|-----------|-----|--------|---------|
| **DeepMotion** | Video-to-3D animation, UE5 compatible | REST API | FBX, BVH | Freemium |
| **Plask Motion** | Single-camera mocap, engine compatible | API | FBX, BVH | Freemium |
| **Move AI** | Highest fidelity markerless mocap | API | FBX | Paid |
| **Krikey AI** | Fastest generation | API | FBX | Paid |

#### Icons / UI Assets
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **Scenario** | Game-specific: icons, sprites, custom training (10-20 images) | REST API + MCP | Freemium |
| **Leonardo AI** | Icons, item designs, UI elements | API | Freemium |
| **Ludo.ai** | Game-focused sprites, icons, UI, backgrounds | API + MCP | Paid |

#### Video Reference (Animation / Cinematics)
| Tool | Strengths | API | Pricing |
|------|-----------|-----|---------|
| **Kling 3.0** | Best value, 4K, multi-shot ($0.029/sec) | Multiple providers | Pay-per-second |
| **Google Veo 3.1** | 4K, most mature API | Vertex AI | $0.15/sec |
| **Sora 2** | Best cinematic coherence, 20s clips | OpenAI API | Pay-per-second |
| **Runway Gen-4.5** | Best control (Motion Brush, camera) | API | Subscription |

### AI Provider Configuration

Configurable per-project via `.uzproject` — swap providers without code changes:

```json
{
  "ai_providers": {
    "code": {
      "primary": "claude-sonnet-4-6",
      "fallback": "deepseek-r1",
      "api_key_env": "ANTHROPIC_API_KEY"
    },
    "texture": {
      "primary": "nano-banana",
      "fallback": "flux-1.1-pro",
      "style_preset": "fantasy_pbr",
      "api_key_env": "NANO_BANANA_API_KEY"
    },
    "3d_model": {
      "primary": "meshy-6",
      "fallback": "tripo",
      "output_format": "glb",
      "api_key_env": "MESHY_API_KEY"
    },
    "voice": {
      "primary": "elevenlabs",
      "default_voice": "custom_npc_warrior",
      "api_key_env": "ELEVENLABS_API_KEY"
    },
    "music": {
      "primary": "aiva",
      "genre": "orchestral_cinematic",
      "api_key_env": "AIVA_API_KEY"
    },
    "sfx": {
      "primary": "elevenlabs-sfx",
      "api_key_env": "ELEVENLABS_API_KEY"
    },
    "animation": {
      "primary": "deepmotion",
      "api_key_env": "DEEPMOTION_API_KEY"
    },
    "icons": {
      "primary": "scenario",
      "custom_model_id": "game_icon_style_v2",
      "api_key_env": "SCENARIO_API_KEY"
    }
  }
}
```

### Architecture

```
┌─ MCP Server (Phase 3) ─────────────────────────────┐
│                                                      │
│  ┌──────────────────────────────────┐               │
│  │  AI Provider Abstraction Layer   │               │
│  │  ai::Generate(category, prompt)  │               │
│  └──────┬───────────────────────────┘               │
│         │ routes to configured provider              │
│  ┌──────▼──────┐  ┌────────┐  ┌────────┐           │
│  │ Provider A   │  │ Prov B  │  │ Prov C  │  ...    │
│  │ (REST API)   │  │ (API)   │  │ (local) │         │
│  └──────┬──────┘  └────┬───┘  └────┬───┘           │
│         │              │           │                 │
│         └──────────────┴───────────┘                 │
│                   │                                  │
│         ┌────────▼────────┐                         │
│         │ Import Pipeline  │                         │
│         │ validate → split │                         │
│         │ → optimize → save│                         │
│         └────────┬────────┘                         │
│                  │                                   │
│         ┌────────▼────────┐                         │
│         │ content/ folder  │                         │
│         │ (.uz* assets)    │                         │
│         └─────────────────┘                         │
└──────────────────────────────────────────────────────┘
```

### Why This Works
- **All formats are JSON** — LLMs can generate valid assets without special tooling
- **JSON Schema validation** — generated files are validated before import, LLM gets error feedback
- **MCP server** — LLM has structured tools to create/modify any asset type
- **Visual feedback loop** — `--screenshot` mode lets LLM see results and iterate
- **Semantic metadata** — every parameter in every format has description, range, and units — LLM understands what it's editing
- **Provider-agnostic** — swap AI providers via config, no code changes
- **Fallback chains** — if primary provider fails/is slow, automatically try fallback

### End-to-End AI VFX Example
```
1. User: "create a fire spell explosion VFX"
2. LLM generates fire_particle texture prompt → Nano Banana → texture imported
3. LLM generates spark mesh prompt → Meshy → mesh imported + split
4. LLM generates fire crackling SFX prompt → ElevenLabs SFX → .wav imported
5. LLM writes fire_explosion.uzmaterial (shader graph JSON)
6. LLM writes fire_explosion.uzvfx (emitter system JSON)
7. Runtime loads VFX → --screenshot → LLM reviews
8. LLM iterates (adjust colors, timing, particle count)
9. Done — zero manual work, complete with sound
```

---

## Platform Services — Native SDK Integration

Unified abstraction layer over platform-specific SDKs for online services.

### Supported Platforms
| Platform | SDK | Services |
|----------|-----|----------|
| **Steam** | Steamworks | Login, achievements, leaderboards, matchmaking, workshop, cloud saves, overlay |
| **Epic** | Epic Online Services (EOS) | Login, achievements, matchmaking, voice chat, anti-cheat |
| **Google** | Google Play Games Services | Login, achievements, leaderboards, ads (AdMob), billing |
| **Sony** | PlayStation SDK (NDA) | PSN login, trophies, matchmaking, store |
| **Microsoft** | Xbox Live / GDK | Xbox login, achievements, Game Pass, store, ads |

### Architecture
```
┌──────────────────────────────────────┐
│  Game Code (C++ / Lua)               │
│  platform::Login(), platform::Pay()  │
└──────────────┬───────────────────────┘
               │ unified API
┌──────────────▼───────────────────────┐
│  Platform Abstraction Layer           │
│  IPlatformServices interface          │
└──┬───────┬───────┬───────┬───────┬──┘
   │       │       │       │       │
 Steam   Epic   Google   Sony   Xbox
```

### Unified Services
| Service | API Example |
|---------|------------|
| **Authentication** | `platform::Login()` → platform-specific OAuth/token flow |
| **Achievements** | `platform::UnlockAchievement("first_blood")` |
| **Leaderboards** | `platform::SubmitScore("speedrun", time)` |
| **Payments / IAP** | `platform::Purchase("item_pack_01")` → platform store |
| **Ads** | `platform::ShowRewardedAd()` → AdMob / platform ad network |
| **Cloud Saves** | `platform::SaveToCloud(slot, data)` |
| **Matchmaking** | `platform::FindMatch(rules)` |
| **Rich Presence** | `platform::SetPresence("In Battle - Level 5")` |

### Design
- **One interface, multiple backends** — game code never touches SDK-specific APIs
- **Multi-platform login** — support linking accounts across platforms
- **Async by default** — all platform calls return futures/callbacks (network-bound)
- **Graceful degradation** — if a service isn't available on a platform, calls are no-ops
- **NDA-safe** — Sony/Xbox SDK integration in separate private modules, not in public repo

---

## Inter-Layer Communication

**Primary approach: Native N-API addon (no IPC/WebSocket overhead)**

The runtime compiles as a native `.node` module (N-API/node-addon-api) that the VSCode extension loads directly in-process. This eliminates all serialization and network overhead — function calls between TypeScript and C++ are direct bindings.

```
┌──────────────────────────────────────────────┐
│  VSCode Extension Process (Electron/Node.js) │
│                                              │
│  ┌─────────────────┐    N-API (in-process)   │
│  │  Extension UI    │◄──────────────────────►│
│  │  (TypeScript)    │    direct function calls│
│  └────────┬────────┘                         │
│           │              ┌──────────────────┐ │
│           │              │  Runtime Engine   │ │
│           │              │  (.node native)   │ │
│           │              │  C++ / DirectX    │ │
│           │              └──────────────────┘ │
│           │                                   │
│           │ MCP protocol                      │
│           ▼                                   │
│  ┌─────────────────┐                         │
│  │  MCP Server      │                         │
│  │  (Node.js)       │                         │
│  └─────────────────┘                         │
└──────────────────────────────────────────────┘
```

**Why N-API instead of IPC/WebSocket:**
- Zero serialization overhead — share pointers, buffers, GPU surfaces directly
- No frame streaming needed — renderer writes to a shared texture, extension reads it
- Eliminates the FPS bottleneck from POC (244 FPS runtime vs webview limit)
- Single process — simpler lifecycle, no spawn/kill management
- N-API is stable across Node.js versions, ships with Electron

**Communication patterns:**

```
┌─ VSCode Extension Process ─────────────────────────────────┐
│                                                            │
│  ┌──────────────┐   N-API    ┌───────────────────────┐    │
│  │ Extension Host│◄─────────►│ Runtime (.node native) │    │
│  │ (TypeScript)  │  direct   │ C++ / DirectX          │    │
│  └──────┬───────┘  calls     └───────────▲───────────┘    │
│         │                                │                 │
│         │ IPC                            │ IPC             │
│         ▼                                │                 │
│  ┌──────────────────────────────┐        │                 │
│  │ Editor Webview Panels         │        │                 │
│  │ (Inspector, Hierarchy,        │◄───────┘                │
│  │  Asset Browser, Console, ...) │  state queries/events   │
│  └──────────────────────────────┘                          │
│         │                                                  │
│         │ MCP protocol                                     │
│         ▼                                                  │
│  ┌──────────────┐                                         │
│  │ MCP Server    │◄──── can also query runtime via IPC    │
│  │ (Node.js)     │                                         │
│  └──────────────┘                                         │
└────────────────────────────────────────────────────────────┘
```

- **Extension Host ↔ Runtime**: N-API direct calls (create entity, load scene, render commands)
- **Runtime → Extension**: callbacks/events (frame ready, profiler data, log output)
- **Webview Panels ↔ Extension Host**: IPC (`postMessage` / `onDidReceiveMessage`) — editor panels (inspector, hierarchy, asset browser, console) communicate with the extension host which relays to/from runtime
- **Webview Panels ↔ Runtime**: indirect via Extension Host IPC → N-API bridge
- **MCP ↔ Runtime**: MCP server can query runtime state via IPC or through the extension host as intermediary
- **Extension ↔ MCP**: standard MCP protocol (stdio/SSE)

---

## Directory Structure (proposed)

```
UzEngine/0.2/
├── docs/                                # Architecture documentation
├── runtime/                             # Native engine (C++)
│   ├── core/                            # Engine foundation (no game/editor logic)
│   │   ├── logging/                     # Log categories, output backends, macros
│   │   ├── profiling/                   # CPU profiler, stat groups, scope timers
│   │   ├── gpu_profiler/                # GPU timestamp queries, breadcrumbs
│   │   ├── crash_report/                # Crash context, minidump, GPU crash capture
│   │   ├── math/                        # Vectors, matrices, transforms, colors
│   │   ├── memory/                      # Allocators, pools, tracking
│   │   ├── containers/                  # Custom containers (if needed beyond STL)
│   │   ├── rhi/                         # RHI abstraction layer
│   │   │   ├── rhi.h                    # Abstract RHI interface
│   │   │   ├── dx12/                    # DirectX 12 backend
│   │   │   ├── dx11/                    # DirectX 11 backend
│   │   │   ├── vulkan/                  # Vulkan backend
│   │   │   └── metal/                   # Metal backend
│   │   └── renderer/                    # High-level render pipeline (PBR, post-fx, passes)
│   ├── scene/                           # Everything needed for a scene to function
│   │   ├── ecs/                         # Entity Component System
│   │   ├── components/                  # Transform, mesh, light, camera, etc.
│   │   ├── systems/                     # Physics, audio, animation, input
│   │   └── serialization/               # Scene load/save (open format)
│   ├── editor/                          # Editor-only runtime features (stripped in shipping)
│   │   ├── grid/                        # Viewport grid rendering
│   │   ├── gizmos/                      # Transform, selection, debug gizmos
│   │   ├── debug_draw/                  # Debug lines, shapes, text overlay
│   │   └── selection/                   # Object picking, selection highlight
│   ├── platform/                        # OS-specific abstraction layer
│   │   ├── windows/                     # Win32 window, input, file system
│   │   ├── macos/                       # macOS/Cocoa specifics
│   │   ├── linux/                       # X11/Wayland specifics
│   │   └── android/                     # Android NDK specifics
│   ├── build_pipeline/                  # Game packager & asset processing
│   │   ├── asset_processor/             # Texture compression, mesh optimization
│   │   ├── shader_compiler/             # HLSL → DXBC/DXIL/SPIR-V/MSL
│   │   ├── packager/                    # Asset packing (.uzpak), exe generation
│   │   └── cook/                        # Platform-specific cooking rules
│   ├── modules/                         # Optional/pluggable engine modules
│   ├── thirdparty/                      # Third-party libraries (isolated)
│   │   ├── source/                      # Libs compiled from source (safe ones only)
│   │   ├── vcpkg/                       # Libs via vcpkg (signed DLLs — avoids antivirus false positives)
│   │   └── vcpkg.json                   # vcpkg manifest for required packages
│   ├── schemas/                         # JSON Schemas for all .uz* file formats
│   │   ├── uzproject.schema.json
│   │   ├── uzscene.schema.json
│   │   ├── uzmaterial.schema.json
│   │   ├── uzprefab.schema.json
│   │   └── uzinput.schema.json
│   ├── shaders/                         # HLSL shaders
│   │   ├── sm6/                         # SM6.6 shaders (primary)
│   │   └── sm5/                         # SM5 fallback shaders
│   ├── tests/                           # Runtime test harness
│   └── CMakeLists.txt
├── napi/                               # N-API bridge: compiles runtime C++ → .node addon
│   ├── CMakeLists.txt                  # cmake-js build config (DX12, MF encoder, runtime linkage)
│   ├── package.json                    # node-addon-api + cmake-js deps
│   └── src/
│       ├── addon.cpp                   # N-API bindings, triple buffer, frame loop
│       ├── dx12_renderer.cpp/.h        # DX12 offscreen renderer (POC; will import from runtime/)
│       └── mf_encoder.cpp/.h           # Media Foundation H.264 encoder
├── extension/                           # VSCode extension (Phase 2)
│   ├── package.json                    # Extension manifest, commands, activation
│   ├── tsconfig.json
│   └── src/
│       └── extension.ts                # Entry point, webview, frame delivery
├── mcp/                                 # MCP server (Phase 3)
├── package.json                         # Root: orchestrates build of napi/ + extension/
└── README.md
```

### Runtime Module Descriptions

Core directories (core, scene, editor, platform) sit at the runtime root. `modules/` holds optional/pluggable engine modules. Each unit is self-contained with explicit dependencies.

| Module | Purpose | Dependencies | Shipping Build |
|--------|---------|-------------|---------------|
| `core/` | Engine foundation: logging, profiling, math, memory, RHI, renderer. | None (leaf module) | Yes |
| `scene/` | ECS, components, systems, serialization. | core | Yes |
| `editor/` | Grid, gizmos, debug draw, selection. | core, scene | **No — stripped** |
| `platform/` | OS abstraction: window, input, file I/O. Per-OS subfolders. | core | Yes (per-platform) |

Non-module directories:

| Directory | Purpose | Shipping Build |
|-----------|---------|---------------|
| `build_pipeline/` | Asset processing, shader compilation, game packaging. | Tool only (not in game) |
| `napi/` | N-API bridge — compiles runtime C++ into `.node` addon for extension. Imports from `runtime/`, adds N-API bindings + frame encoding. | Tool only (editor) |
| `thirdparty/` | Vendored third-party libs. Isolated, no engine headers leak in. | Yes |
| `shaders/` | HLSL shader source (SM6.6 primary, SM5 fallback). | Compiled only |
| `tests/` | Test harness for validating runtime without editor/extension. | No |

---

## Engine Comparison & Architecture Decisions

Comparative analysis of Unreal 5.6, Godot 4.x, and Unity 6 to inform every UzEngine subsystem choice.

### 1. ECS / Entity System

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Hybrid OOP + ECS | Traditional `AActor`/`UActorComponent` (OOP) + Mass Entity (archetype ECS for crowds/large scale). Fragments stored in archetypes. `UMassProcessor` for system execution. |
| **Godot** | Node tree (OOP) | `Node` hierarchy with parent-child relationships. `SceneTree` manages lifecycle. Process groups for threading. No ECS. |
| **Unity** | Hybrid OOP + DOTS ECS | Traditional `GameObject`/`MonoBehaviour` + DOTS ECS (archetype-based, 16KB chunks). `ISystem` (Burst-compiled) + `IJobEntity` for parallel iteration. |

**UzEngine choice: entt (sparse-set archetype ECS)**
- Header-only, MIT, C++17 — fits our stack perfectly
- Fastest in benchmarks for iteration and entity creation
- Sparse-set storage gives O(1) component add/remove (vs archetype migration in Unity DOTS)
- Simpler than Mass Entity or DOTS — no managed runtime, no Burst dependency
- Our lazy component system maps naturally to ECS archetypes

### 2. Threading / Job System

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | TaskGraph + named threads | Named threads (Game, Render, RHI) + worker pool. 3 priority levels. `FTaskGraphInterface` singleton. Modern `UE::Tasks::Task` with pipe dependencies. |
| **Godot** | WorkerThreadPool | `WorkerThreadPool` with indexed tasks for parallel-for. Semaphore sync. `SafeNumeric<T>` for atomics. Process groups allow sub-thread execution. |
| **Unity** | Job System + Burst | Worker threads = logical cores - 1. `IJobParallelFor` for data parallelism. `NativeContainer` safety system. Burst compiler for SIMD. `JobHandle` dependencies. |

**UzEngine choice: Task graph with named threads (Unreal-style)**
- Named threads (Game, Render, RHI) — proven pattern for graphics engines
- Worker pool sized to `std::thread::hardware_concurrency() - 3` (reserve Game/Render/RHI)
- `UZ_PARALLEL_FOR(range, lambda)` for data parallelism (like Unity's `IJobParallelFor`)
- Task dependencies via handles (like Unity's `JobHandle`)
- 3 priority levels: High (render commands), Normal (gameplay), Background (asset loading, navmesh bake)

### 3. Physics

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Chaos Physics (proprietary) | Custom physics. Async stepping, task graph integration. Fixed substep with min/max dt + max substeps. `FChaosScene` per scene. |
| **Godot** | Jolt Physics | `PhysicsServer3D` abstract → `JoltPhysicsServer3D` implementation. Fixed 60Hz. Custom `JoltJobSystem` + `JoltTempAllocator`. RID-based handles. |
| **Unity** | PhysX (classic) / Havok (DOTS) | PhysX for MonoBehaviour. Unity Physics (stateless, deterministic) or Havok (stateful, stable) for DOTS. `FixedStepSimulationSystemGroup` at 1/60s. |

**UzEngine choice: Jolt Physics**
- MIT license, zero royalties (vs Chaos proprietary, Havok paid)
- Faster than PhysX in benchmarks, Godot already validated integration at scale
- Deterministic (important for networking — server/client sync)
- Integrate Jolt's job system with our task graph
- Fixed 60Hz default, configurable substeps
- RID-based handles (like Godot) for ownership-safe resource management

### 4. Audio

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Custom AudioMixer | Submix hierarchy (Main, Reverb, EQ). Separate audio render thread. `FQuartzClockManager` for quantized events. Format settings per codec. |
| **Godot** | Custom AudioServer | `AudioDriver` for platform I/O. Stream/playback/effects framework. Tracks mix time for A/V sync. |
| **Unity** | FMOD-based | Internal FMOD. `AudioSource`/`AudioListener` model. `AudioMixer` with groups (Music, SFX, Dialogue). 3D spatialization at source. |

**UzEngine choice: miniaudio (backend) + custom submix mixer**
- miniaudio handles device abstraction cross-platform (Windows WASAPI, macOS CoreAudio, Linux PulseAudio) — single header, public domain
- Custom submix hierarchy on top (like Unreal): Main → SFX / Music / Voice / Reverb
- Separate audio thread (like Unreal) — never block game thread
- No FMOD/Wwise licensing — free forever
- Spatial audio with distance attenuation, reverb zones, HRTF support
- Quantized clock for rhythm games (inspired by Unreal's Quartz)

### 5. Math Library

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Custom templated | `TVector<T>`, `TQuat<T>`, `TMatrix<T>`, `TTransform<T>`. Conditional SIMD via `VectorRegister`. Large World Coordinates (double precision). |
| **Godot** | Custom | `Vector3`, `Basis`, `Transform3D`, `Quaternion`. `real_t` typedef (float/double). No explicit SIMD. |
| **Unity** | Unity.Mathematics | `float3`, `float4x4`, `quaternion`. Burst maps to SIMD. Shader-like syntax (`math.dot()`, `math.cross()`). |

**UzEngine choice: DirectXMath + thin wrapper**
- We're DX12-first — DirectXMath is the native math lib, zero impedance mismatch
- SIMD by default (SSE2/SSE4/AVX2), not conditional — every operation is vectorized
- Header-only, MIT, maintained by Microsoft
- Thin wrapper for ergonomics: `Vec3`, `Vec4`, `Mat4`, `Quat`, `Transform`
- Add `real_t` support (float/double) for large world coordinates if needed later

### 6. Error Handling

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Assertion macros | `check()` (fatal, stripped in shipping), `ensure()` (non-fatal, logs + continues), `checkf()`/`ensureMsgf()` (with message). `UE_LOG()` for logging. |
| **Godot** | ERR_FAIL macros | `ERR_FAIL_COND()`, `ERR_FAIL_NULL()`, `ERR_FAIL_INDEX()`. Handler hooks. `ErrorHandlerType` enum (ERROR, WARNING, SCRIPT, SHADER). |
| **Unity** | Debug + exceptions | `Debug.Assert()` (dev only), `Debug.Log/LogWarning/LogError`. Standard C# try/catch (not in Burst). |

**UzEngine choice: Unreal-style assertion macros**
- `UZ_CHECK(cond)` — fatal in debug/dev, stripped in shipping (like `check()`)
- `UZ_ENSURE(cond)` — non-fatal, logs + continues (like `ensure()`)
- `UZ_CHECKF(cond, fmt, ...)` — with formatted message
- `UZ_VERIFY(cond)` — always evaluated (even in shipping), used for critical paths
- Integrates with our `UZ_LOG()` system and crash reporter
- No C++ exceptions — zero overhead, deterministic stack unwinding

### 7. Scene Graph / Transform Hierarchy

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | SceneComponent | `USceneComponent` attachment. `UpdateComponentToWorld()` propagation. Mobility flags (Static, Stationary, Movable). |
| **Godot** | Node tree | Parent-child `Node` relationships. Process modes (inherit, pausable, always, disabled). Transform propagation on change. |
| **Unity** | Transform | Every `GameObject` has `Transform`. Parent-child hierarchy. `localPosition`/`position` (world). Dirty flag system. |

**UzEngine choice: Transform hierarchy with dirty flags + mobility**
- Every entity with spatial presence has `TransformComponent`
- Parent-child relationships via entity handles (not pointers — ownership-safe)
- **Dirty flags** — only recalculate world transform when local changed or parent dirty
- **Mobility** (like Unreal): `Static` (baked, never updates), `Movable` (updated every frame)
- Batch update: traverse dirty transforms in parallel per-subtree

### 8. Occlusion Culling

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | GPU instance culling | `FInstanceCullingContext` with load balancer. Indirect draw args via RDG. Per-instance GPU culling. |
| **Godot** | HZB (CPU) | Hierarchical Z-Buffer with mipmap pyramid. Temporal jitter. 9-frame occlusion timeout. AABB projection + mip sampling. |
| **Unity** | Umbra (CPU) | Bake-time voxelize → cells + portals. Runtime camera queries. CPU-based. Best when GPU-bound. |

**UzEngine choice: Two-phase GPU-driven pipeline**
1. **HZB generation** (like Godot) — build depth mip pyramid from previous frame
2. **GPU instance culling** (like Unreal) — per-instance frustum + occlusion test on GPU, output indirect draw args
- Fully GPU-driven — zero CPU readback for culling decisions
- No bake step (unlike Unity's Umbra) — works with fully dynamic scenes
- Temporal reprojection for stability

### 9. Animation System

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Full pipeline | Blend trees, state machines, montages, control rig (procedural IK/constraints), additive layers, anim notify, root motion, retargeting. |
| **Godot** | AnimationMixer/Tree | `AnimationPlayer` (simple) + `AnimationTree` (state machine + blend tree). `BlendSpace1D`/`2D`. Process modes (physics/idle/manual). |
| **Unity** | Mecanim + Playables | `Animator`/`AnimatorController` (state machine, blend trees, layers). `Playables API` for runtime graph construction. Animation Rigging (procedural). Timeline for cinematics. |

**UzEngine choice: Hybrid Unreal/Godot approach**
- **AnimationPlayer** for simple playback (like Godot — lightweight)
- **AnimationTree** for complex blending (state machines, blend trees, blend spaces 1D/2D)
- **Control Rig** for procedural animation (IK, constraints, runtime corrections — like Unreal)
- **Montages** for one-shot overlay animations (attack combos, emotes — like Unreal)
- **Additive layers** for combining animations
- **Retargeting** via skeleton mapping
- All data in JSON — blend trees, state machines, control rig graphs are node-based JSON

### 10. Navigation / AI

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | NavMesh + BT + EQS | Recast/Detour navmesh. Behavior trees with decorators/services/tasks. EQS for spatial queries. AI perception (sight, hearing). |
| **Godot** | NavMap + RVO2 | `NavMap3D` with KdTree. `NavRegion3D`, `NavAgent3D`, `NavObstacle3D`, `NavLink3D`. RVO2 for crowd avoidance. Threaded queries. |
| **Unity** | NavMesh + ML-Agents | `NavMeshSurface`, `NavMeshAgent`, `NavMeshObstacle`. ML-Agents for reinforcement learning (PPO, SAC). |

**UzEngine choice: Recast/Detour + RVO2 + Behavior Trees**
- **Recast/Detour** for navmesh (used by both Unreal and Godot — industry standard)
- **RVO2** for crowd collision avoidance (like Godot)
- **Behavior Trees** with blackboard (like Unreal — tasks, decorators, services)
- **EQS-style queries** for spatial AI decisions ("find best cover point")
- **Perception system** (sight, hearing cones — like Unreal)
- NavRegion + NavAgent + NavObstacle + NavLink (like Godot's clean component model)
- Async navmesh generation via task graph

### 11. Camera / Post-Processing

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Post-process volumes | Priority-based blending, camera stack, effect ordering. `FPostProcessSettings` with per-effect weight. |
| **Godot** | Environment resource | `WorldEnvironment` node. `Environment` resource with fog, tonemap, SSAO, SSR, glow. |
| **Unity** | Volume system (HDRP/URP) | `Volume` components with priority + blend distance. `VolumeProfile` with overrides. Layer-based filtering. |

**UzEngine choice: Post-process volumes with priority blending (Unreal/Unity hybrid)**
- `PostProcessVolume` components with priority, blend distance, weight
- Camera stack for split-screen and picture-in-picture
- Effect ordering: TAA → SSAO → SSR → Bloom → DOF → Motion Blur → Color Grading → Tonemap → Upscaling (DLSS/FSR/XeSS) → UI
- Per-volume override system (only override specific effects, inherit rest)

### 12. Terrain

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Landscape + Nanite | Virtual texturing, world partition, Nanite LOD, foliage instancing, erosion tools. |
| **Godot** | Heightmap (plugin) | Basic heightmap, clipmap rendering. Community terrain plugin (Terrain3D). |
| **Unity** | Built-in terrain | Heightmap, terrain layers, tree painting (SpeedTree), detail/grass, sculpting brushes. |

**UzEngine choice: Heightmap + virtual texturing + GPU-driven foliage**
- Heightmap-based (industry standard across all 3 engines)
- **Virtual texturing** (like Unreal) — tile-based streaming, GPU feedback buffer, on-demand page loading
- **Clipmap LOD** for terrain mesh (like Godot — simpler than Nanite for terrain)
- **GPU instanced foliage** with per-instance culling
- Multi-layer material painting with blend weights
- Erosion/sculpting tools in editor (Phase 2)

### 13. Level Streaming

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | World Partition | Grid-based streaming, streaming volumes, async loading. Level instances. Data layers. |
| **Godot** | Background loading | `ResourceLoader` with async loading + cache modes (reuse/replace/ignore). Manual scene management. |
| **Unity** | Additive scenes | `LoadSceneAsync()` additive mode. `AsyncOperation` progress tracking. One operation advances per frame. |

**UzEngine choice: World partition (Unreal-style)**
- Grid-based spatial partitioning — only load cells near camera
- Async loading via task graph (not blocking game thread)
- Streaming volumes for manual control (trigger-based load/unload)
- Cache modes for loaded assets (reuse, replace, evict)
- Seamless transitions — no loading screens between adjacent cells

### 14. Serialization / Scene Format

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Binary + UObject property system | Custom binary serialization. Property system with reflection. Version migration functions. |
| **Godot** | PackedScene | `SceneState` with name/variant pooling. Binary encoding. `ResourceLoader` with format plugins. |
| **Unity** | YAML + binary | Scenes as YAML (text mode) or binary. AssetDatabase for editor. Addressables for runtime loading. |

**UzEngine choice: JSON (dev/editor) + binary (shipping)**
- **JSON** for all authored assets — human-readable, LLM-friendly, diffable in git
- **Binary** cooked format for shipping builds — name/variant pooling (like Godot) for compression
- Schema version field in every JSON file → automatic migration on load
- Asset references by relative path + content hash (detect broken refs)
- `UZ_SERIALIZE()` macro for component property registration (like Unreal's property system)

### 15. Plugin / Module System

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Module system | `FModuleManager::LoadModule()`. Modules declare dependencies. Primary/secondary game modules. `.uplugin` descriptor. |
| **Godot** | GDExtension | `ClassDB` registration. Extensions compiled as shared libs. Extension manifest. |
| **Unity** | Package Manager (UPM) | `manifest.json`. Assembly Definitions (`.asmdef`) for compilation units. Platform targeting per assembly. |

**UzEngine choice: Module registry with DLL loading (Unreal/Godot hybrid)**
- `UZ_REGISTER_MODULE(name, deps)` macro — declares module + dependencies
- Modules compiled as DLLs, loaded by `ModuleManager`
- Module manifest (JSON) declares name, version, dependencies, compatible engine version
- ClassDB-style component registration (like Godot) — modules register components at load time
- Hot-reload support — unload DLL → serialize state → reload → deserialize

### 16. Testing Strategy

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Automation framework | `FAutomationTestBase`. Screenshot comparison for render regression. Functional tests. |
| **Godot** | doctest | Unit tests via doctest. GDScript tests via `test/` directory. |
| **Unity** | Test Framework | `com.unity.test-framework`. Edit mode + Play mode tests. NUnit-based. |

**UzEngine choice: doctest (unit) + screenshot regression (render)**
- **doctest** for unit tests — header-only, fast compilation, C++17
- **Screenshot regression** — render test scenes via `--screenshot`, compare against golden images (pixel diff with threshold)
- **Integration tests** — automated scene load → simulate → validate state
- **CI pipeline** — GitHub Actions, build matrix (MSVC/Clang/Zig), run tests on every PR

### 17. Versioning / Asset Migration

| Engine | Approach | Details |
|--------|----------|---------|
| **Unreal** | Custom version numbers | Per-asset version, `FCustomVersionRegistration`. Migration functions per version bump. |
| **Godot** | Resource format version | Format version in resource files. `_set_bundled_resources` for compat. |
| **Unity** | ForceText + meta files | `.meta` sidecar files with GUIDs. YAML scenes version-tracked. Asset reimport on version mismatch. |

**UzEngine choice: Schema version + migration registry**
- Every `.uz*` JSON file has `"schema_version": N` field
- `UZ_REGISTER_MIGRATION(type, from_ver, to_ver, func)` — register migration function
- On load: if version < current → run migrations sequentially (v1→v2→v3→...→current)
- Auto-save migrated files (opt-in via project setting)
- Migration log for audit trail

---

## Pending Decisions

All major architectural decisions have been resolved through engine comparison analysis above.

| # | Decision | Status |
|---|----------|--------|
| — | *No pending decisions remaining* | — |

## Resolved Decisions

| # | Decision | Resolution |
|---|----------|------------|
| 1 | Graphics API | RHI multi-backend: DX12 SM6.6 primary, DX12 SM5 / DX11 SM5 / Vulkan / Metal fallbacks |
| 2 | Shader baseline | SM6.6 (develop first), SM5 fallback paths |
| 3 | Rendering reference | Unreal 5.6 techniques, clean-room reimplementation |
| 4 | Dev approach | Runtime-first: validate render before building editor/MCP |
| 5 | Observability | Built from day one: category-based logging, RAII scope profiling, GPU timestamp queries (triple-buffered), crash reports with GPU breadcrumbs |
| 6 | Extension ↔ Runtime protocol | Native N-API addon — runtime compiles as .node, loaded in-process by extension. Zero IPC overhead. |
| 7 | LLM visual feedback | Runtime `--screenshot` + `--headless` mode for vision-based development loop |
| 8 | Project config | `.uzproject` JSON — zero hardcoded values, cascading config (CLI > scene > project > engine defaults) |
| 9 | File formats | All authored data uses JSON with JSON Schema validation. Binary only for cooked/compiled assets. |
| 10 | Rendering pipeline | Deferred shading (forward pass for transparents) |
| 11 | Global illumination | Multi-tier: Lumen-style software RT → light probes → hardware DXR → path tracing |
| 12 | Scripting | C++ (primary, DLL hot-reload) + Lua (secondary, embedded VM) |
| 13 | Asset import | Source files (FBX, glTF) split into engine-native open formats (mesh, skeleton, animations, materials, textures) |
| 14 | Material system | Shader graphs (JSON node-based) + material instances (parameter overrides) |
| 15 | Hot reload | Godot-style: file watcher, atomic GPU swap, C++ DLL serialize/deserialize |
| 16 | UI system | HTML/CSS with reactive framework (Vue.js-style), data binding to C++ and Lua via events |
| 17 | Upscaling | DLSS (NVIDIA), FSR (AMD), Intel XeSS — all supported natively |
| 18 | Shader compilation | DXC (DirectX Shader Compiler) — offline and runtime |
| 19 | Build toolchain | Zig (primary, experimental), MSVC and Clang supported |
| 20 | Networking | Lightweight dedicated server with state sync (Unreal-style replication, optimized) |
| 21 | Localization | Unreal-style: string tables, text IDs, culture-aware formatting, plural rules |
| 22 | Memory model | No GC — Rust-inspired ownership/borrow checking in C++, deterministic, zero-cost in release |
| 23 | Platform services | Native SDK integration: Steam, Epic, Google, Sony, Microsoft — unified abstraction for login, achievements, payments, ads |
| 24 | VFX system | Niagara-style modular system, fully open JSON format, designed for 100% AI generation |
| 25 | AI content pipeline | Configurable multi-AI system: Nano Banana/FLUX/Scenario (textures), Hunyuan/Meshy/Tripo (3D), Suno/AIVA/Stable Audio (music), ElevenLabs (voice/SFX), DeepMotion (mocap), LLM (code/VFX/materials/scripts/UI/scenes) |
| 26 | ECS framework | entt — sparse-set archetype ECS, header-only, MIT, fastest in benchmarks |
| 27 | Math library | DirectXMath + thin wrapper (Vec3, Mat4, Quat, Transform) — SIMD native, DX12-first |
| 28 | Error handling | Unreal-style macros: UZ_CHECK (fatal), UZ_ENSURE (non-fatal), UZ_VERIFY (always-on), no exceptions |
| 29 | Audio | miniaudio (backend) + custom submix mixer (Main → SFX/Music/Voice/Reverb), separate audio thread |
| 30 | Physics | Jolt Physics — MIT, deterministic, faster than PhysX, validated by Godot |
| 31 | Threading | Task graph with named threads (Game, Render, RHI) + worker pool, 3 priority levels |
| 32 | Scene graph | Transform hierarchy with dirty flags + mobility (Static/Movable), batch parallel updates |
| 33 | Occlusion culling | Two-phase GPU-driven: HZB generation + GPU instance culling, zero CPU readback |
| 34 | Animation | Hybrid: AnimationPlayer (simple) + AnimationTree (blend/state machine) + Control Rig + Montages |
| 35 | Navigation/AI | Recast/Detour (navmesh) + RVO2 (crowds) + Behavior Trees + EQS + Perception system |
| 36 | Camera/Post-FX | Post-process volumes with priority blending, camera stack, ordered effect pipeline |
| 37 | Terrain | Heightmap + virtual texturing + clipmap LOD + GPU instanced foliage |
| 38 | Level streaming | World partition (grid-based), async loading via task graph, streaming volumes |
| 39 | Serialization | JSON (dev/editor) + binary (shipping), schema version + migration registry |
| 40 | Plugin system | Module registry with DLL loading, UZ_REGISTER_MODULE macro, ClassDB-style component registration |
| 41 | Testing | doctest (unit) + screenshot regression (render) + CI via GitHub Actions |
| 42 | Asset migration | Schema version per JSON file + UZ_REGISTER_MIGRATION for sequential upgrades |

---

## Changelog
- **2026-03-09**: Initial document. Defined 3 layers, RHI multi-backend strategy, SM6.6 primary target, phased development approach. Added observability, rendering, scripting, asset import, materials, hot reload, UI, upscaling, networking, build toolchain, localization, AAA features, VFX (Niagara-style), AI content pipeline, platform services, memory model (ownership/borrow), thirdparty strategy. Full engine comparison (Unreal 5.6 vs Godot 4.x vs Unity 6) resolving all pending decisions: entt ECS, DirectXMath, Jolt Physics, miniaudio, task graph threading, GPU occlusion, animation system, navigation/AI, post-process volumes, world partition, module system, testing, migration.
