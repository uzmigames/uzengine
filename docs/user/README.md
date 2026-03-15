# UzEngine — User Documentation

Developer-facing documentation for users of UzEngine: getting started guides, system guides, and API reference.

> **Start here**: [index.md](index.md) for the full navigation index.

## Getting Started

| Page | Description |
|------|-------------|
| [getting-started/overview.md](getting-started/overview.md) | Architecture, design goals, and how UzEngine differs from traditional engines |
| [getting-started/building.md](getting-started/building.md) | Prerequisites, dependencies, and build instructions |
| [getting-started/first-window.md](getting-started/first-window.md) | Create a window with a DX12 clear-color render loop |

## Guides

| Guide | Description |
|-------|-------------|
| [guides/architecture.md](guides/architecture.md) | Three-layer design (Extension → Runtime → MCP), process model |
| [guides/lua-scripting.md](guides/lua-scripting.md) | Lua scripting quick start, API reference, examples, best practices |
| [guides/build-system/zig-build.md](guides/build-system/zig-build.md) | Zig build system, cross-compilation, platform guards |
| [guides/build-system/cmake-msvc.md](guides/build-system/cmake-msvc.md) | CMake with Visual Studio 2022 |
| [guides/build-system/cmake-clang.md](guides/build-system/cmake-clang.md) | CMake with Clang-CL and Ninja |
| [guides/rhi/overview.md](guides/rhi/overview.md) | Backend abstraction, device creation, resource management |
| [guides/math/overview.md](guides/math/overview.md) | Vectors, matrices, quaternions, transforms (DirectXMath SIMD) |
| [guides/memory/overview.md](guides/memory/overview.md) | Ownership model, linear/pool/frame allocators, budgets |
| [guides/platform/overview.md](guides/platform/overview.md) | Window creation, input system, OS services |
| [guides/rendering/overview.md](guides/rendering/overview.md) | Frame lifecycle, shader pipeline, DX12 RHI |
| [guides/rendering/shader-pipeline.md](guides/rendering/shader-pipeline.md) | Development workflow, hot-reload, shader caching |
| [guides/rendering/gbuffer-layout.md](guides/rendering/gbuffer-layout.md) | GBuffer layout and deferred rendering data |
| [guides/profiling/overview.md](guides/profiling/overview.md) | Tracy, GPU profiling, DRED breadcrumbs, stat counters |

## API Reference

| Module | Description |
|--------|-------------|
| [reference/core/index.md](reference/core/index.md) | Logging, assertions, config, profiling |
| [reference/core/paths.md](reference/core/paths.md) | Path resolution API |
| [reference/math/index.md](reference/math/index.md) | Vec2, Vec3, Vec4, Mat4, Quat, Transform, Color, AABB, Frustum |
| [reference/containers/index.md](reference/containers/index.md) | RingBuffer, FreeList, HandleMap, FrameArray |
| [reference/rhi/index.md](reference/rhi/index.md) | IRHIDevice, IRHICommandList, IRHISwapChain, handles |
| [reference/rhi/rhi-config.md](reference/rhi/rhi-config.md) | RHI configuration options |
| [reference/shader/index.md](reference/shader/index.md) | ShaderVFS, ShaderCompiler, ShaderCache, ShaderPipeline |
| [reference/asset/index.md](reference/asset/index.md) | VFS, importers, cache, registry, .uzpak archive |
| [reference/renderer/index.md](reference/renderer/index.md) | Camera, vertex formats, UploadContext |
| [reference/platform/index.md](reference/platform/index.md) | PlatformWindow, InputSystem, action mapping |
| [reference/tools/index.md](reference/tools/index.md) | uzembed, uzcook tool reference |
| [reference/tools/uzcook.md](reference/tools/uzcook.md) | uzcook asset cooker CLI reference |
| [reference/tools/uzembed.md](reference/tools/uzembed.md) | uzembed shader embed tool reference |

## Contributing

| Page | Description |
|------|-------------|
| [contributing/code-style.md](contributing/code-style.md) | Naming, formatting, documentation, SOLID principles |
| [contributing/building-and-testing.md](contributing/building-and-testing.md) | Build targets, test runner, coverage requirements |
| [contributing/project-structure.md](contributing/project-structure.md) | Directory layout, file responsibilities, where to add new code |
