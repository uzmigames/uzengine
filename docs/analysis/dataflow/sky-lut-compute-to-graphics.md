<!-- doc-meta
title: Data Flow - Sky LUT Compute to Graphics
description: Complete pipeline trace for sky atmosphere transmittance LUT from compute shader write to graphics shader read
category: rendering
topics: data-flow, sky-atmosphere, compute, graphics, descriptors, uav, srv
status: complete
-->

# Data Flow: Sky Atmosphere Transmittance LUT (Compute → Graphics)

## Overview

This document traces how the sky atmosphere transmittance LUT flows from compute shader output to graphics shader input in UE5 DX12 (non-bindless).

**Scenario:** A compute pass writes a transmittance LUT texture via UAV. Later, a graphics pass reads it as SRV. The flow covers: compute output → RDG barrier → graphics input → shader read.

## Reference: UE5 Implementation

| Stage | File | Function/Line | What It Does |
|-------|------|----------------|--------------|
| 1a. Compute Output Setup | `D3D12DescriptorCache.cpp:219-260` | `BuildUAVTable()` | Reserves online heap slots, copies offline UAV descriptor to online heap |
| 1b. Compute Binding | `D3D12DescriptorCache.cpp:262-287` | `SetUAVTable()` | Calls `SetComputeRootDescriptorTable()` with GPU handle |
| 1c. Compute Execution | (RHI Command List) | `Dispatch()` | GPU executes compute shader, writes LUT via UAV |
| 2. State Transition | `RenderGraph.cpp` | (implicit barrier) | RDG inserts `UAV → SRV` state transition in command list |
| 3a. Graphics SRV Setup | `D3D12DescriptorCache.cpp:432-481` | `BuildSRVTable()` | Reserves online heap slots, copies offline SRV descriptor to online heap |
| 3b. Graphics Binding | `D3D12DescriptorCache.cpp:483-505` | `SetSRVTable()` | Calls `SetGraphicsRootDescriptorTable()` with GPU handle |
| 3c. Graphics Execution | (RHI Command List) | `DrawIndexed()` | GPU executes graphics shader, reads LUT via SRV |
| 4. Shader Access | `SkyAtmosphere.usf` (pseudocode) | `main()` → `texture[t5].Sample()` | Shader reads LUT from descriptor table |

## Detailed Trace

### Stage 1a: Compute Output Setup (BuildUAVTable)

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12DescriptorCache.cpp:219-260`

```cpp
D3D12_GPU_DESCRIPTOR_HANDLE FD3D12DescriptorCache::BuildUAVTable(
    EShaderFrequency ShaderStage,           // SF_Compute
    const FD3D12RootSignature* RootSignature,
    FD3D12UnorderedAccessViewCache& Cache,  // Compute UAV cache
    const UAVSlotMask& SlotsNeededMask,     // Bit 0 set (need UAV slot 0)
    uint32 SlotsNeeded,                     // 1 (just the transmittance UAV)
    uint32& HeapSlot                        // [in/out] next available slot
)
{
    // Line 228-229: Reserve space in online heap
    uint32 FirstSlotIndex = HeapSlot;       // e.g., HeapSlot = 0 at frame start
    HeapSlot += SlotsNeeded;                 // HeapSlot becomes 1 after this call

    // Line 231-232: Get CPU and GPU handles from online heap
    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor(
        CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex)  // Slot 0, CPU-writeable
    );
    D3D12_GPU_DESCRIPTOR_HANDLE BindDescriptor(
        CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex)  // Slot 0, GPU-readable
    );

    // Line 233-250: Gather offline descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptors[MAX_UAVS];
    for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
    {
        if ((SlotIndex < UAVStartSlot) || (UAVs[SlotIndex] == nullptr))
        {
            SrcDescriptors[0] = DefaultViews.NullUAV;  // Fallback if not set
        }
        else
        {
            // Line 247: Get offline descriptor from UAV resource
            SrcDescriptors[0] = UAVs[SlotIndex]->GetOfflineCpuHandle();
            // ↑ This is the transmittance LUT's offline UAV descriptor
            //   Stored in the resource's offline heap, never moves

            // Line 249: Update residency (ensure GPU can access memory)
            Context.UpdateResidency(Cache.Resources[ShaderStage][SlotIndex]);
        }
    }

    // Line 252: Mark dirty bits as clean
    FD3D12UnorderedAccessViewCache::CleanSlots(CurrentDirtySlotMask, SlotsNeeded);

    // Line 257: Copy offline descriptor to online heap
    GetParentDevice()->CopyDescriptors(
        DestDescriptor,          // Online heap, slot 0, CPU handle
        SrcDescriptors,          // Offline heap descriptor
        SlotsNeeded,             // 1 descriptor
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );
    // After this: Online heap slot 0 now contains a copy of the transmittance UAV descriptor

    // Line 259: Return GPU handle for binding
    return BindDescriptor;  // GPU handle to online slot 0
}
```

**Data State After Step 1a:**

| Location | Content | GPU-Readable | Notes |
|----------|---------|---------|-------|
| TransmittanceLUT.OfflineUAV | UAV descriptor | No | Created when texture allocated; permanent |
| OnlineHeap[0] | Copy of UAV descriptor | Yes | Allocated this frame; valid for this compute pass |
| BindDescriptor | GPU virtual address of OnlineHeap[0] | Yes | Points to slot 0 |

### Stage 1b: Compute Binding (SetUAVTable)

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12DescriptorCache.cpp:262-287`

```cpp
void FD3D12DescriptorCache::SetUAVTable(
    EShaderFrequency ShaderStage,  // SF_Compute
    const FD3D12RootSignature* RootSignature,
    FD3D12UnorderedAccessViewCache& Cache,
    uint32 SlotsNeeded,            // 1
    const D3D12_GPU_DESCRIPTOR_HANDLE& BindDescriptor  // GPU handle to online slot 0
)
{
    // Line 265: Get root parameter slot from root signature
    const uint32 RootParameterIndex = RootSignature->UAVRDTBindSlot(SF_Compute);
    // Assume root signature maps ALL_UAVs to root parameter 3
    // ↓ RootParameterIndex = 3

    // Line 275: Set root descriptor table on command list
    Context.GraphicsCommandList()->SetComputeRootDescriptorTable(
        3,                   // Root parameter index
        BindDescriptor       // GPU handle to online heap slot 0
    );
    // GPU command list now records: "At root parameter 3, bind GPU heap handle (online slot 0)"

    // Line 281-282: Mark slots outside current table as dirty for next draw
    const UAVSlotMask OutsideCurrentTableRegisterMask =
        ~(((UAVSlotMask)1 << SlotsNeeded) - (UAVSlotMask)1);
    // Mask = ~1 = 0xFFFFFFFFFFFFFFFE (all slots except 0)
    Cache.Dirty(SF_Compute, OutsideCurrentTableRegisterMask);
    // If next compute pass needs UAV slots 0-2, slot 1-2 are dirty → rebuild table
}
```

**GPU Command List After Step 1b:**

```
SetDescriptorHeaps( { OnlineHeap, SamplerHeap } )
SetComputeRootSignature( RootSig )
SetComputeRootDescriptorTable( 3, OnlineHeap.slot0.GPUHandle )
```

### Stage 1c: Compute Execution (Dispatch)

**File:** (RHI Command List execution)

```cpp
Context.GraphicsCommandList()->Dispatch(8, 8, 1);
// GPU executes compute shader with 8x8x1 thread groups
// Each thread reads from transmittance LUT registers: descriptor_table[3].uav[0]
// → Resolves to online heap slot 0 → offline descriptor → transmittance LUT resource
// GPU writes to transmittance LUT via the UAV
```

**GPU State After Step 1c:**

- Transmittance LUT texture: `D3D12_RESOURCE_STATE_UNORDERED_ACCESS` (write by compute)
- GPU memory: Contains written LUT data (32-bit floats, e.g., 32×32×32)

### Stage 2: State Transition (RDG Barrier)

**File:** `Renderer/Core/RenderGraph.cpp` (implicit)

The render graph builder tracks resource states. When a compute output becomes a graphics input:

```cpp
// Pseudo-code in RDG
ComputePass.Output[TransmittanceLUT] = { State: UAV, ... }
GraphicsPass.Input[TransmittanceLUT] = { State: SRV, ... }
// RDG detects transition: UAV → SRV

// Insert barrier before GraphicsPass
TransitionResource(
    TransmittanceLUT,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
);
// or: D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE (if read by PS only)
```

**GPU Command List After Step 2:**

```
SetDescriptorHeaps( { OnlineHeap, SamplerHeap } )
SetComputeRootSignature( RootSig )
SetComputeRootDescriptorTable( 3, OnlineHeap.slot0.GPUHandle )
Dispatch(8, 8, 1)

ResourceBarrier(                          // ← Inserted by RDG
    TransmittanceLUT,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
)
```

**GPU State After Step 2:**

- Transmittance LUT texture: `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` (readable by non-PS shaders)

### Stage 3a: Graphics SRV Setup (BuildSRVTable)

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12DescriptorCache.cpp:432-481`

```cpp
D3D12_GPU_DESCRIPTOR_HANDLE FD3D12DescriptorCache::BuildSRVTable(
    EShaderFrequency ShaderStage,           // SF_Vertex (sky pass uses VS+PS)
    const FD3D12RootSignature* RootSignature,
    FD3D12ShaderResourceViewCache& Cache,   // Vertex shader SRV cache
    const SRVSlotMask& SlotsNeededMask,     // Multiple bits set (multiple textures)
    uint32 SlotsNeeded,                     // e.g., 5 (sky cubemap, transmittance, etc.)
    uint32& HeapSlot                        // [in/out]
)
{
    // Line 442-443: Reserve space in online heap
    uint32 FirstSlotIndex = HeapSlot;       // e.g., HeapSlot = 100 (after compute)
    HeapSlot += SlotsNeeded;                 // HeapSlot becomes 105

    // Line 445: Get CPU handle from online heap
    D3D12_CPU_DESCRIPTOR_HANDLE DestDescriptor =
        CurrentViewHeap->GetCPUSlotHandle(FirstSlotIndex);  // Slot 100, CPU-writeable

    // Line 446: Array to gather offline descriptors
    D3D12_CPU_DESCRIPTOR_HANDLE SrcDescriptors[MAX_SRVS];

    // Line 450-471: For each shader register
    for (uint32 SlotIndex = 0; SlotIndex < SlotsNeeded; SlotIndex++)
    {
        if (FD3D12ShaderResourceView* SRV = SRVs[SlotIndex])  // e.g., SRVs[4] = transmittance LUT
        {
            // Line 453-454: Get offline SRV descriptor
            SrcDescriptors[SlotIndex] = SRV->GetOfflineCpuHandle();
            // ↑ For transmittance at slot 4: transmittanceLUT.OfflineSRV
            //   This is a DIFFERENT descriptor from the UAV!
            //   Same resource, different view (SRV instead of UAV)

            // Line 456: Update residency
            Context.UpdateResidency(Cache.Resources[ShaderStage][SlotIndex]);
        }
        else
        {
            // Line 468: Fallback for unset slots
            SrcDescriptors[SlotIndex] = DefaultViews.NullSRV;
        }
    }

    // Line 472: Mark dirty bits as clean
    FD3D12ShaderResourceViewCache::CleanSlots(CurrentDirtySlotMask, SlotsNeeded);

    // Line 474: Copy offline descriptors to online heap
    GetParentDevice()->CopyDescriptors(
        DestDescriptor,          // Online heap, slot 100, CPU handle
        SrcDescriptors,          // Multiple offline heap descriptors
        SlotsNeeded,             // 5 descriptors
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );
    // After this:
    // - OnlineHeap[100] = copy of sky cubemap offline SRV
    // - OnlineHeap[101] = copy of some other texture offline SRV
    // - OnlineHeap[102] = copy of some other texture offline SRV
    // - OnlineHeap[103] = copy of some other texture offline SRV
    // - OnlineHeap[104] = copy of TRANSMITTANCE LUT offline SRV ← this one!

    // Line 478: Return GPU handle
    const D3D12_GPU_DESCRIPTOR_HANDLE BindDescriptor =
        CurrentViewHeap->GetGPUSlotHandle(FirstSlotIndex);
    return BindDescriptor;  // GPU handle to online slot 100
}
```

**Data State After Step 3a:**

| Location | Content | GPU-Readable | Notes |
|----------|---------|---------|-------|
| TransmittanceLUT.OfflineSRV | SRV descriptor | No | Created when texture allocated; permanent |
| OnlineHeap[104] | Copy of SRV descriptor | Yes | Allocated this frame; offset from HeapSlot start |
| BindDescriptor | GPU virtual address of OnlineHeap[100] | Yes | Points to table start; descriptor[4] = transmittance |

**Critical Difference:** The offline descriptor is an **SRV** (shader resource view), not a **UAV**. The GPU uses it to read the texture, not write.

### Stage 3b: Graphics Binding (SetSRVTable)

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\D3D12DescriptorCache.cpp:483-505`

```cpp
void FD3D12DescriptorCache::SetSRVTable(
    EShaderFrequency ShaderStage,  // SF_Vertex
    const FD3D12RootSignature* RootSignature,
    FD3D12ShaderResourceViewCache& Cache,
    uint32 SlotsNeeded,            // 5
    const D3D12_GPU_DESCRIPTOR_HANDLE& BindDescriptor  // GPU handle to online slot 100
)
{
    // Line 485: Get root parameter slot
    const uint32 RootParameterIndex = RootSignature->SRVRDTBindSlot(SF_Vertex);
    // Assume root signature maps VS_SRVs to root parameter 1
    // ↓ RootParameterIndex = 1

    // Line 493: Set root descriptor table on command list
    Context.GraphicsCommandList()->SetGraphicsRootDescriptorTable(
        1,                   // Root parameter index
        BindDescriptor       // GPU handle to online heap slot 100
    );
    // GPU command list now records: "At root parameter 1, bind GPU heap handle (online slot 100)"

    // Line 499-500: Mark slots outside current table as dirty
    const SRVSlotMask OutsideCurrentTableRegisterMask =
        ~(((SRVSlotMask)1 << SlotsNeeded) - (SRVSlotMask)1);
    Cache.Dirty(SF_Vertex, OutsideCurrentTableRegisterMask);
}
```

**GPU Command List After Step 3b:**

```
SetDescriptorHeaps( { OnlineHeap, SamplerHeap } )
SetComputeRootSignature( RootSig )
SetComputeRootDescriptorTable( 3, OnlineHeap.slot0.GPUHandle )
Dispatch(8, 8, 1)

ResourceBarrier(
    TransmittanceLUT,
    D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
    D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
)

SetGraphicsRootSignature( RootSig )                        // ← Different signature!
SetGraphicsRootDescriptorTable( 1, OnlineHeap.slot100.GPUHandle )
// ... (sampler binding, CBV binding, etc. omitted)
```

### Stage 3c: Graphics Execution (DrawIndexed)

**File:** (RHI Command List execution)

```cpp
Context.GraphicsCommandList()->DrawIndexed(
    IndexCountPerInstance,
    InstanceCount,
    StartIndexLocation,
    BaseVertexLocation,
    StartInstanceLocation
);
// GPU executes graphics pipeline (VS + PS) with bound descriptors
```

**GPU Execution Flow for Vertex Shader:**

1. GPU fetches root parameter 1 → gets GPU handle to online slot 100
2. Shader compiles to: `SRV at index 4 → descriptor_table[4] → transmittance texture`
3. GPU resolves: `online_slot_100 + 4 = online_slot_104 → copy of transmittance offline SRV`
4. GPU reads transmittance LUT texture via SRV
5. Vertex shader can access: `Texture2D<float4> TransmittanceLUT : register(t4);`

**GPU State After Step 3c:**

- Transmittance LUT texture: `D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE` (still readable)
- GPU memory: Transmittance LUT data copied to registers/cache for fast access

### Stage 4: Shader Access

**File:** (Hypothetical shader code, pseudocode)

```hlsl
// SkyAtmosphere.usf (simplified)
Texture2D<float4> TransmittanceLUT : register(t4);
SamplerState TransmittanceSampler : register(s2);

float4 main(VertexOutput input) : SV_Target
{
    // GPU accesses root parameter 1 → descriptor table at slot 100
    // Offset by 4 → descriptor at slot 104
    // Descriptor = copy of TransmittanceLUT.OfflineSRV
    // SRV points to transmittance texture resource

    float2 uv = input.AtmosphereUV;
    float4 transmittance = TransmittanceLUT.Sample(TransmittanceSampler, uv);
    // ↑ GPU reads transmittance LUT via the SRV

    return float4(transmittance.rgb, 1.0);
}
```

## UzEngine Mapping

| Stage | UzEngine Component | Status | Notes |
|-------|-------------------|--------|-------|
| 1a. Compute UAV Setup | `DescriptorCache::BuildUAVTable()` | NEEDS IMPL | Must manage online heap slots |
| 1b. Compute Binding | `RhiContext::SetComputeRootDescriptorTable()` | NEEDS IMPL | DX12 API wrapper |
| 1c. Compute Execution | `RhiContext::Dispatch()` | NEEDS IMPL | RHI abstraction |
| 2. State Transition | `RenderGraph::AddBarrier()` | NEEDS IMPL | Implicit RDG integration |
| 3a. Graphics SRV Setup | `DescriptorCache::BuildSRVTable()` | NEEDS IMPL | Must manage online heap slots |
| 3b. Graphics Binding | `RhiContext::SetGraphicsRootDescriptorTable()` | NEEDS IMPL | DX12 API wrapper |
| 3c. Graphics Execution | `RhiContext::DrawIndexed()` | NEEDS IMPL | RHI abstraction |
| 4. Shader Access | (Compiled to HLSL bytecode) | N/A | Shader compiler handles |

## Changes Required (One Per File, In Dependency Order)

### 1. runtime/src/engine/renderer/rhi/descriptor_heap.h

Add:

```cpp
/// Offline heap: permanent, per-resource SRV/UAV/CBV
/// GPU-invisible, CPU-accessible
class OfflineDescriptorHeap {
    D3D12_CPU_DESCRIPTOR_HANDLE AllocateDescriptor(
        D3D12_DESCRIPTOR_HEAP_TYPE Type
    );
};

/// Online heap: transient, per-frame or per-pass
/// GPU-visible, CPU-writeable (before use)
class OnlineDescriptorHeap {
    struct Slot {
        D3D12_CPU_DESCRIPTOR_HANDLE CPU;
        D3D12_GPU_DESCRIPTOR_HANDLE GPU;
    };
    Slot ReserveSlots(uint32 Count);
    void CopyDescriptors(
        D3D12_CPU_DESCRIPTOR_HANDLE DestCPU,
        const D3D12_CPU_DESCRIPTOR_HANDLE* SrcCPU,
        uint32 Count
    );
};
```

### 2. runtime/src/engine/renderer/core/descriptor_cache.h

Add:

```cpp
class DescriptorCache {
private:
    OnlineDescriptorHeap viewHeap;
    OnlineDescriptorHeap samplerHeap;
    uint32 nextViewSlot = 0;
    uint32 nextSamplerSlot = 0;

public:
    D3D12_GPU_DESCRIPTOR_HANDLE BuildSRVTable(
        ShaderFrequency stage,
        const RootSignature* rootSig,
        const ShaderResourceViewCache& srvCache,
        uint32 slotsNeeded,
        uint32& outHeapSlot
    );

    void SetSRVTable(
        ShaderFrequency stage,
        const RootSignature* rootSig,
        D3D12_GPU_DESCRIPTOR_HANDLE bindHandle
    );

    D3D12_GPU_DESCRIPTOR_HANDLE BuildUAVTable(
        ShaderFrequency stage,
        const RootSignature* rootSig,
        const UnorderedAccessViewCache& uavCache,
        uint32 slotsNeeded,
        uint32& outHeapSlot
    );

    void SetUAVTable(
        ShaderFrequency stage,
        const RootSignature* rootSig,
        D3D12_GPU_DESCRIPTOR_HANDLE bindHandle
    );

    void OpenCommandList();
    void CloseCommandList();
};
```

### 3. runtime/src/engine/renderer/core/shader_resource_view.h

Add:

```cpp
class ShaderResourceView {
private:
    D3D12_CPU_DESCRIPTOR_HANDLE offlineDescriptor;

public:
    D3D12_CPU_DESCRIPTOR_HANDLE GetOfflineCpuHandle() const {
        return offlineDescriptor;
    }
};
```

### 4. runtime/src/engine/renderer/core/render_graph.cpp

Update `AddPass()` to insert barriers:

```cpp
void RenderGraph::AddPass(...) {
    // After defining pass inputs/outputs, insert transitions
    for (const auto& resource : pass.inputs) {
        if (resource.previousState != resource.requestedState) {
            AddBarrier(resource, resource.previousState, resource.requestedState);
        }
    }
}
```

### 5. runtime/shaders/include/sky_atmosphere.hlsli

No changes needed; shaders already use `Texture2D<T> : register(t4)` syntax.

## Verification Checklist

- [ ] OnlineDescriptorHeap allocates slots linearly, resets at frame start
- [ ] BuildSRVTable reserves slots, copies offline descriptors, returns GPU handle
- [ ] BuildUAVTable reserves slots, copies offline descriptors, returns GPU handle
- [ ] SetSRVTable calls `SetGraphicsRootDescriptorTable()` with root param from root signature
- [ ] SetUAVTable calls `SetComputeRootDescriptorTable()` with root param from root signature
- [ ] RenderGraph inserts `UAV → SRV` barrier before reading compute output as SRV
- [ ] Compute dispatch reads transmittance LUT correctly (writes via UAV)
- [ ] Graphics dispatch reads transmittance LUT correctly (reads via SRV)
- [ ] Frame start resets `nextViewSlot = 0`, `nextSamplerSlot = 0`
- [ ] GPU memory contains correct LUT data (visual verification)

---

**Document Status:** Complete data flow analysis. Ready for implementation.
