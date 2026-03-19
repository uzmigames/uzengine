# UzEngine 0.2 — Documentation

> AAA game engine embedded in VSCode, designed for autonomous AI development.

## Architecture & Planning

| Document | Description |
|----------|-------------|
| [architecture.md](architecture.md) | High-level architecture — layers, threading, rendering backend, build system |
| [roadmap.md](roadmap.md) | Full development roadmap — 9 phases, from foundation to shipping |
| [dag.md](dag.md) | Milestone dependency graph |
| [pdr.md](pdr.md) | Project Design Review — scope, risks, decisions |
| [deprecated/engine.md](deprecated/engine.md) | (Deprecated) Engine layer API — superseded by engine/ directory |
| [deprecated/developer.md](deprecated/developer.md) | (Deprecated) Developer layer docs — superseded by engine/22-developer-layer/ |

## Detailed Engine Documentation

For comprehensive system design, API reference, and implementation details, see [engine/README.md](engine/README.md).

190+ files across 33 sections organized by tier:
- **Sections 00–06**: Architecture, core systems, RHI, shader pipeline (Phase 1 — done)
- **Sections 07–12**: Platform, resource system, lifecycle, ECS, scene, rendering (Phases 2–3)
- **Sections 13–21**: Input, camera, scripting, physics, audio, UI, animation, gameplay, property system (Phases 4–6)
- **Sections 22–30**: Developer tools, localization, networking, VFX, AI pipeline, platform services, IPC, examples (Phases 7–9)
- **Sections 31–33**: Editor framework, engine mode, VFX AI tool (Phase 5)

## Technical Specs

Detailed specifications for each milestone, located in [specs/](specs/):

| Spec | Milestone | Topic |
|------|-----------|-------|
| [1.0-bootstrap](specs/1.0-bootstrap.md) | 1.0 | Project bootstrap, build system, core libraries |
| [1.1-math-containers](specs/1.1-math-containers.md) | 1.1 | Math types (Vec2–4, Mat4, Quat, Transform) and containers |
| [1.2-rhi](specs/1.2-rhi.md) | 1.2 | RHI abstraction — DX12/Vulkan/Metal backends |
| [1.3-renderer](specs/1.3-renderer.md) | 1.3 | Render graph, GBuffer, lighting, post-processing |
| [1.4-scene-system](specs/1.4-scene-system.md) | 1.4 | Scene tree, node types, serialization |
| [1.5-build-pipeline](specs/1.5-build-pipeline.md) | 1.5 | Asset build pipeline (uzcook, uzembed) |
| [1.5-gameplay](specs/1.5-gameplay.md) | 1.5 | Gameplay systems — input, physics, audio |
| [1.5.0-shader-pipeline](specs/1.5.0-shader-pipeline.md) | 1.5 | Shader compilation, caching, hot-reload |
| [2.0-napi-foundation](specs/2.0-napi-foundation.md) | 2.0 | N-API .node addon integration with VSCode |
| [2.1-viewport-ui](specs/2.1-viewport-ui.md) | 2.1 | Viewport HWND + editor UI panels |
| [2.2-2.3-editors](specs/2.2-2.3-editors.md) | 2.2–2.3 | Scene editor, inspector, asset browser |
| [3.0-mcp-integration](specs/3.0-mcp-integration.md) | 3.0 | MCP server for AI-driven development |
| [4.0-polish-shipping](specs/4.0-polish-shipping.md) | 4.0 | Polish, optimization, shipping |
| [index.md](specs/index.md) | — | Spec index and status tracker |

## Reference Engine Analysis

| Directory | Description |
|-----------|-------------|
| [analysis/](analysis/) | Godot 4 and UE5 rendering architecture analysis (POM, tonemap, RenderingDevice) |
| [architecture/](architecture/) | UE5 RHI binding pattern analysis, migration playbooks, shader model compatibility |
| [research/](research/) | Research notes, code audits, platform analysis, and implementation templates |

## Benchmarks

| Directory | Description |
|-----------|-------------|
| [benchmarks/](benchmarks/) | Compiler comparison and runtime performance benchmarks |

## Quality Audits

| Directory | Description |
|-----------|-------------|
| [audits/](audits/) | Documentation and code quality audit reports |

## Implementation Guides

Located in [guides/](guides/):

| Guide | Description |
|-------|-------------|
| [temporal-upscaling.md](guides/temporal-upscaling.md) | Temporal upscaling (TAA/TSR), screen percentage system, dynamic resolution, and Lua API |
| [shader-parameter-binding-guide.md](guides/shader-parameter-binding-guide.md) | Macro system, metadata definition, and `ShaderParameterBinder` usage |
| [shader-parameter-migration-guide.md](guides/shader-parameter-migration-guide.md) | Step-by-step migration from manual CB + resource binding to the new system |
| [ue5-transmittance-lut-porting.md](guides/ue5-transmittance-lut-porting.md) | Porting the UE5 transmittance LUT pass to UzEngine |

## Observability

| Directory | Description |
|-----------|-------------|
| [observability/](observability/) | C++ and TypeScript observability specs (UZ_LOG, Tracy, UZ_CHECK) |

## User Documentation

Located in [user/](user/):

### Getting Started

- [Overview](user/getting-started/overview.md) — What is UzEngine, key concepts
- [Building](user/getting-started/building.md) — Build from source
- [First Window](user/getting-started/first-window.md) — Create your first render window

### Guides

- [Lua Scripting](user/guides/lua-scripting.md) — Write game logic in LuaJIT 2.1 with hot-reload
- **Build System**: [Zig](user/guides/build-system/zig-build.md) · [CMake+MSVC](user/guides/build-system/cmake-msvc.md) · [CMake+Clang](user/guides/build-system/cmake-clang.md)
- [Math](user/guides/math/overview.md) — Vec, Mat4, Quat, Transform usage
- [Memory](user/guides/memory/overview.md) — Engine allocators
- [Platform](user/guides/platform/overview.md) — Window, input, OS abstraction
- [Profiling](user/guides/profiling/overview.md) — Tracy integration, UZ_PROFILE_SCOPE
- [Rendering](user/guides/rendering/overview.md) — Render pipeline overview
- [Shader Pipeline](user/guides/rendering/shader-pipeline.md) — Compilation, caching, hot-reload
- [RHI](user/guides/rhi/overview.md) — Render Hardware Interface usage

### API Reference

- [Core](user/reference/core/index.md) · [Paths](user/reference/core/paths.md)
- [Math](user/reference/math/index.md)
- [Containers](user/reference/containers/index.md)
- [Platform](user/reference/platform/index.md)
- [RHI](user/reference/rhi/index.md) · [RHI Config](user/reference/rhi/rhi-config.md)
- [Renderer](user/reference/renderer/index.md)
- [Shader](user/reference/shader/index.md)
- [Asset](user/reference/asset/index.md)
- [Tools](user/reference/tools/index.md) · [uzcook](user/reference/tools/uzcook.md) · [uzembed](user/reference/tools/uzembed.md)

### Contributing

- [Project Structure](user/contributing/project-structure.md)
- [Code Style](user/contributing/code-style.md)
- [Building & Testing](user/contributing/building-and-testing.md)
