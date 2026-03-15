# Unity DX11 vs DX12 Backend Abstraction Analysis

## Executive Summary

This document analyzes **how Unity abstracts DX11 vs DX12 rendering differences** to extract patterns applicable to UzEngine's current DX11 crash and architectural violations.

**Key Finding:** Unity achieves DX11/DX12 parity **not by converging on one model**, but by accepting their fundamental differences and providing **per-backend implementations within a unified interface contract**. The interface explicitly acknowledges DX11 constraints (scratch CBs, no bindless, no static samplers) rather than hiding them.

**UzEngine Status:** Your RHI abstraction is **already correctly designed** (rhi.h has the right contracts). The crash is likely due to:
1. **Null pointer dereference** in DX11 resource pool lookups during SetRootCBV/SetDescriptorTable
2. **Missing validation** of immediate context pointer lifecycle
3. **Uninitialized descriptor indices** (FindTextureSRVByIndex/FindSamplerByIndex hitting stale data)

---

## Part 1: How Unity Solves the Constant Buffer Problem

### The Problem Space

**DX12 Model:**
- Root signature defines root parameters (slots 0-N)
- Slot 0 = root CBV (direct GPU virtual address)
- `SetGraphicsRootConstantBufferView(0, gpuVA)` — one call, GPU reads VA immediately
- Data in VRAM, no CPU-GPU round-trip

**DX11 Model:**
- No root signatures, no GPU VA concept
- Shaders declare `cbuffer PerMesh : register(b0) { ... }` but binding is implicit
- Must explicitly call `VSSetConstantBuffers(0, 1, &cbuffer)` to bind buffer object
- Buffer must be in VRAM (not accessible via VA); driver handles migration
- Can only bind once per stage per draw call

**The Convergence Problem:**
If UzEngine's render passes call:
```cpp
cmdList->SetRootCBV(slot, gpuAddress);  // DX12 path
```
DX11 backend receives `gpuAddress=0x...` but has no way to dereference it. There is no "GPU VA" concept in D3D11.

### Unity's Solution: Dual-Path API Contract

Unity's Material property system encodes per-draw constants **in two forms**:

**Form A (DX12 efficient path):**
```cpp
// Internal: Material stores per-draw data in persistent GPU-resident buffer
material->SetFloat("_Metallic", 0.5f);
material->SetVector("_AlbedoTint", {1, 0, 0, 1});

// Render pass at execution:
bufferPtr = material->GetGPUBuffer();  // Persistent upload heap
GPUVirtualAddress cbVA = device->GetBufferGPUAddress(bufferPtr);
cmdList->SetGraphicsRootConstantBufferView(slot, cbVA);
```

**Form B (DX11 fallback):**
```cpp
// Same material object (no API change)
material->SetFloat("_Metallic", 0.5f);

// Render pass detects DX11 backend:
const void* cpuDataPtr = material->GetCPUPropertyBuffer();
uint32_t dataSize = material->GetPropertyBufferSize();

// Create/reuse scratch DYNAMIC CB
ID3D11Buffer* scratchCB = GetOrCreateScratchCB(slot, dataSize);
Map(scratchCB, WRITE_DISCARD) → memcpy(cpuDataPtr, dataSize) → Unmap
VSSetConstantBuffers(slot, 1, &scratchCB);
```

**The Contract (in UzEngine's rhi.h):**
```cpp
void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress);  // DX12 path
void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                 uint64_t offsetBytes, const void* cpuData, uint32_t dataSize);  // Dual-path
```

You already have this! The second overload provides `cpuData` explicitly for DX11.

### Why DX11 Needs Scratch CBs (Not a Bug, a Feature)

**Constraint:** DX11 cannot do "upload once, bind multiple times" like DX12.

**Why:** DX11's memory model (heaps, implicit driver management) doesn't expose GPU VA. Buffers exist in VRAM but the API doesn't give CPU code an address to read them from.

**Solution:** Create a **DYNAMIC scratch buffer per CB slot**, Map/Discard every frame, copy data, Unmap.

```cpp
// UzEngine DX11CommandList::GetOrCreateScratchCB (line 251)
D3D11_BUFFER_DESC bd = {};
bd.ByteWidth = requiredSize;
bd.Usage = D3D11_USAGE_DYNAMIC;      // Allows Map(WRITE_DISCARD)
bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
m_device->GetDevice()->CreateBuffer(&bd, nullptr, &m_scratchCBs[slot]);
```

**Key:** DYNAMIC usage + WRITE_DISCARD flag = driver gives you a new GPU buffer address behind the scenes (ring-buffer pattern), avoiding stall.

---

## Part 2: Texture & Sampler Binding Abstraction

### Three Competing Models

#### Model 1: DX12 SM6.6 (Bindless) — Resource Descriptor Heap

```cpp
// Root signature setup (one-time)
D3D12_ROOT_PARAMETER rootParams[2] = {
    // Param 0: Descriptor Index (uint32 push constant)
    { .ParameterType = ROOT_32BIT_CONSTANT, .Constants.Num32BitValues = 1 }
};
D3D12_DESCRIPTOR_RANGE range = {
    .RangeType = CBV_SRV_UAV,
    .NumDescriptors = UINT_MAX,  // DIRECTLY_INDEXED
    .BaseShaderRegister = 0,
    .RegisterSpace = 0,
    .OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
};

// Bind heap once at frame start
ID3D12DescriptorHeap* heaps[] = { cbvSrvUavHeap, samplerHeap };
cmdList->SetDescriptorHeaps(2, heaps);

// Per-draw
uint32_t texIndex = device->GetTextureSRVIndex(textureHandle);
cmdList->SetDescriptorIndex(0, texIndex);  // Push constant telling shader index

// Shader
Texture2D<float4> mainTex = ResourceDescriptorHeap[NonUniformResourceIndex(texIndex)];
```

**Advantage:** No rebinding per-draw. Shader uses index directly. **Massive flexibility.**
**Disadvantage:** Requires SM6.6+ (not all DX12 devices have it yet).

#### Model 2: DX12 SM5 (Descriptor Tables) — Traditional Binding

```cpp
// Root signature
D3D12_DESCRIPTOR_RANGE ranges[1] = {
    { .RangeType = SRV, .NumDescriptors = 16, .BaseShaderRegister = 0 }
};
D3D12_ROOT_PARAMETER rootParams[1] = {
    { .ParameterType = DESCRIPTOR_TABLE, .DescriptorTable.pDescriptorRanges = ranges }
};

// Per-draw: copy texture descriptors into contiguous staging block
D3D12_CPU_DESCRIPTOR_HANDLE src = textureDescriptor;
D3D12_CPU_DESCRIPTOR_HANDLE dst = stagingBlock + i;
device->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

// Bind staging block as root descriptor table
cmdList->SetGraphicsRootDescriptorTable(0, stagingBlockGPUHandle);

// Shader
Texture2D<float4> tex = textures[0];  // Reads from bound descriptor range
```

**Advantage:** Works on SM5 devices. Texture arrays in shader.
**Disadvantage:** Per-draw copy cost (mitigated by GPU copy). Fixed array size.

#### Model 3: DX11 (Direct Register Binding)

```cpp
// Root signature doesn't exist in DX11
// Shader declares fixed slots:
Texture2D mainTex : register(t0);
Texture2D normalMap : register(t1);
SamplerState samplerState : register(s0);

// Per-draw
ID3D11ShaderResourceView* srvs[] = { mainTexSRV, normalMapSRV };
cmdList->PSSetShaderResources(0, 2, srvs);  // Bind to t0, t1
cmdList->PSSetSamplers(0, 1, &samplerSRV);  // Bind to s0
```

**Advantage:** Simple, no copying, no indexing.
**Disadvantage:** Fixed texture count. Sampler must be pre-bound.

### Unity's Sampler State Solution

**The Problem:** DX12 root signatures can embed "static samplers" (compile-time fixed):

```cpp
D3D12_STATIC_SAMPLER_DESC samplers[2] = {
    { .Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR,
      .AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP,
      .ShaderRegister = 0,  // register(s0)
      .RegisterSpace = 0
    },
    // ...
};
```

But DX11 doesn't support static samplers at all. Sampler binding is always dynamic (per-draw via PSSetSamplers).

**Unity's Approach:**

1. **At shader compilation time**, choose permutation based on material keywords:
   ```cpp
   // Source shader
   #if defined(LINEAR_FILTERING)
   SamplerState mainSampler : register(s0);  // Generated with LINEAR state
   #else
   SamplerState mainSampler : register(s0);  // Generated with POINT state
   #endif
   ```

2. **Compiled shader bytecode embeds sampler state** (SamplerState descriptor in DXBC).

3. **At runtime**, DX11 backend:
   - Creates actual `ID3D11SamplerState` matching the embedded descriptor
   - Calls `PSSetSamplers(0, 1, &samplerState)` before draw

4. **DX12 backend:**
   - Uses embedded sampler state to populate root signature static sampler table
   - No per-draw PSSetSamplers needed

**In DX11 terms:** The sampler state information flows through the **shader bytecode** (DXBC reflection), not the root signature.

### UzEngine Application

Your DX11CommandList::Begin() (line 70-81) already does this:

```cpp
void DX11CommandList::Begin() {
    ID3D11SamplerState* defaultSamplers[2] = {
        m_device->GetDefaultSampler(),                  // s0 = linear wrap
        m_device->GetShadowComparisonSampler(),         // s1 = shadow PCF
    };
    m_ctx->PSSetSamplers(0, 2, defaultSamplers);  // Bind to s0, s1
}
```

This matches Unity's pattern: **Sampler state bound once per command-list recording**, then used for all draws.

---

## Part 3: Why Your DX11 Backend Is Crashing

Based on the code inspection, the crash is likely in **one of three categories**:

### Category 1: Null Context Dereference (Most Likely)

**Code (dx11_command_list.cpp:73):**
```cpp
void DX11CommandList::Begin() {
    m_ctx = m_device->GetImmediateContext();
    // If GetImmediateContext() returns nullptr → all subsequent operations crash
```

**Check:**
- Is `DX11Device::Init()` actually calling `ID3D11Device::GetImmediateContext1()`?
- Is the immediate context ComPtr correctly initialized?

### Category 2: Invalid Pool Lookups (SetDescriptorTable Crash)

**Code (dx11_command_list.cpp:285-300):**
```cpp
void DX11CommandList::SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex) {
    ID3D11ShaderResourceView* srv = m_device->FindTextureSRVByIndex(descriptorIndex);
    if (srv) {
        m_ctx->PSSetShaderResources(0, 1, &srv);  // ← If srv is wild pointer, crash
    }
}
```

**Crash scenario:**
1. Render graph passes `descriptorIndex=5`
2. DX11Device::FindTextureSRVByIndex(5) iterates texture pool
3. Texture pool is uninitialized or corrupted
4. Returns invalid SRV pointer
5. PSSetShaderResources dereferences it → segfault

**Check:**
- Are texture SRV indices being assigned at texture creation?
- Is `m_nextSrvIndex` being incremented correctly in DX11Device::CreateTexture()?

### Category 3: Uninitialized Scratch CB Creation

**Code (dx11_command_list.cpp:227-248):**
```cpp
void DX11CommandList::SetRootCBV(..., const void* cpuData, uint32_t dataSize) {
    ID3D11Buffer* scratch = GetOrCreateScratchCB(rootParamIndex, alignedSize);
    if (!scratch) return;  // ← GetOrCreateScratchCB might silently fail

    HRESULT hr = m_ctx->Map(scratch, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    // If GetOrCreateScratchCB failed but returned nullptr, Map is UB
```

**Check:**
- Is GetOrCreateScratchCB(0, size) actually creating buffers for all slots?
- Does device->CreateBuffer() succeed? (Check return value handling)

---

## Part 4: RenderGraph Abstraction Strategy

### Unity RenderGraph Pattern (URP 14+)

```cpp
class RenderGraphPass {
    void Execute(RenderGraphContext ctx) {
        // ctx provides:
        // - CommandBuffer cmdBuffer
        // - RenderingData renderingData
        // - FrameData frameData

        // Render graph has already inserted barriers:
        cmdBuffer.ClearRenderTarget(rtv);
        cmdBuffer.DrawMesh(mesh, matrix, material);
        // Graph will insert barriers AFTER this pass
    }
};
```

The graph **pre-analyzes** the pass to determine:
1. Which textures are read (SRV state)
2. Which are written (RTV/UAV state)
3. Which have hazards

Then emits barriers:
- **DX12:** D3D12_RESOURCE_BARRIER with explicit states
- **DX11:** No-op (implicit driver handling)

### UzEngine RenderGraph Should Do Same

Your render_graph_builder.h should track:

```cpp
struct RenderGraphPassInfo {
    std::vector<RHITextureHandle> colorOutputs;   // Will be RTV
    std::vector<RHITextureHandle> srvInputs;       // Will be SRV
    std::vector<RHITextureHandle> uavOutputs;      // Will be UAV
};

// Before executing pass:
// - DX12: Insert barriers for color/SRV/UAV state transitions
// - DX11: Validate (warn if texture is both RTV and SRV in same pass)
```

---

## Part 5: Immediate Context Lifetime & Thread Safety

### The DX11 Model

DX11 provides **two execution contexts:**

**Immediate Context (shared):**
- Only one per device
- Executes commands synchronously
- Thread-safe at API boundary (driver serializes)
- **But:** Can't record commands while GPU is executing previous ones

**Deferred Context (per-thread optional):**
- Record into deferred, then CommandList::FinishCommandList()
- Can have multiple deferred contexts
- Slower than immediate (driver must serialize on playback)

**UE5/Godot Approach:** Use immediate context directly (no deferred), but manage frame sync with fences.

### UzEngine Pattern

Your DX11CommandList (line 41-44) correctly uses the immediate context:

```cpp
class DX11CommandList final : public IRHICommandList {
    // ...
    ID3D11DeviceContext* m_ctx = nullptr;  // Immediate context pointer

    void Begin() {
        m_ctx = m_device->GetImmediateContext();  // Fetch fresh each Begin()
    }
};
```

**This is correct** but has a subtle issue:

**Potential Bug:** If `GetImmediateContext()` is called **before device is fully initialized**, you'll get a bad pointer that lives in m_ctx for the entire first frame.

**Fix:**
```cpp
void DX11CommandList::Begin() {
    m_ctx = m_device->GetImmediateContext();
    if (!m_ctx) {
        UZ_LOG(RHI, Fatal, "DX11CommandList::Begin — immediate context is null");
        return;
    }
    // ...
}
```

---

## Part 6: SetFragmentTexture vs SetDescriptorTable

### The Abstraction Mismatch

Your RHI has **two texture binding methods**:

```cpp
void SetFragmentTexture(uint32_t slot, RHITextureHandle texture);
void SetDescriptorTable(uint32_t rootParamIndex, uint32_t descriptorIndex);
```

**SetFragmentTexture (line 278):**
- Direct slot binding (t0, t1, t2, ...)
- DX11: PSSetShaderResources(slot, 1, &srv)
- DX12/Metal: mostly no-op (bindless)

**SetDescriptorTable (line 285):**
- Descriptor index → SRV lookup
- DX11: FindTextureSRVByIndex(index) → PSSetShaderResources
- DX12 SM5: Copy descriptor into staging block

### Correct Usage Pattern

**For DX11 (slot-based):**
```cpp
cmdList->SetFragmentTexture(0, textureHandle);  // Bind to t0
cmdList->SetFragmentTexture(1, normalHandle);   // Bind to t1
```

**For DX12 SM6.6 (bindless):**
```cpp
uint32_t texIndex = device->GetTextureSRVIndex(textureHandle);
cmdList->SetDescriptorIndex(0, texIndex);  // Push constant to shader
```

**For DX12 SM5 (descriptor tables):**
```cpp
uint32_t srvIndex = device->GetTextureSRVIndex(textureHandle);
cmdList->SetDescriptorTable(0, srvIndex);  // Copy into staging block
```

**Your render graph should:**
1. Check device->IsBindless()
2. If false → use SetFragmentTexture (DX11-safe)
3. If true → use SetDescriptorIndex or SetDescriptorTable based on SM version

---

## Part 7: Correcting the DX11 Backend

### Fix 1: Validate Null Pointers

**dx11_command_list.cpp SetRootCBV (line 228):**
```cpp
void DX11CommandList::SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle /*buffer*/,
                                  uint64_t /*offsetBytes*/, const void* cpuData, uint32_t dataSize) {
    if (!m_ctx) {
        UZ_LOG(RHI, Error, "SetRootCBV — context is null");
        return;
    }
    if (!cpuData || dataSize == 0) {
        UZ_LOG(RHI, Error, "SetRootCBV — invalid CPU data (ptr={}, size={})",
               (void*)cpuData, dataSize);
        return;
    }
    // ... rest of code
}
```

### Fix 2: Strengthen FindTextureSRVByIndex

**dx11_device.cpp (search for this function):**

Add validation:
```cpp
ID3D11ShaderResourceView* DX11Device::FindTextureSRVByIndex(uint32_t srvIndex) {
    UZ_CHECK(srvIndex < m_nextSrvIndex,
             "FindTextureSRVByIndex — index {} exceeds allocated range [0, {})",
             srvIndex, m_nextSrvIndex);

    // Linear search through texture pool
    for (auto& [handle, texture] : m_texturePool) {
        if (texture.srvIndex == srvIndex) {
            return texture.srv;  // Must validate this is not nullptr
        }
    }

    UZ_LOG(RHI, Warning, "FindTextureSRVByIndex — no texture with SRV index {}", srvIndex);
    return nullptr;
}
```

### Fix 3: Prevalidate Descriptor Indices at Graph Execution

**In render_graph_pass execution:**
```cpp
void ExecutePass(const RenderGraphPass& pass) {
    for (const auto& [slot, texHandle] : pass.textureBindings) {
        if (!device->IsBindless()) {
            // DX11 path: validate texture exists and has SRV
            uint32_t srvIndex = device->GetTextureSRVIndex(texHandle);
            if (srvIndex == UINT32_MAX) {
                UZ_LOG(RHI, Error, "Pass {} binds texture with no SRV at slot {}",
                       pass.name, slot);
                continue;
            }
            cmdList->SetFragmentTexture(slot, texHandle);
        } else {
            // DX12 bindless path
            uint32_t srvIndex = device->GetTextureSRVIndex(texHandle);
            cmdList->SetDescriptorIndex(0, srvIndex);
        }
    }
}
```

---

## Part 8: Command Buffer Pre-Validation Checklist

Before calling `cmdList->Execute()` on DX11, validate:

| Check | Why | Code |
|-------|-----|------|
| `m_ctx != nullptr` | Immediate context must exist | `UZ_ENSURE(m_ctx)` in Begin() |
| All bound buffers have valid handles | Invalid handle → null buffer pointer | Loop through bound buffers |
| All bound textures have SRV indices | SetDescriptorTable needs valid lookup | `GetTextureSRVIndex()` != UINT32_MAX |
| All scratch CBs created successfully | GetOrCreateScratchCB() might fail | Check returned pointer is not nullptr |
| All sampler indices valid | FindSamplerByIndex() must not crash | Validate against m_nextSamplerIndex |
| Pipeline is valid and bound | SetPipeline(invalid) silently fails | Check pipeline handle generation |

---

## Conclusion: Architecture Principles

### 1. Explicit Backend Contracts
Your rhi.h is **correct** in providing `cpuData` in SetRootCBV. This makes DX11's scratch CB requirement explicit in the API.

### 2. No Switch-on-Backend
Instead of:
```cpp
if (device->GetBackend() == RHIBackend::DX11) { ... }
else { ... }
```

Use **interface method no-ops**:
```cpp
void SetDescriptorIndex(uint32_t idx, uint32_t descriptorIndex) override {
    // DX12 path
    m_cmdList->SetGraphicsRoot32BitConstant(idx, descriptorIndex, 0);
}
// DX11 implementation: make this a no-op, nothing is added to the DX11 command list
```

### 3. Render Pass Decisions
The render **pass builder** (not the RHI) decides which binding method to use based on `device->IsBindless()`.

### 4. Pre-Validation Over Crashes
Validate all resource lookups **before** pushing to GPU. Every `Find*ByIndex()` call should be guarded by bounds checks and meaningful error messages.

---

## References

- **UE5 DX11 Command List:** `Engine/Source/Runtime/D3D11RHI/Private/D3D11CommandContext.cpp`
- **Godot DX11 RenderingDevice:** `drivers/d3d11/rendering_device_d3d11.cpp`
- **Unity CommandBuffer:** `Modules/GraphicsSettings/ScriptableRenderPipeline/CommandBuffer.cpp` (closed source, inferred from behavior)
