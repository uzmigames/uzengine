<!-- doc-meta
title: UE5 DX12 Descriptor Architecture - Design Decisions
description: Why UE5 made specific choices in descriptor management, and how they apply to UzEngine
category: rendering
topics: dx12, architecture, descriptors, design-patterns
status: complete
-->

# UE5 DX12 Descriptor Architecture - Design Decisions

## Problem: How to Bind GPU Resources on DX12 Without Bindless?

DX12 requires resources to be bound via a **root signature + descriptor table** pattern. The fundamental challenge:

1. **Resources are created offline** (CPU allocates descriptors in CPU heaps)
2. **GPU must read descriptors** (descriptors must be in GPU-visible heaps)
3. **GPU access is fast** (copying every draw is expensive)
4. **Shaders vary in resource needs** (different passes use different resources)

UE5 solved this with a **two-heap descriptor strategy**.

## Decision 1: Offline + Online Heap Separation

### Problem

If all descriptors lived in GPU-visible heaps, creating a new resource would require:
1. Allocate GPU-visible heap space
2. Create descriptor
3. Persist forever (no way to deallocate from GPU-visible heap efficiently)

After 1000 resources, GPU heap is fragmented and running out of space.

### UE5's Solution

**Offline heaps:** Permanent, CPU-visible, per-resource
- Create descriptor when resource is created
- Descriptor never moves
- Never deallocated (lives as long as resource)

**Online heaps:** Transient, GPU-visible, per-frame or per-pass
- Allocate slots as needed
- Copy descriptors from offline → online
- Reuse slots next frame

### Benefits

- **Resource creation is simple:** Just allocate offline descriptor, done
- **Offline heap is stable:** No fragmentation, no deallocation
- **Online heap is large but bounded:** Heaps loop around, slots reused
- **Lazy binding:** Only copy descriptors for draws that need them

### Cost

- **Per-draw descriptor copy:** ~0.1 µs per descriptor (100 SRVs = 10 µs)
- **Online heap management:** Need ring buffer logic, rollover handling
- **Dirty tracking overhead:** Track which descriptors changed

## Decision 2: Per-Draw Descriptor Table Staging

### Problem

UE5 could batch descriptor copies per-frame:
- Frame start: Gather all SRVs from all draws
- Copy once into online heap
- All draws reuse same table

But this requires:
1. Pre-analyzing all draws before rendering
2. Allocating one monolithic table per resource type
3. Every draw using same table (even if 90% of descriptors are unused)

### UE5's Solution

**Per-draw staging:** Each draw reserves only the descriptors it needs

```cpp
Draw 1: uses 3 textures → allocate 3 slots, bind table
Draw 2: uses 5 textures → allocate 5 new slots, bind different table
Draw 3: uses 3 textures → allocate 3 new slots (not reusing Draw 1's)
```

All allocations packed linearly in heap:
```
Online Heap: [Draw1: 3 slots][Draw2: 5 slots][Draw3: 3 slots]...
```

### Benefits

- **Flexible:** Each draw can have different number of descriptors
- **Fast path:** Sampler caching avoids redundant copies
- **Incremental:** Can add more draws without reallocation
- **Lazy:** Only copy what's needed

### Cost

- **More descriptor copies:** If 1000 draws each copy same samplers, 1000 copies
- **More root parameter changes:** `SetGraphicsRootDescriptorTable()` per draw
- **Complexity:** Need dirty tracking + sampler cache to recover performance

**Mitigation:** Sampler deduplication cache cuts sampler copy overhead from 1000× to ~1×.

## Decision 3: Sampler Deduplication Cache

### Problem

Samplers are expensive to copy:
- Each draw might bind the same 3 samplers (linear, repeat, clamp)
- But online heap doesn't know these are the same
- So it copies 3 descriptors × 1000 draws = 3000 copies

### UE5's Solution

**Sampler map cache:**
1. Hash the sampler ID array: `[5, 10, 7]`
2. Check if already in cache
3. If yes: return cached GPU handle (no copy!)
4. If no: copy, add to cache

```cpp
SamplerArrayDesc Desc = { Count: 3, IDs: [5, 10, 7] };
if (auto* Cached = SamplerMap.Find(Desc)) {
    return *Cached;  // ← No copy!
}
// Copy and cache
D3D12_GPU_DESCRIPTOR_HANDLE Handle = BuildSamplerTable(...);
SamplerMap.Add(Desc, Handle);
```

### Benefits

- **Reduces copies:** 3000 → ~10 (one per unique combo)
- **Fast lookup:** Hash map, O(1) average case
- **Consolidated heap:** Global sampler heap shared across contexts

### Cost

- **Cache invalidation:** When heap loops, entire cache is invalidated
- **Memory overhead:** Hash table storing unique combinations
- **Complexity:** Map management, consolidation at frame end

**Worthwhile:** Sampler patterns are highly repetitive. Cache hit rate ~95%.

## Decision 4: Root Signature Per Shader Permutation

### Problem

UE5 compiles the same shader with multiple permutations (e.g., with/without shadows). Each permutation uses different root signature slots:
- Permutation A: SRVs at root param 1
- Permutation B: SRVs at root param 2

If a single PSO changes shaders, the root signature changes.

### UE5's Solution

**Root signature is part of the PSO hash:**
- Each unique PSO includes unique root signature
- Root signature defines where each resource type binds

```cpp
// In FD3D12GraphicsPipelineState
FD3D12RootSignature* RootSignature;  // ← Per-PSO

// When rendering:
PSO->SetRootSignature();
DescriptorCache.BuildSRVTable(
    ShaderStage,
    PSO->GetRootSignature(),  // ← Get binding slots from PSO
    ...
);
```

### Benefits

- **Flexible:** Each PSO can optimize root signature for its shaders
- **Zero runtime overhead:** Binding slots known at compile time
- **Per-shader customization:** Different shaders can use different slots

### Cost

- **More root signatures:** One per unique shader permutation
- **GPU memory:** Each PSO stores root signature blob
- **Complexity:** Root signature creation during PSO compilation

**Tradeoff:** Few hundred PSOs × small root sig overhead = negligible.

## Decision 5: Explicit Resource State Transitions

### Problem

When compute writes a UAV and graphics reads it as SRV, GPU must transition state:
```
Compute: D3D12_RESOURCE_STATE_UNORDERED_ACCESS
Barrier: ↓ (state transition)
Graphics: D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
```

UE5 could handle this in the descriptor cache, but that couples:
- Descriptor management (which should be isolated)
- Resource state tracking (which should be graph-level)

### UE5's Solution

**Render graph manages state, descriptor cache ignores it:**

```cpp
// Render graph tracks:
ComputePass.Output[LUT] → UAV state
GraphicsPass.Input[LUT] → SRV state
// RDG inserts barrier automatically

// Descriptor cache only copies descriptors:
BuildUAVTable(...) → copies UAV descriptor
// (doesn't care about state)
BuildSRVTable(...) → copies SRV descriptor
// (doesn't care about state)
```

### Benefits

- **Separation of concerns:** Descriptor cache ≠ state tracking
- **Composable:** RDG can optimize barrier placement
- **Debuggable:** State transitions visible in RDG graph
- **Flexible:** Other systems can hook state tracking

### Cost

- **Two separate systems:** Need both descriptor cache + state tracking
- **Coordination:** Must keep them in sync (barriers before SRV binding)
- **Complexity:** More moving parts

**Worthwhile:** Clean architecture worth the extra system.

## Decision 6: Linear Heap Allocator (Not Buddy/Pool)

### Problem

UE5 allocates descriptor heap slots linearly:
```
Frame N: [Slot 0][Slot 1]...[Slot 1000][Slot 1001]... [FULL]
Frame N+1: Loop to Slot 0 (assuming GPU finished Slot 0-1000)
```

Alternative: Buddy allocator or pool allocator for fine-grained control.

### UE5's Solution

**Simple linear allocator with fence-based recycling:**

1. Allocate linearly from `NextSlot`
2. Track GPU fence when heap wraps
3. When GPU catches up, reuse slots

```cpp
if (SlotAfterReservation > HeapSize) {
    FirstSlot = 0;  // Loop to start
    SlotAfterReservation = NumRequested;
    HeapLoopedAround();
}
NextSlot = SlotAfterReservation;
```

### Benefits

- **Simple:** O(1) allocation, just increment counter
- **Cache-friendly:** Linear allocation preserves spatial locality
- **Predictable:** No fragmentation
- **Fast path:** Most allocations are trivial

### Cost

- **All-or-nothing:** Either heap has space or doesn't (no partial reuse)
- **Heap size tuning:** Must guess max slots per frame
- **Rollover stalls:** If GPU hasn't caught up, must stall

**Worthwhile:** Simplicity and performance justify lack of granularity.

## Decision 7: Lazy Local Heap Allocation

### Problem

UE5 pre-allocates a global view heap. But in the editor, it might not be big enough (complex scenes with many materials).

Options:
1. Allocate huge global heap (waste memory for simple games)
2. Dynamically allocate per-context heaps (complex, fragmentation)
3. Allocate lazily on demand (middle ground)

### UE5's Solution

**Lazy local heap allocation:**

```cpp
if (SubAllocatedHeap.IsFull()) {
    if (!LocalViewHeap) {
        LocalViewHeap = new FD3D12LocalOnlineHeap(...);
        LocalViewHeap->Init(BigSize);
    }
    CurrentViewHeap = LocalViewHeap;
    SetDescriptorHeaps();  // Switch to local heap
}
```

Local heap is per-context, allocated only when needed.

### Benefits

- **Memory efficient:** Global heap sized for typical game, local added on demand
- **Editor friendly:** Complex scenes don't pre-allocate 1GB heap
- **Simple:** No fragmentation (each context owns its own local heap)

### Cost

- **Complexity:** Two heap types (sub-allocated, local)
- **Heap switches:** Switching incurs dirty tracking overhead
- **Sync complexity:** Local heaps need fence tracking

**Worthwhile:** Editor needs this; game builds rarely hit it.

## Design Principles Extracted

### 1. Separate Concerns: Offline vs Online

- **Offline:** Resource creation, permanent lifetime
- **Online:** Rendering, transient lifetime
- They never interact except during copy (BuildXxxTable)

**For UzEngine:** Design descriptor allocators with this separation. Don't mix.

### 2. Linear Allocation + Ring Buffer

- Linear allocator is simple and fast
- Ring buffer handles reuse without fragmentation
- Fence-based recycling ensures GPU is ready

**For UzEngine:** Use linear allocator for first implementation. Optimize later if needed.

### 3. Caching + Dirty Tracking

- Cache common patterns (samplers)
- Track dirty state to skip redundant work
- Invalidate cache conservatively (on heap loops)

**For UzEngine:** Implement sampler cache early. It's a big win.

### 4. Root Signature as Binding Contract

- Root signature defines where each resource type binds
- Query root signature to find root parameter index
- Bake root signature into PSO

**For UzEngine:** Standardize root signature across PSOs, minimize variations.

### 5. Explicit State Transitions in Graph

- Descriptor cache ignores state
- Render graph tracks state and inserts barriers
- They coordinate via resource dependencies

**For UzEngine:** Keep these separate. Don't let descriptor cache manage state.

### 6. Progressive Degradation

- Fast path: Sampler cache hit (no copy)
- Slow path: Copy to local heap (stall)
- Both work, just different performance

**For UzEngine:** Design for progressive degradation. Graceful when running out of memory.

## Potential Pitfalls UE5 Avoids

### Pitfall 1: Using GPU Heap for All Descriptors

**Wrong:** Allocate all descriptors (online + offline) in GPU heap

**Why it fails:**
- GPU heap can't be deallocated efficiently
- Fragmentation after 1000 resources
- Memory exhaustion

**UE5's fix:** Offline heap (CPU-visible) for permanent descriptors

### Pitfall 2: One Monolithic Table per Frame

**Wrong:** Analyze all draws, pack one SRV table, all draws reuse it

**Why it fails:**
- Can't handle variable-size descriptor needs
- Wastes slots on unused resources
- Can't incrementally add passes

**UE5's fix:** Per-draw tables, linear packing

### Pitfall 3: Copying Samplers Every Frame

**Wrong:** Build new sampler table every draw, copy all descriptors

**Why it fails:**
- Sampler patterns repeat 1000×
- Copy overhead becomes noticeable
- Descriptor cache fills up

**UE5's fix:** Deduplication cache, shared global sampler heap

### Pitfall 4: Tight Coupling: Descriptors ↔ State

**Wrong:** Descriptor cache also manages resource state transitions

**Why it fails:**
- Descriptor cache becomes bloated
- State tracking is graph-level problem
- Can't optimize barriers independently

**UE5's fix:** Separate systems, render graph manages state

### Pitfall 5: One Root Signature for All Shaders

**Wrong:** Global root signature used by all PSOs

**Why it fails:**
- Different shaders need different binding patterns
- Can't optimize for each shader
- Force all shaders into same layout

**UE5's fix:** Root signature per PSO, custom bindings per shader

## Recommendations for UzEngine

### For Initial Implementation (Non-Bindless, SM6.0)

1. **Implement offline heap** per resource (SRV, UAV, CBV, Sampler)
   - CPU allocator, CPU-visible heap
   - Simple, just need allocation tracking

2. **Implement online heap** with linear allocator + ring buffer
   - GPU-visible heap, sub-allocated or local
   - `ReserveSlots()` increments counter
   - `LoopAround()` resets to slot 0 on wrap

3. **Implement BuildXxxTable() methods**
   - Reserve online slots
   - Copy offline → online
   - Return GPU handle

4. **Implement SetXxxTable() methods**
   - Query root signature for binding slot
   - Call `SetGraphicsRootDescriptorTable()`
   - Mark dirty bits

5. **Implement sampler cache**
   - Hash map of `SamplerID array → GPU handle`
   - Check before copying samplers
   - Invalidate on heap rollover

6. **Wire RenderGraph** to insert state transitions
   - Don't let descriptor cache manage state
   - RDG inserts barriers based on resource dependencies

### For Migration to Bindless (SM6.6+)

1. Consolidate multiple descriptor tables into one
2. Switch to variable-rate indexing in shaders
3. Eliminate sampler cache (not needed)
4. Reduce root signature to 2-4 parameters
5. Benchmark: should see ~2-5% frame time improvement

---

**Document Status:** Design rationale ready for architectural reviews.
