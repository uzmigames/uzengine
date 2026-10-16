# UzEngine 0.6 — Engine Documentation

> **AAA game engine embedded in VSCode, designed for autonomous AI-driven development.**

189 files across 33 sections. Each section has multiple detailed sub-files covering API design, code examples, architecture diagrams, and engine comparisons.

> **Current capability status**: the [capability matrix](../analysis/known-issues-and-partial-implementations.md#capability-matrix) records what the renderer actually supports, with evidence per row. The status columns below describe design coverage, not verification.

## Document Map

### Tier 0 — Architecture Overview

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [00-overview/](00-overview/) | Architecture Overview | 7 | **Reference** | Vision, principles, project structure, execution model, IPC, POC lessons |

### Tier 1 — Built

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [01-core/](01-core/) | Core Systems | 5 | **Done** | Paths, logging, assertions, config, memory |
| [02-math/](02-math/) | Math Library | 4 | **Done** | Vec2/3/4, Mat4, Quat, Transform, Basis, Rect2, DirectXMath SIMD |
| [03-rhi/](03-rhi/) | Render Hardware Interface | 14 | **Done** (DX12+DX11+Vulkan+Metal) | 4-backend GPU abstraction, DX12 SM6.6, DX11, Vulkan 1.3, Metal, PSO cache, GPU breadcrumbs |
| [04-observability/](04-observability/) | Observability | 5 | **Done** | Logging, CPU/GPU profiling (Tracy), stat counters, RenderDoc integration, profile capture |
| [05-shader-pipeline/](05-shader-pipeline/) | Shader Pipeline | 6 | **Done** | VFS, DXC+FXC+Slang compilation, xxHash64 cache, hot-reload, reflection, permutations |
| [06-build-toolchain/](06-build-toolchain/) | Build Toolchain | 5 | **Done** | Zig build (primary), CMake fallback, thirdparty libs, build configs |
| [14-security/](14-security/) | **Security & Memory Safety** | **3** | **Reference** | **RAII ownership conventions, shader compilation security, bytecode validation** |

### Tier 2 — Implemented (Phase 2-3)

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [07-platform/](07-platform/) | Platform Layer | 16 | **Partial** | Win32 window, input, file I/O implemented; Linux/macOS platform planned |
| [08-resource-system/](08-resource-system/) | Resource System | 8 | **Done** | Asset registry, importers, .uzshader/.uzmesh/.uztexture/.uzsdf formats, manifest, cache |
| [09-application-lifecycle/](09-application-lifecycle/) | Application Lifecycle | 7 | **Done** | EngineLoop, subsystem registry, module registry, time manager, project loader |
| [10-entity-component/](10-entity-component/) | Entity/Component | 6 | **Done** | World, WorldSubsystem, EnTT ECS, 11 component types, tick system |
| [11-scene/](11-scene/) | Scene System | 4 | **Partial** | .uzscene loader implemented; world streaming and terrain planned |
| [12-rendering/](12-rendering/) | Rendering Pipeline | 7 | **Done** | Deferred PBR, render graph, GBuffer, shadows (CSM+SDF), 20+ post-process passes, atmosphere, IBL, volumetric clouds, material graph |

### Tier 3 — Gameplay Systems

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [13-input-system/](13-input-system/) | Input System | 4 | **Partial** | Lua input API (keyboard/mouse polling) implemented; Enhanced Input planned |
| [14-camera/](14-camera/) | Camera System | 5 | **Partial** | Camera component + Lua camera API implemented; SpringArm, shake planned |
| [15-scripting/](15-scripting/) | Scripting | 6 | **Done** | LuaJIT 2.1 VM, sandbox, ScriptComponent lifecycle, 12+ Lua API modules, hot-reload |
| [16-physics/](16-physics/) | Physics | 4 | **Planned** | Jolt Physics, rigid bodies, queries, character controller |
| [17-audio/](17-audio/) | Audio | 6 | **Planned** | miniaudio backend, spatial audio, bus mixing, components |
| [18-ui-system/](18-ui-system/) | UI System | 6 | **Planned** | HTML/CSS runtime, data binding, rendering, input routing, console |
| [19-animation/](19-animation/) | Animation | 7 | **Planned** | Skeletal mesh, GPU skinning, state machines, IK, root motion, **sequencer** |
| [20-gameplay/](20-gameplay/) | Gameplay Systems | 6 | **Planned** | Abilities, behavior trees, save system, navigation, data tables |
| [21-property-system/](21-property-system/) | Property System | 4 | **Done** | ClassInfo, ClassRegistry, PropertySerializer — reflection + JSON serialization |

### Tier 4 — Developer Tools & Future

| # | Module | Files | Status | Description |
|---|--------|-------|--------|-------------|
| [22-developer-layer/](22-developer-layer/) | Developer Layer | 6 | **Planned** | Profiling, debug draw, hot-reload, automation, console |
| [23-localization/](23-localization/) | Localization | 5 | **Planned** | String tables, ICU formatting, text shaping, asset localization |
| [24-networking/](24-networking/) | Networking | 5 | **Planned** | Server architecture, replication, RPCs, lag compensation |
| [25-vfx/](25-vfx/) | VFX System | 5 | **Done** | GPU particles (65K/emitter), 15 VFX modules, VFX graph, decals, trails, collision, lights |
| [33-vfx-ai-tool/](33-vfx-ai-tool/) | **VFX AI Tool** | **6** | **Planned** | **Natural language VFX generation, Claude shaders, ComfyUI textures, TRELLIS.2 meshes, orchestrator, user workflow** |
| [26-ai-content-pipeline/](26-ai-content-pipeline/) | AI Content Pipeline | 3 | **Planned** | LLM content generation, validation |
| [27-platform-services/](27-platform-services/) | Platform Services | 3 | **Planned** | Steam integration, upscaling (DLSS/FSR/XeSS) |
| [28-ipc-protocol/](28-ipc-protocol/) | IPC Protocol | 3 | **Planned** | N-API bridge, H.264 frame streaming |
| [29-design-decisions/](29-design-decisions/) | Design Decisions | 3 | **Reference** | POC lessons, technology choices |
| [30-game-examples/](30-game-examples/) | Game Examples | 4 | **Reference** | Pong, Flappy Bird, Tower Defense |
| [31-editor-framework/](31-editor-framework/) | Editor Framework | 21 | **Planned** | Overview, gizmos, undo/redo, inspector, hierarchy (05), asset browser (06), tool modes (07), output panel (08), **node-graph framework (09), material (10), anim controller (11), control rig (12), VFX (13), sequencer (14), behavior tree (15), PCG (16), debug tools (17), viewport modes (18), preferences (19), editor IPC (20), testing (21)** |
| [32-engine-mode/](32-engine-mode/) | Engine Mode | 5 | **Planned** | VSCode IDE transformation, layout management, panel lifecycle, IDE compatibility (VSCode/Cursor/Windsurf), viewport embedding |

## How to Read This

- **Implementing a system?** Start with its numbered directory. Each doc is self-contained with API design, code examples, rationale, and engine comparisons.
- **Understanding architecture?** Start with [00-overview/](00-overview/) then [29-design-decisions/](29-design-decisions/).
- **Looking for examples?** [30-game-examples/](30-game-examples/) has complete game implementations.

## Conventions

- All code examples use C++23
- Engine comparisons reference UE5.6 and Godot 4.x (clean-room — study approach, never copy code)
- `MUST`/`SHALL`/`NEVER` indicate hard requirements
- `SHOULD` indicates strong recommendations
- Status tags: **Done** (implemented + tested), **Partial** (partially implemented), **Active** (currently being worked on), **Planned** (designed, not yet implemented)
