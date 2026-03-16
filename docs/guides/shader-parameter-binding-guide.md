<!-- doc-meta
title: Shader Parameter Binding Implementation Guide
category: Rendering
tags: [Shader Parameters, Uniform Buffers, Implementation, C++]
---

# Shader Parameter Binding Implementation Guide for UzEngine

This guide shows how to implement UE5's metadata-driven shader parameter binding system in UzEngine, adapted for C++23 and modern DX12.

---

## Problem Statement

**Current approach (bad):**
```cpp
// 5 separate RHI calls
cmd_list->SetConstantBuffers(cb_data);
cmd_list->SetShaderTexture(0, diffuse_map);
cmd_list->SetShaderTexture(1, normal_map);
cmd_list->SetSampler(0, linear_sampler);
cmd_list->SetUAV(0, output_buffer);

// Shader hot-reload breaks everything if you change parameter order
// DX11 and DX12 require completely different binding code
```

**Better approach (UE5 pattern):**
```cpp
FMyMaterialParams params;
params.DiffuseMap = diffuse_map;
params.NormalMap = normal_map;
params.Sampler = linear_sampler;
params.OutputBuffer = output_buffer;

// ONE call, metadata handles everything
SetShaderParameters(cmd_list, shader, params);
```

---

## Implementation Steps

### Step 1: Define Metadata Structure

**File: `src/engine/renderer/core/shader_parameters.h`**

```cpp
#pragma once

#include "core/types.h"
#include "core/containers.h"

namespace uz::render {

/// Parameter type classification.
enum class ParameterType : u8 {
    /// Constant buffer data (float, vector, matrix)
    CBuffer,
    /// Texture SRV
    Texture,
    /// Texture UAV
    TextureUAV,
    /// Buffer SRV
    BufferSRV,
    /// Buffer UAV
    BufferUAV,
    /// Sampler state
    Sampler,
    /// Nested struct (CBV only)
    NestedStruct,
};

/// Single parameter member metadata.
struct ParameterMember {
    /// C++ member name ("DiffuseMap", "Sampler", etc.)
    const char* name = nullptr;

    /// HLSL type annotation ("Texture2D", "SamplerState", "float4", etc.)
    const char* shader_type = nullptr;

    /// Byte offset in the C++ struct
    u32 offset = 0;

    /// Size in bytes for CB data, 0 for resources
    u32 cb_size = 0;

    /// Parameter classification
    ParameterType type = ParameterType::CBuffer;

    /// HLSL dimensions (rows/cols for matrices)
    u8 rows = 1, cols = 1;
};

/// Complete reflection metadata for a shader parameter struct.
struct ParameterStructMetadata {
    /// Struct name ("FMaterialParams", etc.)
    const char* struct_name = nullptr;

    /// Total size of C++ struct in bytes
    u32 struct_size = 0;

    /// All members (mixed CB + resource)
    TArray<ParameterMember> members;

    /// Cached: only CB members (scalar/vector/matrix)
    TArray<const ParameterMember*> cb_members;

    /// Cached: only resource members (texture/sampler/etc.)
    TArray<const ParameterMember*> resource_members;

    /// Compute CB size from CB members
    u32 GetCBufferSizeBytes() const {
        u32 total = 0;
        for (auto* mem : cb_members) {
            total = std::max(total, mem->offset + mem->cb_size);
        }
        return (total + 15) & ~15;  // Round to 16 bytes (GPU requirement)
    }
};

}  // namespace uz::render
```

### Step 2: Macro System for Struct Definition

**File: `src/engine/renderer/core/shader_parameter_macros.h`**

```cpp
#pragma once

#include "shader_parameters.h"

namespace uz::render {

/// Helper: Register metadata during static initialization
class ParameterMetadataRegistrar {
public:
    explicit ParameterMetadataRegistrar(const ParameterStructMetadata* metadata) {
        GetRegistry().Add(metadata);
    }

    static TArray<const ParameterStructMetadata*>& GetRegistry();
};

/// Helper: Collect members during struct definition
class ParameterMemberCollector {
private:
    TArray<ParameterMember> members_;

public:
    void AddCBuffer(const char* name, const char* shader_type, u32 offset, u32 size) {
        members_.Add({name, shader_type, offset, size, ParameterType::CBuffer, 1, 1});
    }

    void AddTexture(const char* name, const char* shader_type, u32 offset) {
        members_.Add({name, shader_type, offset, 0, ParameterType::Texture, 0, 0});
    }

    void AddSampler(const char* name, u32 offset) {
        members_.Add({name, "SamplerState", offset, 0, ParameterType::Sampler, 0, 0});
    }

    TArray<ParameterMember> Finalize() { return std::move(members_); }
};

}  // namespace uz::render

// ============================================================================
// PUBLIC MACROS for shader author
// ============================================================================

/// Begin a shader parameter struct declaration.
///
/// Example:
///   BEGIN_SHADER_PARAMETER_STRUCT(FMaterialParams)
///     SHADER_PARAMETER(float, Roughness)
///     SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseMap)
///   END_SHADER_PARAMETER_STRUCT()
#define BEGIN_SHADER_PARAMETER_STRUCT(StructName) \
    struct StructName { \
        static constexpr const char* kMetadataName = #StructName; \
        /* Members declared here */ \
        \
        static const uz::render::ParameterStructMetadata* GetMetadata(); \
        static uz::render::ParameterMemberCollector _StaticMemberCollector;

/// End struct definition and register metadata.
#define END_SHADER_PARAMETER_STRUCT() \
    }; \
    \
    uz::render::ParameterMemberCollector StructName::_StaticMemberCollector; \
    inline const uz::render::ParameterStructMetadata* StructName::GetMetadata() { \
        static uz::render::ParameterStructMetadata metadata = _BuildMetadata(); \
        return &metadata; \
    } \
    static uz::render::ParameterMetadataRegistrar StructName##_Registrar(StructName::GetMetadata());

// Define this in .cpp file to register metadata
#define IMPLEMENT_SHADER_PARAMETER_STRUCT(StructName) \
    static uz::render::ParameterStructMetadata StructName##_Metadata = \
        _BuildMetadata(); \
    uz::render::ParameterMetadataRegistrar StructName##_Registrar(&StructName##_Metadata);

/// Add a constant buffer parameter (float, vector, matrix, etc).
///
/// Example:
///   SHADER_PARAMETER(float4, ViewProj)
///   SHADER_PARAMETER(FMatrix44f, WorldToClip)
#define SHADER_PARAMETER(Type, Name) \
    Type Name = {}; \
    struct _Param_##Name { \
        static void Register(uz::render::ParameterMemberCollector& c) { \
            c.AddCBuffer(#Name, #Type, offsetof(StructName, Name), sizeof(Type)); \
        } \
    };

/// Add a texture parameter (SRV).
///
/// Example:
///   SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseMap)
///   SHADER_PARAMETER_TEXTURE(TextureCube, EnvMap)
#define SHADER_PARAMETER_TEXTURE(ShaderType, Name) \
    ID3D12Resource* Name = nullptr; \
    struct _Param_##Name { \
        static void Register(uz::render::ParameterMemberCollector& c) { \
            c.AddTexture(#Name, #ShaderType, offsetof(StructName, Name)); \
        } \
    };

/// Add a sampler parameter.
///
/// Example:
///   SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)
#define SHADER_PARAMETER_SAMPLER(ShaderType, Name) \
    D3D12_SAMPLER_DESC* Name = nullptr; \
    struct _Param_##Name { \
        static void Register(uz::render::ParameterMemberCollector& c) { \
            c.AddSampler(#Name, offsetof(StructName, Name)); \
        } \
    };
```

### Step 3: Parameter Binding Implementation

**File: `src/engine/renderer/core/shader_parameter_binding.h`**

```cpp
#pragma once

#include "shader_parameters.h"
#include "rhi/rhi.h"

namespace uz::render {

/// Bind all shader parameters in one call.
///
/// This function:
/// 1. Extracts CB data and uploads to GPU
/// 2. Collects resource pointers and binds via descriptor heap
/// 3. Works identically on DX11 and DX12 (implementation varies)
///
/// @param cmd_list      Active command list
/// @param shader        Compiled shader program
/// @param params        Parameter struct (any type with GetMetadata())
/// @param metadata      Pre-computed metadata (optional, auto-fetched if nullptr)
template <typename TParams>
void SetShaderParameters(
    RHICommandList& cmd_list,
    const RHIShader& shader,
    const TParams& params,
    const ParameterStructMetadata* metadata = nullptr)
{
    if (!metadata) {
        metadata = TParams::GetMetadata();
    }

    // Step 1: Upload constant buffer data
    if (metadata->GetCBufferSizeBytes() > 0) {
        UploadCBufferData(cmd_list, shader, params, metadata);
    }

    // Step 2: Bind resources
    BindResourceParameters(cmd_list, shader, params, metadata);
}

/// Implementation: Upload CB data to GPU
inline void UploadCBufferData(
    RHICommandList& cmd_list,
    const RHIShader& shader,
    const void* params_data,
    const ParameterStructMetadata* metadata)
{
    u32 cb_size = metadata->GetCBufferSizeBytes();
    if (cb_size == 0) return;

    // Allocate temp upload buffer (or use persistent ring buffer)
    void* gpu_ptr = AllocateUploadMemory(cb_size);
    memcpy(gpu_ptr, params_data, cb_size);

    // Bind to root parameter 0 (reserved for CB)
    cmd_list.SetGraphicsRootConstantBufferView(0, GetGPUVirtualAddress(gpu_ptr));
}

/// Implementation: Bind texture, sampler, and buffer resources
inline void BindResourceParameters(
    RHICommandList& cmd_list,
    const RHIShader& shader,
    const void* params_data,
    const ParameterStructMetadata* metadata)
{
    if (metadata->resource_members.empty()) {
        return;
    }

    // Allocate descriptor heap for resources
    ID3D12DescriptorHeap* heap = AllocateDescriptorHeap(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
        metadata->resource_members.size());

    u32 heap_offset = 0;

    for (const auto* member : metadata->resource_members) {
        void* member_ptr = (u8*)params_data + member->offset;

        switch (member->type) {
        case ParameterType::Texture: {
            ID3D12Resource* tex = *(ID3D12Resource**)member_ptr;
            if (tex) {
                ID3D12ShaderResourceView* srv = GetOrCreateSRV(tex);
                CopyDescriptor(heap, heap_offset, srv);
            }
            heap_offset++;
            break;
        }
        case ParameterType::TextureUAV: {
            ID3D12Resource* tex = *(ID3D12Resource**)member_ptr;
            if (tex) {
                ID3D12UnorderedAccessView* uav = GetOrCreateUAV(tex);
                CopyDescriptor(heap, heap_offset, uav);
            }
            heap_offset++;
            break;
        }
        case ParameterType::Sampler: {
            D3D12_SAMPLER_DESC* sampler = *(D3D12_SAMPLER_DESC**)member_ptr;
            if (sampler) {
                ID3D12SamplerHeap* sampler_heap = GetSamplerHeap();
                u32 sampler_index = RegisterSampler(sampler);
                // Samplers are in separate heap, tracked separately
            }
            break;
        }
        // ... handle BufferSRV, BufferUAV similarly
        }
    }

    // Bind descriptor heap to root parameter 1 (reserved for resources)
    cmd_list.SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
}
```

### Step 4: HLSL Code Generation

**File: `src/engine/renderer/core/shader_compiler.h`**

```cpp
#pragma once

#include "shader_parameters.h"
#include "core/string.h"

namespace uz::render {

/// Generate HLSL cbuffer declaration from metadata.
///
/// Output example:
///   cbuffer MaterialParams : register(b0) {
///       float Roughness;
///       float4 BaseColor;
///       Texture2D DiffuseMap;
///       SamplerState LinearSampler;
///   };
inline String GenerateHLSLCBufferDeclaration(
    const ParameterStructMetadata* metadata)
{
    String result;
    result.AppendFormat("cbuffer %s : register(b0) {{\n", metadata->struct_name);

    for (const auto* member : metadata->members) {
        result.AppendFormat("    %s %s;\n", member->shader_type, member->name);
    }

    result.Append("};\n");
    return result;
}

/// Inject parameter struct declaration into shader source.
///
/// Called before DXC compilation to ensure parameter names match C++.
inline String InjectedParametersIntoShaderSource(
    const String& original_source,
    const ParameterStructMetadata* metadata)
{
    String cbuffer_decl = GenerateHLSLCBufferDeclaration(metadata);

    // Insert right after any #includes, before cbuffer definitions
    String result = original_source;
    size_t insert_pos = 0;

    // Find last #include line
    size_t last_include = result.find_last_of("#include");
    if (last_include != String::npos) {
        insert_pos = result.find('\n', last_include) + 1;
    }

    result.insert(insert_pos, "\n// Auto-generated parameter struct\n");
    result.insert(insert_pos, cbuffer_decl);

    return result;
}

}  // namespace uz::render
```

### Step 5: Usage Example

**File: `src/engine/renderer/shaders/deferred_lighting.h`**

```cpp
#pragma once

#include "engine/renderer/core/shader_parameter_macros.h"

namespace uz::render::shaders {

/// Parameters for deferred lighting pass.
BEGIN_SHADER_PARAMETER_STRUCT(FDeferredLightingParams)
    /// Light world position
    SHADER_PARAMETER(float3, LightPosition)

    /// Light color and intensity
    SHADER_PARAMETER(float4, LightColorAndIntensity)

    /// Attenuation radius
    SHADER_PARAMETER(float, AttenuationRadius)

    /// G-Buffer: diffuse + metallic
    SHADER_PARAMETER_TEXTURE(Texture2D, GBufferDiffuse)

    /// G-Buffer: normals
    SHADER_PARAMETER_TEXTURE(Texture2D, GBufferNormal)

    /// G-Buffer: depth
    SHADER_PARAMETER_TEXTURE(Texture2D, GBufferDepth)

    /// Shadow map
    SHADER_PARAMETER_TEXTURE(Texture2D, ShadowMap)

    /// Linear sampler for reading
    SHADER_PARAMETER_SAMPLER(SamplerState, LinearSampler)

    /// Point sampler for shadow compare
    SHADER_PARAMETER_SAMPLER(SamplerState, PointSampler)
END_SHADER_PARAMETER_STRUCT()

}  // namespace uz::render::shaders
```

**File: `src/engine/renderer/shaders/deferred_lighting.cpp`** (Render pass using parameters)

```cpp
#include "deferred_lighting.h"
#include "engine/renderer/core/shader_parameter_binding.h"

namespace uz::render {

void RenderDeferredLighting(
    RHICommandList& cmd_list,
    const LightInfo& light,
    const GBufferResources& gbuffer,
    const RHITexture& shadow_map)
{
    // Create parameter struct
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

    // One call binds everything
    SetShaderParameters(cmd_list, GetDeferredLightingShader(), params);

    // Render
    cmd_list.DrawFullscreenTriangle();
}

}  // namespace uz::render
```

---

## DX11 vs DX12 Implementation Differences

### DX12 Path (Descriptor Tables)

```cpp
void BindResourceParameters_DX12(
    ID3D12GraphicsCommandList* cmd_list,
    const void* params_data,
    const ParameterStructMetadata* metadata)
{
    // Allocate heap for all resources
    ID3D12DescriptorHeap* heap = device->CreateDescriptorHeap(...);

    // Copy each resource into heap
    for (auto* member : metadata->resource_members) {
        ID3D12Resource* res = *(ID3D12Resource**)((u8*)params_data + member->offset);
        ID3D12ShaderResourceView* srv = GetSRV(res);
        device->CopyDescriptorsSimple(1, heap_handle, srv_handle, ...);
    }

    // Single descriptor table binding
    cmd_list->SetGraphicsRootDescriptorTable(1, heap->GetGPUDescriptorHandleForHeapStart());
}
```

### DX11 Path (Discrete Bindings)

```cpp
void BindResourceParameters_DX11(
    ID3D11DeviceContext* ctx,
    const void* params_data,
    const ParameterStructMetadata* metadata)
{
    // Slot assignments come from shader reflection
    u32 t_slot = 0, u_slot = 0, s_slot = 0;

    for (auto* member : metadata->resource_members) {
        if (member->type == ParameterType::Texture) {
            ID3D11Resource* res = *(ID3D11Resource**)((u8*)params_data + member->offset);
            ID3D11ShaderResourceView* srv = GetSRV(res);
            ctx->PSSetShaderResources(t_slot++, 1, &srv);
        }
        else if (member->type == ParameterType::Sampler) {
            ID3D11SamplerState* sampler = GetD3D11Sampler(...);
            ctx->PSSetSamplers(s_slot++, 1, &sampler);
        }
    }
}
```

**Key insight:** Metadata separates the *intent* (bind a texture) from the *mechanism* (DX12 descriptor table vs. DX11 PSSetShaderResources).

---

## Testing

**File: `src/engine/renderer/tests/test_shader_parameters.cpp`**

```cpp
#include <gtest/gtest.h>
#include "engine/renderer/core/shader_parameter_macros.h"

namespace uz::render {

// Test struct
BEGIN_SHADER_PARAMETER_STRUCT(FTestParams)
    SHADER_PARAMETER(float, Scalar)
    SHADER_PARAMETER(float4, Vector)
    SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseMap)
END_SHADER_PARAMETER_STRUCT()

TEST(ShaderParametersTest, MetadataGeneration) {
    const auto* meta = FTestParams::GetMetadata();

    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(meta->struct_name, "FTestParams");
    EXPECT_EQ(meta->members.size(), 3);

    // Verify CB members
    EXPECT_EQ(meta->cb_members.size(), 2);  // Scalar + Vector
    EXPECT_EQ(meta->resource_members.size(), 1);  // DiffuseMap
}

TEST(ShaderParametersTest, CBufferSize) {
    const auto* meta = FTestParams::GetMetadata();
    u32 cb_size = meta->GetCBufferSizeBytes();

    // Scalar (4) + padding (12) + Vector (16) = 32 bytes
    EXPECT_EQ(cb_size, 32);
}

TEST(ShaderParametersTest, HLSLGeneration) {
    const auto* meta = FTestParams::GetMetadata();
    String hlsl = GenerateHLSLCBufferDeclaration(meta);

    EXPECT_TRUE(hlsl.Contains("cbuffer FTestParams"));
    EXPECT_TRUE(hlsl.Contains("float Scalar"));
    EXPECT_TRUE(hlsl.Contains("float4 Vector"));
    EXPECT_TRUE(hlsl.Contains("Texture2D DiffuseMap"));
}

}  // namespace uz::render
```

---

## Migration Checklist

- [ ] Implement `ParameterStructMetadata` struct
- [ ] Implement macro system (`BEGIN_SHADER_PARAMETER_STRUCT`, etc.)
- [ ] Implement `SetShaderParameters()` template
- [ ] Implement DX12 descriptor table binding
- [ ] Implement DX11 discrete binding (if DX11 support needed)
- [ ] Implement `GenerateHLSLCBufferDeclaration()`
- [ ] Integrate into shader compiler (inject before DXC)
- [ ] Migrate existing shaders to new parameter struct syntax
- [ ] Test CB data upload correctness
- [ ] Test resource binding correctness
- [ ] Verify shader hot-reload works with new system

---

## Performance Notes

- **CPU overhead:** Metadata iteration is O(num_members), typically 5-20 members per shader
- **GPU overhead:** None — same descriptor binding as before
- **Memory:** Metadata is static, allocated once at startup
- **Hot-reload:** Regenerated HLSL ensures parameter order always matches C++

---

## See Also

- `docs/analysis/rendering/ue5-uniform-buffer-shader-binding-system.md` — Deep UE5 architecture analysis
- `docs/specs/shader-system.md` — Shader system specification
- `docs/specs/renderer.md` — Renderer pass infrastructure

