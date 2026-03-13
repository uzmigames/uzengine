# UzEngine 0.2 — Engine Documentation

> **AAA game engine embedded in VSCode, designed for autonomous AI-driven development.**

189 files across 33 sections. Each section has multiple detailed sub-files covering API design, code examples, architecture diagrams, and engine comparisons.

## Document Map

### Tier 0 — Architecture Overview

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [00-overview/](00-overview/) | Architecture Overview | 7 | **Reference** | Vision, principles, project structure, execution model, IPC, POC lessons |

### Tier 1 — Built / In-Progress

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [01-core/](01-core/) | Core Systems | 5 | **Partial** | Paths, logging, assertions, config, memory |
| [02-math/](02-math/) | Math Library | 4 | **Done** | Vec2/3/4, Mat4, Quat, Transform, Basis, Rect2, DirectXMath SIMD |
| [03-rhi/](03-rhi/) | Render Hardware Interface | 14 | **Done** (DX12+Vulkan) | Multi-backend GPU abstraction, DX12 SM6.6, Vulkan 1.3, SPIR-V, **GPU memory management** |
| [04-observability/](04-observability/) | Observability | 5 | **Partial** | Logging, CPU/GPU profiling, stat counters, crash reporting |
| [05-shader-pipeline/](05-shader-pipeline/) | Shader Pipeline | 6 | **Planned** | VFS, DXC compilation, content-hash cache, hot-reload, reflection |
| [06-build-toolchain/](06-build-toolchain/) | Build Toolchain | 5 | **Active** | Zig build, thirdparty libs, N-API addon, build configs |

### Tier 2 — Next Phase Foundation

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [07-platform/](07-platform/) | Platform Layer | 16 | **Planned (4.3-4.11.0)** | Abstraction interfaces, window, input, file I/O, file watching, process/DLL, system info, dialogs, OS integration, crash handling, **Linux platform, macOS platform, backend registry** |
| [08-resource-system/](08-resource-system/) | Resource System | 8 | **Planned** | Asset registry, .uz* formats, **mesh pipeline, texture pipeline, asset cooking** |
| [09-application-lifecycle/](09-application-lifecycle/) | Application Lifecycle | 7 | **Done** | Bootstrap, subsystems, tick architecture, game instance, demo migration |
| [10-entity-component/](10-entity-component/) | Entity/Component | 6 | **Planned (3b-3.0b)** | **World, Entity handles, ECS (EnTT)**, component hierarchy, subsystems, events |
| [11-scene/](11-scene/) | Scene System | 4 | **Planned** | .uzscene format, **world streaming, terrain, foliage** |
| [12-rendering/](12-rendering/) | Rendering Pipeline | 7 | **Partial (3a-3.1.0)** | **Render world, render graph (API + guide)**, material system, lighting, post-processing |

### Tier 3 — Gameplay Systems

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [13-input-system/](13-input-system/) | Input System | 4 | **Planned** | Enhanced Input, action mapping, runtime rebinding |
| [14-camera/](14-camera/) | Camera System | 5 | **Planned** | SpringArm, modes, shake, post-process volumes |
| [15-scripting/](15-scripting/) | Scripting | 6 | **Planned** | LuaJIT, sandbox, script runner, hot-reload, Lua API reference |
| [16-physics/](16-physics/) | Physics | 4 | **Planned** | Jolt Physics, rigid bodies, queries, character controller |
| [17-audio/](17-audio/) | Audio | 6 | **Planned** | miniaudio backend, spatial audio, bus mixing, components |
| [18-ui-system/](18-ui-system/) | UI System | 6 | **Planned** | HTML/CSS runtime, data binding, rendering, input routing, console |
| [19-animation/](19-animation/) | Animation | 7 | **Planned** | Skeletal mesh, GPU skinning, state machines, IK, root motion, **sequencer** |
| [20-gameplay/](20-gameplay/) | Gameplay Systems | 6 | **Planned** | Abilities, behavior trees, save system, navigation, data tables |
| [21-property-system/](21-property-system/) | Property System | 4 | **Implemented (3b-3.2b MVP)** | Reflection, **JSON serialization**, reactivity |

### Tier 4 — Developer Tools & Future

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [22-developer-layer/](22-developer-layer/) | Developer Layer | 6 | **Planned** | Profiling, debug draw, hot-reload, automation, console |
| [23-localization/](23-localization/) | Localization | 5 | **Planned** | String tables, ICU formatting, text shaping, asset localization |
| [24-networking/](24-networking/) | Networking | 5 | **Planned** | Server architecture, replication, RPCs, lag compensation |
| [25-vfx/](25-vfx/) | VFX System | 5 | **Planned** | GPU particles, VFX modules, VFX graph, decals |
| [33-vfx-ai-tool/](33-vfx-ai-tool/) | **VFX AI Tool** | **6** | **Planned** | **Natural language VFX generation, Claude shaders, ComfyUI textures, TRELLIS.2 meshes, orchestrator, user workflow** |
| [26-ai-content-pipeline/](26-ai-content-pipeline/) | AI Content Pipeline | 3 | **Planned** | LLM content generation, validation |
| [27-platform-services/](27-platform-services/) | Platform Services | 3 | **Planned** | Steam integration, upscaling (DLSS/FSR/XeSS) |
| [28-ipc-protocol/](28-ipc-protocol/) | IPC Protocol | 3 | **Planned** | N-API bridge, H.264 frame streaming |
| [29-design-decisions/](29-design-decisions/) | Design Decisions | 3 | **Reference** | POC lessons, technology choices |
| [30-game-examples/](30-game-examples/) | Game Examples | 4 | **Reference** | Pong, Flappy Bird, Tower Defense |
| [40-editor-framework/](40-editor-framework/) | Editor Framework | 21 | **Planned** | Overview, gizmos, undo/redo, inspector, hierarchy (05), asset browser (06), tool modes (07), output panel (08), **node-graph framework (09), material (10), anim controller (11), control rig (12), VFX (13), sequencer (14), behavior tree (15), PCG (16), debug tools (17), viewport modes (18), preferences (19), editor IPC (20), testing (21)** |
| [41-engine-mode/](41-engine-mode/) | Engine Mode | 5 | **Planned** | VSCode IDE transformation, layout management, panel lifecycle, IDE compatibility (VSCode/Cursor/Windsurf), viewport embedding |

## How to Read This

- **Implementing a system?** Start with its numbered directory. Each doc is self-contained with API design, code examples, rationale, and engine comparisons.
- **Understanding architecture?** Start with [00-overview/](00-overview/) then [29-design-decisions/](29-design-decisions/).
- **Looking for examples?** [30-game-examples/](30-game-examples/) has complete game implementations.

## Conventions

- All code examples use C++17/20
- Engine comparisons reference UE5.6 and Godot 4.x (clean-room — study approach, never copy code)
- `MUST`/`SHALL`/`NEVER` indicate hard requirements
- `SHOULD` indicates strong recommendations
- Status tags: **Done** (implemented + tested), **Partial** (partially implemented), **Active** (currently being worked on), **Planned** (designed, not yet implemented)
