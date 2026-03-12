# UzEngine 0.2 — Engine Documentation

> **AAA game engine embedded in VSCode, designed for autonomous AI-driven development.**

This directory contains the complete engine documentation, organized by implementation priority. Lower numbers = already built or in-progress. Higher numbers = planned for future milestones.

## Document Map

### Tier 0 — Architecture Overview

| # | Module | Status | Description |
|---|--------|--------|-------------|
| [00-overview/](00-overview/) | Architecture Overview | **Reference** | Vision, principles, project structure, execution model, IPC, POC lessons |

Files:
- `01-vision-principles.md` — Vision, core principles, clean code, SOLID, observability rules
- `02-reference-sources.md` — UE5/Godot reference usage, clean-room rules
- `03-project-structure.md` — Directory layout, build pipeline, three-layer architecture
- `04-development-phases.md` — Phase 1 (runtime) → Phase 1.5 (packager) → Phase 2 (editor) → Phase 3 (MCP)
- `05-execution-model.md` — N-API in-process addon, runtime pool, CLI interface
- `06-inter-layer-communication.md` — Extension ↔ Runtime N-API, webview IPC, MCP protocol
- `07-poc-lessons.md` — POC 0.1 and 0.2 findings, frame streaming benchmarks

### Tier 1 — Built / In-Progress

| # | Module | Status | Description |
|---|--------|--------|-------------|
| [01-core/](01-core/) | Core Systems | **Partial** | Paths, logging, assertions, config, memory |
| [02-math/](02-math/) | Math Library | **Done** | Vec2/3/4, Mat4, Quat, Transform, DirectXMath SIMD |
| [03-rhi/](03-rhi/) | Render Hardware Interface | **Done** (DX12) | Multi-backend GPU abstraction, DX12 SM6.6 backend |
| [04-observability/](04-observability/) | Observability | **Partial** | Logging, CPU/GPU profiling, stat counters, crash reporting |
| [05-shader-pipeline/](05-shader-pipeline/) | Shader Pipeline | **Planned** | VFS, DXC compilation, content-hash cache, hot-reload |
| [06-build-toolchain/](06-build-toolchain/) | Build Toolchain | **Active** | Zig build, ~50 third-party libs, N-API addon, build configs |

### Tier 2 — Next Phase Foundation

| # | Module | Status | Description |
|---|--------|--------|-------------|
| [07-platform/](07-platform/) | Platform Layer | **Planned** | Window management, raw input, async file I/O |
| [08-resource-system/](08-resource-system/) | Resource System | **Planned** | Asset registry, .uz* formats, import/cook pipeline |
| [09-application-lifecycle/](09-application-lifecycle/) | Application Lifecycle | **Planned** | Bootstrap, subsystems, tick architecture, plugins |
| [10-entity-component/](10-entity-component/) | Entity/Component | **Planned** | World, ECS (EnTT), component hierarchy, events |
| [11-scene/](11-scene/) | Scene System | **Planned** | .uzscene format, node types, serialization, streaming |
| [12-rendering/](12-rendering/) | Rendering Pipeline | **Planned** | Render world, render graph, deferred PBR, post-processing |

### Tier 3 — Gameplay Systems

| # | Module | Status | Description |
|---|--------|--------|-------------|
| [13-input-system/](13-input-system/) | Input System | **Planned** | Enhanced Input, action mapping, runtime rebinding |
| [14-camera/](14-camera/) | Camera System | **Planned** | SpringArm, modes, shake, post-process volumes |
| [15-scripting/](15-scripting/) | Scripting | **Planned** | LuaJIT integration, sandbox, hot-reload |
| [16-physics/](16-physics/) | Physics | **Planned** | Jolt Physics, rigid bodies, queries, character controller |
| [17-audio/](17-audio/) | Audio | **Planned** | miniaudio + custom submix, spatial audio, voice pool |
| [18-ui-system/](18-ui-system/) | UI System | **Planned** | HTML/CSS reactive UI, data binding, font SDF |
| [19-animation/](19-animation/) | Animation | **Planned** | Skeletal mesh, GPU skinning, state machines, IK |
| [20-gameplay/](20-gameplay/) | Gameplay Systems | **Planned** | Abilities, behavior trees, save system, navigation |
| [21-property-system/](21-property-system/) | Property System | **Planned** | Reflection, serialization, reactivity |

### Tier 4 — Developer Tools & Future

| # | Module | Status | Description |
|---|--------|--------|-------------|
| [22-developer-layer/](22-developer-layer/) | Developer Layer | **Planned** | Profiling (Tracy, GPU), debug draw, hot-reload, automation, console, ImGui |
| [23-localization/](23-localization/) | Localization | **Planned** | i18n (CSV), ICU plurals/formatting, HarfBuzz shaping, per-script fonts, BiDi |
| [24-networking/](24-networking/) | Networking | **Planned** | Dedicated server, client prediction, property replication, RPCs, lag compensation |
| [25-vfx/](25-vfx/) | VFX System | **Planned** | Niagara-style GPU particles, .uzvfx format |
| [26-ai-content-pipeline/](26-ai-content-pipeline/) | AI Content Pipeline | **Planned** | Multi-provider AI generation, import integration |
| [27-platform-services/](27-platform-services/) | Platform Services | **Planned** | Steam, Epic, console SDK abstraction |
| [28-ipc-protocol/](28-ipc-protocol/) | IPC Protocol | **Planned** | Extension ↔ Runtime N-API, frame streaming |
| [29-design-decisions/](29-design-decisions/) | Design Decisions | **Reference** | Architecture rationale, POC lessons, engine comparisons |
| [30-game-examples/](30-game-examples/) | Game Examples | **Reference** | Complete game implementations (Pong, FPS, etc.) |

## How to Read This

- **Implementing a system?** Start with its numbered directory. Each doc is self-contained with API design, code examples, rationale, and engine comparisons.
- **Understanding architecture?** Start with [00-overview/](00-overview/) for vision, principles, and project structure. Then [29-design-decisions/](29-design-decisions/) for per-system rationale.
- **Looking for examples?** [30-game-examples/](30-game-examples/) has complete game implementations showing all systems working together.

## Conventions

- All code examples use C++17/20
- Engine comparisons reference UE5.6 and Godot 4.x (clean-room — study approach, never copy code)
- `MUST`/`SHALL`/`NEVER` indicate hard requirements
- `SHOULD` indicates strong recommendations
- Status tags: **Done** (implemented + tested), **Partial** (partially implemented), **Active** (currently being worked on), **Planned** (designed, not yet implemented)

## Source Documents

This documentation was restructured from three monolithic files:
- `docs/ENGINE.md` — Engine layer API design (~5500 lines)
- `docs/ARCHITECTURE.md` — Overall architecture (~2300 lines)
- `docs/DEVELOPER.md` — Developer-only tooling layer (~170 lines)
