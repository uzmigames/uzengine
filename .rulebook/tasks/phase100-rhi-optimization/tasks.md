# Phase 100 — RHI Optimization Tasks

## P0 — Highest Impact

### 100.1 PSO Cache
- [ ] Define `PsoCacheKey` struct with xxHash64 of full pipeline descriptor
- [ ] Implement in-memory hash map deduplication in `rhi_common` layer
- [ ] Add disk serialization (DX12: `GetCachedBlob`/`CachedPSO`, Vulkan: `VkPipelineCache`, Metal: binary archives)
- [ ] Add warmup/precache API — load known PSOs at scene load time
- [ ] Add hit/miss stat counters (`StatCounter`)
- [ ] Integrate with shader cache (reuse xxHash infrastructure)

### 100.2 GPU Memory Subsystem
- [ ] Define per-category memory budgets (textures, buffers, staging, render targets)
- [ ] Implement streaming pool with mip-level eviction policy
- [ ] Add residency control (DX12: `MakeResident`/`Evict`, Vulkan: sparse binding)
- [ ] Add memory pressure callback (budget exceeded → evict LRU)
- [ ] Add telemetry: allocation counts, fragmentation ratio, budget utilization
- [ ] Implement defrag pass (incremental, budget-limited per frame)

### 100.3 Upload Ring + Copy Queue Integration
- [ ] Create persistent upload ring buffer (mapped, triple-buffered)
- [ ] Wire existing DX12 `m_copyQueue` for texture/buffer uploads
- [ ] Add per-frame upload budget (bytes/frame cap to avoid stalls)
- [ ] Add fence-based completion tracking (upload done → resource usable)
- [ ] Add staging batch API (group multiple small uploads into one copy submission)
- [ ] Wire Vulkan transfer queue (if separate family available)

## P1 — High Impact

### 100.4 Async Compute
- [ ] Create compute queue in DX12 backend (`D3D12_COMMAND_LIST_TYPE_COMPUTE`)
- [ ] Create compute queue in Vulkan backend (separate queue family)
- [ ] Add `RHIQueueType::Compute` command list creation path
- [ ] Add inter-queue synchronization (fence signaling between graphics ↔ compute)
- [ ] Schedule first async pass (SSAO or mipgen) as proof-of-concept
- [ ] Add overlap telemetry (graphics vs compute utilization)

### 100.5 Vulkan Descriptor Overhaul
- [ ] Audit and remove any `vkDeviceWaitIdle()` in update paths
- [ ] Batch descriptor writes per pass (not per draw)
- [ ] Reduce `FlushDescriptorSet()` to once per render pass, not per frame
- [ ] Evaluate `VK_EXT_descriptor_indexing` for bindless path
- [ ] Add descriptor pressure stats (pool utilization, allocation churn)

### 100.6 Parallel Command Recording
- [ ] Create per-thread command allocator pools
- [ ] Add secondary command list / parallel recording API
- [ ] Record independent render graph passes in parallel
- [ ] Merge recorded command lists into final submission
- [ ] Add thread contention telemetry

## P2 — Medium Impact (Future)

### 100.7 GPU-Driven Rendering
- [ ] Add `DrawIndexedIndirect` / `ExecuteIndirect` to RHI interface
- [ ] Implement GPU frustum culling compute shader
- [ ] Add indirect argument buffer generation
- [ ] Implement draw list compaction on GPU
- [ ] Future: meshlet-based rendering (mesh shaders)

### 100.8 Frame Pacing & Latency
- [ ] Measure present-to-display latency per backend
- [ ] Add VRR/FreeSync/G-Sync support (tearing flags)
- [ ] Add frame pacing telemetry dashboard
- [ ] Tune buffer count per backend for latency vs throughput

### 100.9 Backend Parity Strategy
- [ ] Document DX12 SM6.6 high-end path (bindless, mesh shaders, RT)
- [ ] Document Vulkan compatibility path (descriptor indexing, no mesh shaders)
- [ ] Document Metal Tier 2 path (argument buffers, no mesh shaders)
- [ ] Define feature tiers and runtime capability detection
