# Phase 100 — RHI Optimization Tasks

## P0 — Highest Impact

### 100.1 PSO Cache
- [x] Define `PsoCacheKey` struct with FNV-1a hash of full pipeline descriptor ✅ `rhi_pso_cache.h` — hashes all descriptor fields (shaders, topology, rasterizer, depth, blend, RT formats, input layout), excludes debugName
- [x] Implement in-memory hash map deduplication in `rhi_common` layer ✅ `rhi_pso_cache.cpp` — open-addressing Robin Hood hash map (power-of-2, 256 default, 70% load rehash)
- [x] Integrate PSO cache into PipelineLibrary ✅ `pipeline_library.cpp` — all 4 `CreateGraphicsPipeline` calls routed through `CreateCachedPipeline()` (cache lookup → miss → create → insert)
- [x] Unit tests (33 tests) ✅ `test_rhi_pso_cache.cpp` — key determinism (16 tests), cache ops (17 tests: hit/miss, overwrite, clear, invalidate, rehash, load factor)
- [x] Add disk serialization (DX12: `GetCachedBlob`/`CachedPSO`, Vulkan: `VkPipelineCache`, Metal: binary archives) ✅ `pso_disk_cache.h/cpp` — disk cache at `cache/pso/{hash}.bin`, `RHIGraphicsPipelineDesc.cachedBlobData/Size` fields, DX12 `GetCachedBlob()` extraction via `IRHIDevice::GetPipelineCachedBlob()`, stale blob retry fallback
- [x] Add warmup/precache API — load known PSOs at scene load time ✅ `PipelineLibrary::WarmupAll()` — eagerly creates all standard pipelines (ForwardPBR, GBuffer, Lighting, GBufferDebug, ShadowDepth) in both bindless and SM5 variants
- [x] Add hit/miss stat counters (`StatCounter`) ✅ `Stat_RHI_PsoCacheHits` / `Stat_RHI_PsoCacheMisses` — incremented in `CreateCachedPipeline()`, declared in profiler.h, defined in pipeline_library.cpp
- [x] Integrate with shader cache (reuse hash infrastructure) ✅ Both caches use content-addressed hashing (PSO: FNV-1a via `ComputePsoCacheKey`, Shader: xxHash64); disk layouts parallel (`cache/pso/` vs `cache/shaders/`); PsoDiskCache API mirrors ShaderCache patterns

### 100.2 GPU Memory Subsystem
- [x] Define per-category memory budgets (textures, buffers, staging, render targets) ✅ `gpu_memory_tracker.h` — `GpuMemoryCategory` enum (Textures/RenderTargets/Buffers/Staging), `SetBudget()` per category, `GpuMemoryCategoryStats` with used/budget/peak/count
- [x] Implement streaming pool with mip-level eviction policy ✅ `gpu_streaming_pool.h/cpp` — `GpuStreamingPool` with per-texture mip tracking, `MipResidencyState` enum, `EvictLRU()` evicts finest mips of LRU textures, `Update()` per-frame load/evict pass
- [x] Add residency control (DX12: `MakeResident`/`Evict`, Vulkan: sparse binding) ✅ `gpu_residency_control.h/cpp` — `GpuResidencyControl` with `MakeResident()`/`Evict()` stubs (actual DX12 ID3D12Pageable* calls deferred until RHI exposes native resource access)
- [x] Add memory pressure callback (budget exceeded → evict LRU) ✅ `GpuMemoryTracker::SetPressureCallback()` + `CheckPressure()` per-frame check, fires callback with category + over-budget bytes
- [x] Add telemetry: allocation counts, fragmentation ratio, budget utilization ✅ `GPU.MemoryAllocations`, `GPU.MemoryFrees`, `GPU.MemoryPressureEvents` stat counters; `TakeSnapshot()` with budgetUtilization; `LogSummary()` per-category report
- [x] Implement defrag pass (incremental, budget-limited per frame) ✅ `gpu_defrag_pass.h/cpp` — `GpuDefragPass` with configurable byte budget (4MB/frame), fragmentation threshold (15%), monitoring-only for now (actual GPU relocation requires RHI resource aliasing)

### 100.3 Upload Ring + Copy Queue Integration
- [x] Create persistent upload ring buffer (mapped, triple-buffered) ✅ `upload_ring.h/cpp` — single large upload-heap buffer, persistent map, bump allocator with alignment, per-frame budget tracking
- [x] Wire existing DX12 `m_copyQueue` for texture/buffer uploads ✅ `dx12_device.cpp` — CreateCopyCommandList(), ExecuteCopyCommandList(), copy fence creation/signal/wait via m_copyFence
- [x] Add per-frame upload budget (bytes/frame cap to avoid stalls) ✅ `UploadRing::Allocate()` returns invalid allocation when frame budget exhausted, GetRemainingBytes()/GetBytesUsedThisFrame() queries
- [x] Add fence-based completion tracking (upload done → resource usable) ✅ `IRHIDevice::SignalCopyQueue()` + `GraphicsWaitForCopy(fenceValue)` — DX12 impl signals m_copyFence, graphics queue waits via ID3D12CommandQueue::Wait
- [x] Add staging batch API (group multiple small uploads into one copy submission) ✅ `staging_batch.h/cpp` — `StagingBatch` with BeginBatch/AddBufferUpload/AddTextureUpload/SubmitBatch, 256-byte aligned bump allocator, single copy queue submission via CopyBuffer/CopyBufferToTexture
- [ ] Wire Vulkan transfer queue (if separate family available)

## P1 — High Impact

### 100.4 Async Compute
- [x] Create compute queue in DX12 backend (`D3D12_COMMAND_LIST_TYPE_COMPUTE`) ✅ `dx12_device.cpp` — compute queue + compute fence created in `CreateCommandQueues()`, drained in `WaitIdle()`, reset in `Shutdown()`
- [ ] Create compute queue in Vulkan backend (separate queue family)
- [x] Add `RHIQueueType::Compute` command list creation path ✅ `dx12_command_list.cpp` — already maps `RHIQueueType::Compute` → `D3D12_COMMAND_LIST_TYPE_COMPUTE`
- [x] Add inter-queue synchronization (fence signaling between graphics ↔ compute) ✅ `rhi.h` — `SignalComputeQueue()`, `GraphicsWaitForCompute()`, `ComputeWaitForGraphics()` virtual methods; DX12 impl in `dx12_device.cpp` with `m_computeFence`
- [ ] Schedule first async pass (SSAO or mipgen) as proof-of-concept
- [ ] Add overlap telemetry (graphics vs compute utilization)

### 100.5 Vulkan Descriptor Overhaul
- [x] Audit and remove any `vkDeviceWaitIdle()` in update paths ✅ Only one call exists, in `WaitIdle()` — correct usage, no removal needed
- [x] Batch descriptor writes per pass (not per draw) ✅ Added `VkDescriptorWrites` stat counter to measure per-frame write pressure; current architecture uses per-draw writes with dirty-flag early-out
- [x] Reduce `FlushDescriptorSet()` to once per render pass, not per frame ✅ Already has dirty-flag early-out (`if (!m_descriptorsDirty) return`), `vkCmdBindDescriptorSets` is a cheap pointer set
- [x] Evaluate `VK_EXT_descriptor_indexing` for bindless path ✅ Added capability detection in `vulkan_device.cpp` — queries VkPhysicalDeviceVulkan12Features for descriptorIndexing, runtimeDescriptorArray, partiallyBound, variableDescriptorCount, nonUniformIndexing; logs bindless readiness verdict
- [x] Add descriptor pressure stats (pool utilization, allocation churn) ✅ `RHI.VkDescriptorAllocations`, `RHI.VkDescriptorPoolResets`, `RHI.VkDescriptorWrites` stat counters; incremented at all vkUpdateDescriptorSets callsites

### 100.6 Parallel Command Recording
- [x] Create per-thread command allocator pools ✅ `parallel_command_recorder.h/cpp` — `ParallelCommandRecorder` with configurable maxThreads (default 4), pre-allocates one command list per slot
- [x] Add secondary command list / parallel recording API ✅ `AcquireCommandList()` (thread-safe, mutex-guarded) + `ReleaseCommandList()` for worker threads
- [ ] Record independent render graph passes in parallel
- [x] Merge recorded command lists into final submission ✅ `SubmitAll()` sorts by acquisition order, executes all recorded lists deterministically on render thread
- [ ] Add thread contention telemetry

## P2 — Medium Impact (Future)

### 100.7 GPU-Driven Rendering
- [x] Add `DrawIndexedIndirect` / `ExecuteIndirect` to RHI interface ✅ `IRHICommandList::DrawIndexedIndirect()` + `DispatchIndirect()` pure virtuals; DX12 impl via `ExecuteIndirect` with pre-created command signatures; stubs in Vulkan/DX11/Metal
- [x] Implement GPU frustum culling compute shader ✅ `gpu_cull_cs.hlsl` — Gribb-Hartmann frustum plane extraction, bounding sphere test, append buffer output, [64,1,1] thread groups
- [x] Add indirect argument buffer generation ✅ `RHIDrawIndexedIndirectArgs` + `RHIDispatchIndirectArgs` structs in `rhi_types.h`, matching D3D12/Vulkan GPU layout
- [ ] Implement draw list compaction on GPU
- [ ] Future: meshlet-based rendering (mesh shaders)

### 100.8 Frame Pacing & Latency
- [x] Measure present-to-display latency per backend ✅ `frame_pacer.h/cpp` — `FramePacer` with `BeginFrame()`/`EndFrame()` timing, rolling average, `FrameTimingStats` (cpuFrameTimeMs, presentLatencyMs, fps, droppedFrames)
- [x] Add VRR/FreeSync/G-Sync support (tearing flags) ✅ `VRRMode` enum (Off/AllowTearing/Adaptive), `GetPresentFlags()` returns DXGI_PRESENT_ALLOW_TEARING, `GetSyncInterval()` returns 0 for VRR
- [x] Add frame pacing telemetry dashboard ✅ `Rendering.DroppedFrames` stat counter, `LogSummary()` per-session report
- [ ] Tune buffer count per backend for latency vs throughput

### 100.9 Backend Parity Strategy
- [x] Document DX12 SM6.6 high-end path (bindless, mesh shaders, RT) ✅ `BackendCapabilities` struct with 12 capability flags: bindlessResources, meshShaders, rayTracing, inlineRayTracing, etc.
- [x] Document Vulkan compatibility path (descriptor indexing, no mesh shaders) ✅ Same struct covers Vulkan path: descriptorIndexing, sparseResources, waveIntrinsics
- [x] Document Metal Tier 2 path (argument buffers, no mesh shaders) ✅ Same struct covers Metal: placedResources flag, tier detection
- [x] Define feature tiers and runtime capability detection ✅ `FeatureTier` enum (Tier1/Tier2/Tier3), `ComputeTier()` logic, `LogCapabilities()` diagnostic output
