<!-- doc-meta
title: Shader Parameter Binding - Before & After
category: Rendering Architecture
tags: [Refactoring, Code Comparison, Best Practice]
---

# Shader Parameter Binding: Before & After Comparison

## The Problem: Manual Parameter Binding

### BEFORE: Direct RHI Calls (UzEngine Current)

```cpp
// File: renderer/passes/deferred_lighting.cpp

void RenderDeferredLighting(
    RHICommandList& cmd_list,
    const DeferredLightingParams& light,
    const GBufferResources& gbuffer,
    const RHITexture& shadow_map)
{
    // Allocate and upload CB data manually
    struct {
        float3 light_pos;
        float padding0;
        float4 light_color;
        float attenuation_radius;
        float3 padding1;
    } cb_data;

    cb_data.light_pos = light.position;
    cb_data.light_color = light.color_and_intensity;
    cb_data.attenuation_radius = light.radius;

    // Upload to GPU
    ID3D12Resource* cb = AllocateUploadBuffer(sizeof(cb_data), &cb_data);
    cmd_list->SetGraphicsRootConstantBufferView(0, cb->GetGPUVirtualAddress());

    // Bind textures individually (5 separate calls)
    cmd_list->SetGraphicsRootDescriptorTable(1, GetTextureDescriptorHeap());
    // ... then manually set descriptor offsets t0, t1, t2, t3, etc.
    // OffsetInHeap(0) = gbuffer.diffuse
    // OffsetInHeap(1) = gbuffer.normal
    // OffsetInHeap(2) = gbuffer.depth
    // OffsetInHeap(3) = shadow_map

    // Bind samplers separately
    cmd_list->SetGraphicsRootDescriptorTable(2, GetSamplerDescriptorHeap());
    // OffsetInHeap(0) = linear_sampler
    // OffsetInHeap(1) = point_sampler

    cmd_list->DrawFullscreenTriangle();
}
```

**Problems:**

1. **Manual struct padding required** — must match HLSL cbuffer layout (float3 needs explicit padding)
2. **Slot assignment manual** — texture slots (t0, t1, ...) hardcoded in C++
3. **CB data split from resources** — no single source of truth for parameter order
4. **Brittle to shader changes** — reorder a texture in HLSL, entire C++ breaks
5. **No type safety** — no compile-time check that C++ matches HLSL
6. **Difficult hot-reload** — shader recompile requires C++ changes to match new slot order
7. **DX11 port nightmare** — completely different binding code for DX11 backend

### AFTER: Metadata-Driven Binding (UE5 Pattern)

```cpp
// File: renderer/shaders/deferred_lighting.h
#pragma once
#include "engine/renderer/core/shader/shader_parameter_macros.h"

namespace uz::render::shaders {

BEGIN_SHADER_PARAMETER_STRUCT(FDeferredLightingParams)
    SHADER_PARAMETER(float3, LightPosition)
    SHADER_PARAMETER(float4, LightColorAndIntensity)
    SHADER_PARAMETER(float, AttenuationRadius)
    SHADER_PARAMETER_TEXTURE(Texture2D, GBufferDiffuse)
    SHADER_PARAMETER_TEXTURE(Texture2D, GBufferNormal)
    SHADER_PARAMETER_TEXTURE(Texture2D, GBufferDepth)
    SHADER_PARAMETER_TEXTURE(Texture2D, ShadowMap)
    SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
    SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
END_SHADER_PARAMETER_STRUCT()

}  // namespace uz::render::shaders

// File: renderer/passes/deferred_lighting.cpp
#include "shaders/deferred_lighting.h"
#include "engine/renderer/core/shader/shader_parameter_binder.h"

void RenderDeferredLighting(
    RHICommandList& cmd_list,
    const DeferredLightingParams& light,
    const GBufferResources& gbuffer,
    const RHITexture& shadow_map)
{
    // Struct initialization — ONE call
    shaders::FDeferredLightingParams params;
    params.LightPosition = light.position;
    params.LightColorAndIntensity = light.color_and_intensity;
    params.AttenuationRadius = light.radius;
    params.GBufferDiffuse = gbuffer.diffuse->GetRHI();
    params.GBufferNormal = gbuffer.normal->GetRHI();
    params.GBufferDepth = gbuffer.depth->GetRHI();
    params.ShadowMap = shadow_map.GetRHI();
    params.LinearSampler = GetGlobalLinearSampler();
    params.PointSampler = GetGlobalPointSampler();

    // ONE call does everything
    SetShaderParameters(cmd_list, GetDeferredLightingShader(), params);

    cmd_list.DrawFullscreenTriangle();
}
```

**Advantages:**

1. ✅ **No manual padding** — compiler handles it
2. ✅ **Type-safe parameter order** — metadata-driven, compile-time check
3. ✅ **Self-documenting** — parameter struct IS the documentation
4. ✅ **Safe shader changes** — reorder texture in HLSL, metadata updates automatically
5. ✅ **Hot-reload friendly** — shader compiler regenerates HLSL from metadata
6. ✅ **DX11/DX12 transparent** — same C++ code, different backend implementations
7. ✅ **Less boilerplate** — single `SetShaderParameters()` call vs. 5+ manual bindings

---

## Scenario: Shader Hot-Reload

### BEFORE: Breaking Change

You modify the shader:

```hlsl
// ORIGINAL
cbuffer MaterialParams : register(b0) {
    float Roughness;
    float Metallic;
    Texture2D DiffuseMap;
    Texture2D NormalMap;
};

// MODIFIED: Added a texture
cbuffer MaterialParams : register(b0) {
    float Roughness;
    float Metallic;
    Texture2D DiffuseMap;
    Texture2D NormalMap;
    Texture2D AmbientOcclusion;  // ← NEW
};
```

**Problem:** C++ code still expects t2 to be NormalMap, but now it's AmbientOcclusion.

**Solution:** Manually update C++ slot assignments, recompile entire application.

### AFTER: Automatic Handling

```cpp
// shader_resources/pbr_material.h
BEGIN_SHADER_PARAMETER_STRUCT(FPBRMaterialParams)
    SHADER_PARAMETER(float, Roughness)
    SHADER_PARAMETER(float, Metallic)
    SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseMap)
    SHADER_PARAMETER_TEXTURE(Texture2D, NormalMap)
    SHADER_PARAMETER_TEXTURE(Texture2D, AmbientOcclusion)  // ← NEW
END_SHADER_PARAMETER_STRUCT()

// renderer/material_system.cpp
void ApplyPBRMaterial(
    RHICommandList& cmd_list,
    const PBRMaterial& material)
{
    FPBRMaterialParams params;
    params.Roughness = material.roughness;
    params.Metallic = material.metallic;
    params.DiffuseMap = material.diffuse_map->GetRHI();
    params.NormalMap = material.normal_map->GetRHI();
    params.AmbientOcclusion = material.ao_map->GetRHI();  // ← NEW

    // Metadata automatically reflects the new texture
    SetShaderParameters(cmd_list, material.shader, params);
}
```

**When shader recompiles:**
1. DXC compiler reads the new HLSL
2. Shader reflection says: t2 = AmbientOcclusion, t1 = NormalMap, t0 = DiffuseMap
3. Metadata updates to match reflection
4. C++ code **still works** — SetShaderParameters() uses metadata
5. **No C++ recompile needed** (unless logic changes)

---

## Scenario: Adding a New Sampler

### BEFORE: Manual Slot Management

```cpp
// Before: 2 samplers
struct CB {
    float4 color;
    // ... 8 samplers hardcoded at specific slots
};

cmd_list->SetGraphicsRootDescriptorTable(2, sampler_heap);
// Sampler slots hardcoded:
//   s0 = LinearSampler
//   s1 = PointSampler
//   s2 = ComparisonSampler
//   ...
//   s7 = NewSampler  ← ADD HERE

// After: Need to update every shader that uses this slot
// 3 shaders × 5 different branches (VS, PS, CS, etc.) = 15 manual updates
```

### AFTER: Self-Updating

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FMaterialParams)
    // ... other parameters
    SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
    SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
    SHADER_PARAMETER_SAMPLER(SamplerState, ComparisonSampler)
    SHADER_PARAMETER_SAMPLER(SamplerState, NewSampler)  // ← ADD HERE
END_SHADER_PARAMETER_STRUCT()

// In shader code:
params.NewSampler = GetNewSamplerState();

// SetShaderParameters() reads metadata and binds correctly
SetShaderParameters(cmd_list, shader, params);
```

**Metadata automatically:**
- Detects new sampler
- Assigns next available slot
- Binds to correct descriptor heap offset

---

## Scenario: Struct Alignment (The Footgun)

### BEFORE: Manual, Error-Prone

```cpp
// WRONG: Misaligned struct
struct CB_Wrong {
    float a;       // offset 0, size 4
    float3 b;      // offset 4, size 12 ← UNALIGNED! GPU needs 16-byte boundaries
    float4 c;      // offset 16, size 16
};  // size = 32, but GPU interprets b as garbage

// RIGHT: Manually padded
struct CB_Right {
    float a;       // offset 0
    float _pad0[3];// offset 4 (manual padding!)
    float3 b;      // offset 16
    float _pad1;   // offset 28
    float4 c;      // offset 32
};  // size = 48, correct layout

// But who remembers to add padding? Shader breaks silently.
```

### AFTER: Automatic Alignment

```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FAutoAlignedParams)
    SHADER_PARAMETER(float, A)
    SHADER_PARAMETER(float3, B)
    SHADER_PARAMETER(float4, C)
END_SHADER_PARAMETER_STRUCT()

// Metadata system:
// - Tracks member offsets
// - Validates 16-byte CB alignment
// - Compile-time error if misaligned
// - HLSL generated with correct layout

// Result: GPU sees correctly aligned data, zero manual padding
```

The macro system enforces:
```cpp
static_assert((offsetof(Struct, Member) % Alignment) == 0,
    "Member is misaligned");
```

---

## Code Size Comparison

### BEFORE: Manual Approach

**File: deferred_lighting.cpp** (Current)
```cpp
// ~200 lines of manual CB setup + texture binding
struct CB {
    // ... 15 fields with manual padding calculations
};

void Render() {
    CB cb_data;
    cb_data.field1 = ...;
    // ... 20 assignments

    ID3D12Resource* cb = Allocate(...);
    memcpy(cb_data, ...);

    cmd_list->SetGraphicsRootConstantBufferView(0, ...);
    cmd_list->SetGraphicsRootDescriptorTable(1, ...);  // t0-t15
    cmd_list->SetGraphicsRootDescriptorTable(2, ...);  // s0-s7
    cmd_list->SetGraphicsRootDescriptorTable(3, ...);  // u0-u3
}
```

### AFTER: Metadata Approach

**File: deferred_lighting.h** (New)
```cpp
BEGIN_SHADER_PARAMETER_STRUCT(FDeferredLightingParams)
    SHADER_PARAMETER(float4, Color)
    // ... 14 more lines
END_SHADER_PARAMETER_STRUCT()
```

**File: deferred_lighting.cpp** (New)
```cpp
void Render() {
    FDeferredLightingParams params;
    params.field1 = ...;
    // ... 15 assignments

    SetShaderParameters(cmd_list, shader, params);
}
```

**Result:**
- Old: 200 lines + shader reflection mismatches
- New: 50 lines + guaranteed correctness

---

## DX11 vs DX12 Comparison

### BEFORE: Fork Logic in Every Render Pass

```cpp
void RenderDeferred(...) {
#ifdef USE_DX12
    // DX12: Descriptor tables
    ID3D12DescriptorHeap* heap = AllocateHeap(...);
    device->CopyDescriptorsSimple(...);
    cmd_list->SetGraphicsRootDescriptorTable(1, heap_handle);

#elif USE_DX11
    // DX11: Discrete API calls
    ID3D11ShaderResourceView* srvs[] = { gbuffer.diffuse, gbuffer.normal, ... };
    ctx->PSSetShaderResources(0, ARRAYSIZE(srvs), srvs);

    ID3D11SamplerState* samplers[] = { linear_sampler, point_sampler };
    ctx->PSSetSamplers(0, ARRAYSIZE(samplers), samplers);
#endif

    // Actual rendering code duplicated per platform
}

// Every pass has this boilerplate × 20+ passes = 200+ lines of platform-specific code
```

### AFTER: Single Code Path

```cpp
void RenderDeferred(...) {
    FDeferredLightingParams params;
    params.DiffuseMap = gbuffer.diffuse;
    params.NormalMap = gbuffer.normal;
    // ... etc

    // SetShaderParameters() handles DX11 vs DX12 internally
    SetShaderParameters(cmd_list, shader, params);

    // Actual rendering code — platform agnostic
}
```

**Implementation (platform-specific, hidden):**

```cpp
// File: rhi/dx12/shader_binding.cpp
void SetShaderParameters_DX12(...) {
    // Descriptor table logic
}

// File: rhi/dx11/shader_binding.cpp
void SetShaderParameters_DX11(...) {
    // PSSetShaderResources logic
}

// File: rhi/shader_binding.cpp (platform-agnostic dispatcher)
template <typename T>
void SetShaderParameters(RHICommandList& cmd, const T& params) {
    if constexpr (RHI_BACKEND == RHI_DX12) {
        SetShaderParameters_DX12(cmd, params);
    } else {
        SetShaderParameters_DX11(cmd, params);
    }
}
```

**Result:** One platform-agnostic render pass, platform-specific binding logic isolated.

---

## Testing

### BEFORE: Manual Validation

```cpp
TEST(DeferredLighting, CheckParameters) {
    // Manually verify struct layout matches HLSL
    // ...
    // But how do you verify it matches HLSL at compile time?
    // Answer: You don't. It's a runtime trap.
}
```

### AFTER: Compile-Time Verification

```cpp
TEST(ShaderParameterBinding, MetadataGeneration) {
    const auto* meta = FDeferredLightingParams::GetMetadata();

    // Verify metadata matches struct definition
    EXPECT_EQ(meta->struct_name, "FDeferredLightingParams");
    EXPECT_EQ(meta->cb_members.size(), 3);  // Roughness, Metallic, Color
    EXPECT_EQ(meta->resource_members.size(), 4);  // DiffuseMap, NormalMap, AO, Shadow

    // Verify alignment
    u32 cb_size = meta->GetCBufferSizeBytes();
    EXPECT_EQ(cb_size % 16, 0);  // Must be multiple of 16

    // Verify HLSL generation
    String hlsl = GenerateHLSLCBufferDeclaration(meta);
    EXPECT_TRUE(hlsl.Contains("float Roughness"));
    EXPECT_TRUE(hlsl.Contains("Texture2D DiffuseMap"));
}

TEST(ShaderParameterBinding, HotReload) {
    // Load original shader
    auto shader_v1 = CompileShader("deferred.hlsl", "v1_metadata.h");

    // Recompile with new parameter
    auto shader_v2 = CompileShader("deferred.hlsl", "v2_metadata.h");

    // Verify bindings still work
    SetShaderParameters(cmd_list, shader_v2, params_v2);  // ✓ No crash
}
```

---

## Summary Table

| Aspect | BEFORE (Manual) | AFTER (Metadata) |
|--------|-----------------|------------------|
| **Parameter Definition** | Spread across CB struct + reflection | Single struct with macros |
| **Binding Code** | 5-10 manual RHI calls | 1 `SetShaderParameters()` call |
| **Manual Padding** | Required, error-prone | Automatic, validated |
| **Slot Assignment** | Hardcoded in C++ | Metadata-driven |
| **Shader Changes** | Requires C++ updates | Handled by metadata |
| **Hot-Reload** | Breaks easily | Safe, automatic |
| **DX11/DX12 Support** | Separate code paths | Single code path |
| **Lines of Code** | ~200 per pass | ~50 per pass |
| **Type Safety** | None | Compile-time checks |
| **Maintainability** | Low | High |
| **Performance** | Same | Same |
| **Memory Overhead** | Low | Low (static metadata) |

---

## Conclusion

The metadata-driven approach trades:
- **Small compile-time cost** (macro expansion, metadata generation)
- **Small runtime CPU cost** (metadata iteration O(n) where n = parameters, typically 5-20)

For:
- **No manual padding errors**
- **Type-safe parameter binding**
- **Easy shader hot-reload**
- **DX11/DX12 transparency**
- **2-4x less boilerplate**

**Recommended:** Implement this system for UzEngine. Cost-benefit ratio is extremely high.

