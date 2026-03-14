# RHI Backend Compatibility Fix Guide for UzEngine

## Overview

This document provides specific code patterns extracted from UE5 that UzEngine should implement to support both DX11 and DX12 without segfaults or API misuse.

---

## Issue 1: Scratch Constant Buffer Management

### The Problem

UzEngine's current approach (segfaults on first render frame):
- Uses single shared scratch CB
- Maps/Unmaps same buffer repeatedly
- No awareness of GPU pipeline hazards

### Why It Fails

- **DX11**: `Map()` with `DISCARD` flag requires entire buffer be rewritten
- **DX12**: Maps are expensive; should use ring buffer instead
- **Combined**: Unmapping while GPU still reading causes stalls or UAF

### UE5 Solution: FD3D12ConstantBuffer + FD3D12FastConstantAllocator

**Pattern: Circular Buffer Versioning**

```cpp
// Instead of a single scratch CB, use a ring buffer
struct FConstantBufferRing
{
    static const uint32 NumBuffers = 3;  // Triple-buffering for pipelining
    FRHIBuffer* Buffers[NumBuffers];
    uint32 CurrentIndex = 0;
    uint32 CurrentOffset = 0;
    uint32 MaxSize = 65536;

    // Called at frame start or when offset wraps
    void Rotate()
    {
        CurrentIndex = (CurrentIndex + 1) % NumBuffers;
        CurrentOffset = 0;
    }

    // Called when needing to upload constant data
    struct AllocationResult {
        FRHIBuffer* Buffer;
        void* MappedData;
        uint32 Offset;
        uint32 GpuVirtualAddress;  // For DX12 root CBV
    };

    AllocationResult Allocate(uint32 Size)
    {
        if (CurrentOffset + Size > MaxSize) {
            Rotate();
        }

        AllocationResult Result;
        Result.Buffer = Buffers[CurrentIndex];
        Result.Offset = CurrentOffset;

        // Backend-specific: Map or get GPU address
        if (UsingDX12) {
            Result.GpuVirtualAddress = GetD3D12Buffer(Result.Buffer)->GetGpuVirtualAddress() + Result.Offset;
            Result.MappedData = GetUploadHeapPointer(CurrentIndex, Result.Offset);
        } else {  // DX11
            Result.MappedData = GetD3D11MappedPointer(Result.Buffer, Result.Offset);
        }

        CurrentOffset += Size;
        return Result;
    }
};
```

**Usage in render graph:**

```cpp
// Instead of this (WRONG):
void UpdateShaderConstants(FRHICommandList& RHI, const void* Data, uint32 Size) {
    void* Ptr = ScratchCB->Map();  // SEGFAULT: no guarantee GPU is done
    memcpy(Ptr, Data, Size);
    ScratchCB->Unmap();
}

// Do this (CORRECT):
void UpdateShaderConstants(FRHICommandList& RHI, const void* Data, uint32 Size) {
    auto Allocation = ConstantBufferRing.Allocate(Size);
    memcpy(Allocation.MappedData, Data, Size);

    // Now bind the allocated buffer (not the "current" one)
    if (UsingDX12) {
        RHI.SetRootConstantBufferView(0, Allocation.GpuVirtualAddress);
    } else {
        RHI.SetConstantBuffer(0, Allocation.Buffer, Allocation.Offset);
    }
}
```

---

## Issue 2: Texture/SRV Binding (SetFragmentTexture)

### The Problem

UzEngine likely does:
```cpp
// WRONG - direct D3D call
context->PSSetShaderResources(0, 1, &SRV);
```

This only works on DX11 and ties the codebase to a specific backend.

### UE5 Solution: FRHIBatchedShaderParameters

**Step 1: Define Parameter Struct**

```cpp
struct FRHIShaderParameterResource
{
    enum class EType : uint8
    {
        Texture,
        ResourceView,
        UnorderedAccessView,
        Sampler,
        UniformBuffer,
    };

    FRHIResource* Resource = nullptr;
    uint16 Index = 0;
    EType Type = EType::Texture;
};

struct FRHIBatchedShaderParameters
{
    TArray<FRHIShaderParameterResource> ResourceParameters;  // Textures, SRVs, samplers
    TArray<uint8> ParametersData;                           // Raw constant data

    void SetShaderTexture(uint32 Index, FRHITexture* Texture)
    {
        ResourceParameters.Emplace(Texture, (uint16)Index);
    }

    void SetShaderResourceView(uint32 Index, FRHIShaderResourceView* SRV)
    {
        ResourceParameters.Emplace(SRV, (uint16)Index);
    }

    void SetShaderSampler(uint32 Index, FRHISamplerState* Sampler)
    {
        ResourceParameters.Emplace(Sampler, (uint16)Index);
    }

    void SetShaderParameter(uint32 Index, const void* Data, uint32 Size)
    {
        uint32 Offset = ParametersData.Num();
        ParametersData.AddUninitialized(Size);
        memcpy(ParametersData.GetData() + Offset, Data, Size);
    }
};
```

**Step 2: Collect Parameters (Backend-Neutral)**

```cpp
// In your render pass or draw call setup:
void SetupMaterialParameters(FRHICommandList& RHI, const FMaterial& Material)
{
    FRHIBatchedShaderParameters Params;

    // Collect bindings (no backend-specific code)
    Params.SetShaderTexture(0, Material.AlbedoTexture);
    Params.SetShaderTexture(1, Material.NormalTexture);
    Params.SetShaderTexture(2, Material.RoughnessTexture);

    Params.SetShaderSampler(0, Material.MainSampler);
    Params.SetShaderSampler(1, Material.DetailSampler);

    // Apply (backend handles the details)
    RHI.SetBatchedShaderParameters(Material.PixelShader, Params);
}
```

**Step 3: Execute (Backend-Specific)**

```cpp
// In your RHI implementation (D3D11RHI.cpp or D3D12RHI.cpp):

void FD3D11CommandContext::SetBatchedShaderParameters(
    FRHIPixelShader* PixelShaderRHI,
    const FRHIBatchedShaderParameters& Params)
{
    FD3D11PixelShader* PixelShader = ResourceCast(PixelShaderRHI);

    // DX11: Iterate and call PSSetShaderResources for each SRV
    ID3D11ShaderResourceView* SRVs[16] = { nullptr };
    ID3D11SamplerState* Samplers[16] = { nullptr };

    for (const auto& Resource : Params.ResourceParameters) {
        if (Resource.Type == FRHIShaderParameterResource::EType::ResourceView) {
            SRVs[Resource.Index] = ResourceCast<FD3D11ShaderResourceView>(Resource.Resource)->GetD3D11SRV();
        } else if (Resource.Type == FRHIShaderParameterResource::EType::Sampler) {
            Samplers[Resource.Index] = ResourceCast<FD3D11SamplerState>(Resource.Resource)->GetD3D11Sampler();
        }
    }

    Context->PSSetShaderResources(0, 16, SRVs);
    Context->PSSetSamplers(0, 16, Samplers);
}

void FD3D12CommandContext::SetBatchedShaderParameters(
    FRHIPixelShader* PixelShaderRHI,
    const FRHIBatchedShaderParameters& Params)
{
    FD3D12PixelShader* PixelShader = ResourceCast(PixelShaderRHI);

    // DX12: Update descriptor heap and set descriptor table
    for (const auto& Resource : Params.ResourceParameters) {
        DescriptorHeap.SetDescriptor(Resource.Index, Resource.Resource);
    }
    DescriptorHeap.Flush();
    CommandList->SetDescriptorHeaps(1, &DescriptorHeap.GetHeap());
    CommandList->SetGraphicsRootDescriptorTable(0, DescriptorHeap.GetGpuHandle(0));
}
```

---

## Issue 3: Sampler State Binding

### The Problem

Samplers are managed per-texture in UzEngine, likely causing:
- Direct `PSSetSamplers()` calls (DX11 only)
- No descriptor table consideration for DX12
- Inconsistent sampler state

### UE5 Solution: Batched Sampler Binding

Same as Issue 2 — samplers are part of `FRHIBatchedShaderParameters`:

```cpp
// Collect samplers (backend-neutral)
Params.SetShaderSampler(0, LinearSampler);
Params.SetShaderSampler(1, PointSampler);
Params.SetShaderSampler(2, ShadowSampler);

// Execute (backend-specific)
RHI.SetBatchedShaderParameters(PixelShader, Params);
```

**Key difference from textures:**
- DX11: Samplers are separate `ID3D11SamplerState` objects
- DX12: Can be in descriptor table OR static samplers in root signature

For DX12, define static samplers during root signature creation:

```cpp
// In root signature creation
struct FD3D12RootSignatureDef
{
    void AddStaticSampler(uint32 Register, D3D12_FILTER Filter,
                         D3D12_TEXTURE_ADDRESS_MODE AddressU,
                         D3D12_TEXTURE_ADDRESS_MODE AddressV,
                         D3D12_TEXTURE_ADDRESS_MODE AddressW)
    {
        // Create CD3DX12_STATIC_SAMPLER_DESC and add to root signature
    }
};
```

---

## Issue 4: Resource Barriers and State Transitions

### The Problem

UzEngine likely ignores DX12 resource barriers:
```cpp
// WRONG - only works on DX11
void RenderPass() {
    // GPU already using Texture in SRV mode
    // Now trying to write to it without barrier
    RenderTargetView = Texture->GetRTV();
}
```

This causes device removal errors on DX12 (silent hangs or crashes).

### UE5 Solution: Explicit Transition API

**Define Transition Interface:**

```cpp
enum class ERHIAccess : uint32
{
    // Read states
    SRVGraphics = 0,
    SRVCompute = 1,

    // Write states
    RTV = 2,
    UAV = 3,
    DSV = 4,

    // Copy states
    CopySrc = 5,
    CopyDst = 6,
};

enum class ERHIPipeline : uint8
{
    Graphics,
    Compute,
    Copy,
};

struct FRHITransitionInfo
{
    enum class EType { Texture, Buffer, UAV };

    EType Type;
    FRHIResource* Resource;
    ERHIAccess AccessBefore;
    ERHIAccess AccessAfter;
    ERHIPipeline Pipeline;
};
```

**API Usage:**

```cpp
void RenderPass(FRHICommandList& RHI, FRHITexture* Texture)
{
    // Declare we're transitioning from SRV to RTV
    RHI.Transition(Texture, ERHIAccess::SRVGraphics, ERHIAccess::RTV, ERHIPipeline::Graphics);

    // Now bind as render target
    RHI.SetRenderTarget(Texture);
    RHI.DrawIndexedPrimitive(...);

    // Transition back
    RHI.Transition(Texture, ERHIAccess::RTV, ERHIAccess::SRVGraphics, ERHIPipeline::Graphics);
}
```

**Backend Implementation:**

```cpp
// DX11 implementation (essentially no-op, just validates)
void FD3D11CommandContext::Transition(FRHITexture* Texture,
                                      ERHIAccess Before,
                                      ERHIAccess After,
                                      ERHIPipeline Pipeline)
{
    // Track state for validation, but don't emit barriers
    TextureStateCache[Texture] = After;
}

// DX12 implementation (emits actual barriers)
void FD3D12CommandContext::Transition(FRHITexture* Texture,
                                      ERHIAccess Before,
                                      ERHIAccess After,
                                      ERHIPipeline Pipeline)
{
    D3D12_RESOURCE_STATES StateBefore = ConvertAccessToD3D12State(Before);
    D3D12_RESOURCE_STATES StateAfter = ConvertAccessToD3D12State(After);

    if (StateBefore != StateAfter) {
        D3D12_RESOURCE_BARRIER Barrier = {};
        Barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        Barrier.Transition.pResource = ResourceCast<FD3D12Texture>(Texture)->GetResource();
        Barrier.Transition.StateBefore = StateBefore;
        Barrier.Transition.StateAfter = StateAfter;
        Barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        CommandList->ResourceBarrier(1, &Barrier);
    }
}
```

---

## Issue 5: Descriptor/Binding Layout (Root Signature)

### The Problem

UzEngine likely hard-codes DX12 root signature structure:
```cpp
// WRONG - DX12 specific
void CreateRootSignature() {
    D3D12_DESCRIPTOR_RANGE SRVRange;
    SRVRange.BaseShaderRegister = 0;
    SRVRange.NumDescriptors = 10;
    SRVRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
}
```

This needs abstraction for DX11 compatibility.

### UE5 Solution: Root Signature Specification

**Define Neutral Binding Layout:**

```cpp
struct FRHIBindingLayout
{
    enum class EResourceType { CBV, SRV, UAV, Sampler };
    enum class EVisibility { Vertex, Pixel, All };

    struct FSlot
    {
        EResourceType Type;
        uint32 Register;
        uint32 Count;
        EVisibility Visibility;
    };

    TArray<FSlot> Slots;
};

FRHIBindingLayout MyLayout;
MyLayout.Slots.Emplace(FRHIBindingLayout::EResourceType::SRV, 0, 10, FRHIBindingLayout::EVisibility::Pixel);
MyLayout.Slots.Emplace(FRHIBindingLayout::EResourceType::Sampler, 0, 4, FRHIBindingLayout::EVisibility::Pixel);
MyLayout.Slots.Emplace(FRHIBindingLayout::EResourceType::CBV, 0, 1, FRHIBindingLayout::EVisibility::All);
```

**Backend-Specific Creation:**

```cpp
// D3D11 (essentially a no-op, just remembers slot counts)
ID3D11DeviceChild* FD3D11RHI::CreateBindingLayout(const FRHIBindingLayout& Layout)
{
    FD3D11BindingLayout* Result = new FD3D11BindingLayout();
    for (const auto& Slot : Layout.Slots) {
        Result->SlotToResourceMap[Slot.Register] = Slot.Type;
    }
    return Result;
}

// D3D12 (generates descriptor tables and root signature)
ID3D12RootSignature* FD3D12RHI::CreateBindingLayout(const FRHIBindingLayout& Layout)
{
    CD3DX12_DESCRIPTOR_RANGE SRVRange;
    // ... build root signature from neutral layout

    ID3D12RootSignature* RootSig = nullptr;
    Device->CreateRootSignature(..., &RootSig);
    return RootSig;
}
```

---

## Integration Checklist for UzEngine

### Phase 1: Core Abstractions (Fix Immediate Segfaults)

- [ ] Implement `FRHIBatchedShaderParameters` struct
- [ ] Refactor all `SetFragmentTexture()` calls to use batched parameters
- [ ] Implement circular buffer constant buffer allocator
- [ ] Replace scratch CB Map/Unmap with ring buffer Allocate

### Phase 2: State Management

- [ ] Implement `FRHITransitionInfo` and Transition API
- [ ] Add state cache layer that defers execution
- [ ] Update all barrier/transition calls in render graph

### Phase 3: Root Signature Abstraction

- [ ] Define neutral `FRHIBindingLayout`
- [ ] Refactor root signature creation to use layout
- [ ] Implement backend-specific root signature builders

### Phase 4: Sampler Management

- [ ] Move samplers into batched shader parameters
- [ ] Define static samplers for DX12 root signature
- [ ] Remove direct `PSSetSamplers()` calls

### Phase 5: Validation & Testing

- [ ] Add validation layer to catch backend-specific calls
- [ ] Test all render passes on both DX11 and DX12
- [ ] Verify no device removal errors on DX12

---

## Testing Strategy

### Test 1: Verify No Direct D3D Calls
```cpp
// In a debug wrapper, track all calls
void* D3D11GetDeviceContext() {
    check(false, "Direct device context access not allowed above RHI boundary");
    return nullptr;
}
```

### Test 2: Constant Buffer Allocation
```cpp
void Test_ConstantBufferRing() {
    FConstantBufferRing Ring;

    auto Alloc1 = Ring.Allocate(256);
    auto Alloc2 = Ring.Allocate(512);

    // Different buffers (shouldn't reuse same buffer before GPU is done)
    check(Alloc1.Buffer != Alloc2.Buffer || Alloc1.Offset + 256 <= Alloc2.Offset);
}
```

### Test 3: Parameter Batching
```cpp
void Test_BatchedShaderParameters() {
    FRHIBatchedShaderParameters Params;
    Params.SetShaderTexture(0, Texture0);
    Params.SetShaderTexture(1, Texture1);
    Params.SetShaderSampler(0, Sampler0);
    Params.SetShaderParameter(0, &ConstantData, sizeof(ConstantData));

    check(Params.ResourceParameters.Num() == 3);
    check(Params.ParametersData.Num() == sizeof(ConstantData));

    RHICommandList.SetBatchedShaderParameters(Shader, Params);
}
```

---

## Performance Considerations

### Circular Buffer Sizing
- Use 3 buffers for triple-buffering (1 in-flight, 1 queued, 1 available)
- Size: Typical frame needs 64-256 KB of constants
- Rotate when offset + requested size > buffer size

### Descriptor Heap
- For DX12, pre-allocate descriptor heap (e.g., 1000 descriptors)
- Reuse heap across frames if descriptors don't change
- Only call `SetDescriptorHeaps()` if heap actually changed

### State Cache
- Cache last-bound shader to avoid redundant bindings
- Batch all transitions into single `ResourceBarrier()` call
- Avoid redundant descriptor table sets

