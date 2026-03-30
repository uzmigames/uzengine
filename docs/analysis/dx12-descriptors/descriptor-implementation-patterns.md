<!-- doc-meta
title: UE5 DX12 Descriptor - Critical Code Patterns
description: Canonical implementations of each descriptor pattern from UE5 source
category: rendering
topics: descriptors, code-patterns, implementation-reference
status: complete
-->

# UE5 DX12 Descriptor - Critical Code Patterns

This document extracts the EXACT code patterns from UE5 source for each critical operation.

## Pattern 1: BuildSRVTable (Lines 432-481)

Reserve online heap slots, copy offline descriptors to online heap, return GPU handle.

**Key Steps:**
1. Check dirty bits (if not dirty, skip)
2. Reserve slots: `FirstSlotIndex = HeapSlot; HeapSlot += SlotsNeeded;`
3. Get online heap CPU handle: `CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex)`
4. Gather offline descriptors: `SRV->GetOfflineCpuHandle()`
5. Copy: `GetParentDevice()->CopyDescriptors(DestCPU, SrcCPU[], Count, Type)`
6. Mark clean: `CleanSlots(DirtyMask, SlotsNeeded)`
7. Return GPU handle: `CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex)`

**Implementation Template:**
```cpp
uint32 FirstSlot = heapSlot;
heapSlot += slotsNeeded;

D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle =
    onlineHeap->GetCPUSlotHandle(FirstSlot);

// Gather offline descriptors
D3D12_CPU_DESCRIPTOR_HANDLE offlineHandles[MAX];
for (uint32 i = 0; i < slotsNeeded; ++i) {
    offlineHandles[i] = GetOfflineDescriptor(i);
}

// Copy
device->CopyDescriptors(
    cpuHandle, offlineHandles, slotsNeeded,
    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
);

return onlineHeap->GetGPUSlotHandle(FirstSlot);
```

## Pattern 2: SetSRVTable (Lines 483-505)

Query root signature for slot, call SetGraphicsRootDescriptorTable.

**Key Steps:**
1. Get root param: `uint32 slot = RootSig->SRVRDTBindSlot(Stage)`
2. Bind: `CmdList->SetGraphicsRootDescriptorTable(slot, gpuHandle)`
3. Mark outside dirty: `Cache.Dirty(Stage, ~mask)`

```cpp
uint32 rootParam = rootSig->SRVRDTBindSlot(stage);
if (stage == SF_Compute) {
    cmdList->SetComputeRootDescriptorTable(rootParam, gpuHandle);
} else {
    cmdList->SetGraphicsRootDescriptorTable(rootParam, gpuHandle);
}
```

## Pattern 3: Sampler Cache Check (Before Copy)

```cpp
SamplerArrayDesc key = {Count, IDs[]};
if (auto* cached = SamplerMap.Find(key)) {
    return *cached;  // Cache hit, no copy!
}
// Cache miss: copy (pattern 1), then add to map
```

## Pattern 4: HeapLoopAround Callback

When `NextSlot + NumRequested > HeapSize`:
```cpp
FirstSlot = 0;
SlotAfterRequest = NumRequested;
FirstUsedSlot = SlotAfterRequest;
HeapLoopedAround();  // ← Reset sampler cache here
NextSlot = SlotAfterRequest;
```

## Pattern 5: Root Signature Query

```cpp
// Multiple root signatures, each maps resources to slots
inline uint32 SRVRDTBindSlot(EShaderFrequency Stage) const {
    switch(Stage) {
        case SF_Vertex: return BindSlotMap[VS_SRVs];
        case SF_Pixel: return BindSlotMap[PS_SRVs];
        case SF_Compute: return BindSlotMap[ALL_SRVs];
        // ...
    }
}
```

## Critical Properties

**Per-Draw:**
- Allocate fresh online heap slots (don't reuse)
- Copy offline → online
- Bind via SetGraphicsRootDescriptorTable()

**Per-Frame:**
- OpenCommandList() at start (reset heap pointers)
- CloseCommandList() at end (release blocks, consolidate sampler cache)
- HeapLoopAround() when wrap detected (reset sampler map)

**Immutable After Copy:**
- After CopyDescriptors(), descriptors are GPU-readable
- Don't modify or move until GPU finishes
- Next frame reuses same online heap slots

---

**Ready for implementation:** Copy these patterns directly into UzEngine code.
