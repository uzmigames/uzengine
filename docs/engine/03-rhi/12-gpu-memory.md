# GPU Memory Management — Overview

> **Status**: Planned (Phase 1.5 basic D3D12MA, Phase 2 descriptors + streaming, Phase 3 residency)
> **Source**: `runtime/rhi/dx12/gpu_memory.h`, `descriptor_cache.h`, `descriptor_heap_manager.h`, `upload_ring_buffer.h`, `streaming_pool.h`, `memory_stats.h`
> **Depends on**: D3D12, D3D12MA (v3.1+), thirdparty/directx_headers, DXGI

## Purpose

GPU memory management in Direct3D 12 is explicit and low-level — there are no garbage collectors or automatic eviction policies. The GPU memory subsystem abstracts DX12's resource allocation complexity behind a safe, high-performance interface that handles committed/placed/reserved resource types, descriptor heaps, VRAM budgeting, texture streaming, and defragmentation.

## D3D12 Memory Architecture

Direct3D 12 offers three resource allocation modes:

| Mode | Semantics | Use Case |
|------|-----------|----------|
| **Committed** | Resource owns its entire heap | Simple, per-resource, no fragmentation. Slower allocation. |
| **Placed** | Multiple resources share one heap | Dense packing, suballocation efficiency. Requires manual placement. |
| **Reserved** | GPU virtual memory, sparse commit | Streaming, virtual textures, sparse geometry. Complex. |

UzEngine uses **D3D12 Memory Allocator (D3D12MA)** to handle committed and placed resources transparently:

```cpp
// High-level usage (D3D12MA handles placed allocation internally)
GpuAllocation alloc = gpu_memory.Allocate(1024, ResourceType::Buffer);
// D3D12MA may place this in a larger heap with other buffers
// or create a committed resource — transparent to caller
```

## D3D12MA Integration

D3D12MA is a battle-tested allocator from AMD, integrated via `thirdparty/d3d12ma/`:

```
D3D12MA::Allocator
    ├── Block pool (per heap type)
    │   ├── Default heap (GPU-only)
    │   ├── Upload heap (CPU write → GPU read)
    │   └── Readback heap (GPU → CPU read)
    ├── Placement strategy (FirstFit, WorstFit, etc.)
    ├── Defragmentation (copy resources to compact heaps)
    └── Statistics (peak/current allocations)
```

**Heap types**:

| Heap Type | Property | Usage |
|-----------|----------|-------|
| **Default** | GPU-read only, no CPU access | Textures, meshes, render targets, UAV buffers |
| **Upload** | CPU-write, GPU-read (PCIe DMA) | Dynamic constants, vertex data, texture uploads |
| **Readback** | GPU-write, CPU-read (after fence) | GPU readback (occlusion queries, GPU timers) |

Example allocation:

```cpp
// Allocate 1 MB texture in Default heap
GpuAllocation tex_alloc = gpu_memory.Allocate(
    1048576,
    HeapType::Default,
    ResourceType::Texture2D
);

// Allocate 16 KB constant buffer in Upload heap (for CPU updates each frame)
GpuAllocation cb_alloc = gpu_memory.Allocate(
    16384,
    HeapType::Upload,
    ResourceType::Buffer
);
```

## Descriptor Heaps: GPU Resource Binding

Descriptor heaps are arrays of GPU resource handles. There are two main types:

### Shader-Visible Heaps (GPU can see)

Used by shaders to reference resources:

| Descriptor Type | Array Size | Purpose |
|---|---|---|
| **CBV/SRV/UAV** | Often 1M+ entries | Constant, structured, byte-address, UAV buffers; textures and typed buffers |
| **Sampler** | Often 2K entries | Texture samplers (linear, point, anisotropic, etc.) |

Accessed in shaders via:
```hlsl
// Bindless indexing (SM6.6+)
Texture2D<float4> textures[] : register(t0, space0); // t0 = descriptor heap base
float4 color = textures[material.texture_index].Sample(samplers[material.sampler_index], uv);
```

### Non-Shader-Visible Heaps (CPU reference only)

Used to reference render targets and depth-stencil targets:

| Descriptor Type | Purpose |
|---|---|
| **RTV** | Render target descriptors (color output) |
| **DSV** | Depth-stencil view descriptors (depth/stencil output) |

## Bindless Descriptor Table (SM6.6)

SM6.6 enables **bindless** descriptors — a single, global descriptor heap that shaders index into:

```cpp
// Phase 2: Initialize bindless descriptor heaps
descriptor_heap_manager.Init(
    max_srvs: 1000000,
    max_samplers: 2000,
    max_rtvs: 10000,
    max_dsvs: 2000
);

// Phase 2: Allocate descriptors for all materials
for (const Material& mat : materials) {
    uint32_t diffuse_idx = descriptor_heap_manager.AllocateSRV(mat.diffuse_texture);
    uint32_t normal_idx = descriptor_heap_manager.AllocateSRV(mat.normal_texture);
    material_data[i].texture_indices = Vec2(diffuse_idx, normal_idx);
}
```

Benefits:
- **Single draw call for many materials** — change material index, not descriptor tables
- **No root signature limit** — DX12 root signature is 64 DWORDs max; bindless has 1M+ descriptors
- **Material hot-swapping** — update descriptor heap, re-render without PSO change

## Upload Ring Buffer

Per-frame staging for dynamic data (constants, vertex data):

```cpp
// Per frame
upload_ring_buffer.Reset(); // Advance ring, recycle old data

// In frame:
GpuUpload cb_upload = upload_ring_buffer.Upload(
    constants_data,           // source CPU buffer
    sizeof(constants_data),   // size
    256                       // alignment (constants are 256-byte aligned)
);

// cb_upload.gpu_address points to GPU-writable memory
// Copy to GPU-side constant buffer at render time
```

Ring buffer pattern:
```
Frame 1: [SLOT_A (full) | SLOT_B (empty) | SLOT_C (empty)]
         Upload to SLOT_B
Frame 2: [SLOT_A (empty) | SLOT_B (full) | SLOT_C (empty)]
         Upload to SLOT_C
Frame 3: [SLOT_A (empty) | SLOT_B (empty) | SLOT_C (full)]
         Upload to SLOT_A (fence ensures frame 1 no longer reading)
```

Avoids:
- Stalling GPU (waiting for previous frame's slot)
- Over-allocating per-frame upload memory
- Complex synchronization

## VRAM Budget Tracking

IDXGIAdapter3 provides real-time VRAM queries:

```cpp
DXGI_QUERY_VIDEO_MEMORY_INFO memory_info = {};
adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memory_info);

// memory_info.Budget = total available VRAM
// memory_info.CurrentUsage = currently allocated
// memory_info.AvailableForReservation = left for new allocations
```

The GPU memory manager tracks allocations per category and enforces budgets:

```cpp
gpu_memory.SetBudget(MemoryCategory::Textures, 2048);      // 2 GB for textures
gpu_memory.SetBudget(MemoryCategory::Meshes, 512);         // 512 MB for meshes
gpu_memory.SetBudget(MemoryCategory::RenderTargets, 256);  // 256 MB for RTs
// ...

// If allocation would exceed budget:
// Option 1: Evict streaming pool textures
// Option 2: Log warning, fail allocation
// Option 3: Block until GPU completes pending work (stall, bad)
```

## Texture Streaming Pool

Large scenes may have more textures than VRAM. The streaming pool manages a fixed reserve:

```cpp
streaming_pool.Init(
    vram_reserve: 512,  // MB
    mip_strategy: MipEvictionStrategy::Lowest
);

// Add high-resolution texture (12 MB)
StreamingHandle tex = streaming_pool.Add(
    texture_resource,
    resident_mip_level: 0,  // Start with full resolution
    max_streaming_requests: 4
);

// When VRAM pressure rises, pool evicts lowest LOD mips:
// Start: mip 0-8 resident (12 MB)
// Pressure: evict to mip 4-8 (1.5 MB) — still usable, blurry
// Recovery: stream mips 0-3 back in (async)
```

## Residency Management

Large GPU workloads may commit more VRAM than physically available. Residency management swaps resources:

```cpp
gpu_memory.MakeResident(texture_handle);  // GPU needs this, ensure in VRAM
// If not in VRAM: interrupt GPU work, page in from system RAM via PCIe
// Expensive — avoid during frame rendering

gpu_memory.Evict(texture_handle);  // Safe to remove from VRAM
// Pages to system RAM, any GPU reference becomes fault
```

Used for:
- Streaming world segments in large open worlds
- Loading next scene while current scene renders
- Preloading during loading screens (no frame-time impact)

## Memory Defragmentation

D3D12MA provides defragmentation:

```cpp
D3D12MA::DEFRAGMENTATION_STATS stats = {};
gpu_memory.Defragment(
    timeout_ms: 16,  // Spend at most 16ms per frame defragging
    stats: &stats
);

// stats.BytesMoved: bytes copied to compact heaps
// stats.BytesFreed: fragmentation eliminated
```

Example: 256 small allocations scattered across 512 MB heap → compact to 256 MB, free 256 MB.

## Memory Categories

The GPU memory manager tracks allocations across these categories:

| Category | Typical Size | Notes |
|---|---|---|
| **Textures** | 70% of VRAM | Largest consumer; benefits from streaming and mipping |
| **Meshes** | 15% | Vertex/index buffers; relatively static |
| **RenderTargets** | 10% | Swap chain, GBuffer, shadow maps; size varies with resolution |
| **Buffers** | 3% | Constant, structured, byte-address UAVs |
| **Descriptors** | <1% | Descriptor heap memory (small) |
| **Upload** | 1% (per-frame) | Ring buffer, staging; recycled each frame |

Each category has:
- Peak allocation tracker
- Current allocation counter
- Optional budget limit
- Per-allocation leak detection

## Leak Detection & Reporting

On shutdown, GPU memory manager reports unfreed allocations:

```
=== GPU Memory Report ===
Peak allocations:
  Textures: 2.3 GB / 2.0 GB budget (OVERBUDGET)
  Meshes: 456 MB / 512 MB ✓
  Buffers: 128 MB / 256 MB ✓

Active allocations (on shutdown, should be 0):
  Texture "character/head_diffuse.png": 4.2 MB (not freed)
    Allocated at: RenderSystem::LoadCharacter (line 342)
    Age: 5.3 seconds
  Mesh "props/crate_mesh": 2.1 MB (not freed)
    Allocated at: SceneLoader::LoadScene (line 189)
    Age: 2.1 seconds

LEAK: 6.3 MB unfreed
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Allocator | FD3D12DynamicRHI + custom pooling | RenderingDevice pools | D3D12MA (AMD) |
| Descriptor strategy | Per-material descriptor tables | Direct resource binding | **Bindless (SM6.6)** |
| Streaming | Texture streaming pool + mip streaming | Manual streaming via RenderingDevice | Pool + residency management |
| Budgeting | FRHIMemoryStats per category | Implicit (no budget enforcement) | **Explicit per-category budgets** |
| Defragmentation | Built-in, automatic | N/A | D3D12MA (configurable) |
| Residency | GPU residency callbacks | N/A | MakeResident/Evict API |

## Rules

1. **NEVER allocate outside of D3D12MA** — always use `gpu_memory.Allocate()`, never raw `CreateCommittedResource()`
2. **ALWAYS check descriptor heap capacity before allocating** — prevent descriptor heap overflow mid-frame
3. **NEVER evict resources during frame rendering** — only during loading screens or fixed-time passes
4. **ALWAYS align upload data to 256-byte boundaries** — D3D12 constant buffer requirement
5. **ALWAYS track per-frame upload size** — if exceeds ring buffer capacity, increase ring size for next frame
6. **ALWAYS validate budget before allocating** — reject allocation if it would exceed category budget
7. **ALWAYS report memory stats each frame** — peak/current allocation, category breakdown, identifies leaks early
