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


## amd-fidelityfx

- Upstream: https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK
- Version: git (main, 2025)
- License: MIT

AMD FidelityFX SDK — collection of GPU effects. Includes FSR 1.0 (spatial
upscaling via EASU + RCAS), FSR 2.x/3.x (temporal upscaling with motion vectors
and depth history), CAS (Contrast Adaptive Sharpening), and more. Provides
DX12 and Vulkan backends with HLSL/GLSL shader source.

Files extracted from upstream source:

- `sdk/` (FSR, CAS, and other effect source + headers)
- `LICENSE`


## agility

- Upstream: https://www.nuget.org/packages/Microsoft.Direct3D.D3D12
- Version: 1.619.1 (2025)
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
- Version: 3.1.0 (2025)
- License: MIT

GPU memory allocator for DirectX 12. Manages committed/placed resources,
suballocation, and defragmentation.

Files extracted from upstream source:

- `include/` (D3D12MemAlloc.h)
- `src/` (D3D12MemAlloc.cpp)
- `LICENSE`


## dlss

- Upstream: https://github.com/NVIDIA/DLSS
- Version: git (main, 2025)
- License: NVIDIA Software License (proprietary, free for commercial use)

NVIDIA Deep Learning Super Sampling SDK. Provides AI-powered temporal upscaling
using a deep neural network trained on thousands of games. Supports 5 quality
presets: DLAA (native AA), Quality (1.5x), Balanced (1.7x), Performance (2.0x),
and Ultra Performance (3.0x). Requires NVIDIA RTX GPU (Turing+) and GeForce
driver 531.18+.

**NOTE**: This directory is `.gitignored` — the SDK is downloaded fresh by
`scripts/setup.ps1` and is not committed to the repository.

**Integration**: UzEngine loads `_nvngx.dll` dynamically via `LoadLibrary` /
`GetProcAddress` at runtime. No static linking required (avoids MSVC CRT
incompatibility with Zig's mingw libc). If the DLL is not present (non-NVIDIA
GPU), the upscaler gracefully falls back to FSR2 or TSR.

Files from upstream repository:

- `include/` (nvsdk_ngx.h, nvsdk_ngx_defs.h, nvsdk_ngx_helpers.h)
- `lib/Windows_x86_64/x64/` (nvsdk_ngx_d.lib — MSVC import lib, not linked by Zig build)
- `LICENSE.txt`


## directx_headers

- Upstream: https://github.com/microsoft/DirectX-Headers
- Version: 1.619.1 (2025)
- License: MIT

Official DirectX 12 headers from Microsoft. Provides d3d12.h, dxgi1_6.h, etc.
independent of Windows SDK version.

Files extracted from upstream source:

- `include/directx/` (d3d12.h, d3dx12.h, dxgiformat.h, etc.)
- `LICENSE`


## directxmath

- Upstream: https://github.com/microsoft/DirectXMath
- Version: apr2025 (2025)
- License: MIT

SIMD-optimized math library for DirectX. **Primary math foundation** for the engine.
XMFLOAT3, XMMATRIX, XMVECTOR, etc. All engine math (transforms, cameras, physics
vectors) is built on top of DirectXMath intrinsics.

Files extracted from upstream source:

- `Inc/` (DirectXMath.h, DirectXColors.h, DirectXCollision.h, etc.)
- `LICENSE`


## googletest

- Upstream: https://github.com/google/googletest
- Version: 1.17.0 (2025)
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
- Version: 1.9.2602 (2026)
- License: University of Illinois/NCSA Open Source License

DirectX Shader Compiler. Compiles HLSL to DXIL (SM6.x) and SPIR-V.

Files extracted from upstream source:

- `bin/x64/` (dxc.exe, dxcompiler.dll, dxil.dll)
- `inc/` (dxcapi.h, d3d12shader.h)
- `lib/x64/` (dxcompiler.lib)


## etcpak

- Upstream: https://github.com/wolfpld/etcpak
- Version: git (master, 2025)
- License: BSD-2-Clause

GPU texture compression library — same compressor used by Godot Engine.
Fastest BC7 encoder available (~100 Mpx/s on modern CPUs). Supports BC1, BC3,
BC7, ETC1, and ETC2 compression with SIMD acceleration (SSE4.1/AVX2/NEON).
Used by the uzcook texture pipeline to compress PNG/JPG → .uztexture at
build time. Single-source C++, no external dependencies.

Files extracted from upstream source:

- Root (`*.cpp`, `*.hpp` — encoder source + headers)
- `AUTHORS.txt`, `LICENSE`


## ffmpeg

- Upstream: https://github.com/ffmpeg/ffmpeg
- Builds: https://github.com/BtbN/FFmpeg-Builds (pre-built LGPL shared)
- Version: 7.x (master, LGPL shared build)
- License: LGPL 2.1+

FFmpeg shared libraries for H.264 hardware-accelerated video encoding. Used by
the viewport frame streaming pipeline to encode DX12 rendered frames as H.264
bitstream, which is then decoded by WebCodecs VideoDecoder in the VSCode webview
at 120 FPS with <5ms latency (validated by POC 0.2).

Only the shared libraries (DLLs), headers, and import libs are included — no
CLI executables (ffmpeg.exe, ffprobe.exe). The LGPL build excludes GPL-only
codecs, making it safe for proprietary use without source disclosure.

Libraries used:
- `libavcodec` — H.264 encoder/decoder (uses HW acceleration: NVENC, AMF, QSV)
- `libavutil` — pixel formats, memory management, error codes
- `libswscale` — pixel format conversion (RGBA → NV12 for H.264 input)

Files from pre-built package:

- `bin/` (avcodec-*.dll, avutil-*.dll, swscale-*.dll, swresample-*.dll)
- `include/` (libavcodec/, libavutil/, libswscale/, libswresample/)
- `lib/` (.lib import libraries for MSVC/Zig linking)


## freetype

- Upstream: https://github.com/freetype/freetype
- Version: 2.13.3 (2024)
- License: FreeType License (BSD-style) / GPLv2

Industry-standard font rasterization library. Supports TrueType, OpenType, and
WOFF fonts with hinting, anti-aliasing, and subpixel rendering. Required for
CJK characters, complex script shaping (with HarfBuzz), and professional-quality
text rendering in game UI.

Files extracted from upstream source:

- `include/` (ft2build.h, freetype/*.h)
- `src/` (modular source — base, truetype, sfnt, smooth, etc.)
- `LICENSE.TXT`


## glm

- Upstream: https://github.com/g-truc/glm
- Version: 1.0.3 (2026)
- License: MIT

OpenGL Mathematics — header-only C++ math library. GLSL-style types (vec3, mat4,
quat). Used alongside DirectXMath for non-SIMD math and type conversions.

Files extracted from upstream source:

- `glm/` (header-only library)
- `copying.txt`


## libjpeg-turbo

- Upstream: https://github.com/libjpeg-turbo/libjpeg-turbo
- Version: 3.1.3 (2025)
- License: BSD-3-Clause

SIMD-accelerated JPEG codec. Decodes JPEG images 2-6x faster than stb_image
using SSE2/AVX2 intrinsics. Used as the primary JPEG decoder for async
texture decode, texture import, and FLIP image loading. Compiled without
NASM SIMD assembly (pure C fallback) with full 8/12/16-bit multi-precision
support via the `wrapper/` compilation units.

Files extracted from upstream source:

- Root (jpeglib.h, jconfig.h, turbojpeg.h, source files)
- `simd/` (SIMD implementations)
- `LICENSE.md`


## libpng

- Upstream: https://github.com/pnggroup/libpng
- Version: 1.6.49 (2025)
- License: libpng License (permissive)

PNG image format reference implementation. Supports 16-bit depth, interlacing,
gamma correction, and full metadata. More robust than stb_image for the asset
pipeline (cook-time texture import). Requires zlib for DEFLATE compression.

Files extracted from upstream source:

- Root (png.h, pngconf.h, source files)
- `LICENSE`


## lz4

- Upstream: https://github.com/lz4/lz4
- Version: 1.10.0 (2024)
- License: BSD-2-Clause

Extremely fast lossless compression algorithm. Used by the .uzpak asset
archive format to compress asset blobs before XOR scrambling. LZ4 achieves
~500 MB/s compression and ~4 GB/s decompression on modern CPUs.

Files extracted from upstream source:

- `lib/` (lz4.h, lz4.c, lz4hc.h, lz4hc.c, lz4frame.h, lz4frame.c)
- `LICENSE`


## luajit

- Upstream: https://github.com/LuaJIT/LuaJIT
- Version: 2.1 (git master, 2025)
- License: MIT

LuaJIT — Just-In-Time compiler for Lua. Used as the scripting runtime for
per-node game scripts (on_start/on_update/on_stop lifecycle). Sandboxed
environment strips os, io, load, require. Built via `scripts/build_luajit.bat`.

Files extracted from upstream source:

- `src/` (full LuaJIT source + headers)
- `dynasm/` (DynASM preprocessor)
- `COPYRIGHT`


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
- Version: 2.2.4 (2025)
- License: MIT

Microsoft's compact general-purpose allocator. **Primary global allocator** for
the engine (replaces default malloc). ~2x faster than default CRT allocator.

Files extracted from upstream source:

- `include/` (mimalloc.h, mimalloc-override.h)
- `src/` (alloc.c, arena.c, heap.c, init.c, options.c, os.c, page.c, segment.c, etc.)
- `LICENSE`


## msdfgen

- Upstream: https://github.com/Chlumsky/msdfgen
- Version: 1.13 (2025)
- License: MIT

Multi-channel signed distance field generator. Produces MSDF font atlases where
each RGB channel encodes a separate distance field, enabling sharp text rendering
at any scale without per-glyph re-rasterization. Used by the SDF font atlas
system for in-game UI text.

Files extracted from upstream source:

- `core/` (SDF generation core)
- `ext/` (FreeType integration, SVG support)
- `msdfgen.h`, `msdfgen-ext.h`
- `LICENSE.txt`


## nlohmann

- Upstream: https://github.com/nlohmann/json
- Version: 3.12.0 (2025)
- License: MIT

JSON for Modern C++ — single-header JSON parser. Used for .uzproject, .uzscene,
and all engine configuration files.

Files extracted from upstream source:

- `json.hpp` (single header)


## renderdoc

- Upstream: https://github.com/baldurk/renderdoc
- Version: 1.35 (2025)
- License: MIT

RenderDoc in-application API. Single-header C API for triggering GPU frame
captures from within the engine. Loads `renderdoc.dll` at runtime via
`LoadLibrary` — no link-time dependency. When launched from the RenderDoc GUI,
the DLL is pre-injected; otherwise, the engine attempts to load it from the
default install path (`C:\Program Files\RenderDoc\`).

Files extracted from upstream source:

- `renderdoc_app.h` (single header, API version 1.6.0+)

### Engine Integration

The RenderDoc API is fully integrated into UzEngine via
`engine/debug/renderdoc_capture.h` (9 public functions):

| Function | Purpose |
|----------|---------|
| `RenderDocInit()` | Loads DLL from process (GUI injection) or default install path |
| `RenderDocShutdown()` | Cleanup, module refcount management |
| `RenderDocIsLoaded()` | Returns true if renderdoc.dll is present |
| `RenderDocIsConnected()` | Returns true if RenderDoc GUI is actively connected |
| `RenderDocSetCaptureOptions()` | Enables API validation, ref all resources, capture callstacks |
| `RenderDocTriggerCapture()` | Single-frame capture (bound to F12) |
| `RenderDocStartCapture()` | Begin multi-frame capture |
| `RenderDocEndCapture()` | End multi-frame capture |
| `RenderDocIsCapturing()` | Check if capture is in progress |

### Usage

**From RenderDoc GUI** (recommended):
1. Open RenderDoc → File → Launch Application → select `UzEngine.exe`
2. DLL is pre-injected, `RenderDocInit()` detects it automatically
3. Press F12 in-engine or click Capture in RenderDoc GUI

**Standalone** (engine finds RenderDoc on its own):
1. Install RenderDoc to default path (`C:\Program Files\RenderDoc\`)
2. Launch `UzEngine.exe` normally — `RenderDocInit()` loads `renderdoc.dll`
3. Press F12 to trigger a capture, `.rdc` file saved to working directory

**Auto-capture on specific frame** (headless/CI):
```bash
UzEngine.exe --renderdoc-capture 5   # Captures frame 5 automatically
```

### Shader Debugging in RenderDoc

Compile shaders with debug info for step-through:
- `ShaderCompileFlags::generateDebugInfo = true` → adds `-Zi` (debug symbols)
- `ShaderCompileFlags::disableOptimizations = true` → adds `-Od` (no opt)

With these flags, RenderDoc can display HLSL source and step through
shader execution line-by-line.

### Related GPU Debug Infrastructure

| Component | Location | Status |
|-----------|----------|--------|
| DX12 Debug Layer | `rhi/dx12/dx12_device.cpp:225` | Enabled via `RHIDeviceDesc::enableValidation` |
| GPU-Based Validation | `rhi/dx12/dx12_device.cpp:237` | Enabled via `RHIDeviceDesc::enableGpuValidation` |
| DRED Breadcrumbs | `rhi/dx12/dx12_device.cpp:481` | Auto-breadcrumbs + page fault tracking |
| Custom GPU Breadcrumbs | `rhi/dx12/dx12_gpu_breadcrumbs.h` | 256 markers/frame via WriteBufferImmediate |
| Live Object Reporting | `rhi/dx12/dx12_device.cpp:97` | Reports leaked D3D12 objects on shutdown |

See `docs/engine/03-rhi/09-debug-diagnostics.md` for the full GPU debugging guide.


## stb

- Upstream: https://github.com/nothings/stb
- Version: git (master, 2025)
- License: MIT / Public Domain (dual-licensed)

Single-file public domain libraries for C/C++. Used headers:

- `stb_image.h` (v2.30) — loads PNG, JPG, TGA, BMP, PSD, GIF, HDR, PIC
- `stb_image_write.h` (v1.16) — writes PNG, TGA, BMP
- `stb_image_resize2.h` (v2.16) — high-quality image resizing
- `stb_truetype.h` (v1.26) — TrueType font rasterizer (used by bitmap_font)

Fallback image loader for formats not handled by dedicated decoders. PNG is
decoded by spng (SIMD-accelerated), JPEG by libjpeg-turbo. stb_image handles
TGA, BMP, HDR, PSD, GIF, and PIC formats.

Files extracted from upstream source:

- Root (header files)
- `LICENSE`


## smaa

- Upstream: https://github.com/iryoku/smaa
- Version: git (master, 2024)
- License: MIT

Subpixel Morphological Anti-Aliasing. Header-only HLSL/GLSL implementation of
SMAA 1x/T2x/S2x/4x. Better edge detection than FXAA with lower cost than TAA.
Useful as a lightweight AA fallback or in combination with temporal upscaling.

Files extracted from upstream source:

- `Shaders/` (SMAA.hlsl — edge detection, blending weight, neighborhood blending)
- `Textures/` (SearchTex.h, AreaTex.h — precomputed lookup textures)
- `LICENSE.txt`


## slang

- Upstream: https://github.com/shader-slang/slang
- Version: 2025.x (pre-built binaries)
- License: MIT

Slang shader language compiler. Used as an alternative shader compilation
backend on macOS/Linux where DXC is not available. Compiles HLSL source
to SPIR-V and Metal MSL via the Slang C API (`slang_compiler.cpp`).
On Windows, DXC remains the primary compiler; Slang is optional.

Files from pre-built package:

- `include/` (slang.h, slang-com-ptr.h, etc.)
- `bin/` (slang.dll / libslang.so — loaded at runtime via dlopen/LoadLibrary)
- `LICENSE`


## spng

- Upstream: https://github.com/nicolaromano/libspng
- Version: 0.7.4 (2024)
- License: BSD-2-Clause

SIMD-accelerated PNG decoder. ~3x faster than stb_image for PNG decoding
using SSE2/SSSE3/SSE4.1 intrinsics on x86_64. Used as the primary PNG
decoder for the texture pipeline (async texture decode, texture import,
FLIP image loading). Falls through to stb_image for non-PNG formats
(JPEG is handled by libjpeg-turbo).

Compiled with `-DSPNG_USE_MINIZ=0` (uses zlib from vcpkg instead of
bundled miniz) and `-DSPNG_STATIC` (no DLL import/export decorators).

Files extracted from upstream source:

- `spng/` (spng.h, spng.c — single-file library)
- `LICENSE`


## spdlog

- Upstream: https://github.com/gabime/spdlog
- Version: 1.17.0 (2025)
- License: MIT

Fast C++ logging library. Backend for UZ_LOG macros. Supports console, file
(with rotation), and debugger output.

Files extracted from upstream source:

- `include/spdlog/` (headers)
- `src/` (compiled library sources)
- `CMakeLists.txt`
- `LICENSE`


## ufbx

- Upstream: https://github.com/ufbx/ufbx
- Version: 0.14.3 (2025)
- License: MIT

Single-file FBX parser. Reads Autodesk FBX binary and ASCII files without
requiring the proprietary FBX SDK. Extracts meshes, skeletons, skin weights,
animation curves, materials, and scene hierarchy. Used by FbxImporter for
importing skeletal characters and animations.

Files extracted from upstream source:

- `ufbx.h` (public API header)
- `ufbx.c` (implementation, compiled as C11)
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
- Version: vulkan-sdk-1.4.304.1 (2025)
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
- Version: 1.4.342 (2026)
- License: Apache 2.0

Official Vulkan C/C++ API headers from Khronos Group. Provides vulkan.h,
vulkan_core.h, and platform-specific headers (vulkan_win32.h, etc.).
Used by the Vulkan RHI backend.

Files extracted from upstream source:

- `include/vulkan/` (vulkan.h, vulkan_core.h, platform headers)
- `include/vk_video/` (video codec headers)
- `LICENSE.md`


## xatlas

- Upstream: https://github.com/jpcy/xatlas
- Version: git (master, 2024)
- License: MIT

Automatic UV atlas generation. Packs mesh UVs into a single atlas with minimal
stretch and overlap. Used for lightmap UV generation, LOD baking, and texture
atlas creation in the asset pipeline.

Files extracted from upstream source:

- `source/xatlas/` (xatlas.h, xatlas.cpp — single-file library)
- `LICENSE`
