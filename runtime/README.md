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

# Output: zig-out/lib/uzengine_runtime.lib
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
├── build.zig          # Zig build system (compiles C++ via zig cc)
├── build.zig.zon      # Zig package manifest
└── src/
    ├── uz_runtime.h   # Public C API
    └── uz_runtime.cpp # Implementation
```

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
