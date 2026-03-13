# Build Toolchain — Thirdparty Library Catalog

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/thirdparty/`, `runtime/build.zig`
> **Depends on**: Zig build system

## Purpose

UzEngine vendors all thirdparty dependencies as Git submodules under `runtime/thirdparty/`. No package manager (vcpkg, Conan, Hunter) — every dependency is compiled from source by `build.zig`. This gives:

1. **Reproducible builds** — exact same source on every machine, no version resolution at build time
2. **Compile-time control** — engine-specific defines (e.g., `SPDLOG_COMPILED_LIB`, `TRACY_ON_DEMAND`) applied uniformly
3. **Cross-compilation** — all deps compile with `zig cc`, no platform-specific build scripts
4. **No binary blobs** — except DXC and Agility SDK, which are pre-built Microsoft binaries

## Library Inventory

### Core Infrastructure

| Library | Version | Purpose | Compiled | Flags |
|---------|---------|---------|----------|-------|
| **spdlog** | 1.x | Structured logging backend (console + rotating file sinks) | Yes (7 `.cpp` files) | `-DSPDLOG_COMPILED_LIB` |
| **Tracy** | 0.11+ | CPU profiling (instrumentation + viewer) | Yes (`TracyClient.cpp`) | `-DTRACY_ENABLE -DTRACY_ON_DEMAND` |
| **mimalloc** | 2.x | High-performance memory allocator (Microsoft) | Header-only integration | — |
| **nlohmann/json** | 3.x | JSON parsing for config/project files | Header-only | — |
| **xxhash** | 0.8+ | Content-addressed hashing (shader cache keys) | Header-only (`XXH_INLINE_ALL`) | — |
| **GoogleTest** | 1.14+ | Unit testing framework | Yes (`gtest-all.cc`) | `-DGTEST_HAS_PTHREAD=0` |

### Math & SIMD

| Library | Version | Purpose | Compiled | Notes |
|---------|---------|---------|----------|-------|
| **DirectXMath** | 3.x | SIMD math (XMVECTOR, XMMATRIX, XMFLOAT4X4) | Header-only | Primary math backend |
| **GLM** | 0.9.9+ | OpenGL Mathematics (secondary reference) | Header-only | Used sparingly alongside DirectXMath |
| **sse_mathfun** | RJVB fork | SIMD sin/cos/exp/log for SSE | Header-only | Fork of nemequ's archived original |

### Graphics & GPU

| Library | Version | Purpose | Compiled | Notes |
|---------|---------|---------|----------|-------|
| **D3D12MA** | 3.1.0 | GPU memory allocation (Microsoft) | Yes (`dx12_d3d12ma.cpp`) | Special flags: `-fno-sanitize=undefined` |
| **directx-headers** | SDK 614 | DX12 headers for Clang/MinGW | Header-only | Overrides Zig's MinGW d3d12.h (SDK 602) |
| **Agility SDK** | 1.614+ | DX12 runtime + debug layers | **Pre-built DLLs** | Deployed to `build/bin/D3D12/` |
| **DXC** | Latest | DirectX Shader Compiler | **Pre-built DLLs** | `dxcompiler.dll` + `dxil.dll` |
| **SPIRV-Reflect** | Latest | Shader binding reflection (SPIR-V) | Yes (`spirv_reflect.c`, C11) | Auto-extracts resource bindings |

### Texture & Image

| Library | Version | Purpose | Compiled | Notes |
|---------|---------|---------|----------|-------|
| **stb** | Latest | stb_image (load), stb_image_write (save), stb_image_resize2 | Header-only | Ubiquitous single-header libraries |
| **tinyexr** | Latest | EXR/HDR image loading | Header-only + `miniz.c` | miniz compiled as C11 |
| **basis_universal** | Latest | Basis/KTX2 texture transcoder | Yes (`basisu_transcoder.cpp`) | `-DBASISD_SUPPORT_KTX2_ZSTD=0` |
| **astcenc** | Latest | ASTC texture compression | Yes (22 `.cpp` files) | arm-astcenc source library |
| **libjpeg-turbo** | Latest | JPEG encode/decode (SIMD-accelerated) | Header-only integration | Used for frame streaming |

### Containers & Concurrency

| Library | Version | Purpose | Compiled | Notes |
|---------|---------|---------|----------|-------|
| **atomic_queue** | Latest | Lock-free MPMC queue | Header-only | For async task submission |

### Mesh Processing

| Library | Version | Purpose | Compiled | Notes |
|---------|---------|---------|----------|-------|
| **mikktspace** | Latest | Tangent space calculation (MikkT standard) | Linked when needed | Industry-standard tangent generation |

### Allocators (Alternative)

| Library | Version | Purpose | Status | Notes |
|---------|---------|---------|--------|-------|
| **dlmalloc** | Latest | Doug Lea's malloc | Available, not yet integrated | Fallback allocator option |
| **jemalloc** | Latest | Facebook's malloc | Available, not yet integrated | Alternative to mimalloc |

### Package Management

| Library | Version | Purpose | Status |
|---------|---------|---------|--------|
| **vcpkg** | Latest | Microsoft package manager | Available for future deps requiring complex builds |

## Compilation Strategy

### Compiled-from-Source Libraries

Libraries that need engine-specific defines or produce object code are compiled directly in `build.zig`:

```zig
// spdlog — 7 source files, compiled as static library
runtime_mod.addCSourceFiles(.{
    .root = b.path("thirdparty/spdlog/src"),
    .files = &.{
        "spdlog.cpp", "async.cpp", "cfg.cpp",
        "color_sinks.cpp", "file_sinks.cpp",
        "stdout_sinks.cpp", "bundled_fmtlib_format.cpp",
    },
    .flags = cpp_flags,
});

// Tracy — single compilation unit with feature defines
runtime_mod.addCSourceFiles(.{
    .root = b.path("thirdparty/tracy/public"),
    .files = &.{"TracyClient.cpp"},
    .flags = &.{ "-std=c++17", "-DTRACY_ENABLE", "-DTRACY_ON_DEMAND" },
});
```

### Header-Only Libraries

Most libraries are header-only — they appear only in the include path:

```zig
const thirdparty_include_dirs = [_][]const u8{
    "thirdparty/spdlog/include",
    "thirdparty/tracy/public",
    "thirdparty/mimalloc/include",
    "thirdparty/directxmath/Inc",
    "thirdparty/nlohmann",
    // ...
};
```

### Pre-Built Binaries

DXC and Agility SDK are Microsoft-provided pre-built DLLs. They cannot be compiled from source with Clang/Zig due to undocumented MSVC dependencies. These are deployed to the output directory at build time:

```zig
// DXC — required for runtime shader compilation
b.addInstallBinFile(b.path("thirdparty/dxc/bin/x64/dxcompiler.dll"), "dxcompiler.dll");
b.addInstallBinFile(b.path("thirdparty/dxc/bin/x64/dxil.dll"), "dxil.dll");

// Agility SDK — DX12 runtime (enables latest features without OS updates)
b.addInstallBinFile(b.path("thirdparty/agility/bin/x64/D3D12Core.dll"), "D3D12/D3D12Core.dll");
b.addInstallBinFile(b.path("thirdparty/agility/bin/x64/d3d12SDKLayers.dll"), "D3D12/d3d12SDKLayers.dll");
```

## Dependency Graph

```
uzengine_runtime.lib
    ├── spdlog (logging)
    ├── Tracy (profiling)
    ├── SPIRV-Reflect (shader reflection)
    ├── basis_universal (texture transcoding)
    ├── astcenc (ASTC compression)
    ├── miniz (zlib for tinyexr)
    ├── D3D12MA (GPU memory, Windows only)
    └── Header-only: DirectXMath, GLM, nlohmann, xxhash, stb, tinyexr, mimalloc, atomic_queue

test_runtime.exe
    ├── uzengine_runtime.lib (link)
    └── GoogleTest (compiled from source)

uzengine.exe / uzcook.exe
    ├── uzengine_runtime.lib (link)
    └── System: d3d12, dxgi, dxguid, gdi32, user32
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Dependency management | Custom `ThirdParty/` with per-platform binaries | Git submodules + SCons | Git submodules + Zig build |
| Total thirdparty libs | 100+ (PhysX, Chaos, OpenColorIO, etc.) | 40+ (Vulkan SDK, ICU, mbedTLS, etc.) | **24** (lean, add as needed) |
| GPU memory allocator | Custom pools + platform allocators | Vulkan Memory Allocator (VMA) | **D3D12MA** (Microsoft official) |
| Math library | Custom FMath + DirectXMath (DX12 path) | Custom `Vector3`, `Basis`, `Transform3D` | **DirectXMath** (SIMD) + custom wrappers |
| JSON | rapidjson | Custom variant parser | **nlohmann/json** |
| Logging | Custom `UE_LOG` + OutputDevice chain | Custom `print_line` | **spdlog** (compiled) + `UZ_LOG` wrapper |

### Philosophy Difference

- **UE5** vendors everything and builds many libraries from modified source — Unreal often patches thirdparty code for engine-specific needs. This gives maximum control but creates a massive maintenance burden.
- **Godot** also vendors everything but avoids heavy external deps — Godot reimplements many features (physics, rendering) rather than depending on large libraries.
- **UzEngine** uses well-maintained, industry-standard libraries unmodified. The only exception is D3D12MA, which requires Clang-specific warning suppression. This minimizes maintenance while leveraging proven implementations.

## Rules

1. **ALWAYS vendor dependencies as Git submodules** — no downloads at build time, no package manager resolution
2. **NEVER modify thirdparty source code** — use wrapper files and compiler flags instead. If upstream triggers warnings, suppress per-file in `build.zig`
3. **ALWAYS compile thirdparty C++ with the same standard (`-std=c++17`)** — ABI consistency across the entire runtime
4. **ALWAYS deploy pre-built DLLs via `addInstallBinFile`** — tools must find DLLs relative to their exe via `uz::GetExeDir()`
5. **NEVER add a dependency without documenting it here** — every library must have a clear purpose and justification
