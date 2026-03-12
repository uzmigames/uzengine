# Third party libraries

Please keep categories (`##` level) listed alphabetically and matching their
respective folder names. Use two empty lines to separate categories for
readability.


## Patch system

Thirdparty files are downloaded fresh by `scripts/setup.ps1`. To modify an
upstream file without losing changes on the next setup run, use **patches**
(following Godot's convention):

```
runtime/thirdparty/<library>/patches/
  0001-description.patch
  0002-another-fix.patch
```

**Creating a patch:**
```powershell
# 1. Modify the thirdparty file directly
# 2. Run the helper script
.\scripts\create_patch.ps1 -Library stb -File stb_truetype.h -Description "fix-msvc-warning"
# 3. Restore the original file
git checkout -- runtime/thirdparty/stb/stb_truetype.h
# 4. Commit only the .patch file
```

**Rules:**
- Patches use unified diff format (`git apply` compatible)
- Numbered sequentially: `0001-`, `0002-`, etc.
- Applied alphabetically after `setup.ps1` downloads clean sources
- Patch files are committed; modified thirdparty files are NOT
- Document each patch in the library's section below with a one-line description


## agility

- Upstream: https://www.nuget.org/packages/Microsoft.Direct3D.D3D12
- Version: 1.614.1 (2024)
- License: MIT

DirectX 12 Agility SDK — enables SM6.6+ features on older Windows versions.

Files extracted from upstream source:

- `bin/x64/` (D3D12Core.dll, d3d12SDKLayers.dll)
- `include/` (d3d12.h, d3d12sdklayers.h, d3dcommon.h)


## astcenc

- Upstream: https://github.com/ARM-software/astc-encoder
- Version: 5.3.0 (2025)
- License: Apache 2.0

ASTC texture encoder/decoder. Compresses textures to ASTC format for mobile
GPU targets (Android, iOS). Supports LDR and HDR profiles with configurable
quality presets.

Files extracted from upstream source:

- `Source/` (encoder/decoder source + public header)
- `LICENSE`


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


## basis_universal

- Upstream: https://github.com/BinomialLLC/basis_universal
- Version: git (master, 2025)
- License: Apache 2.0

Basis Universal GPU texture compression. Supercompresses textures into an
intermediate format that can be transcoded to any GPU format at load time
(BC7, BC1, ASTC, ETC2, PVRTC). Used by the asset pipeline for offline
texture compression.

Files extracted from upstream source:

- `encoder/` (basisu_comp.cpp, basisu_enc.cpp, etc.)
- `transcoder/` (basisu_transcoder.cpp, basisu_transcoder.h)
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


## googletest

- Upstream: https://github.com/google/googletest
- Version: 1.15.2 (2024)
- License: BSD-3-Clause

Google's C++ testing framework. Used for all runtime unit tests.

Files extracted from upstream source:

- `googletest/` (gtest headers + source)
- `LICENSE`
- `CMakeLists.txt`


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


## libjpeg-turbo

- Upstream: https://github.com/libjpeg-turbo/libjpeg-turbo
- Version: 3.0.3 (2024)
- License: BSD-3-Clause

SIMD-accelerated JPEG codec. Decodes JPEG images 2-6x faster than libjpeg
using SSE2/AVX2 intrinsics. Used as the primary JPEG decoder for texture
import and runtime image loading.

Files extracted from upstream source:

- Root (jpeglib.h, jconfig.h, turbojpeg.h, source files)
- `simd/` (SIMD implementations)
- `LICENSE.md`


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


## stb

- Upstream: https://github.com/nothings/stb
- Version: git (master, 2025)
- License: MIT / Public Domain (dual-licensed)

Single-file public domain libraries for C/C++. Used headers:

- `stb_image.h` (v2.30) — loads PNG, JPG, TGA, BMP, PSD, GIF, HDR, PIC
- `stb_image_write.h` (v1.16) — writes PNG, TGA, BMP
- `stb_image_resize2.h` (v2.16) — high-quality image resizing
- `stb_truetype.h` (v1.26) — TrueType font rasterizer (used by bitmap_font)

Primary image loader for the engine. stb_image handles most formats; libjpeg-turbo
is used for performance-critical JPEG batch imports.

Files extracted from upstream source:

- Root (header files)
- `LICENSE`


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


## tinyexr

- Upstream: https://github.com/syoyo/tinyexr
- Version: 1.0.9 (2024)
- License: BSD-3-Clause

Tiny OpenEXR image loader/saver. Reads `.exr` files (HDR environment maps,
lightmaps, baked GI). Header-only library with optional miniz/zlib dependency.

Files extracted from upstream source:

- `tinyexr.h` (single header)
- `LICENSE`


## tracy

- Upstream: https://github.com/wolfpld/tracy
- Version: 0.13.1 (2025) — Protocol 76
- Commit: `05cceee0df3b8d7c6fa87e9638af311dbabc63cb`
- License: BSD-3-Clause

Frame profiler with nanosecond precision. Backend for UZ_PROFILE_SCOPE macros.
Supports CPU zones, GPU timestamps, memory tracking, and lock profiling.
Requires Tracy GUI v0.13.1 to connect (protocol version must match).

Build flags: `-DTRACY_ENABLE -DTRACY_ON_DEMAND` (global cpp_flags).

Files extracted from upstream source:

- `public/` (TracyClient.cpp, client/, common/, tracy/, libbacktrace/)
- `LICENSE`

Patches:

- `0001-fix-getdesc-abi-directx-headers.patch` — Fix `ID3D12CommandQueue::GetDesc()` ABI
  incompatibility with directx-headers. The upstream `TracyD3D12.hpp` calls
  `queue->GetDesc().Type` (struct-return), but directx-headers declares `GetDesc`
  as an out-param (`void GetDesc(D3D12_COMMAND_QUEUE_DESC*)`). Patch replaces both
  callsites with the out-param form.


## vcpkg

- Upstream: https://github.com/microsoft/vcpkg
- Version: git (latest, 2024)
- License: MIT

C++ package manager. Used to install prebuilt binaries for:
- `zlib` — compression (asset pipeline)
- `zstd` — fast compression (asset streaming)

Not a library itself — manages installation of other libraries.


## vma

- Upstream: https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
- Version: 3.2.1 (2025)
- License: MIT

Vulkan Memory Allocator by AMD. Manages GPU memory sub-allocation,
defragmentation, and memory type selection for Vulkan backends. Analogous
to D3D12MA for the DX12 backend.

Files extracted from upstream source:

- `include/vk_mem_alloc.h` (header-only library)
- `LICENSE`


## volk

- Upstream: https://github.com/zeux/volk
- Version: vulkan-sdk-1.4.309.0 (2025)
- License: MIT

Vulkan meta-loader. Dynamically loads all Vulkan function pointers at runtime
via `volkInitialize()` + `volkLoadDevice()`. Eliminates the need to link
`vulkan-1.lib` statically — the engine loads the Vulkan driver at runtime.

Files extracted from upstream source:

- `volk.h` (header with API declarations)
- `volk.c` (implementation)
- `LICENSE.md`


## vulkan_headers

- Upstream: https://github.com/KhronosGroup/Vulkan-Headers
- Version: vulkan-sdk-1.4.309.0 (2025)
- License: Apache 2.0

Official Vulkan C/C++ API headers from Khronos Group. Provides vulkan.h,
vulkan_core.h, and platform-specific headers (vulkan_win32.h, etc.).
Used by the Vulkan RHI backend.

Files extracted from upstream source:

- `include/vulkan/` (vulkan.h, vulkan_core.h, platform headers)
- `include/vk_video/` (video codec headers)
- `LICENSE.md`
