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
├── src/                     # Runtime C API (uz_runtime.h/.cpp)
├── core/
│   ├── logging/             # uz_log.h — spdlog-backed structured logging
│   ├── assertions/          # uz_assert.h — UZ_CHECK, UZ_ENSURE, UZ_VERIFY
│   ├── profiling/           # uz_profiler.h — Tracy integration, stat counters
│   ├── memory/              # uz_memory.h — Own<T>/Ref<T>, uz_allocators.h
│   ├── config/              # uz_config.h — .uzproject JSON + CLI overrides
│   ├── math/                # uz_math.h — DirectXMath SIMD wrappers
│   ├── containers/          # Ring buffer, free list, handle map, frame array
│   ├── random/              # uz_random.h — PCG generator
│   └── rhi/                 # Abstract interfaces (rhi.h, rhi_types.h)
│       └── dx12/            # DX12 backend implementation
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
tools/ ──→ core/rhi/ ──→ core/ (logging, assertions, math, config)
             │
             ├──→ core/rhi/dx12/  (DX12 backend)
             └──→ core/rhi/vulkan/ (planned)

platform/windows/ ──→ core/ (logging, input)
tests/ ──→ everything (read-only, via link against static lib)
```

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| N-API in-process, not IPC | POC 0.2 proved IPC adds unacceptable latency for frame-rate-sensitive ops |
| Own HWND for 3D viewport | WebGL/webview cannot match native DX12 quality; frame streaming adds latency |
| Zig build system | Single toolchain for C++17 + cross-compilation, no CMake/MSVC dependency |
| SM6.6 bindless primary | One global descriptor heap, no descriptor table management, simpler shaders |
| Agility SDK bundled | Unlocks latest DX12 features regardless of OS-installed runtime |
| JSON for all project files | Human-readable, AI-parseable, diff-friendly |

## Related Pages

- [RHI Overview](rhi/overview.md) — Hardware abstraction layer design
- [Build System](build-system/zig-build.md) — Zig build details
- [Building from Source](../getting-started/building.md) — Quick start
