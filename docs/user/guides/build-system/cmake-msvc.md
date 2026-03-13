# Build System — CMake / MSVC

CMake build using Visual Studio 2022 (MSVC) on Windows.

> **File**: `runtime/CMakeLists.txt` + `runtime/CMakePresets.json`

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| [CMake](https://cmake.org/) | 3.21+ | Build system generator |
| [Visual Studio 2022](https://visualstudio.microsoft.com/) | 17.x | MSVC compiler (C++17) |
| Windows SDK | 10.0.19041+ | DX12 headers and libraries |

## Quick Start

```bash
cd runtime
cmake --preset msvc-debug
cmake --build build/msvc-debug
```

## Available Presets

| Preset | Generator | Build Type | Output Directory |
|--------|-----------|------------|------------------|
| `msvc-debug` | Visual Studio 17 2022 | Debug | `build/msvc-debug/` |
| `msvc-release` | Visual Studio 17 2022 | Release | `build/msvc-release/` |

## Build Commands

```bash
# Configure
cmake --preset msvc-debug

# Build (Debug)
cmake --build build/msvc-debug

# Build (Release)
cmake --preset msvc-release
cmake --build build/msvc-release --config Release

# Run tests
cd build/msvc-debug/Debug
./test_runtime.exe
```

## Compiler Flags

MSVC-specific flags set in `CMakeLists.txt`:

```cmake
add_compile_options(/W4 /permissive- /utf-8)
```

| Flag | Purpose |
|------|---------|
| `/W4` | Warning level 4 (almost all warnings) |
| `/permissive-` | Strict C++ conformance mode |
| `/utf-8` | Source and execution character set UTF-8 |

D3D12MA has per-file suppressions:
```cmake
set_source_files_properties(engine/rhi/dx12/dx12_d3d12ma.cpp
    PROPERTIES COMPILE_OPTIONS "/wd4267;/wd4244"
)
```

## Build Output

All executables are built to `build/msvc-debug/Debug/` (or `Release/`):

| Target | Description |
|--------|-------------|
| `uzengine_runtime.lib` | Static library (all engine code) |
| `test_runtime.exe` | GoogleTest runner |
| `uzengine.exe` | Standalone runtime |
| `uzcook.exe` | Asset cooker CLI |
| `clear_screen_demo.exe` | DX12 clear screen demo |
| `triangle_demo.exe` | DX12 triangle demo |

## DLL Deployment

Post-build commands automatically copy runtime DLLs next to each executable:

```
<target_dir>/
├── D3D12/
│   ├── D3D12Core.dll          # Agility SDK
│   └── d3d12SDKLayers.dll     # Debug layer
├── dxcompiler.dll             # DXC shader compiler
└── dxil.dll                   # DXIL validator
```

## Source Discovery

CMake uses `file(GLOB)` for automatic source discovery — no manual file lists to maintain:

```cmake
file(GLOB SRC_LOGGING     engine/logging/*.cpp)
file(GLOB SRC_SHADER      engine/shader/*.cpp)
file(GLOB SRC_ASSET       engine/asset/*.cpp)
file(GLOB SRC_ECS         engine/ecs/*.cpp)
file(GLOB SRC_JOB         engine/job/*.cpp)
# ... etc
```

**Important**: After adding new `.cpp` files, re-run `cmake --preset msvc-debug` to pick them up (GLOB is evaluated at configure time, not build time).

## Windows-Only Source Handling

Windows-specific sources (DX12 backend, platform/windows) are wrapped with generator expressions:

```cmake
add_library(uzengine_runtime STATIC
    ${RUNTIME_SOURCES}
    $<$<PLATFORM_ID:Windows>:${WINDOWS_SOURCES}>
)
```

Test files with `test_dx12_` or `test_input` prefixes are also Windows-only.

## VS Solution Structure

The generated Visual Studio solution contains these projects:

| Project | Type |
|---------|------|
| `uzengine_runtime` | Static Library |
| `gtest` | Static Library |
| `test_runtime` | Executable |
| `uzengine` | Executable |
| `uzcook` | Executable |
| `clear_screen_demo` | Executable |
| `triangle_demo` | Executable |

Open `build/msvc-debug/uzengine_runtime.sln` in Visual Studio for IDE debugging.

## Troubleshooting

**`error LNK2019: unresolved external symbol`** — Likely a new `.cpp` file was added but CMake wasn't reconfigured. Run `cmake --preset msvc-debug` again.

**D3D12 debug layer crashes** — Install the "Graphics Tools" optional feature in Windows Settings > Optional Features.

**`/W4` warnings in thirdparty code** — These are suppressed per-file via `set_source_files_properties`. If a new thirdparty file needs suppression, add it in `CMakeLists.txt`.

## Related Pages

- [Zig Build](zig-build.md) — Primary build system
- [Clang Build](cmake-clang.md) — CMake with Clang-CL
- [Building from Source](../../getting-started/building.md) — Quick start
