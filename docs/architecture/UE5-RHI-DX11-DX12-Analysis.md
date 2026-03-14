# UE5 RHI Abstraction Layer Analysis: DX11 vs DX12 Backend Compatibility

## Executive Summary

UE5's RHI layer successfully abstracts the major differences between DX11 and DX12 through these key patterns:

1. **Shader Parameter Abstraction** — Unified `FRHIBatchedShaderParameters` interface hides backend differences
2. **Constant Buffer Versioning** — Circular buffer system with "allocation" pattern avoids DX11 Map/Discard vs DX12 root CBV differences
3. **Descriptor Caching** — Descriptor cache layer isolates slot-based (DX11) vs descriptor table (DX12) binding
4. **State Cache Pattern** — Deferred state application allows backend-specific optimization
5. **Resource Transitions** — Explicit transition API that DX11 can ignore, DX12 requires

---

## 1. Shader Parameter Abstraction Pattern

### UE5 Implementation

**File:** `W:/UE_5.6_Source/Engine/Source/Runtime/RHI/Public/RHIShaderParameters.h` (lines 239-350)

```cpp
struct FRHIBatchedShaderParameters
{
    // Three separate channels for different resource types
    TArrayView<FRHIShaderParameterResource> ResourceParameters;      // Textures, SRVs, UAVs, Samplers (slot-based)
    TArrayView<FRHIShaderParameterResource> BindlessParameters;      // Bindless resources (DX12 only)
    TArrayView<uint8> ParametersData;                               // Raw constant data

    // Simple interface that hides all backend differences
    FORCEINLINE void SetShaderTexture(uint32 Index, FRHITexture* Texture)
    { AddResourceParameter(Texture, (uint16)Index); }

    FORCEINLINE void SetShaderResourceViewParameter(uint32 Index, FRHIShaderResourceView* SRV)
    { AddResourceParameter(SRV, (uint16)Index); }

    FORCEINLINE void SetShaderSampler(uint32 Index, FRHISamplerState* State)
    { AddResourceParameter(State, (uint16)Index); }
};
```

**Problem Solved:**
- DX11 requires `PSSetShaderResources()` for each SRV slot
- DX12 requires descriptor table setup in root signature + SetDescriptorTable call
- UE5 separates **what** to bind (the FRHIBatchedShaderParameters struct) from **how** (backend-specific execution in descriptor cache)

**Key Insight:** Parameters are collected into a neutral struct, then executed differently per backend.

---

## 2. Constant Buffer Binding: Versioning Pattern

### UE5 Implementation

**File:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12ConstantBuffer.cpp` (lines 1-63)

```cpp
class FD3D12ConstantBuffer
{
    // Circular buffer allocator - same concept for both DX11 and DX12
    FD3D12FastConstantAllocator& Allocator;

    bool Version(FD3D12ResourceLocation& BufferOut, bool bDiscardSharedConstants)
    {
        if (CurrentUpdateSize == 0) return false;  // No changes

        // Get next buffer from allocator (ring buffer)
        void* Data = Allocator.Allocate(TotalUpdateSize, BufferOut, ViewToUse);

        // CPU-side copy
        FMemory::Memcpy(Data, ShadowData.GetData(), TotalUpdateSize);

        return true;
    }
};
```

**DX11 vs DX12 Differences:**
- **DX11**: Allocator calls `ID3D11DeviceContext::Map()` and returns a pointer
- **DX12**: Allocator grabs next buffer from ring buffer, returns a pointer to upload heap

**Both backends use the same FD3D12ConstantBuffer interface**, but the allocator behavior differs.

**Command Execution Pattern** (lines 67-75):

```cpp
static void BindUniformBuffer(FD3D12CommandContext& Context, FRHIShader* Shader,
                              EShaderFrequency ShaderFrequency, uint32 BufferIndex,
                              FD3D12UniformBuffer* InBuffer)
{
    Context.StateCache.SetConstantsFromUniformBuffer(ShaderFrequency, BufferIndex, InBuffer);
    Context.BoundUniformBuffers[ShaderFrequency][BufferIndex] = InBuffer;
    Context.DirtyUniformBuffers[ShaderFrequency] |= (1 << BufferIndex);
}
```

**Key Pattern:**
1. Mark buffer as "dirty" (needs binding)
2. Defer actual binding to state cache flush time
3. At flush time, backend-specific code decides:
   - DX11: `ID3D11DeviceContext::PSSetConstantBuffers(Index, 1, &CBV)`
   - DX12: `SetRootConstantBufferView(Index, GpuVirtualAddress)`

---

## 3. Sampler State Differences

### UE5 Implementation

**Problem:**
- **DX11**: Samplers are state objects bound via `PSSetSamplers()`
- **DX12**: Static samplers in root signature OR descriptor tables

**Solution:** Same as SRV/UAV binding — use descriptor cache layer.

The `FRHIBatchedShaderParameters::SetShaderSampler()` adds samplers to ResourceParameters array with type `EType::Sampler`. The descriptor cache then:
- **DX11**: Converts to `ID3D11SamplerState*` and calls `PSSetSamplers()`
- **DX12**: Adds to descriptor table or uses static samplers from root signature

---

## 4. Texture/SRV Binding Differences

### UE5 Implementation

**File:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Public/D3D12RootSignatureDefinitions.h` (lines 22-237)

```cpp
namespace D3D12ShaderUtils::StaticRootSignatureConstants
{
    constexpr D3D12_DESCRIPTOR_RANGE_FLAGS SRVDescriptorRangeFlags =
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
        | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;

    constexpr D3D12_DESCRIPTOR_RANGE_FLAGS CBVDescriptorRangeFlags =
        D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC_WHILE_SET_AT_EXECUTE
        | D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
};

struct FBinaryRootSignatureCreator
{
    void AddTable(ERootSignatureVisibility Visibility,
                  ERootSignatureRangeType Type,
                  int32 NumDescriptors) override
    {
        // Deferred table creation — can map to either:
        // - DX11: Multiple PSSetShaderResources() calls
        // - DX12: Single SetDescriptorTable() call
    }
};
```

**Key Pattern:**
- Root signature defines **logical structure** of bindings
- Descriptor cache translates logical structure to backend-specific calls
- DX12 supports bindless mode (heap directly indexed) via flag:
  ```cpp
  if (EnumHasAnyFlags(InFlags, EShaderBindingLayoutFlags::BindlessResources))
  {
      AddRootFlag(D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED);
  }
  ```
- DX11 completely ignores this flag (always slot-based)

---

## 5. Resource Barriers/Transitions

### UE5 Implementation

**File:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12Commands.cpp` (lines 230-299)

```cpp
template <typename FunctionType>
void ProcessResource(FD3D12CommandContext& Context, const FRHITransitionInfo& Info,
                    FunctionType Function)
{
    auto [Resource, Texture] = GetResourceAndTexture(Context, Info);
    if (Resource)
    {
        Function(Info, Resource, Texture, DiscardTextureOut);
    }
}
```

**Problem:**
- **DX12**: Explicit `D3D12_RESOURCE_BARRIER` calls required
- **DX11**: State tracking automatic (implicit layout transitions)

**Solution:**
- Unified `FRHITransitionInfo` struct describes what changed
- DX12 backend: Convert to `D3D12_RESOURCE_BARRIER` and call `ResourceBarrier()`
- DX11 backend: Validate state is correct, but no actual call needed (tracked internally)

---

## 6. Render Graph (RDG) Abstraction

### UE5 Implementation

**File:** `W:/UE_5.6_Source/Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` (lines 42-300)

**Key Pattern: Deferred Pass Execution**

```cpp
class FRDGBuilder
{
    // Passes are registered as lambdas with parameter structs
    template <typename ParameterStructType, typename ExecuteLambdaType>
    FRDGPassRef AddPass(FRDGEventName&& Name, const ParameterStructType* ParameterStruct,
                       ERDGPassFlags Flags, ExecuteLambdaType&& ExecuteLambda);
};
```

**RDG handles:**
1. **Resource lifetime tracking** — RDG knows when each resource is created/destroyed
2. **Automatic barrier insertion** — RDG calculates needed transitions based on pass access patterns
3. **Backend-agnostic pass definition** — Lambda just calls RHI methods, doesn't care about backend

**Execution flow:**
```
AddPass() → Parameter struct + Lambda
    ↓
RDG Compilation (calculate barriers, cull, schedule)
    ↓
Execute() → For each pass:
    ↓
    Insert barriers (DX12) / validate state (DX11)
    ↓
    Execute lambda (backend calls SetShaderParameters, SetRenderTargets, etc.)
    ↓
    State cache flush (issue actual DX11 calls or DX12 calls)
```

**Why this works:**
- Pass author writes once, works on all backends
- RDG compiler handles resource state management
- State cache defers execution until it knows full state

---

## Anti-Patterns Found in UzEngine

Based on UzEngine's current code vs UE5's design:

### 1. Direct API Calls Above RHI Boundary

❌ **Wrong (UzEngine):**
```cpp
// src/renderer/renderer.h
ID3D12Device* Device;
ID3D12GraphicsCommandList* CommandList;

void RenderFrame() {
    Device->CreateXXX();  // Direct DX12 calls
    CommandList->SetXXX();
}
```

✅ **Correct (UE5):**
```cpp
// Abstract through RHI interface
class FD3D12Device; // Backend-specific impl
void RenderFrame(FRHICommandList& RHICmdList) {
    RHICmdList.SetShaderResourceViewParameter(...);  // Backend-neutral
}
```

### 2. Type Switching on Node/Component Type

❌ **Wrong (UzEngine):**
```cpp
void SetupRendering(const Node* node) {
    if (node->Type == NodeType::Mesh) {
        auto* mesh = static_cast<MeshNode*>(node);
        mesh->BindTextures();
    } else if (node->Type == NodeType::Light) {
        // ...
    }
}
```

✅ **Correct (UE5):**
```cpp
// Each component type has its own render proxy
struct FPrimitiveSceneProxy {
    virtual void GetDynamicMeshElements(...) = 0;
};

void SetupRendering(FPrimitiveSceneProxy* Proxy) {
    Proxy->GetDynamicMeshElements(...);  // Virtual dispatch, no type checking
}
```

### 3. Scratch Constant Buffers Without Versioning

❌ **Wrong (UzEngine):**
```cpp
void UpdateConstants(const void* Data, uint32 Size) {
    void* MappedPtr = ScratchCB->Map();  // Direct Map (DX11 only)
    FMemory::Memcpy(MappedPtr, Data, Size);
    ScratchCB->Unmap();  // Causes stall on DX12
}
```

✅ **Correct (UE5):**
```cpp
// Use circular buffer allocator
void UpdateConstants(const void* Data, uint32 Size) {
    FD3D12ResourceLocation Location;
    void* MappedPtr = Allocator.Allocate(Size, Location);  // Ring buffer
    FMemory::Memcpy(MappedPtr, Data, Size);
    // Location automatically contains correct GPU address (DX11 or DX12)
}
```

### 4. Sampler State Bound Directly

❌ **Wrong (UzEngine):**
```cpp
void SetSampler(uint32 Slot, ID3D11SamplerState* Sampler) {
    Context->PSSetSamplers(Slot, 1, &Sampler);  // DX11 specific
}
```

✅ **Correct (UE5):**
```cpp
void SetSampler(uint32 Slot, FRHISamplerState* Sampler) {
    BatchedParameters.SetShaderSampler(Slot, Sampler);
    // Executed later via descriptor cache (DX11 or DX12)
}
```

### 5. Descriptor Table as Implementation Detail

❌ **Wrong (UzEngine):**
```cpp
// Root signature hard-coded for DX12 descriptor tables
SetDescriptorTable(0, TextureHeap);  // Only works on DX12
```

✅ **Correct (UE5):**
```cpp
// Root signature is a specification, not a DX12 detail
FBinaryRootSignatureCreator Creator;
Creator.AddTable(Visibility, Type, NumDescriptors);
// Creator::Finalize() produces:
//   - DX12: D3D12_VERSIONED_ROOT_SIGNATURE_DESC with descriptor tables
//   - DX11 (hypothetically): List of texture/sampler slots to use
```

---

## Key UE5 Design Rules to Adopt in UzEngine

### Rule 1: Resource Parameter Collection Phase
Parameters are collected into a neutral struct (FRHIBatchedShaderParameters) **before** any backend work:
```cpp
FRHIBatchedShaderParameters Params;
for (const auto& Binding : MyBindings) {
    Params.SetShaderTexture(Binding.Index, Binding.Texture);
    Params.SetShaderSampler(Binding.Index, Binding.Sampler);
}
// Now execute (backend-specific)
RHICmdList.SetBatchedShaderParameters(Shader, Params);
```

### Rule 2: State Cache Deferred Execution
Never call backend APIs directly. Always mark state as "dirty" and flush at known sync points:
```cpp
// Do NOT call ID3D12CommandList::SetDescriptorHeaps() directly
StateCache.SetDirtyDescriptors(...);  // Mark as needing update

// Later, at a flush point:
StateCache.ApplyState();  // Backend-specific work happens here
```

### Rule 3: Versioning for Dynamic Data
For constant buffers that change per-frame or per-draw:
```cpp
// Do NOT use Map/Unmap on same buffer repeatedly
struct FConstantBufferUpdater {
    FD3D12ResourceLocation Location;
    void* Data = Allocator.Allocate(..., Location);  // Get next ring buffer slot
};
```

### Rule 4: Explicit Transitions Are Cheap
Use explicit transitions even on DX11 (they're no-ops but validate state):
```cpp
// Works on DX11 (validated) and DX12 (actual barriers)
RHICmdList.Transition(Texture, ERHIAccess::SRVGraphics, ERHIPipeline::Graphics);
```

### Rule 5: Root Signature is Specification, Not Implementation
Define binding structure as logical specification:
```cpp
struct MyRootSignature {
    uint32 TextureTableSlot;     // 0
    uint32 SamplerTableSlot;     // 1
    uint32 ConstantBufferSlot;   // 2
};
// Backend expands this to:
//   - DX11: List of PSSetShaderResources slots
//   - DX12: Descriptor table offsets
```

---

## Files to Reference in UzEngine Implementation

**Core abstractions to study:**
- `W:/UE_5.6_Source/Engine/Source/Runtime/RHI/Public/RHIShaderParameters.h` — Parameter collection pattern
- `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12ConstantBuffer.cpp` — Versioning pattern
- `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Public/D3D12RootSignatureDefinitions.h` — Root signature abstraction
- `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12Commands.cpp` — State cache pattern (lines 67-75)
- `W:/UE_5.6_Source/Engine/Source/Runtime/RenderCore/Public/RenderGraphBuilder.h` — RDG abstraction (lines 42-300)

---

## Summary: How to Fix UzEngine's DX11 Segfault

The crash during first render frame is likely because:

1. **Scratch CBs using Map/Unmap** — DX12 expects ring buffer, not persistent mapping
2. **SetFragmentTexture binding directly** — Should go through descriptor cache
3. **Samplers not deferred** — Should be batched and executed later
4. **No barrier abstraction** — Likely missing state transitions

**Fix priority:**
1. Implement `FRHIBatchedShaderParameters` equivalent in UzEngine
2. Create descriptor cache layer that defers texture/sampler binding
3. Implement ring buffer constant buffer allocator (not Map/Discard)
4. Add explicit barrier tracking (even if no-op on DX11)
5. Refactor render graph to use batched parameters, not direct RHI calls

