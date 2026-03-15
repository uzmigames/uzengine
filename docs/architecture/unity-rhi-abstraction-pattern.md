# Unity RHI Abstraction Patterns — Applicable to UzEngine

## Pattern 1: Constant Buffer Binding — The Scratch CB Pattern

### Problem
DX12 and DX11 have fundamentally incompatible CB binding models:
- **DX12:** GPU virtual address → immediate bind → GPU reads from VRAM
- **DX11:** CPU data → Map → GPU buffer → bind → GPU reads

### Unity Solution

Store material properties in **two representations**:

```cpp
// (Conceptual — Unity uses different API, but same principle)

class MaterialProperties {
    // Form A: For DX12 persistent GPU buffer
    ID3D12Resource* gpuPropertyBuffer;
    uint64_t gpuVirtualAddress;

    // Form B: For DX11 CPU-side data
    std::vector<uint8_t> cpuPropertyBuffer;  // Marshalled float/int4 data

    void SetFloat(const std::string& name, float value) {
        // Update both representations
        cpuPropertyBuffer[offsetOf(name)] = value;
        gpuPropertyBuffer[offset] = value;  // Persistent mapped upload heap
    }
};

// At render time
class RenderPass {
    void ExecuteForDX12() {
        uint64_t cbVA = material->gpuVirtualAddress;
        cmdList->SetGraphicsRootConstantBufferView(slot, cbVA);
        cmdList->DrawIndexed(...);
    }

    void ExecuteForDX11() {
        const void* cpuData = material->cpuPropertyBuffer.data();
        uint32_t dataSize = material->cpuPropertyBuffer.size();

        // DX11 backend handles scratch CB internally
        cmdList->SetRootCBV(slot, nullptr, 0, cpuData, dataSize);
        cmdList->DrawIndexed(...);
    }
};
```

### UzEngine Implementation (Already Correct!)

Your rhi.h dual-path overload:

```cpp
// DX12: GPU VA path (efficient, no copy)
void SetRootCBV(uint32_t rootParamIndex, uint64_t gpuAddress) {
    // DX12 impl: m_cmdList->SetGraphicsRootConstantBufferView(rootParamIndex, gpuAddress);
    // DX11 impl: no-op (no GPU VA concept)
}

// Dual-path: supports both backends
void SetRootCBV(uint32_t rootParamIndex, RHIBufferHandle buffer,
                uint64_t offsetBytes, const void* cpuData, uint32_t dataSize) {
    // DX12 impl: extract buffer->gpuVA + offset → SetGraphicsRootConstantBufferView
    // DX11 impl: GetOrCreateScratchCB → Map/Unmap → VSSetConstantBuffers
}
```

**This is the exact pattern Unity uses.** Your design is **correct**.

### Key Implementation Detail: Alignment

DX11 requires constant buffer sizes to be 16-byte aligned:

```cpp
// UzEngine dx11_command_list.cpp:233
const uint32_t alignedSize = (dataSize + 15u) & ~15u;  // ✓ Correct
```

DX12 typically requires 256-byte aligned offsets for root CBVs. Your offset handling:

```cpp
uint64_t offsetBytes  // Caller ensures 256-byte aligned for DX12
```

**Document this requirement** in the rhi.h comment:

```cpp
/// # Arguments
/// * `offsetBytes` — Byte offset into the buffer (must be 256-byte aligned for DX12,
///   any value OK for DX11 but should still align for performance).
```

---

## Pattern 2: Texture Binding Triage — Bindless vs Descriptor Tables vs Slot Binding

### Problem
Three incompatible texture binding models:
1. **DX12 SM6.6:** Bindless via descriptor heap indices
2. **DX12 SM5:** Descriptor tables (copy descriptors per-draw)
3. **DX11:** Direct register slot binding

### Unity Solution

The **RenderPass builder** (high-level, not RHI) decides which path based on device capabilities:

```cpp
class RenderPass {
    void Record(IRHIDevice* device, IRHICommandList* cmdList) {
        // Decision point: what backend are we on?
        if (device->IsBindless()) {
            // SM6.6+ path
            RecordBindless(cmdList);
        } else if (device->GetShaderModel() >= SM5_0) {
            // SM5 descriptor table path
            RecordDescriptorTables(cmdList);
        } else {
            // Fallback: DX11 slot binding
            RecordSlotBinding(cmdList);
        }
    }

private:
    void RecordBindless(IRHICommandList* cmdList) {
        // SM6.6: push constants with descriptor indices
        uint32_t texIndex = device->GetTextureSRVIndex(mainTexture);
        cmdList->SetDescriptorIndex(0, texIndex);  // Root param 0 = descriptor index slot
        // Shader reads: ResourceDescriptorHeap[index]
    }

    void RecordDescriptorTables(IRHICommandList* cmdList) {
        // SM5: descriptor table with copied descriptors
        uint32_t srvIndex = device->GetTextureSRVIndex(mainTexture);
        cmdList->SetDescriptorTable(0, srvIndex);  // Root param 0 = descriptor table
        // Shader reads: textures[0] (from bound descriptor range)
    }

    void RecordSlotBinding(IRHICommandList* cmdList) {
        // DX11: direct slot binding
        cmdList->SetFragmentTexture(0, mainTexture);  // Bind to t0
        cmdList->SetFragmentTexture(1, normalTexture);  // Bind to t1
        // Shader reads: register(t0), register(t1)
    }
};
```

### UzEngine Application

Your current code **does not have this decision logic**. Add to render_graph_builder:

```cpp
// (Pseudo-code in render_graph_builder.h or render_graph_pass.cpp)

class RenderGraphPass {
    void ExecutePass(const RenderGraphContext& ctx) {
        auto* device = ctx.device;
        auto* cmdList = ctx.cmdList;

        if (device->IsBindless()) {
            // Use SetDescriptorIndex (push constants)
            for (const auto& [slot, texHandle] : m_textureBindings) {
                uint32_t descriptorIndex = device->GetTextureSRVIndex(texHandle);
                if (descriptorIndex == UINT32_MAX) {
                    UZ_LOG(RGDebug, Error, "Texture {} has no SRV", texHandle.index);
                    continue;
                }
                // Root param 0 = descriptor index; shader uses NonUniformResourceIndex
                cmdList->SetDescriptorIndex(0, descriptorIndex);
            }
        } else {
            // Use SetFragmentTexture (DX11-compatible slot binding)
            uint32_t textureSlot = 0;
            for (const auto& [_, texHandle] : m_textureBindings) {
                if (textureSlot >= 16) {  // D3D11 PS has 16 SRV slots
                    UZ_LOG(RGDebug, Warning, "Exceeded 16 texture slots in DX11 mode");
                    break;
                }
                cmdList->SetFragmentTexture(textureSlot, texHandle);
                textureSlot++;
            }
        }

        // Proceed with draw/dispatch
        RecordDrawCalls(cmdList);
    }
};
```

**Key insight:** Your rhi.h **already has both paths** (SetDescriptorIndex vs SetFragmentTexture). The render graph just needs to **choose which one to use**.

---

## Pattern 3: Sampler Binding via Shader Variants

### Problem
DX12 supports "static samplers" in root signatures (compile-time). DX11 does not.

### Unity Solution

Encode sampler state in **shader bytecode**, not root signature.

**Shader source:**
```hlsl
// unity-shader.hlsl (pseudo-code)

#pragma variant_selector LINEAR_FILTERING POINT_FILTERING
#pragma variant_selector WRAP_MODE CLAMP_MODE

Texture2D _MainTex : register(t0);
#if LINEAR_FILTERING && WRAP_MODE
SamplerState _MainSampler : register(s0);  // Linear, wrap
#elif POINT_FILTERING && WRAP_MODE
SamplerState _MainSampler : register(s0);  // Point, wrap
#elif LINEAR_FILTERING && CLAMP_MODE
SamplerState _MainSampler : register(s0);  // Linear, clamp
#else
SamplerState _MainSampler : register(s0);  // Point, clamp
#endif

float4 PSMain(float2 uv : TEXCOORD0) : SV_TARGET {
    return _MainTex.Sample(_MainSampler, uv);
}
```

**Compilation:**
```
- Compile variant A (LINEAR + WRAP): binary embeds Linear+Wrap in shader bytecode
- Compile variant B (POINT + CLAMP): binary embeds Point+Clamp in shader bytecode
```

**Runtime (DX11):**
```cpp
// Shader binary already has sampler descriptor embedded
ID3D11ShaderReflection* refl;
D3DReflect(shaderBytecode, bytecodeSize, IID_ID3D11ShaderReflection, &refl);

D3D11_SHADER_DESC shaderDesc;
refl->GetDesc(&shaderDesc);

for (uint32_t i = 0; i < shaderDesc.BoundResources; i++) {
    D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
    refl->GetResourceBindingDesc(i, &resourceDesc);

    if (resourceDesc.Type == D3D_SIT_SAMPLER) {
        // Extract sampler descriptor from bytecode
        D3D11_SAMPLER_DESC samplerDesc = ExtractSamplerDesc(refl, i);
        // Create actual ID3D11SamplerState
        device->CreateSamplerState(&samplerDesc, &samplerState);
        // Bind at expected slot
        ctx->PSSetSamplers(resourceDesc.BindPoint, 1, &samplerState);
    }
}
```

**Runtime (DX12):**
```cpp
// Use sampler descriptor to populate root signature
D3D12_STATIC_SAMPLER_DESC staticSamplers[1];
ExtractStaticSamplerFromShader(shaderBytecode, staticSamplers[0]);
// Root signature created once with static sampler
```

### UzEngine Application

Your DX11CommandList::Begin() (line 70-81) **already does part of this**:

```cpp
void DX11CommandList::Begin() {
    // Bind pre-created samplers matching DX12 static samplers
    ID3D11SamplerState* defaultSamplers[2] = {
        m_device->GetDefaultSampler(),              // s0 = LINEAR + WRAP
        m_device->GetShadowComparisonSampler(),     // s1 = COMPARISON
    };
    m_ctx->PSSetSamplers(0, 2, defaultSamplers);
}
```

**To complete this pattern:**

1. **Create samplers matching shader permutations** in DX11Device::Init():
   ```cpp
   // s0: Linear, wrap (default)
   D3D11_SAMPLER_DESC linearWrapDesc = {
       .Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR,
       .AddressU = D3D11_TEXTURE_ADDRESS_WRAP,
       .AddressV = D3D11_TEXTURE_ADDRESS_WRAP,
       .AddressW = D3D11_TEXTURE_ADDRESS_WRAP,
   };
   device->CreateSamplerState(&linearWrapDesc, &m_defaultSampler);

   // s1: Comparison, for shadows
   D3D11_SAMPLER_DESC shadowDesc = {
       .Filter = D3D11_FILTER_COMPARISON_MIN_MAG_MIP_LINEAR,
       .ComparisonFunc = D3D11_COMPARISON_LESS_EQUAL,
       // ...
   };
   device->CreateSamplerState(&shadowDesc, &m_shadowComparisonSampler);
   ```

2. **At shader compilation**, reflect the bytecode to extract sampler requirements:
   ```cpp
   // In dx11_pso_creation.cpp (equivalent to dx12_pso_creation.cpp)

   D3D11_SAMPLER_DESC ExtractSamplerFromShaderBytecode(const void* bytecode, size_t size) {
       ID3D11ShaderReflection* refl = nullptr;
       D3DReflect(bytecode, size, IID_ID3D11ShaderReflection, (void**)&refl);

       D3D11_SHADER_DESC shaderDesc;
       refl->GetDesc(&shaderDesc);

       for (uint32_t i = 0; i < shaderDesc.BoundResources; i++) {
           D3D11_SHADER_INPUT_BIND_DESC resourceDesc;
           refl->GetResourceBindingDesc(i, &resourceDesc);

           if (resourceDesc.Type == D3D_SIT_SAMPLER) {
               // Get sampler descriptor from resource binding
               D3D11_SHADER_INPUT_BIND_DESC samplerDesc;
               // (Note: D3D11 doesn't expose sampler state directly from bytecode,
               //  must infer from variable names or shader keywords)

               // Fallback: match by name
               std::string samplerName(resourceDesc.Name);
               if (samplerName.find("Linear") != std::string::npos) {
                   return CreateLinearSamplerDesc();
               } else if (samplerName.find("Shadow") != std::string::npos) {
                   return CreateShadowSamplerDesc();
               }
           }
       }

       return CreateDefaultLinearWrapSamplerDesc();
   }
   ```

---

## Pattern 4: Barrier Abstraction — Explicit vs Implicit

### Problem
DX12 requires **explicit resource state tracking and barriers**. DX11 does not.

### Unity Solution

ResourceBarrier() is **conditionally compiled**:

```cpp
// In SRP render pass builder

class RenderGraphPass {
    void InsertBarriers(IRHICommandList* cmdList) {
        // Conceptually: transition texture from SRV to RTV state
        for (const auto& barrier : m_barriers) {
            // Backend-specific implementation:
            cmdList->ResourceBarrier(&barrier, 1);
        }
    }
};

// In rhi.h implementation:

// DX12:
void DX12CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    D3D12_RESOURCE_BARRIER d3dBarriers[16];
    for (uint32_t i = 0; i < count; i++) {
        auto& rhibarrier = barriers[i];
        auto& d3dbarrier = d3dBarriers[i];

        d3dbarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        d3dbarrier.Transition.pResource = GetD3D12Resource(rhibarrier.texture);
        d3dbarrier.Transition.StateBefore = ToD3D12ResourceState(rhibarrier.beforeState);
        d3dbarrier.Transition.StateAfter = ToD3D12ResourceState(rhibarrier.afterState);
    }
    m_cmdList->ResourceBarrier(count, d3dBarriers);
}

// DX11:
void DX11CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    // Driver handles implicit transitions; this is a no-op.
    // But we can validate for debugging:
    for (uint32_t i = 0; i < count; i++) {
        const auto& barrier = barriers[i];

        // Warn if texture is being used for both input and output in same pass
        if (barrier.beforeState == RHIResourceState::RenderTarget &&
            barrier.afterState == RHIResourceState::ShaderResource) {
            UZ_LOG(RGDebug, Warning, "DX11 implicit barrier: RTV → SRV (may cause hazard)");
        }
    }

    // No-op on DX11 — driver manages transitions
}
```

### UzEngine Implementation (Already Correct!)

Your dx11_command_list.cpp (line 358-362):

```cpp
void DX11CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    // DX11 manages resource state transitions implicitly — intentional no-op.
    (void)barriers;
    (void)count;
}
```

This is **exactly right**. DX11 has no barrier API.

**Improvement:** Add validation logging for shader-unsafe hazards:

```cpp
void DX11CommandList::ResourceBarrier(const RHIBarrierDesc* barriers, uint32_t count) {
    // DX11 manages implicit transitions, but we can detect unsafe patterns
    for (uint32_t i = 0; i < count; i++) {
        const auto& barrier = barriers[i];

        // Detect UAV + SRV hazard (shader-unsafe)
        if ((barrier.beforeState == RHIResourceState::UnorderedAccess &&
             barrier.afterState == RHIResourceState::ShaderResource) ||
            (barrier.beforeState == RHIResourceState::ShaderResource &&
             barrier.afterState == RHIResourceState::UnorderedAccess)) {
            UZ_LOG(RHI, Warning,
                   "ResourceBarrier: DX11 implicit UAV/SRV barrier (texture={}, before={}, after={}). "
                   "GPU driver will serialize; consider restructuring passes to avoid.",
                   barrier.texture.index,
                   static_cast<int>(barrier.beforeState),
                   static_cast<int>(barrier.afterState));
        }
    }
    // No-op on DX11 — driver manages barriers
}
```

---

## Pattern 5: CommandBuffer Validation Checklist

### Before Execute() is Called

**Unity RenderGraph validation:**

```cpp
// In RenderGraph::Execute()
for (const auto& pass : m_passes) {
    // Pre-flight checks
    for (const auto& [handle, texture] : pass.m_outputs) {
        UZ_ENSURE(texture.IsValid(), "Pass {} has invalid output texture", pass.name);
        UZ_ENSURE(device->GetTextureDesc(texture).format != UNKNOWN,
                  "Pass {} binds uninitialized texture {}", pass.name, texture.index);
    }

    for (const auto& [slot, texture] : pass.m_inputs) {
        UZ_ENSURE(texture.IsValid(), "Pass {} has invalid SRV texture at slot {}",
                  pass.name, slot);
    }

    // Execute pass
    pass.Execute(cmdList, device);
}
```

### UzEngine Checklist

Add to render_graph_pass::Execute():

```cpp
void RenderGraphPass::Execute(RenderGraphContext ctx) {
    // Pre-flight validation (DX11-specific)
    if (!ctx.device->IsBindless()) {
        // DX11 path: validate all texture slots have valid SRVs
        uint32_t maxSlots = 16;  // D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT
        uint32_t boundSlots = 0;

        for (const auto& [slot, texHandle] : m_textureBindings) {
            UZ_ENSURE(slot < maxSlots, "Texture slot {} exceeds DX11 limit {}", slot, maxSlots);

            uint32_t srvIndex = ctx.device->GetTextureSRVIndex(texHandle);
            UZ_ENSURE(srvIndex != UINT32_MAX,
                      "Pass {} texture at slot {} has no SRV", m_name, slot);

            boundSlots++;
        }

        UZ_LOG(RGDebug, Debug, "Pass {} binding {} textures (DX11)", m_name, boundSlots);
    }

    // Proceed with binding and drawing
    RecordCommands(ctx.cmdList);
}
```

---

## Conclusion: Why Unity's Approach Works

1. **Explicit backends:** Don't hide backend differences; make them explicit in high-level code
2. **Data duplication:** Store material data in multiple forms (GPU VA, CPU buffer) — cost is negligible
3. **No magic conversions:** SetRootCBV takes `cpuData` explicitly so callers know it's DX11-aware
4. **Per-backend validation:** ResourceBarrier is no-op on DX11 but validates on DX12
5. **Pass-level decisions:** Render passes (not RHI) choose binding strategy based on device capabilities

**Your RHI design is already correct.** The crash is likely in **higher-level code** (render graph, shader binding) that doesn't validate before calling RHI methods.
