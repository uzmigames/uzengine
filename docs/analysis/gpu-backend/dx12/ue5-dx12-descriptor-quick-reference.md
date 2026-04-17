<!-- doc-meta
title: UE5 DX12 Descriptor Management - Quick Reference
description: Quick lookup guide for non-bindless DX12 descriptor patterns
category: rendering
topics: dx12, descriptors, reference
status: complete
-->

# UE5 DX12 Descriptor Management - Quick Reference

## TL;DR: Three Heap Types

| Heap Type | GPU-Visible | Lifetime | Purpose |
|-----------|---|---|---|
| **Offline** | ✗ | Permanent (resource lifetime) | Stores original SRV/UAV/CBV/sampler descriptors, source of truth |
| **Online (sub-allocated)** | ✓ | Per-frame | Shared global heap, linear allocator, most common |
| **Online (local)** | ✓ | Per-frame | Overflow heap, allocated when global is full |

## Per-Draw Binding Pattern

```cpp
// 1. Reserve online heap slots
uint32 heapSlot = 0;  // Persistent across frame

// 2. Build table: copy offline → online
D3D12_GPU_DESCRIPTOR_HANDLE srvTable = BuildSRVTable(
    ShaderFrequency,
    RootSignature,
    SRVCache,
    SlotsNeeded,
    heapSlot  // ← Incremented
);

// 3. Bind table on command list
SetSRVTable(ShaderFrequency, RootSignature, srvTable);

// 4. GPU reads from bound table during draw
DrawIndexed(...);
```

## Key Data Structures

### FD3D12DescriptorCache (per command context)

```cpp
class FD3D12DescriptorCache {
    D3D12_GPU_DESCRIPTOR_HANDLE BuildSRVTable(...);
    D3D12_GPU_DESCRIPTOR_HANDLE BuildUAVTable(...);
    D3D12_GPU_DESCRIPTOR_HANDLE BuildSamplerTable(...);

    void SetSRVTable(...);
    void SetUAVTable(...);
    void SetSamplerTable(...);
};
```

### FD3D12OnlineHeap (GPU-visible)

```cpp
class FD3D12OnlineHeap {
    uint32 ReserveSlots(uint32 NumSlots);
    D3D12_CPU_DESCRIPTOR_HANDLE GetCPUSlotHandle(uint32 Slot);
    D3D12_GPU_DESCRIPTOR_HANDLE GetGPUSlotHandle(uint32 Slot);
};
```

### Root Signature Binding

```cpp
// Root signature maps logical slots → physical root parameters
inline uint32 SRVRDTBindSlot(EShaderFrequency Stage) const {
    return BindSlotMap[VS_SRVs];  // ← Returns root param index
}

// When binding:
SetGraphicsRootDescriptorTable(
    RootSignature->SRVRDTBindSlot(SF_Vertex),  // ← Root param from sig
    GPUHandle  // ← GPU handle to online table
);
```

## Copy Pattern (BuildXxxTable)

```
Offline Heap (permanent)          Online Heap (per-frame)
┌───────────────────────┐         ┌──────────────────┐
│ TextureA SRV          │         │ [reserved slot]  │
│ TextureB SRV          │ Copy → │ Copy of A SRV    │ ← GPU reads from here
│ TextureC SRV          │ ───→  │ Copy of B SRV    │
└───────────────────────┘         │ Copy of C SRV    │
                                  └──────────────────┘
                                  GPU: descriptor[0] → A
                                       descriptor[1] → B
                                       descriptor[2] → C
```

## Three Critical Optimizations

### 1. Sampler Deduplication Cache

```cpp
SamplerArrayDesc Desc = { Count: 3, IDs: [5, 10, 7] };
if (SamplerMap.Find(Desc)) {
    return cached_handle;  // No copy, reuse existing
}
// Otherwise: copy, cache result
```

### 2. Dirty Tracking

```cpp
if (!Cache.DirtySlotMask[ShaderStage]) {
    return;  // Skip rebuild if no changes
}
// Rebuild only when resource changed
```

### 3. Heap Coalescence

All descriptors allocated linearly in one frame:

```cpp
Frame N:
Slot 0-4:    Compute pass UAVs
Slot 5-9:    Sky mesh SRVs
Slot 10-14:  Material SRVs
Slot 15-19:  Lighting SRVs
...
Slot N:      Last draw of frame

Frame N+1:
Slot 0:      First allocation (heap loops, or new frame starts)
```

## Dirty Tracking Rules

**When to mark dirty:**
- Resource set changes (new texture bound)
- Heap changes (online heap switches from sub-allocated to local)

**When to mark clean:**
- After descriptor table is built
- After descriptor table is bound

**When to check:**
- Before building next table → skip if clean

## Heap Switch Triggers

| Event | Action |
|-------|--------|
| Frame starts | `OpenCommandList()` → reset heap pointers |
| Draw calls accumulate | Allocate from heap linearly |
| Heap exhausted | `RollOver()` → loop to slot 0 or allocate new block |
| Global heap full | `SwitchToContextLocalViewHeap()` → use overflow heap |
| Frame ends | `CloseCommandList()` → release used blocks |

## Root Signature Layout (Typical Non-Bindless)

```
Root Parameter Index | Type | Content | Shader Stage |
0                    | CBV  | Frame constants | All |
1                    | DT   | VS SRVs | Vertex |
2                    | DT   | VS Samplers | Vertex |
3                    | DT   | PS SRVs | Pixel |
4                    | DT   | PS Samplers | Pixel |
5                    | DT   | CS SRVs | Compute |
6                    | DT   | CS Samplers | Compute |
7                    | DT   | CS UAVs | Compute |
...
```

(DT = Descriptor Table)

## Compute → Graphics Transition

```
Compute Pass:
  BuildUAVTable(SF_Compute, ..., transmittance) → slot 0
  SetUAVTable(SF_Compute, ...) → SetComputeRootDescriptorTable(7, slot0)
  Dispatch(...)
  GPU writes transmittance texture

Barrier (RDG):
  TransmittanceLUT: UAV → SRV
  Resource state changes, GPU stalls if needed

Graphics Pass:
  BuildSRVTable(SF_Vertex, ..., transmittance) → slot 100 (different!)
  SetSRVTable(SF_Vertex, ...) → SetGraphicsRootDescriptorTable(1, slot100)
  DrawIndexed(...)
  GPU reads transmittance texture
```

**Key:** Different online heap slots, same resource, different descriptors (UAV vs SRV).

## Frame Lifecycle

```
Frame Start:
  1. OpenCommandList() → reset NextSlotIndex = 0
  2. SetDescriptorHeaps() → set view + sampler heaps on command list

Draws:
  3. For each draw:
     - BuildSRVTable(..., heapSlot) → heapSlot increments
     - SetSRVTable(...) → SetGraphicsRootDescriptorTable(...)
     - Similar for samplers, UAVs
     - DrawIndexed(...)

Frame End:
  4. CloseCommandList()
  5. GPU completes work
  6. Return heap blocks to pool
  7. Repeat next frame
```

## Performance Tuning Knobs

| Knob | Impact | Notes |
|------|--------|-------|
| Heap size | Overflow avoidance | Larger = less overflow, more memory |
| Copy overhead | CPU-GPU sync | ~0.1 µs per descriptor |
| Sampler cache | Reuse optimization | Cache hits save descriptor copies |
| Dirty tracking | Skip overhead | ~100 CPU cycles per table if skipped |
| Bindless adoption | Major redesign | Single large heap, zero copying |

## Common Pitfalls

1. **Reusing online slot across frames** → GPU reads stale data
   - **Fix:** Reset `NextSlotIndex = 0` every frame

2. **Not marking dirty on resource change** → GPU reads old descriptor
   - **Fix:** Always call `Cache.Dirty(Stage, mask)` after SetXxxTable

3. **Forgetting resource state transitions** → GPU validation error
   - **Fix:** Use render graph to track state, insert barriers

4. **Heap exhaustion crashes** → No feedback, just crashes
   - **Fix:** Size heaps large, monitor `STAT_ViewHeapChanged`

5. **Mixing offline + online descriptors** → Undefined behavior
   - **Fix:** Always copy offline → online, never use offline GPU handles

## Moving from Non-Bindless to Bindless

**Non-bindless:** Multiple descriptor tables, per-draw copying, ~10-20 root params
- Good for: SM6.0-SM6.5, maximum compatibility
- Bad for: Descriptor copying overhead, fragmented descriptor space

**Bindless:** Single large heap, variable indexing, ~2-4 root params
- Good for: SM6.6+, GPU-driven rendering, minimal copying
- Bad for: Requires shader rewrite, loses offline heap safety

**Migration path:**
1. Implement non-bindless (this analysis)
2. Measure descriptor copy overhead
3. If > 1% frame time: adopt bindless
4. Rewrite shaders to use `Resources[index]` syntax
5. Consolidate multiple descriptor tables into one

---

**Status:** Ready as quick reference during implementation.
