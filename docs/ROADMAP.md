# UzEngine — Implementation Roadmap

## Vision

UzEngine is an AI-native 3D game engine that runs inside VSCode. Written in C++23 with DX12 rendering, LuaJIT scripting, and text-based asset formats (`.uzscene`, `.uzmat`, `.uzproj`), it is designed to be fully read and written by AI agents. Unlike traditional game engines, every file format is plain text, deterministic, and optimized for LLM generation — making the engine itself an interactive canvas for AI-driven game development.

## Current State (Phase 0 — Complete)

Phase 0 established the foundational rendering and communication pipeline:

- **DX12 Rotating Pyramid**: Renders a colored, rotating pyramid in real-time using DirectX 12
- **JPEG Streaming Pipeline**: Captures each frame as JPEG (via libjpeg-turbo), encodes at 60 FPS
- **WebSocket Server**: Streams JPEG frames to VSCode extension over WebSocket in real-time
- **IPC Protocol**: File-based command/response protocol for VSCode extension to control the engine
- **MCP Server**: Anthropic Model Context Protocol with 5 primitives:
  - `engine.ping` — Health check
  - `engine.status` — Frame rate, uptime, memory usage
  - `engine.screenshot` — Capture current frame as PNG/base64
  - `scene.setRotation(x, y, z)` — Control pyramid rotation
  - `scene.setBackground(r, g, b, a)` — Control background color
- **VSCode Extension**: Live viewport panel, server controls, real-time frame display

## How to Read This Roadmap

**Phases are sequential milestones.** Completing Phase N is a prerequisite for entering Phase N+1. Within a phase, many tasks can be parallelized using specialized agents or development teams.

**Each task links to its Rulebook specification** in `.rulebook/tasks/phase*/`. Every spec contains:
- `proposal.md` — High-level design and rationale
- `tasks.md` — Detailed implementation checklist with sub-tasks
- `.metadata.json` — Task status and metadata

**Complexity estimates** are based on scope and integration:
- **S** (1–3 days): Single file/class, clear spec, straightforward implementation
- **M** (1 week): 3–10 files, some design decisions, needs comprehensive tests
- **L** (2–3 weeks): New subsystem, complex interactions, significant integration testing
- **XL** (4+ weeks): Cross-cutting architecture, performance-critical, many integration points

**Dependencies** are listed as blockers — a task cannot start until its dependencies are complete.

---

## Phase Redistribution Plan (2026-03-03)

The original 5-phase structure has been redistributed into **10 phases + Tools track** based on dependency depth analysis. See full plan: [`docs/reviews/PHASE-REDISTRIBUTION-PLAN.md`](reviews/PHASE-REDISTRIBUTION-PLAN.md)

> **Note**: Task folder IDs (e.g. `phase2-pbr-renderer`) are NOT renamed — they are tracking labels only. New phase numbers are in PHASE-REDISTRIBUTION-PLAN.md.

| Phase | Name | Tasks | Goal | Est. Month |
|-------|------|-------|------|-----------|
| 1 | Engine Foundation | 18 | DX12 boots, scene loads, streams to VSCode | 1 |
| 2 | Editor Core | 20 | Full editor workflow, Lua scripting | 2 |
| 3 | Asset Pipeline | 18 | glTF/FBX/texture/audio import, .uzpak | 3–4 |
| 4 | Rendering Quality | 20 | PBR shadows, post-process, UI system | 5–6 |
| 5 | Physics & Gameplay Core | 18 | Jolt physics, audio, input, camera, save | 7–8 |
| 6 | Characters & Animation | 20 | Skeletal mesh, animation AAA suite, VFX | 9–10 |
| 7 | Advanced Rendering | 20 | TAA, SSR, volumetrics, GI, area lights | 11–13 |
| 8 | Gameplay Systems | 22 | Dialogue, vehicles, accessibility, full UI | 14–15 |
| 9 | World Systems | 20 | Terrain, NavMesh, WorldGen, build pipeline | 16–18 |
| 10 | AAA & Stable MVP | 18 | Nanite, DLSS/FSR, tools, CI hardening | 19–22 |
| Tools | Developer Tools | 10 | Parallel — unlock after Phase 2 | Any |
| **Total** | | **188** | **Stable MVP** | **~22 months** |

### Critical Dependency Chains (summary)
- **Foundation**: rhi-design → render-graph → pbr-renderer → bindless → gpu-driven → nanite
- **Scene**: scene-system → lua-scripting → timer-system → gameplay-ability-system
- **Animation**: gltf-import → skeletal-mesh → animation-system → ragdoll / motion-warping
- **Physics**: scene-system → event-bus → physics-system → vehicle / ragdoll / buoyancy
- **Rendering quality**: pbr-renderer → shadows → TAA → SSR → virtual-shadow-maps → DXR

---

## Phase 1: MVP Editor (Target: Functional Development Environment)

**Goal**: Developers can open a project, edit scenes visually in VSCode, attach Lua scripts to nodes, hot-reload scripts and shaders in ~60ms, run gameplay in a separate process, and see shader/script errors annotated in the editor.

**Phase 1 enables**:
- Visual scene editing in VSCode
- Live script/shader hot-reload
- Separate play mode process with seamless editor/play switching
- Foundation for all subsequent phases (all depend on Scene System)

**Exit Criteria**:
- `.uzscene` files open and edit visually in Scene Tree panel
- Lua scripts attach to nodes, execute at runtime, hot-reload in ~60ms
- Play mode spawns separate `render.exe --play` process
- Shader compilation errors annotated inline in VSCode with line numbers
- Unhandled exceptions captured in crash reporter with minidump
- Developers can author complete Lua-driven game logic

### Phase 1 Tasks (Dependency Order)

#### 1. Scene System
- **Rulebook Task**: [phase1-scene-system](../.rulebook/tasks/phase1-scene-system/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: None — foundational
- **Description**: Node tree data structure, `.uzscene` parser/serializer (INI-like format), SceneManager lifetime management, and IPC scene.* handlers for editor integration. All other systems build on scene structure.
- **Definition of Done**:
  - [x] Node class with transform (position, rotation, scale), parent-child hierarchy, component map
  - [x] `.uzscene` format parser and serializer with round-trip fidelity
  - [x] SceneManager load/unload, node creation/destruction, tree traversal APIs
  - [x] IPC handlers: scene.create, scene.delete, scene.setTransform, scene.getTree
  - [x] 95% unit test coverage; integration test with 10-node scene

---

#### 2. Debug & Profiling
- **Rulebook Task**: [phase1-debug-profiling](../.rulebook/tasks/phase1-debug-profiling/)
- **Complexity**: M (1 week)
- **Dependencies**: None — can run in parallel with Scene System
- **Description**: Logging infrastructure, frame timing, memory stats, Tracy CPU/GPU profiler integration, and ImGui debug overlay. IPC engine.status response includes FPS, uptime, memory usage. Enables profiling all subsequent systems.
- **Definition of Done**:
  - [x] Structured logging (level, tag, message) with file + console output
  - [x] Frame timer (delta time, frame rate, frame count)
  - [x] Memory statistics (arena usage, peak alloc, fragmentation)
  - [x] Tracy integration for CPU/GPU timeline capture
  - [x] ImGui overlay: F12 toggle, FPS counter, memory breakdown
  - [x] IPC engine.status with JSON metrics

---

#### 3. Crash Reporting
- **Rulebook Task**: [phase1-crash-reporting](../.rulebook/tasks/phase1-crash-reporting/)
- **Complexity**: M (1 week)
- **Dependencies**: None — early safety feature
- **Description**: Structured Exception Handler (SEH) integration, minidump generation, crash log in JSON format including stack trace, system info, and Lua state dump. VSCode extension notified on crash with one-click crash log view.
- **Definition of Done**:
  - [x] SEH handler catches unhandled exceptions
  - [x] Minidump written to `%APPDATA%/UzEngine/<project>/crashes/`
  - [x] Crash log: timestamp, exception code, stack trace, system info, Lua stack
  - [x] IPC handler: engine.get_crash_logs returns list of recent crashes
  - [x] VSCode extension displays "Engine crashed" banner with log viewer

---

#### 4. Console Commands
- **Rulebook Task**: [phase1-console-commands](../.rulebook/tasks/phase1-console-commands/)
- **Complexity**: M (1 week)
- **Dependencies**: Scene System (commands operate on scene)
- **Description**: In-game debug console (toggle with ~ key), command registry, execution in both C++ and Lua contexts. IPC console.execute handler for remote command execution from VSCode.
- **Definition of Done**:
  - [x] ConsoleCommandRegistry with typed command handlers
  - [x] Overlay console: ~ key toggle, autocomplete, history (up/down)
  - [x] Built-in commands: help, list_commands, find_node, set_transform
  - [x] Lua console: execute Lua expressions, print results
  - [x] IPC handler: console.execute(cmd_string) returns result/error

---

#### 5. Shader Hot-Reload
- **Rulebook Task**: [phase1-shader-hot-reload](../.rulebook/tasks/phase1-shader-hot-reload/)
- **Complexity**: M (1 week)
- **Dependencies**: None — can run in parallel with Scene System
- **Description**: DXC shader compiler integration, file watcher on `.hlsl` files, automatic recompilation, error/warning diagnostics reported to VSCode with file/line/column. Enables rapid shader iteration without restarting.
- **Definition of Done**:
  - [x] DXC (DirectX Shader Compiler) integration as vcpkg dependency
  - [x] Shader file watcher (debounced on save)
  - [x] Compilation error/warning parsing and JSON diagnostic output
  - [x] IPC handler: shader.compile(path) returns bytecode or error
  - [x] VSCode extension displays red squiggles on shader errors
  - [x] 50ms recompile + PSO rebuild

---

#### 6. Lua Scripting
- **Rulebook Task**: [phase1-lua-scripting](../.rulebook/tasks/phase1-lua-scripting/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: Scene System (scripts attach to nodes)
- **Description**: LuaJIT VM instance per scene, script_runner component, binding layer exposing engine APIs as `ctx.*` functions (ctx.scene, ctx.physics, ctx.input), sandbox to prevent OS access, hot-reload in ~60ms. Developers write game logic in Lua, no C++ required.
- **Definition of Done**:
  - [x] LuaJIT VM initialized per scene, managed by SceneManager
  - [x] script_runner component with OnInit/OnUpdate/OnLateUpdate lifecycle
  - [x] Lua bindings: ctx.scene.find_node, ctx.scene.get_transform, ctx.scene.set_transform
  - [x] Lua math library: ctx.math.vec3, ctx.math.quat, matrix operations
  - [x] Script hot-reload (file watch, VM reload in ~60ms, preserve state where possible)
  - [x] Sandbox: no os.*, no io.* (no file/network access)
  - [x] Error messages include file, line, function stack
  - [x] 95% unit test coverage; integration test with 5-node Lua scene

---

#### 7. Timer System
- **Rulebook Task**: [phase1-timer-system](../.rulebook/tasks/phase1-timer-system/)
- **Complexity**: S (1–3 days)
- **Dependencies**: Lua Scripting (timer API is Lua-first)
- **Description**: Lua-facing timer API: `ctx:schedule_after(delay, callback)`, `ctx:schedule_every(interval, callback)`, `ctx:cancel_timer(handle)`, `ctx:schedule_next_frame(callback)`. No C++ exposure needed — implemented as Lua bindings into TimerManager.
- **Definition of Done**:
  - [x] TimerManager tracks active timers, updates in OnUpdate
  - [x] Lua API: ctx:schedule_after, ctx:schedule_every, ctx:cancel_timer
  - [x] schedule_next_frame callback queued for next OnUpdate
  - [x] Timers survive hot-reload (stored separately from scripts)
  - [x] Unit tests: 10 timer scenarios

---

#### 8. Editor Panels
- **Rulebook Task**: [phase1-editor-panels](../.rulebook/tasks/phase1-editor-panels/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: Scene System (data source), Lua Scripting (script editor)
- **Description**: VSCode WebView panels for Scene Tree (node hierarchy, add/delete/reparent), Inspector (transform, components, script picker, script editor), and Asset Browser (file list, file operations). Bidirectional data binding with backend via IPC.
- **Definition of Done**:
  - [x] Scene Tree panel: hierarchical view, right-click add/delete, drag-to-reparent
  - [x] Inspector panel: transform input fields, component list, add component UI
  - [x] Script editor: embedded Lua editor (Monaco), save on Ctrl+S, compile status
  - [x] Asset Browser panel: folder view, asset icons, right-click preview/delete
  - [x] All panels update when scene changes (IPC subscription)
  - [x] 5000+ lines TypeScript, 95% test coverage

---

#### 9. Play Mode
- **Rulebook Task**: [phase1-play-mode](../.rulebook/tasks/phase1-play-mode/)
- **Complexity**: M (1 week)
- **Dependencies**: Scene System, Editor Panels
- **Description**: CLI argument `--editor` (viewport in VSCode) vs. `--play` (separate game window). Play process spawned by editor, receives scene over IPC, runs independently. Editor shows "Play" button in status bar; click stops process.
- **Definition of Done**:
  - [x] Editor-mode engine runs at 60 FPS, streams viewport to VSCode
  - [x] Play-mode spawns separate `render.exe --play` process
  - [x] Scene loaded in play process, all systems active
  - [x] Play process window shows game viewport (full control/input)
  - [x] Status bar: Play/Stop buttons, FPS counter
  - [x] Pressing Stop kills play process, returns to editor

---

#### 10. Asset Browser
- **Rulebook Task**: [phase1-asset-browser](../.rulebook/tasks/phase1-asset-browser/)
- **Complexity**: M (1 week)
- **Dependencies**: Editor Panels
- **Description**: File browser in Asset Browser panel showing project assets. Double-click to preview (scenes, textures, models). Right-click context menu for rename/delete/import. `.import.json` sidecar files show import settings (e.g., texture compression, model LOD settings).
- **Definition of Done**:
  - [x] Asset Browser panel shows folder hierarchy
  - [x] Icons for scene, texture, mesh, material, shader files
  - [x] Double-click preview: scenes (open), textures (show), meshes (3D viewer)
  - [x] Right-click: rename, delete, duplicate, show in explorer
  - [x] `.import.json` sidecar display and edit
  - [x] Drag-and-drop files from Windows Explorer to project folder

---

#### 11. Undo/Redo
- **Rulebook Task**: [phase1-undo-redo](../.rulebook/tasks/phase1-undo-redo/)
- **Complexity**: M (1 week)
- **Dependencies**: Editor Panels
- **Description**: EditorCommand pattern, UndoStack with undo/redo. All editor operations (create node, set transform, etc.) are commands. Ctrl+Z/Ctrl+Shift+Z hotkeys. Dirty flag and "Save before close?" prompt.
- **Definition of Done**:
  - [x] EditorCommand interface with Execute/Undo virtual methods
  - [x] UndoStack (max 100 commands, configurable)
  - [x] Ctrl+Z / Ctrl+Shift+Z hotkeys
  - [x] Scene tree insert/delete/reparent commands
  - [x] Transform property edit commands (debounced while dragging)
  - [x] Dirty flag and file modified indicator in title

---

#### 12. Editor Gizmos
- **Rulebook Task**: [phase1-editor-gizmos](../.rulebook/tasks/phase1-editor-gizmos/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: Editor Panels, Scene System
- **Description**: 3D translate/rotate/scale handles in viewport. Click and drag to transform selected node. Ray-cast hit test to detect handle interaction. World/local space toggle. Grid snap. Gizmo rendered in separate DX12 pass over scene.
- **Definition of Done**:
  - [x] Translate handle: 3 color-coded axes (X=red, Y=green, Z=blue) + center square
  - [x] Rotate handle: 3 color-coded rotation rings
  - [x] Scale handle: 3 axes + center cube
  - [x] Ray-cast from mouse to 3D space, hit test on handles
  - [x] Drag detection and real-time transform update
  - [x] World/Local space toggle (W key)
  - [x] Grid snap (hold Ctrl while dragging)
  - [x] Gizmo overlay rendered in separate DX12 pass

---

#### 13. Multi-Select
- **Rulebook Task**: [phase1-multi-select](../.rulebook/tasks/phase1-multi-select/)
- **Complexity**: M (1 week)
- **Dependencies**: Editor Gizmos
- **Description**: SelectionManager tracking multiple selected nodes. Ctrl+click to add/remove from selection. Box select with mouse drag. Inspector shows shared properties (transform, tags). Gizmo operates on all selected nodes simultaneously.
- **Definition of Done**:
  - [x] SelectionManager: add_node, remove_node, clear, is_selected API
  - [x] Ctrl+click to add/remove nodes from selection
  - [x] Mouse drag creates selection box, all nodes inside selected
  - [x] Inspector shows common transform (grayed if different)
  - [x] Gizmo moves/rotates/scales all selected nodes
  - [x] Bulk delete (Delete key), duplicate (Ctrl+D)

---

### Phase 1 Milestone Diagram

```
                  [Scene System] ────────────┬─→ [Console Cmds]
                        │                     │
                        ├──→ [Lua Scripting] ─┴──→ [Timer System]
                        │
                        ├──→ [Play Mode]
                        │
                        └──→ [Editor Panels] ──┬─→ [Asset Browser]
                                                ├─→ [Undo/Redo]
                                                └─→ [Editor Gizmos] ──→ [Multi-Select]

[Shader Hot-Reload] ────────────────────────────────────── (parallel, no deps)
[Debug & Profiling] ────────────────────────────────────── (parallel, no deps)
[Crash Reporting]   ────────────────────────────────────── (parallel, no deps)
```

**Critical path**: Scene System → Editor Panels → Editor Gizmos → Multi-Select (8 weeks minimum).

---

## Phase 2: Rendering & Asset Foundation

**Goal**: Production-quality visual rendering with PBR materials, complete asset import pipeline (glTF, FBX), real-time shadows, UI rendering, and AI-native generation tools.

**Phase 2 enables**:
- Visually polished 3D scenes
- Professional asset workflows
- Gameplay with physical materials
- Game UI rendering for HUDs and menus
- AI agents generating scenes and shaders

**Exit Criteria**:
- PBR materials with GGX BRDF, IBL, and tone-mapping
- glTF and FBX import with automatic LOD generation
- Cascaded shadow maps (CSM) with 3 cascades and PCF
- Real-time SSAO and Screen-Space Reflections
- UI system for game HUDs (HTML/CSS-like authoring)
- Audio playback (XAudio2, spatial 3D)
- MCP tools for AI scene generation, shader co-authoring, and asset analysis

### Phase 2 Tasks (Dependency Order)

#### 1. PBR Renderer
- **Rulebook Task**: [phase2-pbr-renderer](../.rulebook/tasks/phase2-pbr-renderer/)
- **Complexity**: XL (4+ weeks)
- **Dependencies**: None — foundational rendering upgrade
- **Description**: Deferred rendering pipeline with G-Buffer (albedo, normal, roughness, metallic, emission), GGX BRDF, image-based lighting (IBL), and post-process stack. Replaces Phase 0 forward pipeline. Mandatory prerequisite for all Phase 2 rendering tasks.
- **Definition of Done**:
  - [x] G-Buffer: 4 render targets (RGBA16F × 3, R8 depth + stencil)
  - [x] GGX BRDF implementation (split-sum approximation)
  - [x] IBL with precomputed cubemap (radiance + irradiance)
  - [x] Deferred lighting pass: directional + point lights
  - [x] Post-process stack: tone-mapping, bloom, gamma correction
  - [x] Performance: 60 FPS at 1080p with 100+ lights

---

#### 2. Shader Library
- **Rulebook Task**: [phase2-shader-library](../.rulebook/tasks/phase2-shader-library/)
- **Complexity**: M (1 week)
- **Dependencies**: PBR Renderer
- **Description**: Shared HLSL include library (`.hlsli` files): BRDF functions, lighting math, shadow sampling, skinning math, noise functions, global constant buffer. Single source of truth for all shaders.
- **Definition of Done**:
  - [x] `shader_lib/brdf.hlsli` — GGX, Schlick-F0, geometry functions
  - [x] `shader_lib/lighting.hlsli` — directional/point/spot light evaluation
  - [x] `shader_lib/shadows.hlsli` — PCF, cascade selection, fade
  - [x] `shader_lib/skinning.hlsli` — blend shape, bone transforms
  - [x] `shader_lib/noise.hlsli` — Perlin, Worley, hash functions
  - [x] Global constant buffer: view, projection, light list, time

---

#### 3. Shader Variants
- **Rulebook Task**: [phase2-shader-variants](../.rulebook/tasks/phase2-shader-variants/)
- **Complexity**: M (1 week)
- **Dependencies**: PBR Renderer, Shader Library
- **Description**: Shader permutation system with variant keys (e.g., `USE_PARALLAX_MAP`, `USE_NORMAL_MAP`). Auto-detect variants from material properties. Offline PSO cache. Variant compilation commands in VSCode.
- **Definition of Done**:
  - [x] Variant system: shader + key set → PSO
  - [x] Auto-detection from material (scan textures and flags)
  - [x] PSO cache file (binary, versioned)
  - [x] Offline compiler: `uzengine --compile-shaders`
  - [x] VSCode command: "Compile Shader Variants"

---

#### 4. Shadow Maps
- **Rulebook Task**: [phase2-point-spot-shadows](../.rulebook/tasks/phase2-point-spot-shadows/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: PBR Renderer
- **Description**: Cascaded Shadow Maps (CSM) for directional lights (3 cascades), dual-paraboloid shadows for point lights, perspective shadows for spot lights. PCF 2×2 filtering, shadow atlases to reduce bind count.
- **Definition of Done**:
  - [x] Directional light CSM: 3 cascades, optimal cascade splits
  - [x] Point light shadows: dual-paraboloid map
  - [x] Spot light shadows: perspective projection
  - [x] Shadow atlases (2K×2K, multiple lights per atlas)
  - [x] PCF 2×2 filtering in light pass
  - [x] Cascade fade to reduce banding

---

#### 5. GLTF Import
- **Rulebook Task**: [phase2-gltf-import](../.rulebook/tasks/phase2-gltf-import/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: PBR Renderer
- **Description**: fastgltf library integration, mesh optimization with meshoptimizer (LOD generation), GPU buffer upload. Import dialog in Asset Browser shows material preview. Generates `.uzmesh` (binary mesh + metadata).
- **Definition of Done**:
  - [x] fastgltf parser (C++17, handles .glb/.gltf + embedded images)
  - [x] meshoptimizer LOD generation (3 levels by default)
  - [x] Material conversion: glTF PBR → engine material
  - [x] GPU vertex/index buffer upload
  - [x] `.uzmesh` format: mesh data + material slots + metadata
  - [x] Import dialog with preview window

---

#### 6. FBX Import
- **Rulebook Task**: [phase2-fbx-import](../.rulebook/tasks/phase2-fbx-import/)
- **Complexity**: M (1 week)
- **Dependencies**: GLTF Import (follows same pipeline)
- **Description**: ufbx library (lightweight FBX parser), mesh import with animation metadata extraction. Skinning info preserved for Phase 3. Preview panel in Asset Browser.
- **Definition of Done**:
  - [x] ufbx integration (C library, handles .fbx format)
  - [x] Mesh import with hierarchy preservation
  - [x] Animation clip metadata (duration, frame rate, curve data)
  - [x] Skin/skeleton extraction (joint names, weights)
  - [x] Material conversion (Phong → PBR approximation)
  - [x] FBX preview in Asset Browser

---

#### 7. Texture Tools
- **Rulebook Task**: [phase2-texture-tools](../.rulebook/tasks/phase2-texture-tools/)
- **Complexity**: M (1 week)
- **Dependencies**: PBR Renderer
- **Description**: DirectXTex integration for texture import/compression (BC7, BC5, BC6H), mipmap generation, normal map detection (blue channel compression). Texture preview panel with format/size display.
- **Definition of Done**:
  - [x] DirectXTex: PNG/JPG/TGA/DDS input
  - [x] BC7 compression (RGB), BC5 (normal maps), BC6H (HDR)
  - [x] Mipmap generation (box filter, 1 level/8 pixels)
  - [x] Normal map auto-detection and BC5 compression
  - [x] Texture preview: display compressed size, format, dimensions
  - [x] Import settings dialog: compression level, mipmap count

---

#### 8. Asset PAK
- **Rulebook Task**: [phase2-asset-pak](../.rulebook/tasks/phase2-asset-pak/)
- **Complexity**: M (1 week)
- **Dependencies**: Texture Tools, GLTF Import, FBX Import
- **Description**: `.uzpak` container format (zstd-compressed archive), virtual file system for runtime asset streaming. CLI packer tool. Shipping builds use PAK instead of loose assets.
- **Definition of Done**:
  - [x] `.uzpak` format: zstd-compressed, file index, asset metadata
  - [x] PAKReader: virtual filesystem, stream-on-demand
  - [x] Packer CLI: `uzengine --pack-assets project/`
  - [x] Runtime loading: AssetManager loads from PAK or loose
  - [x] Incremental packing (only changed files)

---

#### 9. GPU Instancing
- **Rulebook Task**: [phase2-gpu-instancing](../.rulebook/tasks/phase2-gpu-instancing/)
- **Complexity**: M (1 week)
- **Dependencies**: GLTF Import, PBR Renderer
- **Description**: Dynamic GPU instancing for batching identical meshes. BatchBuilder collects instances with different transforms into single draw call. StructuredBuffer for per-instance data (transform, color). SV_InstanceID in shader.
- **Definition of Done**:
  - [x] BatchBuilder: instance collection, transform StructuredBuffer upload
  - [x] Shader integration: SV_InstanceID indexing
  - [x] Visibility culling: per-instance frustum/occlusion test
  - [x] Dynamic update: add/remove instances at runtime
  - [x] Performance: 1000+ identical meshes with single draw call

---

#### 10. SSAO (Screen-Space Ambient Occlusion)
- **Rulebook Task**: [phase2-ssao](../.rulebook/tasks/phase2-ssao/)
- **Complexity**: M (1 week)
- **Dependencies**: PBR Renderer
- **Description**: HBAO+ algorithm in compute shader, bilateral blur for temporal smoothing, quality levels (low/medium/high), integration into deferred lighting pass.
- **Definition of Done**:
  - [x] HBAO+ compute shader (horizon-based AO)
  - [x] Bilateral blur (2 passes, edge-aware)
  - [x] Quality levels: sample count 8/16/32
  - [x] Integration: modulate ambient term in light pass
  - [x] Performance: 2ms at 1080p (high quality)

---

#### 11. UI System
- **Rulebook Task**: [phase2-ui-system](../.rulebook/tasks/phase2-ui-system/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: PBR Renderer, Shader Variants
- **Description**: Canvas-based UI authoring (HTML/CSS-like). UIRoot scene node, UI shader with text rendering (SDF fonts), data binding with CSS custom properties. Layout system (flexbox-inspired). Event callbacks (onClick, onHover).
- **Definition of Done**:
  - [x] UIRoot component: hierarchy of UI elements (panel, button, text, image)
  - [x] Layout: position, size, anchors, margin/padding
  - [x] Styling: color, font, border, shadow (via CSS custom properties)
  - [x] Text rendering: SDF font rasterization, multi-language support
  - [x] Event system: onClick, onHover callbacks (Lua or C++)
  - [x] Data binding: {{variable}} syntax, auto-update on value change

---

#### 12. Audio System
- **Rulebook Task**: [phase2-audio-system](../.rulebook/tasks/phase2-audio-system/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: Scene System
- **Description**: XAudio2 audio engine, 4-bus mixing (master, SFX, music, UI), spatial 3D audio (cone, rolloff, Doppler), WAV/MP3/OGG decoding, hot-reload, Lua/C++ playback API.
- **Definition of Done**:
  - [x] XAudio2 initialization, voice creation, submix chains
  - [x] 4-bus mixer: master, sfx, music, ui (each with volume + effects)
  - [x] AudioSource component: 2D/3D, looping, priority
  - [x] Spatial audio: position, velocity, cone, rolloff
  - [x] Codec support: WAV (native), MP3 (ffmpeg), OGG (vorbis)
  - [x] Lua API: ctx.audio:play(path, volume, loop)

---

#### 13. AI-Native Features
- **Rulebook Task**: [phase2-ai-native-features](../.rulebook/tasks/phase2-ai-native-features/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: All Phase 2 rendering systems (to provide AI context)
- **Description**: MCP tools for AI agents: `scene_generate` (prompt → scene JSON), `shader_assist` (shader code → optimized HLSL), `script_generate` (behavior description → Lua script), `scene_analyze` (screenshot/scene JSON → structured analysis). Enables AI co-authoring.
- **Definition of Done**:
  - [x] `scene_generate`: prompt → `.uzscene` JSON, validate schema
  - [x] `shader_assist`: HLSL code review, optimization suggestions, inline docs
  - [x] `script_generate`: behavior description → Lua script with bindings
  - [x] `scene_analyze`: analyze uploaded screenshot, suggest improvements
  - [x] `ai.context`: provide engine capabilities/API reference to AI
  - [x] All tools documented in VSCode MCP sidebar

---

#### 14. Prefab Edit Mode
- **Rulebook Task**: [phase2-prefab-edit-mode](../.rulebook/tasks/phase2-prefab-edit-mode/)
- **Complexity**: M (1 week)
- **Dependencies**: Editor Panels, Undo/Redo
- **Description**: Open prefab asset in isolation (separate viewport), edit structure/scripts, save changes propagate to all instances. Override panel shows which properties differ from prefab. Prefab inheritance shown in Scene Tree (icon indicator).
- **Definition of Done**:
  - [x] Double-click prefab asset opens prefab editor
  - [x] Prefab isolation: only this prefab editable
  - [x] Override panel: list of instance overrides, right-click apply/discard
  - [x] Instance-level property override (transform, material, etc.)
  - [x] Save propagates to all instances (with override preservation)
  - [x] Prefab inheritance icon in Scene Tree

---

### Phase 2 Milestone Diagram

```
[PBR Renderer] ──┬──→ [Shader Library] ──→ [Shader Variants]
                 ├──→ [Shadow Maps]
                 ├──→ [SSAO]
                 └──→ [UI System]

[GLTF Import] ──┬──→ [GPU Instancing]
[FBX Import]  ──┤
[Texture Tools]─┴──→ [Asset PAK]

[Audio System] ──────────────────────────────────────── (parallel)

[AI-Native Features] (depends on all rendering systems)
[Prefab Edit Mode] (depends on Editor Panels)
```

**Critical path**: PBR Renderer → Shader Library → Shader Variants (3 weeks minimum).

---

## Phase 3: Game Systems

**Goal**: Full gameplay capability with physics, animation, skeletal meshes, input, camera controls, particles, save/load, and localization.

**Phase 3 enables**:
- Character animation and physics-based movement
- Complex interactive scenes with rigid bodies and triggers
- Input handling with customizable key bindings
- Gameplay state persistence across sessions
- Multi-language game content

**Exit Criteria**:
- Skeletal mesh rendering with GPU skinning and morph targets
- Animation state machine with blending and notifies
- Jolt Physics integration with character controller
- Enhanced Input system (InputAction + MappingContext)
- Camera system with SpringArm and multiple modes
- Particle effects with GPU simulation
- Save/Load system with slots and autosave
- Localization with CSV-based translations

### Phase 3 Tasks (Dependency Order)

#### 1. Skeletal Mesh
- **Rulebook Task**: [phase3-skeletal-mesh](../.rulebook/tasks/phase3-skeletal-mesh/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: GLTF Import (bone/weight import)
- **Description**: GPU skinning with bone matrices, morph target blending (blend shapes), physics asset integration (bone collision shapes). Skeleton component holds joint hierarchy and transforms.
- **Definition of Done**:
  - [x] Skeleton component: joint tree, transform buffers
  - [x] GPU skinning: bone matrix StructuredBuffer, skinning shader pass
  - [x] Morph targets: blend shape weights per mesh, GPU interpolation
  - [x] Physics asset: capsule per bone, ragdoll initialization
  - [x] Animation playback integration (Phase 3)

---

#### 2. Animation System
- **Rulebook Task**: [phase3-animation-system](../.rulebook/tasks/phase3-animation-system/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: Skeletal Mesh, Event Bus
- **Description**: AnimClip asset (keyframes, curves), AnimStateMachine for state-based playback, blending (1D/2D), AnimNotify events (footstep, attack, etc.), IK solver, root motion extraction.
- **Definition of Done**:
  - [x] AnimClip: keyframe animation data, sample(time) API
  - [x] AnimStateMachine: states, transitions, blend graph
  - [x] Layered animation (base layer + additive layers)
  - [x] AnimNotify: event system integrated with Event Bus
  - [x] IK: two-bone IK solver for limbs
  - [x] Root motion: extract motion from animation, apply to node

---

#### 3. Ragdoll
- **Rulebook Task**: [phase3-ragdoll](../.rulebook/tasks/phase3-ragdoll/)
- **Complexity**: M (1 week)
- **Dependencies**: Skeletal Mesh, Physics System
- **Description**: Active ragdoll with Jolt motor constraints, blend between animation and ragdoll, impact response. Toggle ragdoll mode at runtime.
- **Definition of Done**:
  - [x] Ragdoll initialization from skeleton
  - [x] Jolt motor constraints for active ragdoll
  - [x] Blend between animation (enabled) and ragdoll (disabled)
  - [x] Impact response: character falls to ragdoll on damage
  - [x] Recovery: ragdoll → animation blending

---

#### 4. Input System
- **Rulebook Task**: [phase3-input-system](../.rulebook/tasks/phase3-input-system/)
- **Complexity**: M (1 week)
- **Dependencies**: Lua Scripting, Event Bus
- **Description**: Enhanced Input system with InputAction (semantic input like "Jump") and MappingContext (key binding layer). XInput support. Runtime rebinding. Callbacks in Lua/C++.
- **Definition of Done**:
  - [x] InputAction: value (axis 1D/2D), triggered (button)
  - [x] MappingContext: bindings (keyboard, mouse, gamepad)
  - [x] Input state: current and previous frame (for edge detection)
  - [x] XInput polling: analog triggers/sticks, button states
  - [x] Lua API: ctx.input:is_pressed(action), ctx.input:get_axis(action)
  - [x] Runtime rebind: change key bindings in-game

---

#### 5. Camera System
- **Rulebook Task**: [phase3-camera-system](../.rulebook/tasks/phase3-camera-system/)
- **Complexity**: M (1 week)
- **Dependencies**: Input System
- **Description**: Camera component with transform control, SpringArm for third-person, multiple camera modes (orbiting, first-person, top-down), screen shake, depth-of-field, `.uzcam` preset save/load.
- **Definition of Done**:
  - [x] Camera component: view/projection matrices, viewport configuration
  - [x] SpringArm: distance, angle, lag, collision detection
  - [x] Camera modes: first-person, third-person, orbiting, top-down
  - [x] Screen shake: amplitude, duration, decay
  - [x] Depth-of-field: focal distance, f-stop, bloom integration
  - [x] `.uzcam` preset: save/load camera settings

---

#### 6. Physics System
- **Rulebook Task**: [phase3-physics-system](../.rulebook/tasks/phase3-physics-system/)
- **Complexity**: XL (4+ weeks)
- **Dependencies**: Scene System, Event Bus
- **Description**: Jolt Physics integration, rigid body component (dynamic/static/kinematic), collider shapes (box, sphere, capsule, mesh), triggers, character controller, raycasting, gravity and damping, collision events.
- **Definition of Done**:
  - [x] Jolt integration: world simulation, body/shape creation
  - [x] Collider shapes: box, sphere, capsule, convex mesh, terrain
  - [x] Rigidbody component: mass, drag, constraints
  - [x] Character controller: movement, gravity, ground detection
  - [x] Triggers: OnTriggerEnter/Exit events (Lua callbacks)
  - [x] Raycasting: query, debug visualization
  - [x] Lua API: ctx.physics:raycast, ctx.physics:get_rigidbody
  - [x] Performance: stable 60 FPS with 1000+ bodies

---

#### 7. Particle System
- **Rulebook Task**: [phase3-particle-system](../.rulebook/tasks/phase3-particle-system/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: GPU Instancing, Event Bus
- **Description**: GPU compute particle simulation with behavior modules (gravity, drag, collision). `.uzfx` format for effect definition. Billboard rendering with LOD. AnimNotify integration (particles spawned on animation events).
- **Definition of Done**:
  - [x] ParticleEmitter component: spawn rate, lifetime, velocity
  - [x] Behavior modules: gravity, drag, collision, vortex, attractors
  - [x] Particle lifecycle: init, update, death with data preservation
  - [x] Billboard rendering: size/rotation/color per particle
  - [x] `.uzfx` format: behavior graph, material, spawn rate curve
  - [x] LOD system: particle count scales with distance
  - [x] AnimNotify callback: spawn particles on keyframe

---

#### 8. Event Bus
- **Rulebook Task**: [phase3-event-bus](../.rulebook/tasks/phase3-event-bus/)
- **Complexity**: S (1–3 days)
- **Dependencies**: Lua Scripting
- **Description**: Type-safe C++ EventBus<T> template, Lua string-dispatch signals. Engine emits predefined events: OnSceneLoad, OnNodeDestroy, OnPhysicsCollision, OnAnimNotify, etc.
- **Definition of Done**:
  - [x] EventBus<T>: Subscribe, Publish, Unsubscribe
  - [x] Lua signal dispatch: ctx.signal:connect("physics.collision", callback)
  - [x] Predefined events: scene, physics, animation, input
  - [x] Event data structure and payload
  - [x] Subscription deregistration (weak references)

---

#### 9. Tags & Groups
- **Rulebook Task**: [phase3-tags-groups](../.rulebook/tasks/phase3-tags-groups/)
- **Complexity**: S (1–3 days)
- **Dependencies**: Scene System
- **Description**: Node tags (bitset<64> for up to 64 tags), CTags ECS component, find_by_tag queries. Used for gameplay logic (enemies, triggers, collectable items). Lua API and IPC handlers.
- **Definition of Done**:
  - [x] CTags component: tag bitset, assign/unassign API
  - [x] Tag definitions: max 64, configure in project
  - [x] Query: find_nodes_with_tag(tag) returns node list
  - [x] Lua API: ctx.scene:find_by_tag("enemy") → array
  - [x] IPC: scene.find_by_tag(tag) → [nodeId, ...]

---

#### 10. Save System
- **Rulebook Task**: [phase3-save-system](../.rulebook/tasks/phase3-save-system/)
- **Complexity**: M (1 week)
- **Dependencies**: Scene System, Lua Scripting
- **Description**: Save/load game state with msgpack serialization. Save slots 0–9, autosave slot (-1 with rotating buffer). Thumbnail capture (320×180 JPEG). Node transform, component data, Lua table persistence. Storage in `%APPDATA%/UzEngine/<project>/saves/`.
- **Definition of Done**:
  - [x] SaveManager: write_slot, read_slot, delete_slot API
  - [x] SaveWriter/SaveReader: lambda-based serialization
  - [x] Msgpack format: compact binary, supports all C++ types
  - [x] Thumbnail: JPEG capture at save time
  - [x] Autosave: rotating buffer on scene transitions
  - [x] Lua API: ctx.saves:write_slot(0, {player_health=100})
  - [x] Graceful fallback: missing keys use defaults

---

#### 11. Localization
- **Rulebook Task**: [phase3-localization](../.rulebook/tasks/phase3-localization/)
- **Complexity**: M (1 week)
- **Dependencies**: Scene System, Lua Scripting
- **Description**: CSV-based internationalization (human-readable, AI-friendly). Configurable locales in `.uzproj` (en, pt_br, es, fr, de, ja). Runtime language switching. Font fallback per locale (CJK fonts). CSV hot-reload in editor. 3-tier fallback: current locale → English → key itself.
- **Definition of Done**:
  - [x] LocaleManager: set_locale, get_text, get_font API
  - [x] CSV parser: UTF-8 with CJK support, escaping, comments, BOM
  - [x] CSV format: key, en, pt_br, es, ... (extensible)
  - [x] Hot-reload: file watcher, re-parse on save
  - [x] Font fallback: per-locale font override in config
  - [x] Lua API: ctx.locale:get_text("ui.menu.play")
  - [x] 3-tier fallback: never crashes on missing key
  - [x] Performance: <1ms key lookup

---

#### 12. SSR (Screen-Space Reflections)
- **Rulebook Task**: [phase3-ssr](../.rulebook/tasks/phase3-ssr/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: PBR Renderer, Shader Library
- **Description**: Hi-Z ray marching SSR, temporal reprojection for smoothness, IBL fallback for roughness. Post-process pass. Integrated into deferred lighting pipeline.
- **Definition of Done**:
  - [x] Hi-Z pyramid: depth downsampling
  - [x] Ray march: start from pixel, trace in view space
  - [x] Temporal reprojection: reuse from previous frame
  - [x] IBL fallback: blend with environment for rough surfaces
  - [x] Integration: modulate specular term in light pass
  - [x] Performance: 3ms at 1080p

---

#### 13. TAA (Temporal Anti-Aliasing)
- **Rulebook Task**: [phase3-taa](../.rulebook/tasks/phase3-taa/)
- **Complexity**: M (1 week)
- **Dependencies**: PBR Renderer
- **Description**: Temporal anti-aliasing with Halton jitter pattern, velocity G-Buffer channel, variance clamping for ghosting prevention, sharpening post-process.
- **Definition of Done**:
  - [x] Halton sequence jitter: 16 samples over frames
  - [x] Velocity pass: output screen-space motion to G-Buffer
  - [x] Temporal resolve: blend current + reprojected previous
  - [x] Variance clamping: prevent ghosting on fast motion
  - [x] Sharpening: subtle FXAA-style unsharp mask
  - [x] Performance: <1ms overhead vs. MSAA

---

### Phase 3 Milestone Diagram

```
[Skeletal Mesh] ──→ [Animation System] ──→ [Ragdoll]
                                         (Physics System deps)

[Physics System] ──────────────────────┬─→ [Ragdoll]
                                        └─→ [Input System]

[Input System] ──→ [Camera System]

[Event Bus] ──────→ (used by all physics/animation/input)
[Tags & Groups] ──→ (queries for gameplay)
[Save System] ────→ (depends on Scene)
[Localization] ───→ (depends on Scene)

[Particle System] (depends on GPU Instancing)
[SSR] ─────────→ (screen-space rendering)
[TAA] ─────────→ (temporal filter)
```

---

## Phase 4: Advanced Features

**Goal**: Shipping capability, extensibility, large world support, and advanced gameplay systems.

**Exit Criteria**:
- Complete build pipeline (cook → pack → compile → stage → package)
- Windows NSIS installer with auto-update
- NavMesh with pathfinding and local avoidance
- Plugin system with stable C ABI
- Terrain system with heightmap, splatmap, LOD, and procedural foliage

### Phase 4 Tasks

#### 1. Build Pipeline
- **Complexity**: L (2–3 weeks)
- **Dependencies**: All Phase 3 (complete game systems)
- **Description**: Cook (scene/asset processing), Pack (`.uzpak` creation), Compile (DXC shaders), Stage (output directory), Package (NSIS installer). Shipping builds optimize for size/performance.

#### 2. NavMesh
- **Rulebook Task**: [phase4-navmesh](../.rulebook/tasks/phase4-navmesh/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: Physics System, Terrain System (for baking)
- **Description**: Recast + Detour navmesh baking, NavMeshAgent component, RVO2 local avoidance, dynamic obstacles. Pathfinding queries return waypoint paths.

#### 3. Plugin System
- **Rulebook Task**: [phase4-plugin-system](../.rulebook/tasks/phase4-plugin-system/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: All Phase 3 (stable C ABI)
- **Description**: `.uzplugin` manifest (C++ plugin interface), stable C ABI for cross-version compatibility, VSCode extension plugins, DLL hot-reload.

#### 4. Terrain System
- **Rulebook Task**: [phase4-terrain-system](../.rulebook/tasks/phase4-terrain-system/)
- **Complexity**: XL (4+ weeks)
- **Dependencies**: Physics System, GPU Instancing
- **Description**: `.uzterrain` asset with heightmap, splatmap (multi-layer texture blending), chunk-based LOD, procedural foliage GPU instancing, water plane, physics collision.

#### 5. DXR Ray Tracing RHI Extension
- **Rulebook Task**: [phase4-dxr-ray-tracing-rhi](../.rulebook/tasks/phase4-dxr-ray-tracing-rhi/)
- **Complexity**: XL (4+ weeks)
- **Dependencies**: Bindless Resources, Render Graph
- **Description**: Extends RHI with DXR acceleration structures (BLAS + TLAS), ray tracing pipeline (RGS + Miss + HitGroup shaders), DX12 `DispatchRays()`. Enables raytraced reflections, shadows, AO, and GI as optional passes.

#### 6. GPU-Driven Rendering
- **Rulebook Task**: [phase4-gpu-driven-rendering](../.rulebook/tasks/phase4-gpu-driven-rendering/)
- **Complexity**: XL (4+ weeks)
- **Dependencies**: Bindless Resources, HZB Occlusion, Job System
- **Description**: Per-frame InstanceDataBuffer with all entity transforms, GPU occlusion cull compute shader writing to DrawIndirectBuffer, `ExecuteIndirect` replaces per-entity draw loop. Enables 10,000+ object scenes at 60fps.

#### 7. Temporal Upsampling (DLSS/FSR/XeSS)
- **Rulebook Task**: [phase4-temporal-upsampling-dlss-fsr](../.rulebook/tasks/phase4-temporal-upsampling-dlss-fsr/)
- **Complexity**: L (2–3 weeks)
- **Dependencies**: TAA (motion vectors required), Render Graph
- **Description**: DLSS 3 (NVIDIA), FSR 3 (AMD open source), XeSS (Intel) integration. Render at 50-75% resolution, upscale to target. Doubles performance at 4K with near-native quality.

#### 8. Nanite-like Virtual Geometry
- **Rulebook Task**: [phase4-nanite-virtual-geometry](../.rulebook/tasks/phase4-nanite-virtual-geometry/)
- **Complexity**: XL (4+ weeks)
- **Dependencies**: GPU-Driven Rendering, Bindless Resources
- **Description**: Meshlet-based geometry pipeline, cluster BVH hierarchical culling, software rasterizer for small triangles, visibility buffer rendering. Removes LOD authoring burden for static geometry.

#### 9. Advanced Physics
- **Rulebook Tasks**: [phase4-rope-chain-simulation](../.rulebook/tasks/phase4-rope-chain-simulation/), [phase4-soft-body-physics](../.rulebook/tasks/phase4-soft-body-physics/), [phase4-buoyancy-system](../.rulebook/tasks/phase4-buoyancy-system/), [phase4-wind-system-unified](../.rulebook/tasks/phase4-wind-system-unified/), [phase4-convex-decomposition-vhacd](../.rulebook/tasks/phase4-convex-decomposition-vhacd/)
- **Complexity**: M–L each
- **Dependencies**: Physics System
- **Description**: Rope/chain via Jolt distance constraints, soft body via Jolt v4.0 SoftBody, buoyancy (Archimedes force on submerged volumes), unified wind system (single authoritative wind field for vegetation/cloth/particles), VHACD convex decomposition for arbitrary mesh physics.

---

## Phase 0 Architecture Design Tasks

> These design tasks must be completed **before** implementation of dependent phases. No code is produced — only specs, API designs, and architectural decisions.

| Task | Description | Blocks |
|------|-------------|--------|
| [phase0-job-system-design](../.rulebook/tasks/phase0-job-system-design/) | JobDispatcher API, work-stealing scheduler, enkiTS evaluation | All multi-threaded systems |
| [phase0-audio-abstraction-layer](../.rulebook/tasks/phase0-audio-abstraction-layer/) | IAudioEngine interface + NullAudioEngine + backend selection | Phase 2 audio system |
| [phase0-platform-abstraction-layer](../.rulebook/tasks/phase0-platform-abstraction-layer/) | IPlatform interface for filesystem/threads/timers | CI + cross-platform |
| [phase0-render-graph-design](../.rulebook/tasks/phase0-render-graph-design/) | Render graph DAG + barrier scheduling design | Phase 2 renderer |
| [phase0-rhi-design](../.rulebook/tasks/phase0-rhi-design/) | RHI vtable for DX12/Vulkan/Metal | All rendering |

---

## New Tasks Added by AAA Gap Analysis (V2)

> The following tasks were identified in [MASTER-GAP-ANALYSIS-V2.md](../reviews/MASTER-GAP-ANALYSIS-V2.md) as gaps compared to AAA engine standards. They are organized by phase.

### New Phase 1 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| [phase1-script-execution-safety](../.rulebook/tasks/phase1-script-execution-safety/) | Watchdog thread + instruction budget — prevents `while true do end` freeze | M |
| [phase1-ecs-spatial-query](../.rulebook/tasks/phase1-ecs-spatial-query/) | Sphere/AABB overlap queries on ECS registry | S |
| [phase1-ecs-system-scheduler](../.rulebook/tasks/phase1-ecs-system-scheduler/) | Parallel ECS system groups (reads/writes non-overlapping) | M |
| [phase1-entity-templates](../.rulebook/tasks/phase1-entity-templates/) | Prefab-lite entity templates for quick spawning | S |
| [phase1-lua-coroutines](../.rulebook/tasks/phase1-lua-coroutines/) | LuaJIT coroutine support for async-style game logic | S |
| [phase1-lua-module-system](../.rulebook/tasks/phase1-lua-module-system/) | `require()` path resolution for Lua modules | S |
| [phase1-lua-dap-server](../.rulebook/tasks/phase1-lua-dap-server/) | DAP debugger server — breakpoints in Lua scripts | L |
| [phase1-vscode-syntax-highlighting](../.rulebook/tasks/phase1-vscode-syntax-highlighting/) | TextMate grammar for `.uzscene`, `.uzmat`, `.uzproj` | S |

### New Phase 2 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| [phase2-bindless-resources](../.rulebook/tasks/phase2-bindless-resources/) | **P0** Descriptor heap bindless architecture — prerequisite for GPU-driven rendering | XL |
| [phase2-ibl-pipeline](../.rulebook/tasks/phase2-ibl-pipeline/) | **P0** IBL compute bakers — ambient lighting for PBR (metallic surfaces currently black) | L |
| [phase2-render-graph-async-compute](../.rulebook/tasks/phase2-render-graph-async-compute/) | Async compute queue utilization in render graph | M |
| [phase2-occlusion-culling-hzb](../.rulebook/tasks/phase2-occlusion-culling-hzb/) | Hierarchical Z-Buffer GPU occlusion culling | L |
| [phase2-auto-exposure-color-grading](../.rulebook/tasks/phase2-auto-exposure-color-grading/) | Histogram auto-exposure + LUT color grading + tone mapping | M |
| [phase2-physics-multithreading](../.rulebook/tasks/phase2-physics-multithreading/) | Jolt `JPH::JobSystemThreadPool` configuration | S |
| [phase2-simd-math-optimization](../.rulebook/tasks/phase2-simd-math-optimization/) | DirectXMath SIMD Vec3/Mat4 for transform/culling loops | M |
| [phase2-memory-profiler](../.rulebook/tasks/phase2-memory-profiler/) | Per-arena allocation tracking + Tracy + VSCode panel | M |
| [phase2-cvar-developer-config-system](../.rulebook/tasks/phase2-cvar-developer-config-system/) | `CVar<T>` runtime config — no recompile for engine parameters | M |
| [phase2-frame-pacing-vsync-modes](../.rulebook/tasks/phase2-frame-pacing-vsync-modes/) | DXGI flip model + VRR/G-Sync + frame pacing | M |
| [phase2-asset-hot-reload-textures-meshes](../.rulebook/tasks/phase2-asset-hot-reload-textures-meshes/) | FileWatcher + GPU in-place resource update | M |
| [phase2-sdf-font-atlas](../.rulebook/tasks/phase2-sdf-font-atlas/) | msdf-atlas-gen baking + MSDF shader for crisp text | M |
| [phase2-profiler-instrumentation-api](../.rulebook/tasks/phase2-profiler-instrumentation-api/) | `UZ_PROFILE_SCOPE()` macro + Lua profiler API | S |
| [phase2-audio-profiler-metering](../.rulebook/tasks/phase2-audio-profiler-metering/) | Voice pool + DSP CPU + bus VU meters | M |
| [phase2-audio-bus-dynamic-graph](../.rulebook/tasks/phase2-audio-bus-dynamic-graph/) | Data-driven bus DAG — designers add buses without code changes | M |
| [phase2-script-intellisense-lsp](../.rulebook/tasks/phase2-script-intellisense-lsp/) | LuaLS + generated `ctx.d.lua` type defs | M |
| [phase2-script-linting-static-analysis](../.rulebook/tasks/phase2-script-linting-static-analysis/) | luacheck + custom UzEngine rules in VSCode | S |
| [phase2-ui-profiler-devtools](../.rulebook/tasks/phase2-ui-profiler-devtools/) | UIPass timing + element count + VSCode DevTools panel | M |
| [phase2-ui-hot-reload-stable](../.rulebook/tasks/phase2-ui-hot-reload-stable/) | Diff-based CSS/HTML patch — no white flash on reload | M |
| [phase2-ui-testing-framework](../.rulebook/tasks/phase2-ui-testing-framework/) | Screenshot pixel-diff UI regression tests | L |
| [phase2-ui-reference-resolution](../.rulebook/tasks/phase2-ui-reference-resolution/) | Scale matrix for 1920×1080 reference resolution | S |
| [phase2-ui-rich-text](../.rulebook/tasks/phase2-ui-rich-text/) | BBCode-like markup for bold/color/icon inline text | M |
| [phase2-ui-text-input-fields](../.rulebook/tasks/phase2-ui-text-input-fields/) | `<uz-input>` keyboard capture + IME + selection | M |
| [phase2-ui-state-machine-nav-stack](../.rulebook/tasks/phase2-ui-state-machine-nav-stack/) | UIScreenManager push/pop with transition animations | M |
| [phase2-ui-theme-system](../.rulebook/tasks/phase2-ui-theme-system/) | CSS design token theme files + runtime switching | M |

### New Phase 3 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| [phase3-motion-warping](../.rulebook/tasks/phase3-motion-warping/) | Root motion trajectory warping for ledge grab / door reach | L |
| [phase3-aim-offset](../.rulebook/tasks/phase3-aim-offset/) | 2D additive pose offset for weapon/head aiming | M |
| [phase3-procedural-foot-placement](../.rulebook/tasks/phase3-procedural-foot-placement/) | 2-bone IK foot placement on uneven terrain | M |
| [phase3-sync-markers-inertialization](../.rulebook/tasks/phase3-sync-markers-inertialization/) | Gait sync markers + inertial blending (no more transition pops) | L |
| [phase3-animation-compression](../.rulebook/tasks/phase3-animation-compression/) | ACL library — 8:1 animation compression | M |
| [phase3-jiggle-bones-secondary-anim](../.rulebook/tasks/phase3-jiggle-bones-secondary-anim/) | Spring-damper secondary physics on bones (hair, capes) | M |
| [phase3-animation-lod](../.rulebook/tasks/phase3-animation-lod/) | Bone count reduction at distance (4 LOD levels) | M |
| [phase3-distance-matching](../.rulebook/tasks/phase3-distance-matching/) | Playback rate adjustment for footstep sync | M |
| [phase3-stride-orientation-warping](../.rulebook/tasks/phase3-stride-orientation-warping/) | Hip rotation + stride deformation for omnidirectional movement | M |
| [phase3-mirror-animation](../.rulebook/tasks/phase3-mirror-animation/) | Flip poses L/R — halves directional animation authoring | S |
| [phase3-volumetric-fog-froxel](../.rulebook/tasks/phase3-volumetric-fog-froxel/) | Froxel scatter/transmittance — godray shafts and haze | L |
| [phase3-sky-atmosphere-bruneton](../.rulebook/tasks/phase3-sky-atmosphere-bruneton/) | Bruneton precomputed atmospheric scattering | L |
| [phase3-subsurface-scattering](../.rulebook/tasks/phase3-subsurface-scattering/) | Jimenez separable SSS for skin/wax/leaves | M |
| [phase3-clustered-shading-3d](../.rulebook/tasks/phase3-clustered-shading-3d/) | 3D frustum cluster grid for 200+ point lights | L |
| [phase3-motion-blur](../.rulebook/tasks/phase3-motion-blur/) | Per-object velocity buffer + radial blur pass | M |
| [phase3-area-lights-ltc](../.rulebook/tasks/phase3-area-lights-ltc/) | LTC area lights (rect/tube/disk) | L |
| [phase3-virtual-shadow-maps](../.rulebook/tasks/phase3-virtual-shadow-maps/) | Virtual shadow map cache — stable cascade shadows | L |
| [phase3-physics-batch-queries](../.rulebook/tasks/phase3-physics-batch-queries/) | Parallel raycast batches via job system | M |
| [phase3-compound-colliders](../.rulebook/tasks/phase3-compound-colliders/) | Multi-shape Jolt compound collider per entity | M |
| [phase3-physics-regions-gravity-zones](../.rulebook/tasks/phase3-physics-regions-gravity-zones/) | Per-volume gravity/damping override | M |
| [phase3-explosion-radial-force](../.rulebook/tasks/phase3-explosion-radial-force/) | Radial impulse API for explosions | S |
| [phase3-physics-debug-profiler](../.rulebook/tasks/phase3-physics-debug-profiler/) | Jolt stats + body overlay visualization | S |
| [phase3-audio-hrtf-binaural](../.rulebook/tasks/phase3-audio-hrtf-binaural/) | OpenAL Soft HRTF for headphone 3D audio | M |
| [phase3-audio-reverb-zones](../.rulebook/tasks/phase3-audio-reverb-zones/) | XAudio2 reverb submix per zone volume | M |
| [phase3-audio-lod](../.rulebook/tasks/phase3-audio-lod/) | 4-tier audio quality reduction at distance | M |
| [phase3-audio-animation-sync](../.rulebook/tasks/phase3-audio-animation-sync/) | AnimNotify audio events — frame-accurate footsteps | M |
| [phase3-audio-occlusion](../.rulebook/tasks/phase3-audio-occlusion/) | Physics raycasts for geometry-based audio muffling | M |
| [phase3-accessibility-framework](../.rulebook/tasks/phase3-accessibility-framework/) | Colorblind modes, subtitles, reduced motion, high contrast | L |
| [phase3-controller-haptics](../.rulebook/tasks/phase3-controller-haptics/) | XInput rumble API with presets and fade envelopes | S |
| [phase3-game-mode-state-machine](../.rulebook/tasks/phase3-game-mode-state-machine/) | GameModeManager push/pop stack with lifecycle callbacks | M |
| [phase3-day-night-cycle](../.rulebook/tasks/phase3-day-night-cycle/) | Time-of-day system with keyframe light/sky curves | M |
| [phase3-time-dilation](../.rulebook/tasks/phase3-time-dilation/) | Global time scale for slow-motion effects | S |
| [phase3-seeded-rng](../.rulebook/tasks/phase3-seeded-rng/) | Deterministic xorshift64 RNG for replay systems | S |
| [phase3-game-settings-framework](../.rulebook/tasks/phase3-game-settings-framework/) | Typed settings + quality presets + JSON persistence | M |
| [phase3-ktx2-basis-universal](../.rulebook/tasks/phase3-ktx2-basis-universal/) | Universal texture compression — one file for all GPU formats | L |
| [phase3-normal-map-baking](../.rulebook/tasks/phase3-normal-map-baking/) | GPU compute high-poly → low-poly normal baking | L |
| [phase3-auto-uv-unwrap](../.rulebook/tasks/phase3-auto-uv-unwrap/) | xatlas auto-unwrap for lightmap UV channel | M |
| [phase3-opus-audio-codec](../.rulebook/tasks/phase3-opus-audio-codec/) | libopus — 40% smaller audio vs OGG at same quality | M |
| [phase3-ui-drag-drop](../.rulebook/tasks/phase3-ui-drag-drop/) | Inventory drag-and-drop with ghost element | M |
| [phase3-ui-world-space-billboards](../.rulebook/tasks/phase3-ui-world-space-billboards/) | 3D world-space HTML UI attached to entities | L |
| [phase3-ui-minimap-widget](../.rulebook/tasks/phase3-ui-minimap-widget/) | `<uz-minimap>` with icons, zoom, fog-of-war | L |
| [phase3-ui-accessibility](../.rulebook/tasks/phase3-ui-accessibility/) | ARIA roles, screen reader, high contrast, focus rings | M |
| [phase3-font-fallback-cjk](../.rulebook/tasks/phase3-font-fallback-cjk/) | CJK font fallback chain with Noto fonts | M |

### New Phase 4 Tasks

| Task | Description | Complexity |
|------|-------------|------------|
| [phase4-dxr-ray-tracing-rhi](../.rulebook/tasks/phase4-dxr-ray-tracing-rhi/) | RHI DXR extension — BLAS/TLAS + ray pipeline | XL |
| [phase4-gpu-driven-rendering](../.rulebook/tasks/phase4-gpu-driven-rendering/) | GPU-generated draw calls via ExecuteIndirect | XL |
| [phase4-temporal-upsampling-dlss-fsr](../.rulebook/tasks/phase4-temporal-upsampling-dlss-fsr/) | DLSS 3 + FSR 3 + XeSS temporal upsampling | L |
| [phase4-nanite-virtual-geometry](../.rulebook/tasks/phase4-nanite-virtual-geometry/) | Meshlet cluster LOD — eliminates LOD authoring | XL |
| [phase4-rope-chain-simulation](../.rulebook/tasks/phase4-rope-chain-simulation/) | Jolt constraint chain + tube renderer | M |
| [phase4-soft-body-physics](../.rulebook/tasks/phase4-soft-body-physics/) | Jolt v4 SoftBody — capes, flags, deformable props | L |
| [phase4-buoyancy-system](../.rulebook/tasks/phase4-buoyancy-system/) | Archimedes buoyancy force for water gameplay | M |
| [phase4-wind-system-unified](../.rulebook/tasks/phase4-wind-system-unified/) | Global wind field used by vegetation/cloth/particles/hair | M |
| [phase4-convex-decomposition-vhacd](../.rulebook/tasks/phase4-convex-decomposition-vhacd/) | V-HACD auto-decomposition for arbitrary mesh physics | M |
| [phase4-virtual-texturing](../.rulebook/tasks/phase4-virtual-texturing/) | Sparse virtual textures for unlimited texture resolution | XL |
| [phase4-cloth-simulation](../.rulebook/tasks/phase4-cloth-simulation/) | Jolt/GPU cloth simulation for garments | L |
| [phase4-destructible-meshes](../.rulebook/tasks/phase4-destructible-meshes/) | Voronoi fracture + debris physics | L |
| [phase4-visual-scripting](../.rulebook/tasks/phase4-visual-scripting/) | Node graph visual scripting as Lua alternative | XL |

---

## Phase 5: Future (Not Scheduled)

Ideas beyond Phase 4 for future development:

- **Networking**: Rollback netcode (GGPO), dedicated server support, cross-platform multiplayer
- **World Streaming**: Partition large worlds, asynchronous chunk loading, memory budgets
- **Cinemachine-style Camera Director**: Cinematic camera tracks, blend between cameras, procedural dolly shots
- **Dialogue System**: Conversation trees, branching dialogue, character voice mapping, subtitles
- **Tilemap 2D Mode**: 2D gameplay support, tileset painting, physics for 2D
- **Console/Mobile Platform Support**: Switch, PS5, iOS/Android builds, platform-specific optimizations
- **Motion Matching**: Database-driven animation selection for natural locomotion
- **Global Illumination**: Lumen-style probe-based dynamic GI
- **Vehicle Physics**: Wheel suspension, drivetrain, aerodynamics
- **Adaptive Music**: Beat-synchronized dynamic music with layer mixing

---

## Implementation Velocity

### Parallelism Strategy

**Specialized agents can work simultaneously** on independent tasks:
- **Engine Systems Engineer**: Scene System, Physics System, Event Bus, Timer System
- **Rendering Engineer**: PBR Renderer, Shader Library, Shadow Maps, SSAO, SSR, TAA
- **Tools & Asset Pipeline Engineer**: GLTF/FBX Import, Texture Tools, Asset PAK, Prefab Editor
- **Gameplay Engineer**: Lua Scripting, Animation System, Input System, Camera System
- **Tools Developer (VSCode)**: Editor Panels, Asset Browser, Undo/Redo, Editor Gizmos
- **Audio Engineer**: Audio System
- **Physics & Animation Specialist**: Skeletal Mesh, Ragdoll, Physics Character Controller

### Definition of "Done" for Every Task

Each task is complete only when:

1. **Specification approved** — Rulebook `proposal.md` documents design and rationale
2. **Implementation complete** — All `tasks.md` checklist items finished
3. **Tests passing** — 95% code coverage minimum (unit + integration tests)
4. **Documentation updated** — New docs in `/docs/specs/` or `/docs/engine/`
5. **Integration verified** — Works with existing Phase N systems, no regressions
6. **Code review passed** — Peer review or AI review for consistency
7. **Performance verified** — Meets frame rate target (60 FPS @ 1080p minimum)

### Hot-Reload First

For each new file type and system, implement hot-reload from day 1:
- **Shaders** (.hlsl files): Recompile on save, inject new PSO
- **Scripts** (.lua files): Reload VM, preserve entity references
- **Scenes** (.uzscene files): Reload node tree without full restart
- **Materials** (.uzmat files): Reload parameters, rebuild shader variants
- **Assets** (imported .uzmesh, .uzfx): Reload GPU buffers if format changed

Hot-reload is worth the extra implementation effort — it reduces iteration time from seconds to milliseconds.

---

## Complexity Legend

| Label | Estimate | Characteristics |
|-------|----------|-----------------|
| **S** | 1–3 days | Single file/class, clear spec, straightforward logic |
| **M** | 1 week | 3–10 files, some design decisions, needs comprehensive tests |
| **L** | 2–3 weeks | New subsystem, complex interactions, significant integration testing required |
| **XL** | 4+ weeks | Cross-cutting architecture, performance-critical, many integration points, extensive testing |

---

## Glossary

| Term | Definition |
|------|-----------|
| **G-Buffer** | Deferred rendering intermediate: albedo, normal, roughness, metallic, depth, motion |
| **PSO** | Pipeline State Object (DX12) — shader + blend + rasterizer state |
| **IBL** | Image-Based Lighting: environment cubemap provides indirect lighting |
| **LOD** | Level of Detail: multi-resolution assets (3 levels typical) |
| **IPC** | Inter-Process Communication: JSON file protocol between editor and engine |
| **MCP** | Model Context Protocol: API for Claude/AI agents to query engine |
| **ECS** | Entity Component System: data-driven architecture (Scene + Components) |
| **IK** | Inverse Kinematics: solver for limb positioning |
| **AnimNotify** | Animation event: trigger behavior at keyframe (footstep, attack) |
| **Root Motion** | Character movement extracted from animation |
| **HBAO+** | Horizon-Based Ambient Occlusion algorithm |
| **SSR** | Screen-Space Reflections: local reflections from screen-space depth |
| **TAA** | Temporal Anti-Aliasing: temporal filtering for smooth edges |
| **CSM** | Cascaded Shadow Maps: multiple resolution shadow cascades |
| **PCF** | Percentage Closer Filtering: soft shadow edges via depth comparison |
| **HDR** | High Dynamic Range: floating-point color with extended luminance |
| **BC6H** | DirectX texture compression for HDR (unsigned/signed) |
| **CJK** | Chinese, Japanese, Korean text — requires special font rendering |
| **SDF** | Signed Distance Field: compact font glyph representation |
| **RVO2** | Reciprocal Collision Avoidance: local path planning algorithm |

