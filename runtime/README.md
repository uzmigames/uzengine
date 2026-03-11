# @uzmigames/runtime

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Zig](https://img.shields.io/badge/Zig-0.15-orange.svg)]()

UzEngine core runtime — the engine itself. Contains rendering (RHI, deferred pipeline, post-processing), ECS, physics, audio, input, and all gameplay systems.

**This is a standalone C++ library with zero Node/N-API dependency.** It compiles as a static library consumed by [`napi/`](../napi/) for VSCode integration, or by CLI tools and standalone game executables.

## Build

```bash
# Release
zig build -Doptimize=ReleaseFast

# Debug
zig build

# Output: zig-out/bin/uzengine.exe
```

## Architecture

The runtime exposes a **C API** (`extern "C"`) so it can be consumed by any language or build system:

```c
// uz_runtime.h
const char* uz_runtime_version(void);
const char* uz_runtime_backend(void);
int         uz_runtime_init(void);
void        uz_runtime_shutdown(void);
```

As the engine grows, subsystem APIs (RHI, ECS, Scene, etc.) will follow the same pattern — C API surface, C++ implementation.

## Directory Structure

```
runtime/
├── build.zig                      # Zig build system (compiles C++ via zig cc)
├── build.zig.zon                  # Zig package manifest
├── .clang-format                  # Code formatting rules
├── .clang-tidy                    # Linting rules (warnings-as-errors)
├── src/
│   ├── uz_runtime.h               # Public C API
│   └── uz_runtime.cpp             # Implementation
├── core/
│   ├── math/                      # SIMD math (Vec2/Vec3/Vec4, Mat4, Quat, Transform)
│   ├── geometry/                  # Geometry types (Plane, AABB, Frustum)
│   ├── containers/                # Data structures (RingBuffer, FreeList, HandleMap, FrameArray)
│   ├── memory/                    # Memory management (Own<T>, Ref<T>, allocators)
│   ├── random/                    # PCG random number generator
│   ├── logging/                   # Structured logging via spdlog
│   ├── assertions/                # UZ_CHECK, UZ_ENSURE, UZ_VERIFY macros
│   ├── profiling/                 # Tracy CPU profiler integration
│   └── config/                    # .uzproject JSON configuration
├── platform/
│   └── windows/                   # Win32 window, input system
├── tests/
│   └── core/                      # 160+ unit tests (27 test suites)
└── thirdparty/                    # All dependencies committed to repo
    ├── spdlog/
    ├── tracy/
    ├── directxmath/
    ├── d3d12ma/
    ├── atomic_queue/
    ├── spirv-reflect/
    ├── xxhash/
    ├── sse_mathfun/
    └── [others...]
```

## Milestones

| Milestone | Status | Features |
|-----------|--------|----------|
| **1.0** | ✅ Complete | Logging, assertions, profiling, memory, platform window, input, config |
| **1.1** | ✅ Complete | Math (SIMD), Geometry, Color space, Containers, Allocators, Random (160 tests) |
| **1.2** | ⏳ Pending | RHI abstraction, DX12 SM6.6 backend, swapchain, frame pacing |
| **1.3** | 📋 Planned | Renderer (deferred, post-processing, culling) |
| **1.4** | 📋 Planned | Scene system (ECS, transforms, serialization) |
| **1.5** | 📋 Planned | Gameplay (physics, animation, audio, scripting) |

## Code Quality

- **160 tests passing** across 27 test suites (6ms execution)
- **100% Rust-style documentation** (`///` comments on all public APIs)
- **Zero compiler warnings** via clang-tidy (warnings-as-errors mode)
- **clang-format** applied to all source files
- **95%+ test coverage** required for all new code
- **≤1500 lines per file** (single responsibility principle)

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
