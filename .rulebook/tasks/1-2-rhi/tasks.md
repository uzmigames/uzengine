# Milestone 1.2 — RHI Implementation Tasks

## Phase 1: RHI Interface Design ✅
- [x] 1.1 Design RHI types and descriptors (`core/rhi/rhi_types.h`)
  - RHIHandle<Tag> generational handles (Buffer, Texture, Pipeline, Shader, Sampler)
  - Enums: RHIFormat, BufferUsage, TextureUsage, ResourceState, PrimitiveTopology, CompareFunc, BlendFactor/Op, CullMode, FillMode, IndexFormat, ShaderStage, QueueType, FilterMode, AddressMode, MemoryType
  - Descriptor structs: BufferDesc, TextureDesc, SwapChainDesc, ShaderDesc, GraphicsPipelineDesc, SamplerDesc, Viewport, ScissorRect, BarrierDesc, RenderPassDesc, MemoryBudget
- [x] 1.2 Design abstract interfaces (`core/rhi/rhi.h`)
  - IRHIFence: Signal, WaitCPU, IsComplete, GetCompletedValue
  - IRHICommandList: Begin/End/Execute, SetPipeline, SetVertexBuffer/IndexBuffer, SetViewport/Scissor, SetDescriptorIndex (SM6.6 bindless), DrawIndexed/Draw/Dispatch, ResourceBarrier, ClearRenderTarget/DepthStencil, CopyBuffer/Texture, BeginRenderPass/EndRenderPass
  - IRHISwapChain: GetCurrentBackBuffer, GetCurrentBackBufferIndex, Present, Resize, GetWidth/Height/Format
  - IRHIDevice: Create/Destroy for Buffer, Texture, Shader, Pipeline, Sampler, SwapChain, Fence, CommandList + WaitIdle, GetAdapterName, GetFeatureLevel, GetMemoryBudget
- [x] 1.3 Create backend factory (`core/rhi/rhi_backend.h`)
  - RHIBackend enum (DX12, Vulkan, Metal, Null)
  - RHIDeviceDesc: backend, enableValidation, enableGpuValidation, adapterIndex
  - CreateRHIDevice / DestroyRHIDevice factory functions
- [x] 1.4 Update build.zig (add `core/rhi` to include dirs)
- [x] 1.5 Add compile-only validation tests (`tests/core/rhi/test_rhi_types.cpp`)

## Phase 2: DX12 SM6.6 Backend (`core/rhi/dx12/`) ✅
- [x] 2.1 Implement DX12Device (`dx12_device.h/.cpp`)
  - IDXGIFactory6 + IDXGIAdapter4 enumeration
  - ID3D12Device creation with feature level auto-detection (12_2 → 12_1 → 12_0)
  - D3D12MA v3.1.0 initialization
  - Global CBV_SRV_UAV descriptor heap (1M descriptors, SM6.6 bindless)
  - Global sampler heap (2048), RTV heap (256), DSV heap (64)
  - Direct + Copy command queues
  - SM6.6 Resource Binding Tier 3 validation
- [x] 2.2 Implement DX12CommandList (`dx12_command_list.h/.cpp`)
  - ID3D12GraphicsCommandList wrapping (base interface for Clang/MinGW compat)
  - Triple-buffered command allocators (per frame-in-flight)
  - Descriptor heap binding on Begin() for SM6.6 bindless
  - SetViewport, SetScissor, SetDescriptorIndex, Draw, DrawIndexed, Dispatch
  - TransitionBarrier, ClearRTV (DX12-native methods pre-texture-pool)
- [x] 2.5 Wire CreateRHIDevice / DestroyRHIDevice factory
- [x] 2.3 Implement resource creation ✅
  - CreateBuffer/Texture/Shader/Sampler via D3D12MA + DescriptorAllocator + ResourcePool
  - Bindless SRV/UAV in global CBV_SRV_UAV heap, RTV/DSV in CPU-only heaps
  - Typeless depth formats for SRV+DSV combo, persistent CPU mapping for Upload/Readback
  - New files: dx12_resources.h, dx12_descriptor_allocator.h/.cpp, dx12_resource_creation.h/.cpp
- [x] 2.4 Implement DX12 pipeline state ✅
  - D3D12_GRAPHICS_PIPELINE_STATE_DESC construction from RHIGraphicsPipelineDesc
  - Global bindless root signature (SM6.6 DIRECTLY_INDEXED, 1 root constant at b0)
  - Full state conversion: rasterizer, blend, depth-stencil, topology → D3D12 enums
  - DX12Pipeline struct with ResourcePool generational handles
  - PSO cache with content-hash lookup (xxHash64) — deferred to Phase 6

## Phase 3: Swapchain & Frame Pacing ✅
- [x] 3.1 Implement DX12SwapChain (`dx12_swapchain.h/.cpp`)
  - IDXGISwapChain4 via CreateSwapChainForHwnd (FLIP_DISCARD)
  - Triple-buffered back buffers with RTV descriptors
  - Present with tearing support (variable refresh rate)
  - Resize handling (release + recreate back buffers)
- [x] 3.2 Implement DX12Fence (`dx12_fence.h/.cpp`)
  - ID3D12Fence wrapping with Win32 event
  - Signal, WaitCPU, IsComplete, GetCompletedValue
- [x] 3.3 Frame pacing loop (validated in clear_screen_demo)
  - Wait for frame N-2 fence before reusing its command allocator
  - Signal fence after Execute() for each frame
  - Triple-buffer cycling (0, 1, 2)
- [x] 3.4 Handle DXGI errors (DXGI_ERROR_DEVICE_REMOVED, DEVICE_RESET) ✅
  - DRED enabled (auto-breadcrumbs + page fault tracking) ✅
  - LogDeviceRemoved(): queries DRED1, logs breadcrumb trail + page fault VA + alloc names
  - Swapchain Present calls LogDeviceRemoved() on device loss

## Phase 4: Validation & Clear Screen ✅
- [x] 4.1 Enable D3D12 debug layer
  - ID3D12Debug activation in dev builds
  - GPU-based validation (optional, via RHIDeviceDesc)
  - DRED enabled by default
- [x] 4.2 Clear screen render loop (`dx12_clear_screen.cpp` + `clear_screen_demo.cpp`)
  - 6069 FPS on RTX 4090 (0.16ms/frame)
  - Animated sine-wave RGB clear color
  - Per-frame: Begin → Barrier → ClearRTV → Barrier → End → Execute → Present
- [x] 4.3 Frame time profiling ✅
  - DX12GpuProfiler: ID3D12QueryHeap (TIMESTAMP) with triple-buffered readback
  - BeginTimestamp/EndTimestamp per scope, ResolveFrame reads N-2 results
  - GPU frequency-based tick-to-ms conversion, 64 scopes/frame max
  - New files: dx12_gpu_profiler.h/.cpp
- [ ] 4.4 GPU breadcrumbs (dev builds)
  - WriteBufferImmediate for GPU breadcrumb markers
  - Readback breadcrumb buffer on device removal
  - Log breadcrumb trail with context

## Phase 5: Backend Selection & Runtime Executable
- [ ] 5.1 Backend selection system (`core/rhi/rhi_config.h/.cpp`)
  - Resolve backend from: CLI args → project config → user config → auto-detect
  - `--rhi dx12|dx12-sm5|vulkan|metal` CLI flag
  - `--shader-model sm5|sm6.6` CLI flag
  - SM6.6 capability probe → automatic SM5 fallback
  - Load `.uzproject` and `~/.uzengine/config.json` for persistent settings
- [ ] 5.2 Standalone runtime executable (`tools/uzengine_runtime.cpp`)
  - CLI argument parser (--scene, --screenshot, --rhi, --shader-model, --headless, etc.)
  - Interactive windowed mode (default): open scene, render until close
  - Headless mode: render N frames, capture screenshot, exit
  - Debug overlays: --overlay wireframe|normals|depth|gbuffer
  - GPU validation flags: --gpu-validation, --gpu-breadcrumbs
  - Profiling mode: --profile --frames N
  - Exit codes: 0 = success, 1 = render error, 2 = shader compile error, 3 = device lost
- [ ] 5.3 Shader model negotiation in DX12Device
  - Probe SM6.6 (Resource Binding Tier 3) → SM6.0 → SM5.1 → SM5.0
  - Store negotiated SM in device for shader loading decisions
  - Log negotiated SM at init time

## Phase 6: Shader Compilation Integration
> Full spec: `docs/specs/1.5.0-shader-pipeline.md`
> POC 1 lesson: NEVER embed shaders as C header strings during dev — causes stale cache.
> Two modes: `UZ_SHADER_DEV` (load from disk + VFS) / `UZ_SHADER_SHIP` (precompiled archive).

- [ ] 6.1 Shader VFS (`core/shader/shader_vfs.h/.cpp`)
  - Virtual path mounting: `Mount("/engine", "shaders/engine")`
  - Path resolution: `/engine/pbr.hlsl` → `shaders/engine/pbr.hlsl`
  - Source loading from disk (dev) — no C header embedding
- [ ] 6.2 Shader source manager (`core/shader/shader_source_manager.h/.cpp`)
  - Load source via ShaderVFS, parse `#include` recursively
  - Compute xxHash64(source + sorted includes + defines + target + compiler_version)
  - Content-addressed identity — no timestamps in cache keys
- [ ] 6.3 Shader dependency graph (`core/shader/shader_dependency_graph.h/.cpp`)
  - Forward graph: file → its includes (transitive)
  - Reverse graph: file → all shaders that depend on it
  - Incremental update on file change (rebuild edges for changed file only)
- [ ] 6.4 Shader cache (`core/shader/shader_cache.h/.cpp`)
  - Cache directory: `cache/shaders/{backend}_{sm}/`
  - Lookup by xxHash64 — no explicit invalidation needed (hash changes on source change)
  - Manifest.json for debugging only (hash → source path mapping)
  - Periodic GC: delete unreferenced cache entries
- [ ] 6.5 DXC integration (`core/shader/shader_compiler.h/.cpp`)
  - Load DXC (dxcompiler.dll) dynamically — not linked at build time
  - HLSL → DXIL compilation (DX12 SM6.6 / SM6.0)
  - HLSL → SPIRV compilation (Vulkan target)
  - FXC fallback for SM5 DXBC (`d3dcompiler_47.dll`)
  - Compilation error reporting with source location
- [ ] 6.6 Permutation system (`core/shader/shader_permutation.h/.cpp`)
  - Parse `#pragma uz_permutation` from shader source
  - Expand variants: each permutation = unique define set = unique hash
  - Max 256 variants per shader (build error if exceeded)
- [ ] 6.7 Reflection-driven binding layout
  - SPIRV-Reflect: extract resource bindings from DXIL/SPIRV
  - Auto-generate root signature (DX12) from reflection data
  - Auto-generate descriptor set layout (Vulkan) from reflection data
  - No manual binding specification — all derived from shader source
- [ ] 6.8 Shader hot-reload (dev builds, `core/shader/shader_hot_reload.h/.cpp`)
  - FileWatcher (ReadDirectoryChangesW) on mounted shader directories
  - On change: mark file + all dependents dirty (via reverse-dep graph)
  - Lazy recompile on next use (dirty flag pattern, inspired by Godot)
  - Invalidate affected PSO cache entries
  - Log: shader path, dependent count, recompile time
- [ ] 6.9 Post-switch shader compilation
  - On backend/SM change: scan manifest for new (backend, sm) pair
  - Compile missing shaders before first frame
  - Progress: log in headless, loading screen in windowed
  - Parallel compilation across all CPU cores
- [ ] 6.10 Shader archive for shipping (`core/shader/shader_archive.h/.cpp`)
  - Binary archive (`shaders.uzpak`) with hash-indexed bytecode
  - Build-time: enumerate all shaders via VFS, expand permutations, compile all
  - 100% compile or build error — never ship with missing shaders
  - `#if UZ_SHADER_DEV` strips VFS/FileWatcher/DXC from shipping binary
- [ ] 6.11 SPIRV-Cross integration (future backends)
  - SPIRV → MSL transpilation (Metal)
  - SPIRV → GLSL transpilation (OpenGL ES fallback)

## Phase 7: Testing & Documentation
- [x] 7.1 RHI interface compile tests (`tests/core/rhi/test_rhi_types.cpp`)
  - 23 tests: handles, flags, descriptors, interface abstract checks
- [ ] 7.2 DX12 backend tests (`tests/core/rhi/test_dx12_device.cpp`)
  - Device creation/destruction (requires GPU)
  - Buffer/texture creation with various descs
  - Swapchain create/resize/destroy
  - Fence signal/wait cycle
- [ ] 7.3 Render loop integration test
  - Clear screen for 10 frames, validate no validation errors
  - Measure frame time variance
- [ ] 7.4 Backend selection tests
  - CLI arg parsing: --rhi, --shader-model
  - Auto-detect fallback chain
  - Config file loading (.uzproject, user config)
- [ ] 7.5 Shader pipeline tests
  - ShaderVFS mount/resolve/load
  - ShaderDependencyGraph forward/reverse tracking
  - ShaderCache hit/miss with hash verification
  - Content hash changes when include changes (no stale cache)
  - Permutation expansion and 256-variant cap
  - Hot-reload dirty flag propagation
- [ ] 7.6 Update docs/specs/1.2-rhi.md with implementation details
- [ ] 7.7 ~~Update docs/ARCHITECTURE.md with shader pipeline details~~ ✅ Done
  - Created `docs/specs/1.5.0-shader-pipeline.md` (full shader pipeline spec)
  - Updated `docs/ARCHITECTURE.md` shader section (references spec, summarizes two-mode arch)

## Success Criteria
- [x] All RHI abstract interfaces compile and link
- [x] DX12 backend initializes on SM6.6-capable GPU
- [x] D3D12MA tracks all allocations, zero leaks on shutdown
- [x] Swapchain presents animated color (6069 FPS on RTX 4090)
- [x] D3D12 debug layer + DRED enabled
- [ ] Backend selectable via CLI --rhi flag
- [ ] SM6.6 → SM5 fallback with correct shader bytecode
- [ ] Standalone exe runs with --scene and --screenshot
- [ ] Shader pipeline: VFS + content-hash cache + hot-reload with zero stale cache
- [ ] Shader cache prevents redundant recompilation
- [ ] GPU timestamp profiling shows per-frame timing
- [ ] 95%+ test coverage on RHI types and DX12 backend
- [ ] All files ≤1500 lines, Rust-style docs

<!-- Phase: In Progress -->
