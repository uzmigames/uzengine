# RHI Abstraction Quick Reference Card

## The 5 Critical Patterns

### Pattern 1: Batched Shader Parameters

**What:** Collect binding instructions into neutral struct before execution

**When:** Every time you bind textures, samplers, or constants

**How:**
```cpp
FRHIBatchedShaderParameters Params;
Params.SetShaderTexture(0, MyTexture);
Params.SetShaderSampler(0, MySampler);
Params.SetShaderParameter(0, &Constants, Size);
RHICmdList.SetBatchedShaderParameters(PixelShader, Params);
```

**Why:** Separates what (bindings) from how (DX11 PSSetX vs DX12 descriptor table)

**UE5 Reference:** `W:/UE_5.6_Source/Engine/Source/Runtime/RHI/Public/RHIShaderParameters.h` (lines 239-350)

---

### Pattern 2: Ring Buffer Constants

**What:** Use circular buffer of constant buffers instead of Map/Discard

**When:** Updating per-frame or per-draw constant data

**How:**
```cpp
// Frame N:
auto Alloc = ConstantBufferRing.Allocate(256);
memcpy(Alloc.MappedData, FrameN_Constants, 256);
RHICmdList.SetConstantBuffer(0, Alloc.Buffer, Alloc.Offset);

// Frame N+1:
auto Alloc = ConstantBufferRing.Allocate(256);  // Gets different buffer
memcpy(Alloc.MappedData, FrameNPlus1_Constants, 256);
RHICmdList.SetConstantBuffer(0, Alloc.Buffer, Alloc.Offset);
```

**Why:** DX11 Map(DISCARD) is implicit ring buffer; make it explicit for DX12

**UE5 Reference:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12ConstantBuffer.cpp`

---

### Pattern 3: Descriptor Cache (Deferred Binding)

**What:** Defer texture/sampler binding until flush time

**When:** Need to support both slot-based (DX11) and table-based (DX12) binding

**How:**
```cpp
// Mark as dirty (no API call)
RHIStateCache.SetFragmentTexture(0, Texture0);
RHIStateCache.SetFragmentTexture(1, Texture1);

// Later, at draw time:
RHIStateCache.Flush();  // Issues actual PSSetShaderResources or SetDescriptorTable
```

**Why:** DX11 uses slot-based, DX12 uses tables; cache abstracts the difference

**UE5 Reference:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12DescriptorCache.cpp`

---

### Pattern 4: Explicit Transitions

**What:** Explicitly specify resource state changes

**When:** Changing how GPU accesses resource (SRV → RTV, etc.)

**How:**
```cpp
// Transition Texture from SRV (readable) to RTV (writable)
RHICmdList.Transition(Texture, ERHIAccess::SRVGraphics, ERHIAccess::RTV, ERHIPipeline::Graphics);

// Now safe to use as render target
RHICmdList.SetRenderTarget(Texture);
RHICmdList.DrawCall();

// Transition back
RHICmdList.Transition(Texture, ERHIAccess::RTV, ERHIAccess::SRVGraphics, ERHIPipeline::Graphics);
```

**Why:** DX12 requires explicit barriers; DX11 ignores (validation only)

**UE5 Reference:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12Commands.cpp` (lines 230-299)

---

### Pattern 5: State Cache Flush Points

**What:** Defer all state changes until known sync points

**When:** Every render pass or compute dispatch

**How:**
```cpp
void RenderPass(FRHICommandList& RHICmdList) {
    // Collect state changes (all deferred)
    RHICmdList.SetViewport(...);
    RHICmdList.SetBlendState(...);
    RHICmdList.SetRasterizerState(...);
    RHICmdList.SetBatchedShaderParameters(...);

    // At first actual GPU work, flush cache
    RHICmdList.DrawCall();  // ← Triggers Flush
}
```

**Why:** Batches state changes; avoids redundant API calls

**UE5 Reference:** `W:/UE_5.6_Source/Engine/Source/Runtime/D3D12RHI/Private/D3D12CommandContext.cpp`

---

## Anti-Patterns: NEVER DO THESE

### ❌ Direct D3D Calls
```cpp
// WRONG
context->PSSetShaderResources(0, 1, &SRV);

// RIGHT
RHICmdList.SetBatchedShaderParameters(Shader, Params);
```

### ❌ Map/Unmap Every Frame
```cpp
// WRONG
void* Ptr = ScratchCB->Map();
memcpy(Ptr, Data, Size);
ScratchCB->Unmap();

// RIGHT
auto Alloc = ConstantBufferRing.Allocate(Size);
memcpy(Alloc.MappedData, Data, Size);
```

### ❌ Type Switching
```cpp
// WRONG
if (node->Type == NodeType::Mesh) { ... }
else if (node->Type == NodeType::Light) { ... }

// RIGHT
node->Proxy->GetDynamicMeshElements(...);  // Virtual dispatch
```

### ❌ Ignoring Resource State
```cpp
// WRONG
// Texture is in SRV state
RenderTargetView = Texture->GetRTV();
DrawCall();  // ← DEVICE REMOVAL on DX12

// RIGHT
RHICmdList.Transition(Texture, ERHIAccess::SRVGraphics, ERHIAccess::RTV);
RenderTargetView = Texture->GetRTV();
DrawCall();
```

### ❌ Immediate Binding
```cpp
// WRONG
for (int i = 0; i < 10; ++i) {
    RHICmdList.SetFragmentTexture(i, Textures[i]);
}

// RIGHT
FRHIBatchedShaderParameters Params;
for (int i = 0; i < 10; ++i) {
    Params.SetShaderTexture(i, Textures[i]);
}
RHICmdList.SetBatchedShaderParameters(Shader, Params);
```

---

## Implementation Checklist

- [ ] **Phase 1: Core**
  - [ ] Implement `FRHIBatchedShaderParameters`
  - [ ] Replace all `SetFragmentTexture()` calls
  - [ ] Implement ring buffer constant allocator

- [ ] **Phase 2: State Management**
  - [ ] Create `FRHIStateCache`
  - [ ] Add `.Flush()` call before draw
  - [ ] Implement descriptor cache layer

- [ ] **Phase 3: Transitions**
  - [ ] Define `ERHIAccess` enum
  - [ ] Implement `Transition()` API
  - [ ] Add to all resource state changes

- [ ] **Phase 4: Validation**
  - [ ] Block direct D3D calls
  - [ ] Validate state machine (no invalid transitions)
  - [ ] Check binding completeness

---

## Common Mistakes

### Mistake 1: Ring Buffer Too Small
```cpp
// WRONG
FConstantBufferRing Ring(1024);  // Only 1 KB per buffer!

// RIGHT
FConstantBufferRing Ring(256 * 1024);  // 256 KB per buffer
```

**Why:** If you run out of space and re-use buffer before GPU is done reading, HAZARD.

### Mistake 2: Forgetting Transitions
```cpp
// WRONG
RHICmdList.SetRenderTarget(Texture);  // Texture still in SRV state from previous pass
RHICmdList.DrawCall();

// RIGHT
RHICmdList.Transition(Texture, ERHIAccess::SRVGraphics, ERHIAccess::RTV);
RHICmdList.SetRenderTarget(Texture);
RHICmdList.DrawCall();
```

### Mistake 3: Nested Flush Calls
```cpp
// WRONG
StateCache.Flush();
StateCache.SetFragmentTexture(0, Texture);  // ← New state change
StateCache.Flush();  // ← Redundant

// RIGHT
StateCache.SetFragmentTexture(0, Texture);
StateCache.Flush();  // ← Once at end
```

### Mistake 4: Using DX11 Constants in DX12
```cpp
// WRONG
ID3D11Buffer* CB = GetD3D11ConstantBuffer();
// This type doesn't exist in DX12 backend!

// RIGHT
FD3D12ResourceLocation CBLocation = ConstantBufferRing.Allocate(Size);
// Works on both backends
```

### Mistake 5: Descriptor Heap Overs-izing
```cpp
// WRONG
CreateDescriptorHeap(1000000);  // Way too many descriptors
// Wastes VRAM, slow to iterate

// RIGHT
CreateDescriptorHeap(1000);  // Typical frame binds 100-500 descriptors
```

---

## Performance Tips

### Tip 1: Reuse Batched Parameters
```cpp
// Instead of creating new FRHIBatchedShaderParameters every draw:
FRHIBatchedShaderParameters MaterialParams;
// Set once
MaterialParams.SetShaderTexture(0, AlbedoTex);
MaterialParams.SetShaderTexture(1, NormalTex);
// Reuse 100 times
for (int i = 0; i < 100; ++i) {
    RHICmdList.SetBatchedShaderParameters(Shader, MaterialParams);
    RHICmdList.DrawCall();
}
```

### Tip 2: Descriptor Cache Hits
```cpp
// Good cache locality:
RHICmdList.SetFragmentTexture(0, Texture0);
RHICmdList.SetFragmentTexture(1, Texture1);
RHICmdList.DrawCall();  // ← Cache hit

RHICmdList.DrawCall();  // ← Cache hit (same textures)
RHICmdList.DrawCall();  // ← Cache hit (same textures)

RHICmdList.SetFragmentTexture(0, Texture2);  // ← Cache miss
RHICmdList.DrawCall();  // ← Flush happens, re-binds
```

### Tip 3: Batch Transitions
```cpp
// Instead of:
for (int i = 0; i < 100; ++i) {
    RHICmdList.Transition(Textures[i], SRV, RTV);
}
RHICmdList.Flush();  // Issues 100 barriers

// Batch them:
TArray<FRHITransitionInfo> Transitions;
for (int i = 0; i < 100; ++i) {
    Transitions.Add({Textures[i], SRV, RTV});
}
RHICmdList.TransitionBatch(Transitions);  // Issues 1 call with 100 barriers
```

---

## Debugging Checklist

When you get a segfault or device removal error:

- [ ] Is there a Map/Unmap on the same CB frame-to-frame?
- [ ] Are parameters collected before execution (no direct PSSetX calls)?
- [ ] Is state cache being flushed before draws?
- [ ] Are all resource transitions declared?
- [ ] Are ring buffers large enough (100+ KB per buffer)?
- [ ] Is descriptor cache invalidating when bindings change?
- [ ] Are you accessing GPU resources from CPU without proper sync?

---

## File Locations (Windows)

```
UE5 RHI Reference Implementation:
├─ W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\
│  ├─ RHIShaderParameters.h          ← Batched parameters
│  └─ RHIResources.h                 ← Base types
├─ W:\UE_5.6_Source\Engine\Source\Runtime\D3D12RHI\Private\
│  ├─ D3D12Commands.cpp              ← State cache (BindUniformBuffer pattern)
│  ├─ D3D12ConstantBuffer.cpp        ← Ring buffer
│  └─ D3D12DescriptorCache.cpp       ← Descriptor caching
└─ W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\
   └─ RenderGraphBuilder.h            ← RDG deferred execution
```

---

## One-Sentence Summary

**UE5 abstracts DX11/DX12 differences by separating resource binding (collection) from execution (backend-specific flush), using explicit ring buffers for constants, deferred state caching for descriptors, and explicit transitions for resource state validation.**

