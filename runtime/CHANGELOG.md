# Changelog — runtime

> **Repository**: https://github.com/uzmigames/uzengine

## [0.2.15] — 2026-03-13

### Security & Memory Safety Audit — COMPLETE
- **Descriptor heap bounds checking**: UZ_CHECKF assertions in CpuHandle/GpuHandle, out-of-bounds guard in Free()
- **Shader bytecode validation**: 64MB max size check before PSO creation
- **Fence overflow protection**: WaitIdle() detects UINT64_MAX and resets
- **Debug layer rate limiting**: ID3D12InfoQueue configured with message count limit (1024) and suppressed noisy IDs
- **Integer overflow checks**: UINT_MAX validation in buffer SRV/UAV element count calculations
- **ComPtr RAII migration**: Root signature serialization blobs and breadcrumb cmdList2 converted from manual Release() to ComPtr
- **ID3D12InfoQueue GUID**: Added to dx12_guids.h for Zig/MinGW ABI compatibility
- **8 new tests**: DescriptorAllocator overflow, free invalid/OOB, recycle, handle offsets, capacity, count tracking
- **CrashHandler fix**: Fixed double-skip in WalkStack → CaptureRawStackTrace call chain
- **Shader compiler include support**: Added includeDirs to ShaderCompileDesc, DXC CreateDefaultIncludeHandler, FXC D3D_COMPILE_STANDARD_FILE_INCLUDE
- **Documentation**: RAII ownership conventions + shader security docs in docs/engine/14-security/
- All 2648 tests passing

## [0.2.14] — 2026-03-13

### Shader Correctness Audit — COMPLETE
- **Shared HLSL includes**: extracted duplicated PBR/shadow code into reusable headers
  - `pbr_common.hlsli` — DistributionGGX, GeometrySmith, FresnelSchlick, EvaluateBRDF, ACESFilm
  - `shadow_common.hlsli` — SampleShadowPCF (atlas boundary clamping), SampleShadowMap (clean cascade selection)
- **ACES tonemapping**: replaced per-channel Reinhard with ACESFilm in mesh_ps.hlsl and deferred_lighting_ps.hlsl
- **Shader quality fixes**:
  - `[earlydepthstencil]` on opaque pixel shaders (mesh_ps, gbuffer_ps)
  - BC5 two-channel normal map reconstruction guard
  - Tangent transform uses normalMatrix instead of model matrix (mesh_vs.hlsl)
  - Removed hardcoded near/far plane from debug depth shader (reads PerFrameCB)
  - PCF kernel UV clamping to cascade atlas quadrant boundaries
  - Refactored cascade selection loop (removed dual-assignment pattern)
  - Fixed fullscreen_vs.hlsl bit-trick comment
- **Register binding fixes**: aligned constant_buffers.h/lighting_pass.h comments with actual b2 usage, fixed overlay_text_ps.hlsl field naming
- **Shader compiler include support**: added `includeDirs` to ShaderCompileDesc, DXC CreateDefaultIncludeHandler, FXC D3D_COMPILE_STANDARD_FILE_INCLUDE
- **Documentation**: register binding convention + shader includes docs in docs/engine/12-rendering/
- All 2640 tests passing (21 shader cross-backend compilation tests)

## [0.2.13] — 2026-03-13

### Engine API Consolidation (Phase 5.2.0) — COMPLETE
- **DemoApplication base class** (`engine/app/demo_application.h/.cpp`):
  - Eliminates window/device/swapchain/fence boilerplate from all demos
  - Virtual hooks: `OnInit()`, `OnFrame(dt)`, `OnResize(w,h)`, `OnShutdown()`
  - Triple-buffered frame sync, resize handling, minimize sleep, FPS stats
- **PipelineLibrary** (`engine/renderer/core/pipeline_library.h/.cpp`):
  - Lazy PSO cache for ForwardPBR, GBuffer, Lighting, GBufferDebug pipelines
  - Uses ShaderManager + VertexLayout::Standard() internally
  - 31 unit tests
- **Demo migrations** — all 3 demos now extend DemoApplication:
  - `textured_quad`: main.cpp reduced to 15 lines
  - `pbr_test`: main.cpp reduced to 43 lines (from 125), full PBR pipeline preserved
  - `deferred_test`: main.cpp reduced to 42 lines, deferred/forward toggle preserved
- **Build fixes**: resolved include paths (constant_buffers.h, upload_context.h, vertex.h, cascade.h), removed duplicate fps_camera_controller from extra_sources
- **Log category namespace fix**: moved all `UZ_DECLARE_LOG_CATEGORY` outside `namespace uz {}` across 6 files (gpu_mesh.cpp, shader_manager.cpp, pipeline_library.cpp, text_renderer.cpp, bitmap_font.cpp, stats_overlay.cpp)
- All 2641 tests passing, all 3 demos verified running at ~480 FPS
- Task archived

## [0.2.12] — 2026-03-13

### Rendering Roadmap — 0.1 Parity Planning (Phase 3A)
- **7 new Rulebook tasks** created for visual feature parity with 0.1 POC:
  - `phase3a-3.6.0-multi-light` — Point + spot light evaluation (8 max), CPU distance culling
  - `phase3a-3.7.0-ibl-environment` — BRDF LUT, irradiance cubemap, prefiltered specular, SkyLight
  - `phase3a-3.8.0-sky-atmosphere` — Bruneton physical atmosphere, Rayleigh/Mie, height fog
  - `phase3a-3.9.0-advanced-post-processing` — GTAO, SSR, TAA, DoF, motion blur, auto-exposure, color grading, contact shadows, screen effects, PostProcessVolume
  - `phase3a-3.10.0-volumetric-effects` — Ray-marched clouds (3D noise), god rays, temporal filtering
  - `phase3a-3.11.0-gltf-mesh-primitives` — glTF 2.0 importer (fastgltf), sphere/cone/cylinder/plane/torus generators
  - `phase3a-3.12.0-material-format` — .uzmat files, MaterialManager (cache + hot-reload)
  - `phase5-5.1.0-editor-visual-tools` — Transform gizmos, node picking, selection outline, debug draw, wireframe
- Each task includes proposal.md, tasks.md checklist, and specs/ with Given/When/Then scenarios
- Updated `docs/engine/12-rendering/README.md` with complete rendering roadmap (Phase 3A → Phase 5 → Phase 7+)

## [0.2.11] — 2026-03-13

### Vulkan Backend Deferred Tests (Phase 1)
- **Barrier tests** (`tests/engine/rhi/test_vulkan_barriers.cpp`):
  - 64 CPU-only tests: ToVkImageLayout, ToVkAccessFlags, ToVkPipelineStage, GetAspectFlags, IsDepthFormat
  - 8 GPU integration tests: texture barrier round-trips (color, depth, depth+stencil), buffer barriers, null/empty guards
- **Format conversion tests** (`tests/engine/rhi/test_vulkan_format_conversion.cpp`):
  - 22 CPU-only tests: all RHIFormat → VkFormat mappings including Unknown and out-of-range
  - 16 GPU integration tests: texture creation with each non-depth format on real Vulkan device
- **Total**: 102 new tests (137 total Vulkan tests), all passing

## [0.2.10] — 2026-03-12

### Tick System (Phase 3b-3.3b)
- **TickableComponent** (`engine/ecs/tickable_component.h/.cpp`):
  - Base class for components that receive per-frame `OnTick(float dt)` callbacks
  - Configurable `TickGroup` (PrePhysics, PostPhysics, Update, PostUpdate)
  - Enable/disable toggle, interval-based throttling with overshoot preservation
- **TickSystem** (`engine/ecs/tick_system.h/.cpp`):
  - Per-group flat vector storage for cache-friendly iteration
  - Entity validity checks, duplicate registration guard
  - Zero allocations on tick hot path
- **TickSubsystem** (`engine/ecs/tick_subsystem.h/.cpp`):
  - ISubsystem adapter dispatching all 4 tick groups in order each frame
  - Depends on World subsystem
- **51 tests** (`tests/engine/ecs/test_tick_system.cpp`)

## [0.2.9] — 2026-03-12

### Property System (Phase 3b-3.2b)
- **PropertyType enum** (`engine/property/property_types.h`):
  - 10 reflected types: Bool, Int, Float, Double, String, Vec2, Vec3, Vec4, Quat, Entity
  - PropertyFlags bitfield: ReadOnly, Hidden, Replicated, SaveGame, Transient
  - Bitwise operators and `HasFlag()` helper
- **PropertyInfo** (`engine/property/property_info.h`):
  - Offset-based field descriptor: name, type, offset, size, description, category, flags
- **ClassInfo** (`engine/property/class_info.h/.cpp`):
  - Reflected class descriptor: name, properties vector, placement-new factory, instance_size
  - FindProperty (linear scan), ForEachProperty, PropertyCount, AddProperty<T> template
- **ClassRegistry** (`engine/property/class_registry.h/.cpp`):
  - Singleton registry mapping class names to ClassInfo descriptors
  - Register, FindClass, IsRegistered, ForEachClass, ClassCount
  - Logs registration/overwrites via Property log category
- **UZ_COMPONENT macro** (`engine/property/property_macros.h`):
  - Auto-registers class in ClassRegistry during static initialization
  - Provides `ThisClass` typedef, `GetStaticClassInfo()`, placement-new factory
- **UZ_PROP / UZ_PROP_EX macros** for declarative property registration
- **GetPropertyType<T>** compile-time specializations for all 10 types
- **PropertySerializer** (`engine/property/property_serializer.h/.cpp`):
  - JSON serialization/deserialization via nlohmann/json
  - Math types as arrays, Entity as {id, generation} object
  - Transient property exclusion, unknown field tolerance
- **74 tests** (`tests/engine/property/test_property_system.cpp`)

## [0.2.8] — 2026-03-12

### Resource Loaders (Phase 2.2)
- **.uzmesh binary format** (`engine/asset/formats/uzmesh_format.h/.cpp`):
  - 64-byte header (magic `UZMS` 0x534D5A55, version 1), LOD and submesh descriptors
  - `VertexPosNormUV` interleaved layout (32 bytes: position + normal + UV)
  - 64-byte aligned vertex/index data regions for direct GPU upload
  - `WriteUzMesh()` / `ReadUzMesh()` serialization with magic/version validation
- **.uztexture binary format** (`engine/asset/formats/uztexture_format.h/.cpp`):
  - 64-byte header (magic `UZTX` 0x5554585A, version 1), mip level descriptors (24 bytes each)
  - `TextureFormat` enum: Uncompressed_RGBA8, BC1, BC3, BC5, BC7 (future compression)
  - `WriteUzTexture()` / `ReadUzTexture()` with mip chain offset/size validation
- **MeshImporter v1** (`engine/asset/importers/mesh_importer.h/.cpp`):
  - glTF 2.0 parsing via fastgltf v0.8 (.glb and .gltf)
  - Extracts positions (required), normals (optional), UVs (optional) from first mesh primitive
  - Auto-generates flat-shading normals when normals are missing (cross-product per triangle)
  - meshoptimizer vertex cache + fetch optimization (`meshopt_optimizeVertexCache` + `meshopt_optimizeVertexFetch`)
  - Outputs single-LOD single-submesh .uzmesh binary
- **TextureImporter v1** (`engine/asset/importers/texture_importer.h/.cpp`):
  - Image decoding via stb_image (PNG, JPG, JPEG, TGA → RGBA8)
  - Box-filter mipmap chain generation (floor(prev/2), min 1, down to 1x1)
  - Outputs .uztexture binary with complete mip chain
- **uzcook integration**: MeshImporter + TextureImporter registered alongside ShaderImporter
  - Importer-specific options (shader stage detection only for shader files)
  - Output extension mapping: `.uzmesh`, `.uztexture`, `.uzshader`
  - `stb_impl.cpp` added for stb_image compilation
- **97 new tests** (1158 total): format roundtrip, error paths, importer validation, GLB/PNG in-memory parsing
- **Asset directory reorganized** into `formats/`, `importers/` subdirectories
- **Thirdparty**: fastgltf v0.8.0 + simdjson v3.9.4, meshoptimizer v0.21

## [0.2.7] — 2026-03-12

### Job System (Phase 2.1)
- **Worker thread pool** (`engine/job/job_system.h/.cpp`):
  - N-1 worker threads (hardware concurrency minus main thread)
  - Per-priority lock-free queues via `atomic_queue::AtomicQueue2`
  - Work-stealing: idle workers steal from other workers' queues (high-priority first)
  - Implements `ISubsystem` (PreInit phase, depends on Logging)
- **Job struct** (`engine/job/job_types.h`):
  - Function pointer + 48-byte inline closure data (no heap allocation)
  - `MakeJob()` template for type-erased lambda capture
  - `JobPriority` enum: Critical, High, Normal, Low
- **Counter** (`engine/job/counter.h/.cpp`):
  - Atomic completion counter with mutex/condvar-based `Wait()`
  - `Reset(N)`, `Decrement()`, `IsComplete()`, multi-waiter support
- **Fence** (`engine/job/fence.h/.cpp`):
  - Higher-level sync: buffers jobs via `AddWork()`, flushes on `Wait()`
  - Reusable after `Wait()` completes
- **ParallelFor\<T\>** (`engine/job/job_system.h`):
  - Automatic work partitioning by worker count
  - Supports empty spans, single elements, large arrays
- **Debug access guards** (`engine/job/access_guard.h`):
  - `ReadAccess<T>` / `WriteAccess<T>` wrappers with atomic race detection
  - Zero-cost in release builds (compiled out via `#ifdef UZ_DEBUG`)
- **Thread-local profiling**: per-worker `executedJobs` and `stolenJobs` counters
- **97 new tests** (job types, counter, fence, job system, stress test 100K jobs)

## [0.2.6] — 2026-03-12

### Application Lifecycle (Phase 2.0)
- **ISubsystem interface** (`engine/app/subsystem.h`):
  - `InitPhase` enum: PreInit, Init, Start (3-phase initialization)
  - `TickGroup` enum: PrePhysics, PostPhysics, Update, PostUpdate, None
  - Dependency declaration via `GetDependencies()` for topological sort
- **SubsystemRegistry** (`engine/app/subsystem_registry.h/.cpp`):
  - Kahn's algorithm topological sort for initialization ordering
  - 3-phase init (PreInit → Init → Start), reverse-order shutdown
  - Tick group dispatch: `RunTickGroup(group, deltaTime)` per frame
  - Cycle detection with descriptive error messages
- **TimeManager** (`engine/app/time_manager.h/.cpp`):
  - Fixed-timestep accumulator pattern (configurable dt, default 1/60s)
  - Delta time clamping at 0.25s (prevents spiral-of-death)
  - Frame counting, total elapsed time, raw vs smoothed delta
- **EngineLoop** (`engine/app/engine_loop.h/.cpp`):
  - `Run(argc, argv)` and `Run(const EngineConfig&)` entry points
  - Fixed-step physics ticks + variable-rate update in main loop
  - `EngineConfig` struct for headless mode, custom tick rates, max frames
  - Command-line parsing (--headless, --max-frames, --fixed-dt)
- **Module registry** (`engine/app/module_registry.h/.cpp`):
  - `UZ_REGISTER_MODULE(Name, Func)` static self-registration macro
  - Function-local static vector avoids static initialization order fiasco
  - Duplicate detection with descriptive error logging
- **Subsystem wrappers** (`engine/app/subsystem_wrappers.h`):
  - LoggingSubsystem, PathsSubsystem, ConfigSubsystem adapters
  - Wrap existing free functions as ISubsystem implementations
- **97 new tests** (subsystem registry, time manager, engine loop, module registry)

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
