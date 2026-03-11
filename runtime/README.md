# @uzmigames/runtime

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)]()
[![Zig](https://img.shields.io/badge/Zig-0.15-orange.svg)]()
[![MSVC](https://img.shields.io/badge/MSVC-17.14-blue.svg)]()
[![Clang](https://img.shields.io/badge/Clang-21-green.svg)]()

UzEngine core runtime — the engine itself. Contains rendering (RHI, deferred pipeline, post-processing), ECS, physics, audio, input, and all gameplay systems.

**This is a standalone C++ library with zero Node/N-API dependency.** It compiles as a static library consumed by [`napi/`](../napi/) for VSCode integration, or by CLI tools and standalone game executables.

## Build

Three compilers are supported. Use `scripts/` for all operations.

```bash
# Zig (default, recommended)
scripts/build.ps1                        # or: cd runtime && zig build
scripts/build.ps1 --compiler=msvc        # MSVC via CMake
scripts/build.ps1 --compiler=clang       # Clang via CMake
```

Output goes to `runtime/build/`:
- Zig: `build/bin/`
- CMake: `build/<preset>/Debug/` (or `Release/`)

## Test

```bash
scripts/test.ps1                         # Zig (default)
scripts/test.ps1 --compiler=msvc         # MSVC
scripts/test.ps1 --compiler=clang        # Clang
```

## Lint & Format

```bash
scripts/lint.ps1                         # clang-tidy on all source dirs
scripts/format.ps1                       # clang-format on all source files
scripts/format.ps1 --check               # CI mode (check only, no write)
```

## Architecture

The runtime exposes a **C API** (`extern "C"`) so it can be consumed by any language or build system:

```c
// runtime.h
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
├── CMakeLists.txt                 # CMake build (MSVC, Clang)
├── CMakePresets.json              # CMake presets (msvc-debug/release, clang-debug/release)
├── .clang-format                  # Code formatting rules
├── .clang-tidy                    # Linting rules (warnings-as-errors)
├── src/
│   ├── runtime.h                  # Public C API
│   └── runtime.cpp                # Implementation
├── core/
│   ├── math/                      # SIMD math (Vec2/Vec3/Vec4, Mat4, Quat, Transform)
│   ├── containers/                # Data structures (RingBuffer, FreeList, HandleMap, FrameArray)
│   ├── memory/                    # Memory management (Own<T>, Ref<T>, allocators)
│   ├── random/                    # PCG random number generator
│   ├── logging/                   # Structured logging via spdlog
│   ├── assertions/                # UZ_CHECK, UZ_ENSURE, UZ_VERIFY macros
│   ├── profiling/                 # Tracy CPU profiler integration
│   ├── config/                    # .uzproject JSON configuration
│   ├── rhi/                       # Render Hardware Interface (DX12 backend)
│   ├── shader/                    # Shader pipeline (VFS, cache, compiler, hot-reload)
│   ├── paths/                     # Exe-relative resource resolution
│   └── renderer/                  # GPU upload context
├── platform/
│   └── windows/                   # Win32 window, input system
├── demos/
│   ├── clear_screen/              # Animated clear color (6069 FPS on RTX 4090)
│   └── triangle/                  # SM6.6 bindless triangle
├── tools/
│   └── uzengine.cpp               # Standalone runtime executable
├── tests/
│   └── core/                      # 444 unit tests (64 test suites)
└── thirdparty/                    # All dependencies committed to repo
```

## Milestones

| Milestone | Status | Features |
|-----------|--------|----------|
| **1.0** | ✅ Complete | Logging, assertions, profiling, memory, platform window, input, config |
| **1.1** | ✅ Complete | Math (SIMD), Geometry, Color space, Containers, Allocators, Random |
| **1.2** | ✅ Complete | RHI abstraction, DX12 SM6.6 backend, swapchain, clear screen demo |
| **1.3** | ⏳ In Progress | Triangle demo, buffer upload, shader pipeline |
| **1.4** | 📋 Planned | Asset pipeline (importers, DDC cache, .uzpak, VFS) |

## Code Quality

- **444 tests passing** across 64 test suites
- **Three compilers**: Zig (Clang/MinGW), MSVC, Clang (clang-cl)
- **100% Rust-style documentation** (`///` comments on all public APIs)
- **Zero compiler warnings** via clang-tidy (warnings-as-errors mode)
- **clang-format** applied to all source files
- **95%+ test coverage** required for all new code
- **≤1500 lines per file** (single responsibility principle)

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
