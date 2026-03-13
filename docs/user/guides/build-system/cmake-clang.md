# Build System — CMake / Clang

CMake build using Clang-CL (LLVM) with Ninja generator on Windows.

> **File**: `runtime/CMakeLists.txt` + `runtime/CMakePresets.json`

## Prerequisites

| Tool | Version | Purpose |
|------|---------|---------|
| [CMake](https://cmake.org/) | 3.21+ | Build system generator |
| [LLVM/Clang](https://releases.llvm.org/) | 15+ | Clang-CL compiler (MSVC-compatible) |
| [Ninja](https://ninja-build.org/) | 1.11+ | Fast build system |
| Windows SDK | 10.0.19041+ | DX12 headers and libraries |

Clang-CL must be on your PATH. Install via Visual Studio Installer ("C++ Clang tools for Windows") or standalone LLVM.

## Quick Start

```bash
cd runtime
cmake --preset clang-debug
cmake --build build/clang-debug
```

## Available Presets

| Preset | Generator | Compiler | Build Type | Output Directory |
|--------|-----------|----------|------------|------------------|
| `clang-debug` | Ninja | `clang-cl` | Debug | `build/clang-debug/` |
| `clang-release` | Ninja | `clang-cl` | Release | `build/clang-release/` |

## Build Commands

```bash
# Configure
cmake --preset clang-debug

# Build
cmake --build build/clang-debug

# Release
cmake --preset clang-release
cmake --build build/clang-release

# Run tests
cd build/clang-debug
./test_runtime.exe
```

## Compiler Flags

Clang-specific flags set in `CMakeLists.txt`:

```cmake
add_compile_options(-Wall -Wextra)
```

D3D12MA has per-file suppressions for upstream warnings:
```cmake
set_source_files_properties(engine/rhi/dx12/dx12_d3d12ma.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-switch;-Wno-unused-private-field"
)
```

## Why Clang-CL?

| Advantage | Description |
|-----------|-------------|
| MSVC ABI compatible | Links against Windows SDK and MSVC libraries |
| Better diagnostics | Clearer error messages than MSVC |
| Sanitizer support | AddressSanitizer, UBSan via `-fsanitize=` |
| Ninja generator | Faster incremental builds than VS generator |
| Cross-validation | Catches bugs that MSVC ignores (strict standard conformance) |

## Build Output

Same targets as MSVC, output to `build/clang-debug/`:

| Target | Description |
|--------|-------------|
| `uzengine_runtime.lib` | Static library |
| `test_runtime.exe` | GoogleTest runner |
| `uzengine.exe` | Standalone runtime |
| `uzcook.exe` | Asset cooker CLI |
| `clear_screen_demo.exe` | DX12 clear screen |
| `triangle_demo.exe` | DX12 triangle demo |

## DLL Deployment

Same as MSVC — post-build copies Agility SDK and DXC DLLs next to each executable.

## Troubleshooting

**`clang-cl: not found`** — Ensure LLVM bin directory is on PATH. Check with `clang-cl --version`.

**Ninja not found** — Install via `winget install Ninja-build.Ninja` or download from [ninja-build.org](https://ninja-build.org/).

**`-Wunused-private-field` in D3D12MA** — Already suppressed per-file. If new thirdparty warnings appear, add suppressions in `CMakeLists.txt`.

**Link errors with Windows SDK** — Clang-CL uses the MSVC linker (`lld-link` or `link.exe`). Ensure Visual Studio C++ build tools are installed.

## Related Pages

- [Zig Build](zig-build.md) — Primary build system
- [MSVC Build](cmake-msvc.md) — CMake with MSVC
- [Building from Source](../../getting-started/building.md) — Quick start
