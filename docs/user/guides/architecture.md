# Architecture

UzEngine is a three-layer system: a VSCode extension (TypeScript), a C++ runtime
loaded as a native N-API addon, and an MCP server for AI integration.

## Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                  VSCode Extension (TypeScript)                  │
│  ┌──────────┐ ┌───────────┐ ┌──────────┐ ┌──────────────────┐  │
│  │ Viewport │ │ Inspector │ │ Hierarchy│ │  Asset Browser   │  │
│  │ (webview)│ │ (webview) │ │ (webview)│ │  (webview)       │  │
│  └──────────┘ └───────────┘ └──────────┘ └──────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                 Runtime (C++ / N-API .node addon)               │
│  ┌─────┐ ┌──────────┐ ┌─────────┐ ┌───────┐ ┌──────────────┐  │
│  │ RHI │ │ Renderer │ │ Physics │ │ Audio │ │    Scene/ECS  │  │
│  │     │ │          │ │  (Jolt) │ │       │ │    (EnTT)     │  │
│  └──┬──┘ └──────────┘ └─────────┘ └───────┘ └──────────────┘  │
│     │ DX12 | Vulkan | Metal                                    │
├─────┼───────────────────────────────────────────────────────────┤
│     │           MCP Server (Node.js)                            │
│     │  AI tools, content generation, scene manipulation         │
└─────┼───────────────────────────────────────────────────────────┘
      ▼
   ┌──────┐
   │  GPU │
   └──────┘
```

## Process Model

The runtime runs **in-process** with the VSCode extension (via N-API). No separate
process, no IPC overhead. The 3D viewport renders to its own native HWND with a DX12
swapchain — no frame streaming for the main viewport.

2D editor panels (inspector, hierarchy, asset browser) use webview panels.
When these need a live preview, H.264 frame streaming delivers 120 FPS via
Media Foundation + WebCodecs.

## Directory Structure

```
runtime/
├── src/                     # Runtime C API (runtime.h/.cpp)
├── core/
│   ├── logging/             # log.h — spdlog-backed structured logging
│   ├── assertions/          # engine_assert.h — UZ_CHECK, UZ_ENSURE, UZ_VERIFY
│   ├── profiling/           # profiler.h — Tracy integration, stat counters
│   ├── memory/              # engine_memory.h — Own<T>/Ref<T>, allocators.h
│   ├── config/              # config.h — .uzproject JSON + CLI overrides
│   ├── math/                # engine_math.h — DirectXMath SIMD wrappers
│   ├── containers/          # Ring buffer, free list, handle map, frame array
│   ├── random/              # random.h — PCG generator
│   ├── rhi/                 # Abstract interfaces (rhi.h, rhi_types.h)
│   │   └── dx12/            # DX12 backend implementation
│   ├── shader/              # Shader pipeline (VFS, compiler, cache, hot-reload)
│   │   ├── shader_vfs.h           # Virtual filesystem for shader sources
│   │   ├── shader_source_manager.h # Include parsing, content hashing
│   │   ├── shader_dependency_graph.h # Forward/reverse include tracking
│   │   ├── shader_cache.h         # Disk-based bytecode cache
│   │   ├── shader_compiler.h      # DXC integration (HLSL→DXIL/SPIRV)
│   │   ├── shader_permutation.h   # Feature flag variants
│   │   ├── shader_hot_reload.h    # FileWatcher + dirty flags
│   │   └── shader_archive.h       # Shipping mode precompiled archive
│   └── paths/               # Path resolution (paths.h)
├── platform/
│   └── windows/             # Win32 window, input system
├── tools/                   # Standalone executables (runtime, demo)
├── tests/                   # GoogleTest suites (mirrors source structure)
├── thirdparty/              # All third-party dependencies (git-tracked)
├── build.zig                # Zig build system
└── CHANGELOG.md             # Version history
```

## Dependency Flow

Dependencies flow downward. No circular references.

```
tools/ ──→ core/rhi/ ──→ core/shader/ ──→ core/ (logging, assertions, math, config)
             │
             ├──→ core/rhi/dx12/  (DX12 backend)
             └──→ core/rhi/vulkan/ (planned)

core/shader/ ──→ core/paths/ (resource resolution)
             ──→ core/ (VFS, source loading, DXC integration)

platform/windows/ ──→ core/paths/ (path resolution)
                  ──→ core/ (logging, input)

tests/ ──→ everything (read-only, via link against static lib)
```

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| N-API in-process, not IPC | POC 0.2 proved IPC adds unacceptable latency for frame-rate-sensitive ops |
| Own HWND for 3D viewport | WebGL/webview cannot match native DX12 quality; frame streaming adds latency |
| Zig + CMake build systems | Zig (primary) + CMake (MSVC/Clang). Three verified compilers: Zig, MSVC, Clang |
| SM6.6 bindless primary | One global descriptor heap, no descriptor table management, simpler shaders |
| Agility SDK bundled | Unlocks latest DX12 features regardless of OS-installed runtime |
| JSON for all project files | Human-readable, AI-parseable, diff-friendly |
| Content-addressed shader cache | xxHash64 of source + includes eliminates stale cache bugs during iteration |
| Exe-relative path resolution | All resources (DLLs, shaders, configs) relative to executable, never CWD |
| Virtual shader filesystem | Mount points decouple shader code from filesystem, enables mods/overrides |
| Lazy shader compilation | Mark dirty on file change, compile on next use (batches rapid saves) |

## Related Pages

- [RHI Overview](rhi/overview.md) — Hardware abstraction layer design
- [Shader Pipeline](rendering/shader-pipeline.md) — Compilation, caching, hot-reload
- [Build System](build-system/zig-build.md) — Zig build details
- [Building from Source](../getting-started/building.md) — Quick start
- [API: Core Reference](../reference/core/index.md) — Logging, paths, config
