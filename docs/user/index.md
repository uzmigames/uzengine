# UzEngine Documentation

Welcome to the [UzEngine](https://github.com/uzmigames/uzengine) developer documentation.
UzEngine is a AAA-quality game engine designed for autonomous AI-assisted development,
embedded inside Visual Studio Code.

> **Current version**: 0.2.0 — Runtime render foundation (DX12 SM6.6 bindless)
> **Repository**: https://github.com/uzmigames/uzengine

---

## Getting Started

New to UzEngine? Start here. These guides walk you through setup, your first build,
and a working render window — step by step.

| Page | Description |
|------|-------------|
| [Overview](getting-started/overview.md) | Architecture, design goals, and how UzEngine differs from traditional engines |
| [Building from Source](getting-started/building.md) | Prerequisites, dependencies, and build instructions |
| [Your First Window](getting-started/first-window.md) | Create a window with a DX12 clear-color render loop |

## Guides

Topic-based guides for each engine system. Each guide explains concepts, shows usage
patterns, and links to the relevant API reference.

| Topic | Description |
|-------|-------------|
| [Architecture](guides/architecture.md) | Three-layer design (Extension → Runtime → MCP), process model |
| [Build System](guides/build-system/zig-build.md) | Zig build system, cross-compilation, platform guards |
| [RHI — Render Hardware Interface](guides/rhi/overview.md) | Backend abstraction, device creation, resource management |
| [Math Library](guides/math/overview.md) | Vectors, matrices, quaternions, transforms (DirectXMath SIMD) |
| [Memory & Allocators](guides/memory/overview.md) | Ownership model, linear/pool/frame allocators, budgets |
| [Platform Abstraction](guides/platform/overview.md) | Window creation, input system, OS services |
| [Rendering](guides/rendering/overview.md) | Frame lifecycle, shader pipeline, DX12 RHI |
| [Shader Pipeline](guides/rendering/shader-pipeline.md) | Development workflow, hot-reload, shader caching |
| [Profiling & Debugging](guides/profiling/overview.md) | Tracy, GPU profiling, DRED breadcrumbs, stat counters |

## API Reference

Exhaustive reference for every public type, function, and enum — organized by module.
Generated from source-level `///` documentation comments.

| Module | Description |
|--------|-------------|
| [Core](reference/core/index.md) | Logging, assertions, config, profiling, [paths](reference/core/paths.md) |
| [Math](reference/math/index.md) | Vec2, Vec3, Vec4, Mat4, Quat, Transform, Color, AABB, Frustum |
| [Containers](reference/containers/index.md) | RingBuffer, FreeList, HandleMap, FrameArray |
| [RHI](reference/rhi/index.md) | IRHIDevice, IRHICommandList, IRHISwapChain, handles, [config](reference/rhi/rhi-config.md) |
| [Platform](reference/platform/index.md) | PlatformWindow, InputSystem, action mapping |

## Contributing

| Page | Description |
|------|-------------|
| [Code Style](contributing/code-style.md) | Naming, formatting, documentation, SOLID principles |
| [Building & Testing](contributing/building-and-testing.md) | Build targets, test runner, coverage requirements |
| [Project Structure](contributing/project-structure.md) | Directory layout, file responsibilities, where to add new code |

---

## Documentation Conventions

- **Guide pages** explain *why* and *how* — they link to Reference pages for exact signatures.
- **Reference pages** list *every* public symbol — they link back to Guides for context.
- Code examples are complete and compilable, not fragments.
- Prerequisites are listed at the top of each page when prior reading is assumed.

## Version History

See [CHANGELOG](../../runtime/CHANGELOG.md) for detailed release notes.
