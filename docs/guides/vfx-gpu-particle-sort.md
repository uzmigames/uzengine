<!-- doc-meta
title: GPU Particle Sort System
category: guide
engine: uzengine
theme: rendering
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# GPU Particle Sort System

## Overview

The GPU particle sort system implements a **bitonic merge sort algorithm** to depth-order particles for correct alpha-blended rendering. Particles are sorted back-to-front (farthest to nearest along the camera view direction) so that when drawn in sorted order, distant particles render first and near particles render last, producing correct alpha compositing without manual depth-buffer manipulation.

## Context

This system is part of the UzEngine particle VFX pipeline. See [VFX Architecture](../architecture/vfx-architecture.md) for context on how sorting integrates with simulation, rendering, and the render graph.

## Algorithm

### Bitonic Merge Sort Overview

@ref Batcher 1968 "Sorting Networks and Their Applications" — the bitonic merge sort is a comparison-based sorting network that can be fully parallelized on GPUs. Unlike sequential sorts, it has fixed structure: a series of compare-and-swap operations at predetermined indices that guarantee sorted output regardless of input order.

UzEngine implements a **two-phase bitonic sort**:

1. **Phase 0 (Local Sort)**: Each thread group sorts 256 elements within itself using shared memory, with zero global memory traffic during the sort. This is the fast part.
2. **Phase 1 (Global Merge)**: The CPU repeatedly dispatches merge passes that join sorted blocks across thread group boundaries, doubling block size each iteration until the full array is sorted.

### Sort Key Generation

The sort key is the **signed distance along the camera's view direction**:

```cpp
float viewDist = dot(particlePos - cameraPos, cameraForward);
```

This value is then converted to a sortable unsigned integer using the IEEE 754 float-to-uint mapping:

```hlsl
uint FloatToSortableUint(float f)
{
    uint bits = asuint(f);
    uint mask = -int(bits >> 31) | 0x80000000;
    return bits ^ mask;
}
```

This conversion preserves float ordering under unsigned integer comparison. The key is then **bitwise-inverted** to achieve descending order (farthest particles get the largest keys):

```hlsl
return ~key;  // Inverted: farthest particle → largest key
```

### Sort Order

**Descending order**: largest key → smallest key (back-to-front). After sorting, `sortIndices[0]` contains the index of the farthest particle, and `sortIndices[aliveCount-1]` contains the nearest particle.

### Dead Particle Handling

Particles beyond `aliveCount` are assigned the sentinel key `0x00000000`, which is the smallest unsigned integer. In a descending sort, these particles sort to the end of the array and are never rendered.

### Phase 0: Local Sort + Key Generation

Each thread group processes `SORT_GROUP_SIZE` (256) elements in shared memory:

1. Each thread loads one particle position and generates its sort key via `GenerateSortKey()`.
2. Threads with out-of-bounds indices (globalIdx >= particleCount) receive the dead sentinel key.
3. All threads participate in a complete bitonic sort network within the group (log₂(256) = 8 stages).
4. Results are written to global memory output buffers.

**Shared memory layout**:
- `gs_keys[256]`: sort keys
- `gs_indices[256]`: original particle indices
- Total: 2048 bytes per thread group (well under typical limits)

### Phase 1: Global Merge

After Phase 0, we have `ceil(paddedCount / 256)` independent sorted blocks. Phase 1 merges these blocks:

```
for (blockSize = 512; blockSize <= paddedCount; blockSize <<= 1)
{
    for (subBlockSize = blockSize >> 1; subBlockSize > 0; subBlockSize >>= 1)
    {
        // Dispatch one merge pass with current (blockSize, subBlockSize) params
        Dispatch(ceil(paddedCount / 256), 1, 1);
        UAVBarrier();
    }
}
```

Each dispatch in Phase 1 is a **compare-and-swap pass**: each thread compares element at index `globalIdx` with element at index `globalIdx ^ subBlockSize`, and swaps if out of order.

The `blockSize` parameter determines which threads participate and the sort direction (ascending or descending within the block).

## Buffer Layout

The GPU particle sort owns two `RWStructuredBuffer<uint>` buffers per emitter:

| Buffer | Size | Purpose |
|--------|------|---------|
| `sortKeys` | `paddedCount * 4 bytes` | Per-element sort keys (distance-based, unsigned int) |
| `sortIndices` | `paddedCount * 4 bytes` | Per-element original particle indices (0..aliveCount-1) |

Both buffers are padded to the next power of two of the original particle capacity:

```cpp
paddedCount = NextPowerOfTwo(capacity);
```

This padding is mandatory for bitonic sort to function — the algorithm requires a power-of-two array length to form a valid sorting network.

### Memory Example

For a 1000-particle emitter:
- Original capacity: 1000
- Padded count: 1024 (2^10)
- Total memory: 1024 * 4 * 2 = **8192 bytes** (8 KB)

## Dispatch Pattern

### Phase 0 Dispatch

```cpp
sortCB.sortPhase = 0;
Dispatch(ceil(paddedCount / 256), 1, 1);
UAVBarrier();
```

For a 1024-element array, this is `Dispatch(4, 1, 1)` — 4 thread groups of 256 threads each.

### Phase 1 Dispatch Loop

```cpp
sortCB.sortPhase = 1;
uint groupCount = ceil(paddedCount / 256);

for (uint blockSize = 512; blockSize <= paddedCount; blockSize <<= 1)
{
    for (uint subBlockSize = blockSize >> 1; subBlockSize > 0; subBlockSize >>= 1)
    {
        sortCB.bitonicBlockSize = blockSize;
        sortCB.bitonicSubBlockSize = subBlockSize;
        UploadAndBindConstantBuffer(sortCB);
        Dispatch(groupCount, 1, 1);
        UAVBarrier();
    }
}
```

**Example for 1024 elements**:
- blockSize: 512, 1024
  - subBlockSize: 256, 128, 64, 32, 16, 8, 4, 2, 1, and then 512, 256, 128, 64, 32, 16, 8, 4, 2, 1
  - Total Phase 1 dispatches: 10 + 10 = **20 dispatches**

Total dispatches for 1024 elements: 1 (Phase 0) + 20 (Phase 1) = **21 dispatches**.

## Performance Characteristics

### Time Complexity

**O(n log²n) comparisons** for n elements, where:
- Phase 0: O(n) comparisons (local sort, one pass per thread group)
- Phase 1: O(n log²n) total comparisons across all merge passes

This is optimal for a comparison-based sorting network. The constant factors are also favorable on GPUs due to high parallelism and memory coalescing.

### GPU Characteristics

| Property | Value |
|----------|-------|
| Thread group size | 256 threads |
| Threads per comparison | 1 (one comparison-swap per thread during global merge) |
| Local sort throughput | 256 elements per dispatch, zero global memory traffic |
| Shared memory per group | 2048 bytes (16 uint elements per thread) |
| Phase 0 compute | 8 stages × 8 sub-passes (64 barrier synchronizations) |
| Phase 1 dispatches | O(log²n) where n = paddedCount |
| Occupancy | 128-256 threads/group (60-100% occupancy on Turing/RDNA/GCN) |

### Memory Characteristics

| Property | Value |
|----------|-------|
| Memory footprint | 2 × paddedCount × 4 bytes |
| Typical memory (1K particles) | 8 KB for keys + 8 KB for indices = 16 KB |
| Bandwidth (Phase 0) | Minimal (load positions once, no written keys leave the group) |
| Bandwidth (Phase 1) | O(n log²n) reads and writes to global UAVs |
| L2 cache efficiency | High (element indices are spatially coherent) |

### Scaling

| Particle Count | Padded Count | Phase 0 Groups | Phase 1 Dispatches | Total Dispatches |
|---|---|---|---|---|
| 256 | 256 | 1 | 0 | 1 |
| 512 | 512 | 2 | 4 | 5 |
| 1024 | 1024 | 4 | 20 | 21 |
| 10,000 | 16,384 | 64 | 624 | 625 |
| 100,000 | 131,072 | 512 | 6,656 | 6,657 |

For 100K particles, expect ~7K dispatches. This is acceptable on modern GPUs (dispatches are fast on NVIDIA and AMD), but consider particle batching for very large counts.

## Integration

The sort system is invoked from the render graph's **VFX_ParticleSort** pass:

1. **Input**: Particle positions buffer (SRV), emitter's aliveCount
2. **Output**: Sorted indices buffer (UAV), consumed by billboard and mesh render passes
3. **Dependencies**: Must execute after particle simulation, before billboard/mesh rendering

The render graph integrates sorting as:

```cpp
RenderGraphPass("VFX_ParticleSort")
    .Read(particlePositionsBuffer)
    .Write(sortedIndicesBuffer)
    .Execute([...](RenderGraphContext& ctx) {
        gpuSort->DispatchSort(
            device, ctx.commandList, sortPipeline, cbRing,
            emitter->aliveCount,
            camera.worldPos, camera.forwardDir,
            positionsSrvIndex);
    });
```

Billboard and mesh render passes then iterate `sortedIndices[0..aliveCount-1]` for back-to-front rendering.

## API Reference

### GPUParticleSort::Create()

```cpp
[[nodiscard]] bool Create(IRHIDevice* device, uint32_t particleCapacity);
```

Allocates sort key and index buffers. Call once per emitter.

**Parameters**:
- `device`: RHI device for buffer creation
- `particleCapacity`: Maximum particles this sort will handle (>0)

**Returns**: `true` on success, `false` if allocation fails. On failure, no buffers are allocated.

**Example**:
```cpp
uz::GPUParticleSort sort;
if (!sort.Create(device, 10000)) {
    UZ_LOG(VFX, Error, "Failed to create particle sort for emitter");
    return false;
}
```

### GPUParticleSort::Destroy()

```cpp
void Destroy(IRHIDevice* device);
```

Releases sort buffers and resets state. Safe to call on unallocated sorts (no-op if device is null).

**Example**:
```cpp
sort.Destroy(device);
```

### GPUParticleSort::IsValid()

```cpp
[[nodiscard]] bool IsValid() const noexcept;
```

Returns `true` if buffers have been allocated and are ready to sort.

### GPUParticleSort::GetPaddedCount()

```cpp
[[nodiscard]] uint32_t GetPaddedCount() const noexcept;
```

Returns the padded (power-of-two) element count. Useful for dispatch group calculations.

### GPUParticleSort::DispatchSort()

```cpp
void DispatchSort(
    IRHIDevice* device,
    IRHICommandList* cmd,
    RHIPipelineHandle pipeline,
    CBRingAllocator& cbRing,
    uint32_t aliveCount,
    const float* cameraPos,
    const float* cameraForward,
    uint32_t positionsSrvIndex);
```

Fills `VFXSortConstants` and dispatches both Phase 0 and Phase 1 of the bitonic sort.

**Parameters**:
- `device`: RHI device for querying UAV descriptor indices
- `cmd`: Command list to record dispatches into
- `pipeline`: Compiled ParticleSortCS pipeline handle
- `cbRing`: CB ring allocator for per-dispatch constant buffer uploads
- `aliveCount`: Number of alive particles to sort (0 returns immediately)
- `cameraPos`: Camera world-space position as float[3]
- `cameraForward`: Camera world-space forward direction as float[3] (must be normalized)
- `positionsSrvIndex`: Bindless SRV descriptor index for the particle positions buffer

**Preconditions**:
- `IsValid()` must return `true`
- `pipeline` must be valid and compiled from `particle_sort_cs.hlsl`
- `cameraForward` must be normalized
- `aliveCount` must be ≤ `capacity`

**Example**:
```cpp
// From render graph pass
sort.DispatchSort(
    device, cmd, sortPipeline, cbRing,
    emitter->aliveCount,
    camera.worldPos.data(),
    camera.forward.data(),
    particlePositionsSrvIdx);
```

### VFXSortConstants

CPU-side mirror of the shader constant buffer (80 bytes total, 16-byte aligned):

```cpp
struct VFXSortConstants {
    // Control
    uint32_t particleCount;        // Alive particles to sort
    uint32_t sortPhase;            // 0 = Phase 0, 1 = Phase 1
    uint32_t paddedCount;          // Power-of-two padded count
    uint32_t _pad0;

    // Camera
    float    cameraPosX, cameraPosY, cameraPosZ;
    float    _pad1;
    float    cameraForwardX, cameraForwardY, cameraForwardZ;
    float    _pad2;

    // Bindless indices
    uint32_t positionsSrvIndex;
    uint32_t sortKeysUavIndex;
    uint32_t sortIndicesUavIndex;
    uint32_t _pad3;

    // Bitonic merge parameters (Phase 1)
    uint32_t bitonicBlockSize;
    uint32_t bitonicSubBlockSize;
    uint32_t _pad4, _pad5;
};

static_assert(sizeof(VFXSortConstants) == 80);
```

### NextPowerOfTwo()

```cpp
constexpr uint32_t NextPowerOfTwo(uint32_t v) noexcept;
```

Rounds up to the next power of two. Used internally to pad particle counts for bitonic sort.

**Examples**:
- `NextPowerOfTwo(1) == 1`
- `NextPowerOfTwo(255) == 256`
- `NextPowerOfTwo(256) == 256`
- `NextPowerOfTwo(10000) == 16384`

## Implementation Files

| File | Purpose |
|------|---------|
| `runtime/engine/vfx/core/gpu_particle_sort.h` | Header with GPUParticleSort struct and constants |
| `runtime/engine/vfx/core/gpu_particle_sort.cpp` | Create/Destroy/DispatchSort implementation |
| `runtime/shaders/runtime/vfx/particle_sort_cs.hlsl` | Bitonic merge sort compute shader (Phase 0 + Phase 1) |

## References

- @ref Batcher 1968 "Sorting Networks and Their Applications" — foundational bitonic sort algorithm
- @ref Peters & Schulz-Hildebrandt 2023 "Fast GPU-based sorting" — modern GPU sort optimization
- @ref Herf 2001 "Radix Tricks" — IEEE float-to-sortable-uint conversion technique

## Key Takeaways

- Bitonic merge sort is a **comparison network** that achieves O(n log²n) parallel comparisons with no data dependencies beyond synchronization barriers.
- **Phase 0** (local sort) is fast because 256 elements are sorted within shared memory with zero global memory traffic.
- **Phase 1** (global merge) is a series of compare-and-swap passes that merge sorted blocks across thread groups. The number of dispatches grows logarithmically with particle count.
- **Sort keys** are computed from camera-relative distance along the view direction, converted to sortable uint via IEEE 754 bit-twiddling, and inverted for back-to-front (descending) order.
- **Dead particles** (beyond aliveCount) are assigned the smallest key (0x00000000) and automatically sort to the end of the array.
- **Memory overhead** is minimal: 8 bytes per particle (key + index). For typical FX with 10K particles, this is 80 KB.

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation |
