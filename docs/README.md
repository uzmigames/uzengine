# UzEngine 0.2 — Documentation

> AAA game engine embedded in VSCode, designed for autonomous AI development.

## Architecture & Planning

| Document | Description |
|----------|-------------|
| [ARCHITECTURE.md](ARCHITECTURE.md) | High-level architecture — layers, threading, rendering backend, build system |
| [ENGINE.md](ENGINE.md) | Engine layer API — lifecycle, ECS, components, input, physics, audio, rendering |
| [DEVELOPER.md](DEVELOPER.md) | Developer layer — tools and systems stripped from shipping builds |
| [ROADMAP.md](ROADMAP.md) | Full development roadmap from zero to AAA-ready |
| [DAG.md](DAG.md) | Milestone dependency graph |
| [PDR.md](PDR.md) | Project Design Review — scope, risks, decisions |

## Detailed Engine Documentation

For comprehensive system design, API reference, and implementation details, see [engine/README.md](engine/README.md).

This contains 30 sections organized by tier (architecture → built systems → gameplay → future):
- **Sections 00–06**: Architecture, core systems, RHI, shader pipeline (built/in-progress)
- **Sections 07–12**: Resource system, ECS, scene, rendering (next phase)
- **Sections 13–21**: Input, physics, audio, UI, animation, gameplay (foundation systems)
- **Sections 22–30**: Developer tools, localization, networking, **VFX, AI content pipeline, platform services**, IPC, examples

## Technical Specs

Detailed specifications for each milestone, located in [specs/](specs/):

| Spec | Milestone | Topic |
|------|-----------|-------|
| [1.0-bootstrap](specs/1.0-bootstrap.md) | 1.0 | Project bootstrap, build system, core libraries |
| [1.1-MATH-CONTAINERS](specs/1.1-MATH-CONTAINERS.md) | 1.1 | Math types (Vec2–4, Mat4, Quat, Transform) and containers |
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
| [INDEX.md](specs/INDEX.md) | — | Spec index and status tracker |

## Benchmarks

| Document | Description |
|----------|-------------|
| [BENCHMARK_COMPILERS.md](benchmarks/BENCHMARK_COMPILERS.md) | Compiler comparison (Zig/Clang/MSVC) — build times, binary size, runtime perf |

## Observability

| Document | Description |
|----------|-------------|
| [observability-cpp.md](observability/observability-cpp.md) | C++ logging, profiling, assertions (UZ_LOG, Tracy, UZ_CHECK) |
| [observability-ts.md](observability/observability-ts.md) | TypeScript extension observability |

## User Documentation

Located in [user/](user/):

### Getting Started

- [Overview](user/getting-started/overview.md) — What is UzEngine, key concepts
- [Building](user/getting-started/building.md) — Build from source
- [First Window](user/getting-started/first-window.md) — Create your first render window

### Guides

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
