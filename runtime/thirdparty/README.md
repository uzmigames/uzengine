# Third party libraries

Please keep categories (`##` level) listed alphabetically and matching their
respective folder names. Use two empty lines to separate categories for
readability.


## agility

- Upstream: https://www.nuget.org/packages/Microsoft.Direct3D.D3D12
- Version: 1.614.1 (2024)
- License: MIT

DirectX 12 Agility SDK — enables SM6.6+ features on older Windows versions.

Files extracted from upstream source:

- `bin/x64/` (D3D12Core.dll, d3d12SDKLayers.dll)
- `include/` (d3d12.h, d3d12sdklayers.h, d3dcommon.h)


## atomic_queue

- Upstream: https://github.com/max0x7ba/atomic_queue
- Version: git (master, 2024)
- Commit: `bce850fdce9010db3111e79bfe45040d42c678b1`
- License: MIT

Lock-free multi-producer multi-consumer queues. Used for inter-thread communication
(render thread ↔ game thread, job system).

Files extracted from upstream source:

- `include/atomic_queue/` (header-only)
- `LICENSE`


## crashpad

- Upstream: https://github.com/nickvdp/mini-crashpad
- Version: git (main, 2024)
- Commit: TBD (repo unavailable)
- License: Apache 2.0

Minimal crash reporting library. Generates minidumps on crash with call stack,
thread state, and GPU breadcrumbs.

Files extracted from upstream source:

- `src/`, `include/`
- `LICENSE`


## d3d12ma

- Upstream: https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
- Version: 2.0.1 (2024)
- Commit: `6a61103936dfadedca584e1a521fab6943d3b5ad`
- License: MIT

GPU memory allocator for DirectX 12. Manages committed/placed resources,
suballocation, and defragmentation.

Files extracted from upstream source:

- `include/` (D3D12MemAlloc.h)
- `src/` (D3D12MemAlloc.cpp)
- `LICENSE`


## directx_headers

- Upstream: https://github.com/microsoft/DirectX-Headers
- Version: 1.614.1 (2024)
- Commit: `48a762973271c5a75869946bf1fdbc489a628a5c`
- License: MIT

Official DirectX 12 headers from Microsoft. Provides d3d12.h, dxgi1_6.h, etc.
independent of Windows SDK version.

Files extracted from upstream source:

- `include/directx/` (d3d12.h, d3dx12.h, dxgiformat.h, etc.)
- `LICENSE`


## directxmath

- Upstream: https://github.com/microsoft/DirectXMath
- Version: oct2024 (2024)
- Commit: `0d821781da4ed0f20cad4e30e618afab45a08f0f`
- License: MIT

SIMD-optimized math library for DirectX. **Primary math foundation** for the engine.
XMFLOAT3, XMMATRIX, XMVECTOR, etc. All engine math (transforms, cameras, physics
vectors) is built on top of DirectXMath intrinsics.

Files extracted from upstream source:

- `Inc/` (DirectXMath.h, DirectXColors.h, DirectXCollision.h, etc.)
- `LICENSE`


## dlmalloc

- Upstream: https://gee.cs.oswego.edu/dl/html/malloc.html
- Version: 2.8.6 (2024)
- License: Public Domain

Doug Lea's malloc — lightweight general-purpose allocator. Used as fallback
allocator and for per-subsystem arenas.

Files extracted from upstream source:

- `dlmalloc.c` (single file, self-contained)


## dxc

- Upstream: https://github.com/microsoft/DirectXShaderCompiler
- Version: 1.8.2407 (2024)
- License: University of Illinois/NCSA Open Source License

DirectX Shader Compiler. Compiles HLSL to DXIL (SM6.x) and SPIR-V.

Files extracted from upstream source:

- `bin/x64/` (dxc.exe, dxcompiler.dll, dxil.dll)
- `inc/` (dxcapi.h, d3d12shader.h)
- `lib/x64/` (dxcompiler.lib)


## glm

- Upstream: https://github.com/g-truc/glm
- Version: 1.0.1 (2024)
- Commit: `0af55ccecd98d4e5a8d1fad7de25ba429d60e863`
- License: MIT

OpenGL Mathematics — header-only C++ math library. GLSL-style types (vec3, mat4,
quat). Used alongside DirectXMath for non-SIMD math and type conversions.

Files extracted from upstream source:

- `glm/` (header-only library)
- `copying.txt`


## jemalloc

- Upstream: https://github.com/jemalloc/jemalloc
- Version: 5.3.0 (2023)
- Commit: `cc611be82c57f8f2411347abb96af8c705c972eb`
- License: BSD-2-Clause

High-performance general-purpose allocator. Alternative to mimalloc for
benchmarking and profiling.

Files extracted from upstream source:

- `include/`, `src/`
- `COPYING`


## mimalloc

- Upstream: https://github.com/microsoft/mimalloc
- Version: 2.1.7 (2024)
- Commit: `7c5c43f58b51baace21fadaeed24fdfeb2977ed0`
- License: MIT

Microsoft's compact general-purpose allocator. **Primary global allocator** for
the engine (replaces default malloc). ~2x faster than default CRT allocator.

Files extracted from upstream source:

- `include/` (mimalloc.h, mimalloc-override.h)
- `src/` (alloc.c, arena.c, heap.c, init.c, options.c, os.c, page.c, segment.c, etc.)
- `LICENSE`


## nlohmann

- Upstream: https://github.com/nlohmann/json
- Version: 3.11.3 (2023)
- License: MIT

JSON for Modern C++ — single-header JSON parser. Used for .uzproject, .uzscene,
and all engine configuration files.

Files extracted from upstream source:

- `json.hpp` (single header)


## spdlog

- Upstream: https://github.com/gabime/spdlog
- Version: 1.15.3 (2025)
- Commit: `6fa36017cfd5731d617e1a934f0e5ea9c4445b13`
- License: MIT

Fast C++ logging library. Backend for UZ_LOG macros. Supports console, file
(with rotation), and debugger output.

Files extracted from upstream source:

- `include/spdlog/` (headers)
- `src/` (compiled library sources)
- `CMakeLists.txt`
- `LICENSE`


## tracy

- Upstream: https://github.com/wolfpld/tracy
- Version: 0.11.1 (2024)
- Commit: `5d542dc09f3d9378d005092a4ad446bd405f819a`
- License: BSD-3-Clause

Frame profiler with nanosecond precision. Backend for UZ_PROFILE_SCOPE macros.
Supports CPU zones, GPU timestamps, memory tracking, and lock profiling.

Files extracted from upstream source:

- `public/` (TracyClient headers + source)
- `CMakeLists.txt`
- `LICENSE`


## vcpkg

- Upstream: https://github.com/microsoft/vcpkg
- Version: git (latest, 2024)
- License: MIT

C++ package manager. Used to install prebuilt binaries for:
- `zlib` — compression (asset pipeline)
- `zstd` — fast compression (asset streaming)

Not a library itself — manages installation of other libraries.
