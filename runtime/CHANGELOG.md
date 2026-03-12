# Changelog — runtime

> **Repository**: https://github.com/uzmigames/uzengine

## [0.2.5] — 2026-03-12

### DX11 Black Screen Fix
- **Root cause**: Deferred context rendering produces black frames — UE5 and Godot do NOT use deferred contexts on DX11
- **Rewritten `DX11CommandList`** to use immediate context directly (matching UE5 pattern)
  - Removed `CreateDeferredContext` / `FinishCommandList` / `ExecuteCommandList` entirely
  - All draw, clear, barrier, copy commands go directly to the immediate context
  - `Begin()` / `End()` / `Execute()` are now no-ops (satisfy RHI interface only)
- **Constant buffer binding**: Changed from `UpdateSubresource` on DEFAULT buffers to `Map(WRITE_DISCARD)` on DYNAMIC buffers (UE5 pattern)
- **All 5 demos verified rendering on DX11**: clear_screen, triangle, textured_quad (2755 FPS), camera_test, mesh_test

### RHI-Agnostic Screenshot Capture
- **`ScreenshotCapture`** rewritten to use only `IRHIDevice` / `IRHICommandList` / `IRHISwapChain` (was DX12-only)
- **`CopyTextureToBuffer`** added to `IRHICommandList` interface — implemented in DX12, DX11, Vulkan
  - DX12: `CopyTextureRegion` with placed footprint
  - DX11: Staging texture + `CopyResource` + `Map(READ)` + row-by-row memcpy
  - Vulkan: `vkCmdCopyImageToBuffer`
- **`UnmapBuffer`** added to `IRHIDevice` interface — DX12 no-op (persistent mapping), DX11 explicit Unmap, Vulkan vmaUnmapMemory
- **`SetRootCBV`** extended with `cpuData` + `dataSize` params for DX11 shadow-buffer pattern
- **`CBAllocation`** now includes `bufferHandle` and `offsetBytes` fields
- Screenshot verified on DX11 and DX12 via `--screenshot` flag in camera_test and mesh_test demos

### Research
- UE5 DX11 architecture analysis saved to `docs/research/UE5_DX11_ANALYSIS.md`
- Godot analysis files moved to `docs/research/`

## [0.2.4] — 2026-03-12

### Vulkan RHI Backend
- **Full Vulkan backend** (`engine/rhi/vulkan/`): VulkanDevice, VulkanCommandList, VulkanSwapChain, VulkanFence
  - volk dynamic loader (VK_NO_PROTOTYPES) — no static vulkan-1.lib linking
  - VMA v3.2.1 GPU memory allocator
  - VK_KHR_dynamic_rendering for render passes (no VkRenderPass objects)
  - VK_KHR_synchronization2 for pipeline barriers
  - VK_KHR_timeline_semaphore for fences (1:1 IRHIFence mapping)
  - VK_KHR_buffer_device_address for GPU virtual address queries
  - MAILBOX present mode, triple-buffered swapchain
  - Descriptor set layout: 16 textures (binding 0-15) + 16 samplers (16-31) + 16 UBOs (32-47)
  - Per-frame descriptor set allocation and automatic flush before draw calls
- **Automatic HLSL→SPIR-V compilation**: ShaderPipeline auto-detects Vulkan backend and compiles to SPIR-V via DXC (`-spirv -fspv-target-env=vulkan1.3`)
  - DXC SPIRV binding shifts: `-fvk-t-shift 0 0`, `-fvk-s-shift 16 0`, `-fvk-b-shift 32 0`
  - Bindless auto-disabled for Vulkan (ResourceDescriptorHeap not supported with -spirv)
  - Separate `vulkan_spirv` cache directory for SPIR-V bytecode
  - Zero demo code changes required — fully transparent to application layer
- **IRHIDevice::GetBackend()** virtual method + `GetActiveRHIBackend()` global accessor
- **IsBindless()** returns false for Vulkan (descriptor set model, not DX12 ResourceDescriptorHeap)
- **Build system**: Vulkan sources, volk.c, include paths added to runtime build.zig and all 5 demo build.zig files
- **Thirdparty**: vulkan-headers (SDK 1.4.309), volk, VMA added to setup.ps1 and thirdparty/README.md
- **All 5 demos verified**: clear_screen, triangle (5187 FPS), textured_quad (5334 FPS), camera_test (332 FPS), mesh_test (331 FPS) on RTX 4090 Vulkan 1.4.325

## [0.2.3] — 2026-03-12

### Build System
- **CMake**: `TRACY_ENABLE` + `TRACY_ON_DEMAND` promoted from PRIVATE to PUBLIC definitions
- **CMake**: Added `UZ_PROFILING_ENABLED` define so `UZ_PROFILE_*` macros are active
- **CMake**: Added Tracy link libs (`ws2_32`, `dbghelp`, `advapi32`) for networking/callstack support
- **build.zig**: Tracy link libs added to Zig build (conditional on `UZ_PROFILING_ENABLED`)

### Tracy CPU Instrumentation
- **UZ_PROFILE_FUNCTION()** added to ~39 engine .cpp files across all subsystems:
  - RHI backends: DX12Device, DX12CommandList, DX12SwapChain, DX12Fence, DX11Device, DX11CommandList, DX11SwapChain
  - Shader pipeline: ShaderPipeline, ShaderCompiler, ShaderCompilerFXC, ShaderSourceManager, ShaderDependencyGraph, ShaderCache
  - VFS & assets: VirtualFileSystem, AssetCache, TextureImporter, MeshImporter
  - Renderer: Renderer, TextRenderer, StatsOverlay, EditorGrid
  - Platform: PlatformWindow, InputSystem, ScreenshotCapture
- **UZ_PROFILE_FRAME_MARK()** at swapchain Present boundary for frame-level analysis
- Function-level profiling captures allocation, I/O, GPU command recording, and compilation time per subsystem

### Tracy GPU Profiling
- **DX12 integration** (`engine/rhi/dx12/dx12_device.cpp`, `dx12_swapchain.cpp`):
  - TracyD3D12Context integrated into DX12Device Init/Destroy
  - `TracyD3D12Collect` + `TracyD3D12NewFrame` called in DX12SwapChain::Present
  - Captures GPU timeline for all compute/graphics work on the direct queue
- **DX11 integration** (`engine/rhi/dx11/dx11_device.cpp`, `dx11_swapchain.cpp`):
  - TracyD3D11Context integrated into DX11Device Init/Destroy
  - `TracyD3D11Collect` called in DX11SwapChain::Present
  - Captures GPU timeline for deferred context command lists
- **TracyD3D12.hpp ABI patch** (`thirdparty/tracy/patches/0001-fix-getdesc-abi-directx-headers.patch`):
  - Fixes GetDesc() struct-return compatibility with directx-headers (out-parameter form for Clang/MinGW)
  - Godot-style numbered patch, applied by `scripts/setup.ps1`

### Custom GPU Timestamp Profiler
- **DX12 implementation** (`engine/rhi/dx12/dx12_gpu_profiler.h/.cpp`):
  - ID3D12QueryHeap-based timestamp queries (TIMESTAMP type)
  - Triple-buffered (frame-in-flight) query resolve buffer
  - GPU frequency via `ID3D12CommandQueue::GetTimestampFrequency()` — converts GPU ticks to milliseconds
  - Readback buffer with Map/Unmap pattern
- **DX11 implementation** (`engine/rhi/dx11/dx11_gpu_profiler.h/.cpp`):
  - Individual ID3D11Query objects for each timestamp (TIMESTAMP + TIMESTAMP_DISJOINT pair)
  - Triple-buffered, GetData() CPU readback with synchronization
  - Frequency extracted from TIMESTAMP_DISJOINT query result
  - Discards results when `Disjoint` flag is set (GPU frequency changed mid-frame)
- **Shared types**(`engine/rhi/gpu_profiler_types.h`):
  - GpuTimingResult struct: timestamp label, start/end ticks, ms duration
  - kMaxGpuTimestamps = 64 per frame, kProfilerFramesInFlight = 3
  - Used by both DX12 and DX11 backends
- **Exposure**: GetGpuProfiler() on IRHIDevice, non-fatal warning on init failure

### Stat Counters
- **8 built-in counters** (thread-safe atomic):
  - DrawCalls: incremented per BeginRenderPass
  - Triangles: incremented per bound mesh (vertex count / 3)
  - BufferUploads: incremented in UploadContext::Flush
  - ShaderCompiles: incremented in ShaderPipeline::CompileShader (non-cache hits)
  - CacheHits: incremented on shader cache lookup success
  - CacheMisses: incremented on shader cache lookup failure
  - VFSReads: incremented on VirtualFileSystem::ReadFile
  - PipelineCreations: incremented in CreateGraphicsPipeline
- **UZ_STAT_INCREMENT macro** — atomic increment with stat ID, compile-time category filtering
- **Per-frame reset**: Stats exposed to IPC handlers for editor telemetry (Phase 2+)

## [0.2.2] — 2026-03-12

### DX11 RHI Backend
- **Complete DX11 backend** implementing all 4 RHI interfaces:
  - **DX11Device** (29 methods): D3D11CreateDevice, feature level 11_0+, debug layer, SM5 slot-based SRV/UAV binding
  - **DX11CommandList** (28 methods): deferred context recording, immediate context execution, SM5 register-based binding
  - **DX11SwapChain** (7 methods): DXGI FLIP_DISCARD, tearing support, resize handling
  - **DX11Fence** (4 methods): emulated timeline fence via D3D11_QUERY_EVENT + CPU monotonic counter
- **ResourcePool\<T\>** extracted to shared `rhi/resource_pool.h` (reused by DX12 and DX11)
- **RHI factory** unified in `rhi_factory.cpp` — dispatches DX12/DX11 via `RHIBackend` enum
- **DX11-specific design**: no explicit barriers (ResourceBarrier = no-op), slot-based binding (not bindless), deferred context command lists
- **96 new DX11 tests** (test_dx11_device.cpp, test_dx11_render_loop.cpp, test_dx11_resources.cpp)

### Backend-Agnostic Demos
- **All 5 demos refactored** from DX12-specific code to abstract RHI interfaces:
  - `clear_screen`, `triangle`, `textured_quad`, `camera_test`, `mesh_test`
  - Use only `IRHIDevice*`, `IRHICommandList*`, `IRHISwapChain*`, `IRHIFence*`
  - `--rhi dx11|dx12` CLI flag for runtime backend selection
- **DX12 swapchain fix**: backbuffers now properly registered in device texture pool with RTV descriptors allocated through the descriptor allocator, enabling abstract handle-based `ClearRenderTarget` and `ResourceBarrier`
- **Screenshot capture** gracefully skips on non-DX12 backends (DX12 readback API only)
- **832 total tests passing** across all suites

## [0.2.1] — 2026-03-12

### FXC Dual-Compiler (SM5.0 Fallback)
- **Problem solved**: DXC only supports SM 6.0+. Older GPUs (RX 580, DX12 driver bugs) need SM 5.0.
- **Solution**: Dual-compiler pattern — SM >= 6.0 → DXC (DXIL), SM 5.0 → FXC (DXBC)
- **ShaderCompilerFXC** (`core/shader/shader_compiler_fxc.h/.cpp`):
  - Dynamic FXC loading via `LoadLibrary` + `GetProcAddress` on `d3dcompiler_47.dll`
  - Compiles HLSL → DXBC bytecode for SM5.0
  - FXC types (IFxcBlob, FxcShaderMacro) defined inline to avoid header conflicts
- **ShaderPipeline routing** (`core/shader/shader_pipeline.h/.cpp`):
  - Added `RequiresDXC()` logic — automatically routes SM5.0 to FXC, SM6.0+ to DXC
  - Added `SetCompilerFXC()` to manually override compiler selection
  - Unified compile interface — caller doesn't need to know which compiler is used
- **RHI changes** (`core/rhi/rhi_backend.h`, `dx12_device.h/.cpp`):
  - Added `SM5_0` to ShaderModelCompile enum
  - Added `std::optional<ShaderModel> forceShaderModel` to RHIDeviceDesc
  - NegotiateShaderModel() now respects forceShaderModel (allows `UZ_FORCE_SM5=1` env var)
- **HLSL shader dual-path** — All runtime shaders use `#ifdef UZ_BINDLESS`:
  - **SM6.6** (UZ_BINDLESS=1): ResourceDescriptorHeap[], SamplerDescriptorHeap[], SV_VertexID
  - **SM5.0** (!UZ_BINDLESS): register-bound textures (t0), Input Assembler vertex input (POSITION, TEXCOORD0, COLOR0)
- **Text rendering on SM5.0** (`editor/font/text_renderer.h/.cpp`):
  - Added `bool bindless` param to Init()
  - SM5 path uses Input Assembler pipeline: vertex input layout (POSITION float2, TEXCOORD0 float2, COLOR0 float4), SetVertexBuffer, SetDescriptorTable for font atlas
- **Stats overlay on SM5.0** (`editor/stats/stats_overlay.h/.cpp`):
  - Added `bool bindless` param to Init()
  - Passes through to TextRenderer
- **Demos updated** (all 4 demos: triangle, textured_quad, camera_test, mesh_test):
  - FXC DLL initialization and compiler setup
  - `UZ_FORCE_SM5=1` environment variable for testing SM5.0 path
  - Bindless flag propagation to overlay/text rendering
  - All demos verified working in both SM6.6 bindless and SM5.0 non-bindless modes
- **Build system** (`build.zig`, demo `build.zig` files):
  - FXC DLL (`d3dcompiler_47.dll`) deployed to `zig-out/bin/` during build
  - Available for runtime dynamic loading
- **Vendor DLL** (`runtime/thirdparty/fxc/bin/x64/d3dcompiler_47.dll`):
  - Vendored alongside project (ensures consistent shader compilation across team)
- **Shader cache now differentiates** SM5.0 vs SM6.6 via ShaderTarget enum (separate cache dirs)
- **Verification**: All 4 demos tested and working in both modes:
  - ✅ SM6.6 bindless (default): full rendering + overlays + grid
  - ✅ SM5.0 forced (UZ_FORCE_SM5=1): full rendering + overlays + grid via FXC

## [0.2.0] — 2026-03-11

### Stats Overlay
- **StatsOverlay** (`editor/stats/stats_overlay.h/.cpp`):
  - FPS counter, frame time (ms), GPU adapter name, resolution display
  - Configurable position (top-left/top-right/bottom-left/bottom-right)
  - Uses TextRenderer for bitmap font rendering
  - Toggle visibility at runtime

### Camera System & 3D Space
- **Z-up coordinate system**: All camera, controller, and grid operations use left-handed X-forward, Y-right, Z-up (matching UE5)
- **Camera space conversion**: Internal storage uses Y-up quaternions for consistency, public API converts via ToYUp/FromYUp helpers at boundaries
- **View matrix**: Swap matrix transforms Z-up coordinates to Y-up before feeding to XMMatrixLookToLH (Y-up space)
- **FPS camera controller rewritten**:
  - Euler angle-based orientation system (pitch, yaw, roll)
  - LookAt vector construction from Euler angles
  - Ground-projected movement (WASD moves along flat plane, not in view direction)
  - Delta-time smoothing for stable, responsive controls
- **Grid depth correctness**:
  - Pixel shader now outputs explicit SV_Depth to Z=0 plane
  - Grid pipeline changed from depthWrite=false to depthWrite=true to enable depth output
  - Prevents grid from overlapping with actual 3D geometry
- **Cube positioning**: Vertex offsets set cube bottom face at Z=0 for correct grid alignment

### Screenshot System
- **ScreenshotCapture utility** (`runtime/editor/screenshot/screenshot.h/.cpp`):
  - `Init()`: Create staging buffer for back buffer readback
  - `CaptureBackBuffer()`: Async GPU → CPU copy via staging buffer (MapDiscard pattern)
  - `SaveToPNG()`: PNG encoding via stb_image_write from RGBA8 readback
  - `Shutdown()`: Cleanup GPU resources
- **Integration**: Editor utility for automated back buffer → PNG capture (off-screen rendering, CI screenshots, automated testing)
- **Back buffer readback**: Proper DX12 staging + fence synchronization to ensure GPU work completes before CPU reads

### Build System
- **CMake support** — Added `CMakeLists.txt` and `CMakePresets.json` for MSVC and Clang on Windows
  - Presets: `msvc-debug`, `msvc-release`, `clang-debug`, `clang-release`
  - Static library + test executable + 3 demo executables
  - DLL deployment (Agility SDK, DXC) via post-build commands
- **Cross-compiler DX12 ABI** — `dx12_compat.h` normalizes struct-return COM methods (MSVC vs MinGW)
- **Three compilers verified**: Zig (Clang/MinGW), MSVC, Clang (clang-cl)
- **Unified output directory** — Zig now defaults to `build/` (same as CMake), override with `-p <path>`

### Milestone 1.2 — RHI (Rendering Hardware Interface)
- **RHI abstract interfaces** (`core/rhi/rhi.h`, `rhi_types.h`, `rhi_backend.h`):
  - IRHIDevice, IRHICommandList, IRHISwapChain, IRHIFence — pure virtual interfaces
  - RHIHandle<Tag> generational handles (Buffer, Texture, Pipeline, Shader, Sampler)
  - 20+ enums, 15+ descriptor structs, backend factory (CreateRHIDevice/DestroyRHIDevice)
- **DX12 SM6.7 backend** (`core/rhi/dx12/`):
  - DX12Device: IDXGIFactory6 + IDXGIAdapter4 + ID3D12Device, feature level auto-detect (12_2→12_0)
  - D3D12MA v3.1.0 integration for GPU memory management
  - Global descriptor heaps: CBV_SRV_UAV (1M, SM6.6 bindless), Sampler (2048), RTV (256), DSV (64)
  - Direct + Copy command queues, triple-buffered command allocators
  - SM6.6 Resource Binding Tier 3 validation
  - DescriptorAllocator: O(1) free-list allocator for all heap types
  - ResourcePool<T>: generational handle validation with use-after-free protection
  - CreateBuffer: vertex/index/constant/structured via D3D12MA, auto SRV/UAV for bindless
  - CreateTexture: 2D/Cube/Array with SRV + optional RTV/DSV/UAV, typeless depth for SRV+DSV
  - CreateShader: bytecode copy with pool storage
  - CreateSampler: sampler descriptor in global sampler heap
  - CreateGraphicsPipeline: full PSO from RHIGraphicsPipelineDesc with root signature
  - Global bindless root signature (SM6.6 DIRECTLY_INDEXED, one root constant at b0)
  - State conversion: cull, fill, blend, depth-stencil, topology → D3D12 enums
  - No input layout — SM6.6 bindless pulls vertex data from SRV
  - Persistent CPU mapping for Upload/Readback buffers
- **DX12 swapchain** (`dx12_swapchain.h/.cpp`):
  - IDXGISwapChain4 (FLIP_DISCARD), triple-buffered, tearing support
  - Resize handling (release + recreate back buffers)
- **DX12 fence** (`dx12_fence.h/.cpp`):
  - ID3D12Fence with Win32 event, Signal/WaitCPU/IsComplete/GetCompletedValue
- **DX12 command list** (`dx12_command_list.h/.cpp`):
  - ID3D12GraphicsCommandList (base interface for Clang/MinGW compat)
  - SM6.6 bindless heap binding on Begin(), SetDescriptorIndex for root constants
- **Clear screen demo** (`demos/clear_screen/`):
  - Animated sine-wave RGB clear color, 6069 FPS on RTX 4090 (0.16ms/frame)
  - Triple-buffered frame pacing with fence synchronization
- **Agility SDK activation** (`core/rhi/dx12/dx12_agility.cpp`):
  - Exports D3D12SDKVersion + D3D12SDKPath for runtime SDK loading
  - DLL deployment: D3D12Core.dll + d3d12SDKLayers.dll → build/bin/D3D12/
  - Unlocks FL 12_2 + SM6.6 on supported hardware (RTX 4090: FL 12_1→12_2, SM6.5→SM6.6)
  - Linked into every executable (demo + runtime), not the static lib
- **Standalone runtime executable** (`tools/uzengine.cpp`, `demos/triangle/`):
  - CLI flags: --scene, --width, --height, --headless, --rhi, --shader-model, --gpu-validation
  - Windowed mode with DX12 clear screen render loop, headless mode for CI/testing
  - `zig build run` step for quick iteration
- **Debug & validation**:
  - D3D12 debug layer + GPU-based validation (optional)
  - DRED enabled (auto-breadcrumbs + page fault tracking)
- **DX12 Clang/MinGW compatibility**:
  - Microsoft directx-headers (SDK 614) with correct GUIDs
  - Struct-return ABI: out-parameter form for GetCPUDescriptorHandleForHeapStart etc.
  - Base COM interfaces only (ID3D12Device, ID3D12GraphicsCommandList)
- **Cross-platform build** (`build.zig`):
  - Platform-aware: Windows-only code (DX12, platform/windows) guarded behind `is_windows`
  - DirectXMath compiles on Linux via dotnet/runtime sal.h stub (SAL annotations as no-ops)
  - Cross-compile validated: `zig build -Dtarget=x86_64-linux-gnu` passes
  - Setup script (`setup.ps1`) downloads sal.h automatically after DirectXMath clone
- **Backend selection** (`core/rhi/rhi_config.h/.cpp`):
  - RHIConfig resolution: CLI → .uzproject → platform auto-detect
  - ShaderModel enum: SM5_0, SM5_1, SM6_0, SM6_6
  - Flags: --rhi, --shader-model, --gpu-validation, --gpu-breadcrumbs, --adapter
  - ToDeviceDesc() conversion to RHIDeviceDesc
- **Shader model negotiation** (DX12Device):
  - NegotiateShaderModel(): probes SM6.6 → SM6.0 → SM5.1 → SM5.0
  - Root signature adapts: DIRECTLY_INDEXED for SM6.6, standard for lower
- **DXC DLL deployment** (`build.zig`):
  - `dxcompiler.dll` + `dxil.dll` copied to `build/bin/` alongside executables
  - Test runner executes from install directory so `GetExeDir()` finds deployed DLLs
- **321 unit tests** — 19 engine shader + 77 shader pipeline + 22 RHI config + 35 RHI type/resource + 8 paths + 160 core/math/container

### Image Libraries (Texture Pipeline Dependencies)
- **stb** (v2.30/1.16/2.16) — stb_image, stb_image_write, stb_image_resize2 for PNG/JPG/TGA/BMP/HDR loading, PNG writing, and high-quality resizing
- **libjpeg-turbo** (3.0.3) — SIMD-accelerated JPEG codec (SSE2/AVX2), 2-6x faster than libjpeg
- **tinyexr** (1.0.9) — OpenEXR HDR image loader/saver with bundled miniz for compression
- **basis_universal** (git 2025) — GPU texture supercompression transcoder (BC7, BC1, ASTC, ETC2, PVRTC)
- **astcenc** (5.3.0) — ASTC texture encoder/decoder for mobile GPU targets (Android, iOS)
- **22 integration tests** — Real functional tests for all 5 image libraries (round-trip encode/decode, format validation, error handling)
- **Build system** — Both Zig (`build.zig`) and CMake (`CMakeLists.txt`) updated with include paths, source compilation, and test registration

### Embedded Engine Shaders
- **`uzembed` build tool** (`tools/uzembed.cpp`):
  - Single-file C++ tool: `.hlsl` → `.hlsl.gen.h` (`constexpr char[]` in `uz::generated` namespace)
  - Auto-generates `engine_shaders.gen.h` index with `enum class EngineShader` + `EngineShaders::GetSource()`/`GetName()` registry
  - CLI: `uzembed --input-dir <dir> --output-dir <dir>`, deterministic alphabetical output
  - `--quiet` / `-q` flag suppresses stdout during build (errors still go to stderr)
  - Adding new shaders: drop `.hlsl` in `runtime/shaders/` (any subdirectory) and rebuild — no manual registration
- **`ShaderPipeline::LoadBytecodeFromSource()`** — compile from in-memory HLSL string (bypasses VFS/cache/hot-reload)
- **Migration**: TextRenderer and EditorGrid now use embedded shaders — no VFS mount or disk paths needed
- **`engine_shaders.h`** — thin wrapper that includes the auto-generated index header
- **19 unit tests** — source non-empty, HLSL keyword validation, filename convention, stage suffix, distinctness
- **Build integration**: Zig `build.zig` compiles and runs `uzembed` before runtime library compilation

### Mesh Format (.uzmesh)
- **Binary mesh format** (`core/renderer/mesh.h/.cpp`):
  - 128-byte fixed header (`MeshHeader`): magic `0x484D5A55`, version 1, AABB bounds
  - Two vertex formats: `Vertex` (48B, position+normal+uv+tangent) and `SimpleVertex` (36B, position+normal+uv)
  - `MeshBuilder`: `CreateFromVertices()`, `CreateFromSimpleVertices()`, `CreateCube()` (Z-up, bottom at Z=0)
  - `LoadMesh()` / `SaveMesh()`: binary I/O with magic/version validation and truncation detection
  - 25 unit tests (header, builder, file I/O round-trip, error cases)
- **Mesh shaders** (`shaders/runtime/mesh_vs.hlsl`, `mesh_ps.hlsl`):
  - SM6.6 bindless vertex fetch via `ResourceDescriptorHeap[index]`
  - N·L directional lighting with ambient, optional albedo texture

### Shader Pipeline
- **Shader VFS** (`core/shader/shader_vfs.h/.cpp`):
  - Virtual path mounting: `Mount("/engine", "shaders/engine")`
  - Path resolution, exists check, binary file reading
  - Backslash normalization, last-mounted-wins priority
  - 14 unit tests
- **Shader source manager** (`core/shader/shader_source_manager.h/.cpp`):
  - Load source via VFS, parse `#include "path"` recursively (max depth 32)
  - xxHash64 identity hash: source + sorted includes + defines + target + compiler version
  - Content-addressed — no timestamps, no stale cache bugs
  - Include deduplication, relative/absolute path resolution
  - 14 unit tests
- **Shader dependency graph** (`core/shader/shader_dependency_graph.h/.cpp`):
  - Forward graph: file → its includes (direct dependencies)
  - Reverse graph: file → all shaders that depend on it (transitive BFS)
  - Incremental edge rebuild on file change, diamond dependency handling
  - 13 unit tests
- **Shader cache** (`core/shader/shader_cache.h/.cpp`):
  - Disk-based bytecode cache: `cache/shaders/{backend}_{sm}/{hash}.bin`
  - Lookup by xxHash64 — hash changes on source change, no explicit invalidation
  - Store/Lookup/Contains/Remove, entry count and total size statistics
  - 11 unit tests
- **DXC shader compiler** (`core/shader/shader_compiler.h/.cpp`):
  - Dynamic loading of dxcompiler.dll via LoadLibrary/GetProcAddress
  - HLSL → DXIL compilation (SM6.6, SM6.0)
  - HLSL → SPIRV compilation (Vulkan 1.3 target via `-spirv`)
  - Preprocessor defines, debug info (`-Zi`), optimization control (`-Od`)
  - DXC version query (IDxcVersionInfo) — DXC 1.8 confirmed
  - Auto-extracted IIDs via constexpr ParseUUID from CROSS_PLATFORM_UUIDOF
  - 17 unit tests
- **Engine path resolution** (`core/paths/paths.h/.cpp`):
  - `GetExeDir()`: cached executable directory via platform-native API
  - `ResolveEngineResource()`: resolve relative paths against exe directory
  - Follows UE5/Godot pattern — never CWD, never hardcoded absolute paths
  - Build system deploys DLLs to `build/bin/` via `addInstallBinFile`
  - 8 unit tests
- **Shader pipeline spec**: `docs/specs/1.5.0-shader-pipeline.md`
  - Two-mode design: UZ_SHADER_DEV (VFS + disk load + hot-reload) / UZ_SHADER_SHIP (precompiled archive)
  - Learned from POC 1 failures: never embed shaders as C header strings during dev

### Runtime CLI & Backend Selection (docs only)
- **Runtime executable design**: CLI flags (--scene, --screenshot, --rhi, --shader-model, --headless)
- **Backend selection priority**: CLI → .uzproject → user config → auto-detect
- **Post-switch shader compilation**: compile missing shaders before first frame when backend changes

### Milestone 1.1 — Math & Containers
- **Math foundation** (`core/math/engine_math.h`) — SIMD-accelerated vectors and matrices via DirectXMath:
  - Vec2, Vec3, Vec4 with zero-initialization, SIMD operations (dot, cross, normalize)
  - Mat4 with identity default, transforms (point/direction), decomposition, comparisons
  - Quat with slerp, lerp, axis-angle, vector rotation
  - Transform with position/rotation/scale composition, decomposition, interpolation
- **Geometry types** (`core/math/geometry.h`) — Frustum culling and collision:
  - Plane (half-space, signed distance for frustum clipping)
  - AABB (axis-aligned bounds with containment, intersection, expansion tests)
  - Frustum (6-plane view frustum extraction from projection matrix, conservative culling)
- **Color space conversions** (`core/math/color.h`) — Linear ↔ sRGB gamma correction:
  - LinearColor (float4, linear RGB for rendering)
  - sRGBColor (uint8_t RGBA, sRGB for storage/UI)
  - Gamma-correct conversions, hex color support
- **Containers** (`core/containers/`) — Zero-copy, allocation-friendly data structures:
  - RingBuffer: Lock-free SPSC circular queue with acquire/release semantics
  - FreeList: Intrusive free list for element-stable allocation
  - HandleMap: Generation-based handle → T mapping (safe indices)
  - FrameArray: Temporary allocations with frame lifetime scoping
- **Memory allocators** (`core/memory/allocators.h`) — Named, budget-tracked:
  - LinearAllocator: Bump pointer with reset (frame/streaming allocation)
  - PoolAllocator: Fixed-size block pool with free chain
  - FrameAllocator: Linear allocator with auto-reset per frame
- **Random number generation** (`core/random/random.h`) — PCG generator:
  - PCG (Permuted Congruential Generator) XSH-RR variant
  - 64-bit state, 32-bit output, statistically excellent (passes BigCrush)
  - Seeded and stream variants for deterministic parallel generation
  - Utility methods: Float, Range, Int, UInt, Bool, UnitSphere, UnitDisk
- **160 unit tests** — Complete coverage of all math/container/memory types (27 test suites)
- **Rust-style documentation** — All public types/methods documented with `///` comments

### Added
- Initial Zig build system (`build.zig`)
- Thirdparty dependency setup via `scripts/setup.ps1`
- Stub N-API entry point linking to extension
- Unit tests via `zig build test` (`tests/test_runtime.cpp`)
- clang-format config (`.clang-format`)
- clang-tidy config with warnings-as-errors (`.clang-tidy`)

### Milestone 1.0 — Bootstrap & Project Setup
- **Logging system** (`core/logging/`) — spdlog-backed, 12 categories (Core, RHI, Renderer, Scene, Physics, Audio, Input, Platform, Memory, IO, Config, Window), compile-time level filtering, UZ_LOG macro
- **Assertion system** (`core/assertions/`) — UZ_CHECK (fatal), UZ_ENSURE (non-fatal), UZ_VERIFY (never stripped), stack trace via CaptureStackBackTrace, debugger break support
- **CPU profiler** (`core/profiling/`) — Tracy integration (compile-time toggleable), StatCounter with atomic int64, 6 stat groups
- **Memory foundation** (`core/memory/`) — Rust-inspired ownership: Own<T>, Ref<T>, RefMut<T> with debug borrow checker (std::atomic)
- **Platform window** (`platform/windows/`) — Win32 HWND creation, WndProc message pump, fullscreen toggle, show/hide
- **Input system** (`platform/windows/`) — Keyboard (VK codes), mouse (move/button/scroll), action mapping with RegisterAction/IsActionDown/IsActionJustPressed
- **Config system** (`core/config/`) — .uzproject JSON loader (nlohmann/json), dotted-path resolution, CLI override cascade (--key=value), typed getters with defaults
- **JSON schema** (`schemas/uzproject.schema.json`) — JSON Schema 2020-12 for .uzproject validation
- **25 test functions, 78 assertions** — full coverage of all 1.0 systems
