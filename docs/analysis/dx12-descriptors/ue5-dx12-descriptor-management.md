<!-- doc-meta
title: UE5 DX12 Descriptor Management on Non-Bindless Hardware
description: Complete analysis of UE5's descriptor management pattern for DX12 without bindless support (SM6.0-SM6.5)
category: rendering
topics: dx12, rhi, descriptor-management, root-signatures, non-bindless
status: complete
-->

# UE5 DX12 Descriptor Management on Non-Bindless Hardware

## Overview

UE5's DX12 RHI supports both **bindless** (SM6.6+) and **non-bindless** (SM6.0-SM6.5) rendering paths. This document analyzes the **non-bindless descriptor management pattern** — how UE5 binds shader resources without variable-rate descriptor indexing.

**Key Finding:** UE5 uses a **per-draw descriptor table staging pattern** where descriptors are copied from offline heaps into online GPU-visible heaps, then bound via descriptor table root parameters.

## Architecture Overview

### Descriptor Heap Strategy (Non-Bindless)

UE5 maintains **three categories of descriptor heaps:**

1. **Offline heaps** (CPU-only, per-resource)
   - Allocated per SRV, UAV, CBV, Sampler
   - Not GPU-visible
   - Permanent lifetime
   - Source of truth for resource binding

2. **Online heaps** (GPU-visible, per-command-context)
   - Allocated per command context
   - Sub-allocated from global heaps or locally allocated
   - Transient slots used for a single pass or frame
   - Slots are reserved at draw time, then released when frame completes

3. **Global heaps** (GPU-visible, per-device)
   - Sampler heap: shared across all contexts, contains unique sampler combinations
   - View heap: sub-allocated to contexts, returns allocations when GPU catches up

### File Locations

| Component | File |
|-----------|------|
| Descriptor cache (main logic) | `Engine/Source/Runtime/D3D12RHI/Private/D3D12DescriptorCache.h/cpp` |
| Online/offline heaps | `Engine/Source/Runtime/D3D12RHI/Private/D3D12Descriptors.h/cpp` |
| Root signature layout | `Engine/Source/Runtime/D3D12RHI/Private/D3D12RootSignature.h` |
| State cache (dirty tracking) | `Engine/Source/Runtime/D3D12RHI/Private/D3D12StateCachePrivate.h/cpp` |

## Data Structure: FD3D12DescriptorCache

### Header: D3D12DescriptorCache.h (lines 303-436)

```cpp
class FD3D12DescriptorCache : public FD3D12DeviceChild, public FD3D12SingleNodeGPUObject
{
private:
    // The previous heaps set on the current command list
    ID3D12DescriptorHeap* LastSetViewHeap = nullptr;
    ID3D12DescriptorHeap* LastSetSamplerHeap = nullptr;

    // Current heap pointers for slot allocation
    FD3D12OnlineHeap* CurrentViewHeap = nullptr;
    FD3D12OnlineHeap* CurrentSamplerHeap = nullptr;

    // Heap implementations:
    FD3D12LocalOnlineHeap* LocalViewHeap = nullptr;           // Overflow heap, per-context
    FD3D12LocalOnlineHeap LocalSamplerHeap;                   // Overflow heap, per-context
    FD3D12SubAllocatedOnlineHeap SubAllocatedViewHeap;        // Sub-allocated from global, per-context

    // Sampler deduplication
    FD3D12SamplerMap SamplerMap;                              // Hash map of SamplerArrayDesc → GPU handle
    TArray<FD3D12UniqueSamplerTable> UniqueTables;            // Pending unique sampler tables for consolidation
    TSharedPtr<FD3D12SamplerSet> LocalSamplerSet;

    // Non-bindless state
    bool bUsingViewHeap = true;
    bool bUsingExplicitCacheHeaps = false;
};
```

## Key Methods: BuildXxxTable Pattern

All three resource table types follow the same pattern:

### 1. BuildSRVTable (D3D12DescriptorCache.cpp, lines 432-481)

```
// STEP 1: Reserve heap slots from CurrentViewHeap (online heap)
uint32 FirstSlotIndex = HeapSlot;
HeapSlot += SlotsNeeded;
D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor = CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex);

// STEP 2: Gather source descriptors from offline heaps
D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptors[MAX_SRVS];
for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
{
    if (FD3D12ShaderResourceView* SRV = SRVs[SlotIndex])
    {
        SrcDescriptors[SlotIndex] = SRV->GetOfflineCpuHandle();  // ← offline heap
        Context.UpdateResidency(Cache.Resources[ShaderStage][SlotIndex]);
    }
    else
    {
        SrcDescriptors[SlotIndex] = DefaultViews.NullSRV;        // ← fallback
    }
}

// STEP 3: Copy to online heap (GPU-visible)
GetParentDevice()->CopyDescriptors(
    DestDescriptor,                           // online heap destination
    SrcDescriptors,                           // offline heap sources
    SlotsNeeded,
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
);

// STEP 4: Return GPU-visible handle for binding
const D3D12_GPU_DESCRIPTOR_HANDLE BindDescriptor =
    CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex);
return BindDescriptor;
```

**Critical Detail:** The copy happens **per draw call** (or per-shader-stage binding), not once per frame. If 100 draws need SRVs, the descriptor cache allocates 100 separate ranges and copies descriptors 100 times.

### 2. BuildSamplerTable (D3D12DescriptorCache.cpp, lines 326-406)

Same pattern, with **one difference**: deduplication cache.

```cpp
// STEP 1: Check if this sampler combination already exists
FD3D12SamplerArrayDesc Desc = { SlotsNeeded, ... };
D3D12_GPU_DESCRIPTOR_HANDLE* FoundDescriptor = SamplerMap.Find(Desc);
if (FoundDescriptor)
{
    return *FoundDescriptor;  // ← Cache hit! Return existing GPU handle
}

// STEP 2-4: Same as SRVs, but on sampler heap
D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor =
    CurrentSamplerHeap->GetCPUSlotHandle(FirstSlotIndex);

for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
{
    SrcDescriptors[SlotIndex] = Samplers[SlotIndex]->OfflineDescriptor;
}

GetParentDevice()->CopyDescriptors(
    DestDescriptor,
    SrcDescriptors,
    SlotsNeeded,
    FD3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER
);

// STEP 5: Cache the result for next time
UniqueTables.Add(FD3D12UniqueSamplerTable(Desc, SrcDescriptors));
SamplerMap.Add(Desc, BindDescriptor);

return BindDescriptor;
```

**Optimization:** If the same samplers are used multiple times, the cache lookup avoids re-copying. The sampler map is reset when the heap loops around (line 23).

### 3. BuildUAVTable (D3D12DescriptorCache.cpp, lines 219-260)

```cpp
uint32 FirstSlotIndex = HeapSlot;
HeapSlot += SlotsNeeded;

D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor(
    CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex)
);

for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
{
    if ((SlotIndex < UAVStartSlot) || (UAVs[SlotIndex] == nullptr))
    {
        SrcDescriptors[SlotIndex] = DefaultViews.NullUAV;
    }
    else
    {
        SrcDescriptors[SlotIndex] = UAVs[SlotIndex]->GetOfflineCpuHandle();
        Context.UpdateResidency(Cache.Resources[ShaderStage][SlotIndex]);
    }
}

GetParentDevice()->CopyDescriptors(
    DestDescriptor,
    SrcDescriptors,
    SlotsNeeded,
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
);

return BindDescriptor;
```

## Binding: SetXxxTable Methods

After building a table, it's bound via root parameter:

### SetSRVTable (D3D12DescriptorCache.cpp, lines 483-505)

```cpp
void FD3D12DescriptorCache::SetSRVTable(
    EShaderFrequency ShaderStage,
    const FD3D12RootSignature* RootSignature,
    FD3D12ShaderResourceViewCache& Cache,
    uint32 SlotsNeeded,
    const D3D12_GPU_DESCRIPTOR_HANDLE& BindDescriptor
)
{
    const uint32 RootParameterIndex = RootSignature->SRVRDTBindSlot(ShaderStage);

    if (ShaderStage == SF_Compute)
    {
        Context.GraphicsCommandList()->SetComputeRootDescriptorTable(
            RootParameterIndex,
            BindDescriptor
        );
    }
    else
    {
        Context.GraphicsCommandList()->SetGraphicsRootDescriptorTable(
            RootParameterIndex,
            BindDescriptor
        );
    }

    // Mark slots outside the current table range as dirty for the next draw
    const SRVSlotMask OutsideCurrentTableRegisterMask =
        ~(((SRVSlotMask)1 << SlotsNeeded) - (SRVSlotMask)1);
    Cache.Dirty(ShaderStage, OutsideCurrentTableRegisterMask);
}
```

**Key Insight:** After setting a descriptor table, UE5 marks slots outside the current table's range as dirty. This ensures that if a later draw uses more slots, the descriptor table is re-bound. For example:
- Draw 1 binds SRVs at slots 0-2 (SlotsNeeded=3)
- Draw 2 needs slots 0-5 (SlotsNeeded=6)
- UE5 marks slots 3-63 as dirty after Draw 1
- Draw 2 detects slot 4 is dirty → rebuilds table → rebinds

## Root Signature Layout (Non-Bindless)

### Structure: D3D12RootSignature.h (lines 13-42)

The root signature is composed of descriptor tables for each shader stage and resource type:

```cpp
enum ERootParameterKeys
{
    // Per-stage entries
    PS_SRVs,        // Root parameter slot for pixel shader SRVs
    PS_CBVs,        // Descriptor table slot for pixel shader CBVs
    PS_RootCBVs,    // Direct CBV slots (inline, not table)
    PS_Samplers,    // Root parameter slot for pixel shader samplers
    VS_SRVs,
    VS_CBVs,
    VS_RootCBVs,
    VS_Samplers,
    VS_UAVs,
    // ... same for GS, MS, AS
    ALL_SRVs,       // Compute stage
    ALL_CBVs,
    ALL_RootCBVs,
    ALL_Samplers,
    ALL_UAVs,
};
```

### Binding Slot Resolution

The root signature stores a mapping from `ERootParameterKeys` → actual root parameter index:

```cpp
class FD3D12RootSignature
{
private:
    uint8 BindSlotMap[RPK_RootParameterKeyCount];  // Maps enum to root param index

public:
    inline uint32 SRVRDTBindSlot(EShaderFrequency ShaderStage) const
    {
        switch (ShaderStage)
        {
        case SF_Vertex: return BindSlotMap[VS_SRVs];
        case SF_Pixel:  return BindSlotMap[PS_SRVs];
        case SF_Compute: return BindSlotMap[ALL_SRVs];
        // ... etc
        }
    }

    inline uint32 SamplerRDTBindSlot(EShaderFrequency ShaderStage) const
    {
        // Similar lookup
    }

    inline uint32 UAVRDTBindSlot(EShaderFrequency ShaderStage) const
    {
        // Similar lookup
    }
};
```

**Implication:** The root signature is **compiled once per unique shader permutation**. It defines which root parameter index holds which resource table. When binding, the cache queries the root signature to find the correct slot, then calls `SetGraphicsRootDescriptorTable`.

## Per-Frame Descriptor Heap Management

### Online Heap Ring Buffer: FD3D12OnlineHeap

```cpp
class FD3D12OnlineHeap : public FD3D12DeviceChild
{
protected:
    TRefCountPtr<FD3D12DescriptorHeap> Heap;   // GPU-visible heap
    uint32 NextSlotIndex = 0;                  // Where next allocation starts
    uint32 FirstUsedSlot = 0;                  // Where GPU is still reading from
    const bool bCanLoopAround;                 // Can wrap to slot 0 when full

public:
    bool CanReserveSlots(uint32 NumSlots);
    uint32 ReserveSlots(uint32 NumSlotsRequested);
    void SetNextSlot(uint32 NextSlot);
};
```

**Heap Lifecycle:**
1. Frame N starts
2. Draws allocate from `NextSlotIndex` forward
3. GPU consumes slots, command list completes
4. Frame N+1 starts → `OpenCommandList()` resets `NextSlotIndex = 0` (or waits for GPU to catch up)
5. Heaps loop around when `NextSlotIndex + NumRequested > HeapSize`

### Two Heap Implementations

#### SubAllocatedOnlineHeap (default, non-bindless)

```cpp
class FD3D12SubAllocatedOnlineHeap : public FD3D12OnlineHeap
{
private:
    FD3D12OnlineDescriptorBlock* CurrentBlock = nullptr;
    FD3D12DescriptorCache& DescriptorCache;
    FD3D12CommandContext& Context;

    bool AllocateBlock();  // Request new block from global heap
};
```

- Allocates descriptor blocks from **global heap**
- When block is full, allocates a new block
- Blocks are returned to global heap when command list finishes
- Lightweight: just manages one sub-allocated range at a time

#### LocalOnlineHeap (overflow, per-context)

```cpp
class FD3D12LocalOnlineHeap : public FD3D12OnlineHeap
{
private:
    struct FPoolEntry
    {
        TRefCountPtr<FD3D12DescriptorHeap> Heap;
        FD3D12SyncPointRef SyncPoint;
    };
    TQueue<FPoolEntry> ReclaimPool;  // Heaps waiting for GPU to finish

public:
    void Init(uint32 InNumDescriptors, ERHIDescriptorHeapType InHeapType);
};
```

- Created lazily if global heap is full or doesn't have space
- Each context owns its own overflow heap
- Heaps recycled when GPU finishes (sync point tracking)
- Only used when sub-allocated heap can't provide more space

**Allocation Flow:**
1. `BuildSRVTable()` calls `ReserveSlots(SlotsNeeded)` on `CurrentViewHeap`
2. If `CurrentViewHeap == SubAllocatedViewHeap` and block is full → `AllocateBlock()` from global
3. If global is exhausted → `SwitchToContextLocalViewHeap()` uses local overflow heap
4. After command list → slots are released, sync point ensures GPU finished

## Sampler Deduplication

### FD3D12SamplerArrayDesc (D3D12DescriptorCache.h, lines 101-121)

```cpp
struct FD3D12SamplerArrayDesc
{
    uint32 Count;
    uint16 SamplerID[MAX_SAMPLERS];  // IDs, not handles

    inline bool operator==(const FD3D12SamplerArrayDesc& rhs) const
    {
        if (Count != rhs.Count) return false;
        return 0 == FMemory::Memcmp(SamplerID, rhs.SamplerID,
                                     sizeof(SamplerID[0]) * Count);
    }
};

typedef FD3D12ConservativeMap<FD3D12SamplerArrayDesc, D3D12_GPU_DESCRIPTOR_HANDLE>
    FD3D12SamplerMap;
```

**How It Works:**

1. Each `FD3D12SamplerState` has a unique `ID`
2. When building a sampler table, create a `SamplerArrayDesc` of `{Count=3, IDs=[5, 10, 7]}`
3. Look up the desc in `SamplerMap`
4. **Cache hit:** Return cached GPU handle, no copy needed
5. **Cache miss:** Copy descriptors, store in `UniqueTables`, add to `SamplerMap`

**Consolidation:** At end of frame (`CloseCommandList`, line 195):

```cpp
GetParentDevice()->GetGlobalSamplerHeap().ConsolidateUniqueSamplerTables(
    UniqueTables
);
UniqueTables.Reset();
```

The global sampler heap consolidates all unique sampler combinations from all contexts into a shared set. This amortizes sampler table creation across contexts.

## Descriptor Heap Heap Rollover & Dirty Tracking

### Rollover Detection

When an online heap runs out of slots:

```cpp
// FD3D12OnlineHeap::ReserveSlots (D3D12DescriptorCache.cpp, lines 949-995)
uint32 FirstRequestedSlot = NextSlotIndex;
uint32 SlotAfterReservation = NextSlotIndex + NumSlotsRequested;

if (bCanLoopAround && SlotAfterReservation > HeapSize)
{
    FirstRequestedSlot = 0;
    SlotAfterReservation = NumSlotsRequested;
    FirstUsedSlot = SlotAfterReservation;
    HeapLoopedAround();  // ← notify derived class
}

NextSlotIndex = SlotAfterReservation;
```

### Dirty Tracking on Rollover

```cpp
// FD3D12DescriptorCache::HeapLoopedAround (D3D12DescriptorCache.cpp, lines 19-25)
void FD3D12DescriptorCache::HeapLoopedAround(ERHIDescriptorHeapType InHeapType)
{
    if (InHeapType == ERHIDescriptorHeapType::Sampler)
    {
        SamplerMap.Reset();  // ← Clear sampler cache, descriptors may be overwritten
    }
}
```

When the heap loops, the sampler map is cleared. The next draw will re-copy samplers because they're not in the cache anymore.

### Heap Changed Dirtying

When the **descriptor heap itself changes** (from sub-allocated to local, or global sampler heap):

```cpp
// FD3D12DescriptorCache::SetDescriptorHeaps (D3D12DescriptorCache.cpp, lines 70-142)
if (LastSetViewHeap != PendingViewHeap)
{
    bHeapChanged = true;
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    if (!BindlessResourcesHeap)
#endif
    {
        Context.StateCache.DirtyViewDescriptorTables();  // ← Mark all view tables dirty
    }
}

if (LastSetSamplerHeap != PendingSamplerHeap)
{
    bHeapChanged = true;
    if (!IsUsingBindlessSamplers())
    {
        Context.StateCache.DirtySamplerDescriptorTables();  // ← Mark all sampler tables dirty
        SamplerMap.Reset();  // ← Cache is invalid in new heap
    }
}

// Actually set heaps on command list
if (bHeapChanged)
{
    ID3D12DescriptorHeap* ppHeaps[] = { PendingViewHeap, PendingSamplerHeap };
    Context.GraphicsCommandList()->SetDescriptorHeaps(
        UE_ARRAY_COUNT(ppHeaps),
        ppHeaps
    );
}
```

**Key Pattern:** UE5 defers heap binding until tables are actually set. If a frame has no draws, heaps are never set. This avoids unnecessary state changes.

## Resource State Transitions (Compute → Graphics)

When a compute shader writes a UAV (e.g., transmittance LUT computation) and a graphics pass reads it as SRV:

### 1. Compute Pass (Offline)

```cpp
// Render graph associates UAV with compute output
ctx.SetComputeRootDescriptorTable(UAVSlot, BuildUAVTable(...));
ctx.Dispatch(...);
```

### 2. Transition (Render Graph Barrier)

The render graph tracks resource state. After the compute pass, it inserts a barrier:

```cpp
// Implied by render graph dependency: UAV → SRV
// D3D12CommandContext::TransitionResource() or similar
// D3D12_RESOURCE_STATE_UNORDERED_ACCESS → D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
```

### 3. Graphics Pass (Offline)

```cpp
// Next pass reads the LUT as SRV
ctx.SetGraphicsRootDescriptorTable(SRVSlot, BuildSRVTable(...));
ctx.DrawIndexed(...);
```

**Important:** State transitions are **render-graph-level**, not RHI-level. The descriptor cache doesn't track resource state — it only manages descriptor copying. The render graph builder tracks resource states and inserts barriers as needed.

## Data Flow Example: Sky Atmosphere LUT

### Component Property → Renderer → Descriptor Table → Shader

**Problem:** A sky atmosphere compute pass writes a transmittance LUT to UAV[0]. A graphics pass later reads it as SRV[5]. How does the descriptor flow?

**Flow:**

1. **Offline Descriptor Creation (frame 0)**
   - Create transmittance LUT texture (UAV)
   - Create SRV(transmittance LUT)
   - Both stored in offline heaps of the texture resource

2. **Compute Pass (frame N)**
   ```cpp
   // Compute shader output
   FRDGTextureRef TransmittanceLUT = GraphBuilder.CreateTexture(Desc, ...);
   BuildSRVTable(SF_Compute, ...) → copies ComputeOutputUAV to online heap
   SetGraphicsRootDescriptorTable(ComputeSlot, GPUHandle)
   Dispatch(...)
   ```
   - Descriptor copied from offline UAV heap to online heap
   - GPU writes texture via UAV

3. **Transition (Render Graph)**
   - Render graph inserts UAV → SRV transition
   - Resource state changes from UNORDERED_ACCESS to SHADER_RESOURCE

4. **Graphics Pass (frame N)**
   ```cpp
   // Pixel shader input
   BuildSRVTable(SF_Pixel, ...) → copies TransmittanceLUT SRV to online heap
   SetGraphicsRootDescriptorTable(PixelSlot, GPUHandle)
   DrawIndexed(...)
   ```
   - Descriptor copied from offline SRV heap to **different online heap slot** than compute
   - GPU reads texture via SRV

**Key Insight:** The **offline descriptors never move**. Only the **online heap copies** change slot. This allows multiple passes to reference the same resource without descriptor aliasing issues.

## Fullscreen Passes on Non-Bindless DX12

### Example: Sky Atmosphere Pixel Shader

Fullscreen passes don't need special handling. The standard flow applies:

```cpp
// FSkyAtmosphereRendering.cpp (pseudocode, actual code is complex)
void RenderSkyAtmosphere(FRDGBuilder& GraphBuilder, ...)
{
    // Create render target
    FRDGTextureRef SkyTarget = GraphBuilder.CreateTexture(...);

    // Add graphics pass
    GraphBuilder.AddPass(
        FRDGEventName(TEXT("SkyAtmosphere")),
        ...,
        [SkyTarget, TransmittanceLUT, ...](FRHICommandList& RHICmdList)
        {
            // Bind render target
            RHICmdList.SetRenderTarget(SkyTarget, ...);

            // Set shader
            RHICmdList.SetGraphicsPipelineState(PSO);

            // Bind descriptors
            DescriptorCache.BuildSRVTable(SF_Pixel, ..., TransmittanceLUT);
            DescriptorCache.SetSRVTable(SF_Pixel, ..., ...);

            DescriptorCache.BuildSamplerTable(SF_Pixel, ...);
            DescriptorCache.SetSamplerTable(SF_Pixel, ...);

            // Draw fullscreen quad
            RHICmdList.DrawIndexedPrimitive(
                ScreenTriangleIndexBuffer,
                0,  // BaseVertexIndex
                0,  // MinIndex
                3,  // NumVertices (triangle)
                0,  // StartIndex
                1,  // NumPrimitives
                1   // NumInstances
            );
        }
    );
}
```

No bindless-specific code. Fullscreen passes use the exact same descriptor binding path as regular geometry passes.

## Differences: Bindless vs Non-Bindless

### Non-Bindless (SM6.0-SM6.5, Analyzed Here)

- **Root Signature:** Contains descriptor tables per shader stage per resource type
- **Descriptor Copying:** Per-draw, staged into online heaps
- **Root Parameters Used:** ~10-20 (one per stage per resource type)
- **Shader Code:** Uses explicit `SamplerStates[i]`, `Texture2D<T> ResourceName : register(t2)`
- **Deduplication:** Sampler tables cached by ID array

### Bindless (SM6.6+, Not Analyzed Here)

- **Root Signature:** Minimal, often just one descriptor table (all SRVs/UAVs in one heap)
- **Descriptor Copying:** Rare, one global heap per resource type
- **Root Parameters Used:** ~2-4 (view heap, sampler heap, maybe static sampler heap)
- **Shader Code:** Uses bindless indexing `Resources[srvIndex]`, variable-rate indexing
- **Deduplication:** Not needed, single large heap

UE5 has a compile-time flag `PLATFORM_SUPPORTS_BINDLESS_RENDERING` (line 48 of DescriptorCache.cpp) to switch strategies.

## Critical Implementation Details for UzEngine

### 1. Per-Draw Descriptor Table Management

Unlike some engines that pre-allocate descriptor tables, UE5 **allocates fresh descriptors per draw**:

```cpp
// For each draw that uses SRVs:
uint32 NewHeapSlot = 0;
D3D12_GPU_DESCRIPTOR_HANDLE Handle = BuildSRVTable(sf, rootSig, srvCache, ..., NewHeapSlot);
SetSRVTable(sf, rootSig, srvCache, ..., Handle);
```

The `NewHeapSlot` variable is **accumulated across the frame**, allowing all draws to share slots in a linear allocator pattern.

**Implication for UzEngine:** If implementing non-bindless, you must:
- Maintain a per-frame slot counter
- Allocate fresh descriptor tables for each unique shader stage/resource type combination
- Track which ranges are still in use (for bindless adoption later)

### 2. Dirty Tracking is Essential

The descriptor cache relies on **state cache dirty tracking** to avoid redundant descriptor table bindings:

```cpp
// If pixel shader SRVs haven't changed, don't rebuild
if (!Cache.DirtySlotMask[SF_Pixel])
{
    return;  // Skip SetSRVTable
}
```

**Implication for UzEngine:** Build a dirty mask system:
- One mask per resource type per shader stage
- Mark slots dirty when resource is set
- Clear dirty bits after binding
- Check dirty bits to skip redundant bindings

### 3. Sampler Map Caching is Performance-Critical

The sampler deduplication cache is a bottleneck. Most frames reuse the same samplers:

```cpp
FD3D12SamplerArrayDesc Desc = { 3, {5, 10, 7} };
if (SamplerMap.Find(Desc))
{
    return cached_handle;  // No copy, ~0 GPU overhead
}
```

**Implication for UzEngine:** Implement sampler caching early. If 100 draws use the same 3 samplers, caching saves 99 descriptor copies.

### 4. Offline Descriptor Heaps are Permanent

SRV/UAV/CBV/Sampler descriptors are created once when the resource is created and stored offline:

```cpp
// In resource constructor
FD3D12ShaderResourceView::FD3D12ShaderResourceView(...)
{
    Device->GetDescriptorAllocator().Allocate(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        OfflineDescriptor  // Stored and never moves
    );
    Device->CreateShaderResourceView(..., OfflineDescriptor);
}
```

**Implication for UzEngine:** Separate your descriptor allocators:
- **Offline allocator:** Permanent, per-resource
- **Online allocator:** Transient, per-frame, ring buffer

### 5. Heap Switching is Expensive

When CurrentViewHeap changes (from sub-allocated to local), all descriptor tables are marked dirty:

```cpp
if (LastSetViewHeap != PendingViewHeap)
{
    Context.StateCache.DirtyViewDescriptorTables();  // All tables marked dirty
    SetDescriptorHeaps(true);  // Force bind
}
```

**Implication for UzEngine:** Minimize heap switches:
- Size your global heaps large enough to avoid overflow to local heaps
- If you must overflow, batch multiple passes into one local heap allocation
- Monitor `STAT_ViewHeapChanged` and `STAT_SamplerHeapChanged` stats

### 6. Root Signature Layout Affects Performance

Each unique root signature requires different descriptor table slots:

```cpp
// Root sig A: root param 0 = SRVs, root param 1 = samplers
// Root sig B: root param 0 = samplers, root param 1 = SRVs (different order)

// Binding for sig A
SetGraphicsRootDescriptorTable(0, SRVHandle);  // ← slot 0
SetGraphicsRootDescriptorTable(1, SamplerHandle);  // ← slot 1

// Binding for sig B uses different slots!
```

**Implication for UzEngine:** Standardize your root signature layout:
- All graphics pipelines use same slot order
- Minimize root signature variations
- Consider a canonical "graphics root signature" for most passes

## Visual Summary: Descriptor Flow per Draw

```
┌─────────────────────────────────────────────────────────────┐
│ Draw Call: DrawIndexed(...)                                 │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ State Cache: Check if SRV table is dirty for pixel shader   │
│ if (!DirtySlotMask[SF_Pixel]) return;  // Skip if clean     │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ BuildSRVTable()                                              │
│ 1. Reserve online heap slots: uint32 slot = 0; slot += 5    │
│ 2. Gather from offline heaps: SrcDesc[0] = TextureSRV       │
│ 3. Copy to online: Device->CopyDescriptors(online, offline) │
│ 4. Return GPU handle to online slot 0                       │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ SetSRVTable()                                                │
│ 1. Query root signature: rootParamIndex = SRVRDTBindSlot()  │
│ 2. Bind: SetGraphicsRootDescriptorTable(slot, GPUHandle)    │
│ 3. Mark outside range dirty: Dirty(SF_Pixel, ~mask)         │
└─────────────────────────────────────────────────────────────┘
                           ↓
┌─────────────────────────────────────────────────────────────┐
│ GPU: Reads descriptor table from bound GPU handle            │
│ For each pixel, shader reads t[5] → desc[0] → texture       │
└─────────────────────────────────────────────────────────────┘
```

## Performance Characteristics

### Copy Overhead

- **Descriptor copy:** ~0.1 microseconds per descriptor (CPU side)
- **100 SRVs per draw:** ~10 microseconds per draw
- **1000 draws:** ~10 milliseconds per frame copy overhead

UE5 masks this with early dirty checks and sampler caching.

### Bind Overhead

- **SetGraphicsRootDescriptorTable:** ~10-20 CPU cycles (DX12 validation overhead)
- **Per unique table:** ~100-200 CPU cycles total (including dirty check)
- **1000 draws, 500 unique tables:** ~500 microseconds

### Heap Allocation

- **ReserveSlots:** ~100 CPU cycles (inline heap pointer arithmetic)
- **Mostly free** if heap has space
- **Expensive only on rollover** (needs new block allocation or heap switch)

## Configuration & Tuning

### Heap Sizes

UE5 configures via console variables or config files:

- `NumLocalViewDescriptors`: Typically 16K-64K (per-context)
- `GlobalViewHeapSize`: Typically 256K-512K (shared)
- `GlobalSamplerHeapSize`: Typically 8K-16K (shared, sampler-heavy)

For UzEngine, estimate based on worst-case frame:
- Max unique resource tables per frame × 1.5 safety margin

### Sampler Consolidation

The global sampler heap consolidates unique tables from all contexts. This is called at end-of-frame:

```cpp
GetParentDevice()->GetGlobalSamplerHeap().ConsolidateUniqueSamplerTables(
    UniqueTables
);
```

UE5 batches sampler consolidation to reduce allocations.

## References

- **UE5 Source**: `/Engine/Source/Runtime/D3D12RHI/Private/`
- **Microsoft DX12 Docs**: https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures
- **GPU Programming Guide**: Descriptor binding is a critical path for GPU utilization
- **Related Specs**: Resource State Transitions (D3D12 UAV → SRV transitions)

## Appendix: Non-Bindless vs Bindless Code Paths

### Bindless Check (D3D12DescriptorCache.h, lines 314-320)

```cpp
#if PLATFORM_SUPPORTS_BINDLESS_RENDERING
    bool IsUsingBindlessResources() const { return bBindlessResources; }
    bool IsUsingBindlessSamplers()  const { return bBindlessSamplers;  }
#else
    constexpr bool IsUsingBindlessResources() const { return false; }
    constexpr bool IsUsingBindlessSamplers()  const { return false; }
#endif
```

At compile time, non-bindless platforms compile out bindless code paths. UzEngine should do the same:

```cpp
#if UZ_SUPPORTS_BINDLESS
    bool IsBindless() const { return bBindless; }
#else
    constexpr bool IsBindless() const { return false; }
#endif
```

This ensures zero overhead for non-bindless hardware.

---

**Document Status:** Complete analysis of UE5's non-bindless DX12 descriptor management. Ready for implementation reference.
