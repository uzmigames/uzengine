<!-- doc-meta
title: UE5 Uniform Buffer & Shader Binding System
category: Rendering Architecture
tags: [RHI, Shader Parameters, Uniform Buffers, Resource Binding, DX11, DX12]
---

# UE5 Uniform Buffer & Shader Binding System - Deep Analysis

## Executive Summary

UE5's shader parameter system separates **constant data** (CBVs) from **resource references** (SRVs/UAVs/Samplers) at the C++ struct definition level, NOT at the HLSL struct level. This allows:

1. **Single C++ struct** → **Both C++ layout + HLSL code** without textures inside the actual CBV
2. **Resource binding happens separately** via RHI calls, not through struct packing
3. **DX11/DX12 transparency** — same C++ code works on both via metadata-driven binding

This document explains the pattern, how textures are NOT packed into CBVs, and how to adapt this pattern for UzEngine.

---

## Pattern 1: The C++ Struct Illusion

### What UE5 Does

When you write:

```cpp
BEGIN_UNIFORM_BUFFER_STRUCT(FMyParams, RENDERCORE_API)
    SHADER_PARAMETER(float, MyScalar)
    SHADER_PARAMETER(FVector4f, MyColor)
    SHADER_PARAMETER_TEXTURE(TextureCube, SkyLightCubemap)  // NOT in CBV!
    SHADER_PARAMETER_SAMPLER(SamplerState, MipSampler)      // NOT in CBV!
END_UNIFORM_BUFFER_STRUCT()
```

**The C++ struct layout is:**
- `float MyScalar` (4 bytes) → IN the actual CBV buffer
- `FVector4f MyColor` (16 bytes) → IN the actual CBV buffer
- `FRHITexture* SkyLightCubemap` (8 bytes, 64-bit pointer) → NOT in CBV, stored in struct for metadata
- `FRHISamplerState* MipSampler` (8 bytes, pointer) → NOT in CBV, stored in struct for metadata

### Key Insight: Metadata vs. Data

The macros **encode metadata** about resource references:

```cpp
#define SHADER_PARAMETER_TEXTURE(ShaderType,MemberName) \
    INTERNAL_SHADER_PARAMETER_EXPLICIT(
        UBMT_TEXTURE,  // ← Base type enum tells RHI "this is a resource"
        TShaderResourceParameterTypeInfo<FRHITexture*>,
        FRHITexture*,
        MemberName,
        , = nullptr,
        EShaderPrecisionModifier::Float,
        TEXT(#ShaderType),  // ← "TextureCube" — passed to shader compiler
        false)
```

**Critical fields:**
- `UBMT_TEXTURE` = `EUniformBufferBaseType::UBMT_TEXTURE` — tells RHI this is a resource, not CB data
- `bIsStoredInConstantBuffer = false` — **this flag prevents it from being in the actual CBV memory**
- `TEXT(#ShaderType)` → HLSL type annotation for shader compiler (e.g., `TextureCube`)

### The Struct Metadata: `FShaderParametersMetadata`

After compilation, UE5 builds metadata that lists:

```cpp
struct FShaderParametersMetadata::FMember {
    FName MemberName;           // "SkyLightCubemap"
    const TCHAR* ShaderType;    // "TextureCube"
    uint16 Offset;              // Byte offset in struct (for actual CB data)
    uint16 NumBytes;            // 0 for resources (indicates "not in CB")
    EUniformBufferBaseType BaseType;  // UBMT_TEXTURE, UBMT_SAMPLER, etc.
    uint32 NumRows, NumColumns, NumElements;
    const FShaderParametersMetadata* StructMetadata;  // For nested structs
};
```

**Two separate arrays in metadata:**
- `BufferParameters` — only scalar/vector/matrix members → packed into CBV
- `ResourceParameters` — textures, SRVs, UAVs, samplers → NOT packed, bound separately

See: `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterMetadata.h`

---

## Pattern 2: How Resources Bind Separately on DX11 vs DX12

### DX11: Discrete API Calls

```
CBV Data:    [MyScalar=1.5, MyColor=1,0,0,1] → RHICreateUniformBuffer()
SRV Binding:  PSSetShaderResources(0, 1, &SkyLightCubemap_SRV)  // t0 in shader
Sampler:      PSSetSamplers(0, 1, &MipSampler)                  // s0 in shader
```

The **shader compiler** maps:
- `MyScalar` → CB index 0, offset 0
- `MyColor` → CB index 0, offset 16
- `SkyLightCubemap` → SRV slot t0 (auto-assigned by compiler based on reflection)
- `MipSampler` → Sampler slot s0

UE5 **doesn't manually assign slots** — it reads from shader reflection after compilation.

See: `FShaderParameterMap` (ShaderParameterMap.h, line ~170)

### DX12: Descriptor Tables

```
Root Signature:
  - RootParameter[0] = CBV (register b0) → Points to descriptor heap with CB
  - RootParameter[1] = DescriptorTable
      - SRV range: register t0, 16 descriptors
      - Sampler range: register s0, 8 descriptors

Descriptor Heap:
  [t0] = SkyLightCubemap SRV
  [s0] = MipSampler

SetGraphicsRootDescriptorTable(1, DescriptorTable);
```

Same **C++ struct** is used to:
1. **Upload CB data** (MyScalar, MyColor only) to GPU
2. **Extract resource pointers** and bind via SetGraphicsRootDescriptorTable

The metadata tells DX12 backend:
- Offset in struct → offset in CB upload
- BaseType = UBMT_TEXTURE → "grab this pointer and add to descriptor table"

See: `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Private\DynamicRHI.cpp` (SetShaderParameters implementation)

---

## Pattern 3: The HLSL Side — Texture Inside cbuffer Syntax

This is the magic: **HLSL allows syntax that LOOKS like textures are in cbuffer, but they're not.**

```hlsl
// This is VALID HLSL but NOT what you think it means:
cbuffer MyParams : register(b0)
{
    float MyScalar;
    float4 MyColor;
    TextureCube SkyLightCubemap;  // ← SYNTAX OK, but see shader IL
};

SamplerState MipSampler : register(s0);
```

**What the HLSL compiler actually produces:**

Constant Buffer (register b0):
```
[0:4]     = MyScalar (float)
[16:32]   = MyColor (float4)
           // TextureCube is NOT in buffer
```

Shader Resource Views:
```
t0 = SkyLightCubemap
s0 = MipSampler
```

The texture inside the `cbuffer` **is syntax sugar** for "declare a texture with SRV auto-mapping."

UE5 generates HLSL **exactly** like this via `CreateUniformBufferShaderDeclaration()`:

```cpp
// From ShaderParameters.cpp, UE::ShaderParameters namespace
FString CreateUniformBufferShaderDeclaration(
    const TCHAR* Name,
    const FShaderParametersMetadata& UniformBufferStruct,
    const FRHIUniformBufferShaderBindingLayout* UniformBufferSBLayout)
{
    FString Result;
    Result += FString::Printf(TEXT("cbuffer %s : register(b%d)\n{\n"), Name, BindingSlot);

    for (const auto& Member : UniformBufferStruct.GetMembers())
    {
        if (Member.GetBaseType() == UBMT_TEXTURE)
        {
            // Generate: TextureCube SkyLightCubemap;
            Result += FString::Printf(TEXT("\t%s %s;\n"),
                Member.GetShaderType(),   // "TextureCube"
                *Member.GetName());       // "SkyLightCubemap"
        }
        else if (Member.GetBaseType() == UBMT_SAMPLER)
        {
            // Generate: SamplerState MipSampler;
            Result += FString::Printf(TEXT("\t%s %s;\n"),
                Member.GetShaderType(),
                *Member.GetName());
        }
        else
        {
            // Generate: float MyScalar;
            Result += FString::Printf(TEXT("\t%s %s;\n"),
                GetHLSLTypeName(Member),
                *Member.GetName());
        }
    }

    Result += TEXT("};\n");
    return Result;
}
```

**See**: `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameters.h` line 41

The shader compiler's reflection output tells UE5:
- `MyScalar` → CB offset 0, size 4
- `MyColor` → CB offset 16, size 16
- `SkyLightCubemap` → t0 (SRV slot)
- `MipSampler` → s0 (Sampler slot)

---

## Pattern 4: Metadata-Driven Binding

### `SetShaderParameters()` Flow

**C++ caller:**
```cpp
FMyParams Params;
Params.MyScalar = 1.5f;
Params.MyColor = FVector4f(1, 0, 0, 1);
Params.SkyLightCubemap = SkyLightTexture->GetRHI();
Params.MipSampler = SamplerState->GetRHI();

SetShaderParameters(RHICmdList, ComputeShader, Params);
```

**What `SetShaderParameters()` does:**

```cpp
// From ShaderParameterStruct.cpp
void SetShaderParameters(
    FRHIBatchedShaderParameters& BatchedParameters,
    const FShaderParameterBindings& Bindings,
    const FShaderParametersMetadata* ParametersMetadata,
    const void* ParametersData)
{
    // 1. Upload CB data (scalars/vectors only)
    for (const auto& Member : ParametersMetadata->GetMembers())
    {
        if (Member.GetBaseType() != UBMT_TEXTURE &&
            Member.GetBaseType() != UBMT_SAMPLER &&
            ...)  // Skip non-CB types
        {
            // This is float/vector/matrix data
            void* MemberPtr = (uint8*)ParametersData + Member.GetOffset();
            BatchedParameters.SetShaderParameter(
                BufferIndex, BaseIndex, Member.GetNumBytes(), MemberPtr);
        }
    }

    // 2. Bind resources
    for (const auto& Member : ParametersMetadata->GetMembers())
    {
        if (Member.GetBaseType() == UBMT_TEXTURE)
        {
            FRHITexture* TexturePtr = *(FRHITexture**)((uint8*)ParametersData + Member.GetOffset());
            BatchedParameters.SetShaderTexture(SlotIndex, TexturePtr);
        }
        else if (Member.GetBaseType() == UBMT_SAMPLER)
        {
            FRHISamplerState* SamplerPtr = *(FRHISamplerState**)(...);
            BatchedParameters.SetShaderSampler(SlotIndex, SamplerPtr);
        }
    }
}
```

**The RHI then:**
- **DX11:** PSSetShaderResources(), PSSetSamplers()
- **DX12:** Builds descriptor table offsets from metadata

**See**: `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterStruct.h` lines 247-294

---

## Pattern 5: Actual Source Files & Key Functions

### Header Files (Public API)

| File | Key Content |
|------|-------------|
| `ShaderParameterMacros.h` | `SHADER_PARAMETER()`, `BEGIN_UNIFORM_BUFFER_STRUCT()`, macro implementations |
| `ShaderParameterMetadata.h` | `FShaderParametersMetadata` struct, member list, reflection |
| `ShaderParameterStruct.h` | `SetShaderParameters()` template implementations |
| `ShaderParameters.h` | Metadata builder, `CreateUniformBufferShaderDeclaration()` |

### Implementation Files (Private)

| File | Key Content |
|------|-------------|
| `ShaderParameterStruct.cpp` | SetShaderParameters() dispatcher to RHI |
| `RHICommandList.cpp` | RHI-side implementation for DX11/DX12 |

### Code Paths

1. **Compile-time (offline):**
   - Macro generates struct layout + metadata
   - Shader compiler reflection reads struct from HLSL
   - Metadata merged with reflection

2. **Runtime (GPU upload):**
   - `SetShaderParameters()` reads metadata
   - Filters: only CB data → `RHICreateUniformBuffer()`
   - Resources → RHI backend handles binding

---

## UzEngine Adaptation Strategy

### Current State
UzEngine likely uses:
- Direct CB uploads via `ID3D12Resource` or DX11 equivalent
- Manual texture binding via `SetShaderResources()` equivalents
- No metadata-driven reflection

### Adaptation Pattern

#### Phase 1: Metadata System (P0 dependency)

Create a lightweight reflection system:

```cpp
// uz/core/shader/shader_parameters.h

enum class EShaderParameterType : uint8 {
    Float,
    Float2, Float3, Float4,
    Int32, Uint32,
    Matrix44,
    Texture,
    SRV,
    UAV,
    Sampler,
    NestedStruct,
};

struct ShaderParameterMember {
    const char* name;
    const char* shader_type;      // "float4", "Texture2D", etc.
    uint16_t offset;               // Byte offset in C++ struct
    uint16_t cb_size;              // 0 if resource (not in CB)
    EShaderParameterType type;
    uint8_t rows, cols, elements;
};

struct ShaderParameterMetadata {
    const char* struct_name;
    uint32_t struct_size;
    TArray<ShaderParameterMember> cb_members;      // Only scalars/vectors
    TArray<ShaderParameterMember> resource_members; // Textures/SRVs/UAVs
};
```

**Key insight:** Separate arrays for CB data vs. resources — exactly like UE5.

#### Phase 2: Macro System (inspired by UE5)

```cpp
// uz/core/shader/shader_parameters.h

#define BEGIN_SHADER_PARAMETER_STRUCT(StructName) \
    struct StructName { \
        static const ShaderParameterMetadata* GetMetadata(); \
        // Members will be added here by macros

#define SHADER_PARAMETER(Type, Name) \
    Type Name;  // CB data — stored directly in struct

#define SHADER_PARAMETER_TEXTURE(ShaderType, Name) \
    ID3D12Resource* Name = nullptr;  // Resource pointer stored, NOT in CB

#define END_SHADER_PARAMETER_STRUCT() \
    }; \
    // Metadata registration code

// Usage:
BEGIN_SHADER_PARAMETER_STRUCT(FMyParams)
    SHADER_PARAMETER(float4, ViewProj)
    SHADER_PARAMETER_TEXTURE(Texture2D, DiffuseMap)
END_SHADER_PARAMETER_STRUCT()
```

**Why this works:**
- C++ struct has BOTH CB data AND resource pointers
- Metadata separates them at runtime
- `SetShaderParameters()` reads metadata to:
  - Upload only CB members to GPU
  - Bind resource members via SetShaderResources()

#### Phase 3: SetShaderParameters() Implementation

```cpp
// uz/core/shader/shader_parameter_binder.h

void SetShaderParameters(
    uz::CommandList& cmd_list,
    const uz::Shader& shader,
    const void* params_data,
    const ShaderParameterMetadata* metadata)
{
    // Step 1: Upload CB data
    uint32_t cb_size = 0;
    for (const auto& member : metadata->cb_members) {
        cb_size = std::max(cb_size, member.offset + member.cb_size);
    }
    if (cb_size > 0) {
        ID3D12Resource* cb = CreateUploadBuffer(params_data, cb_size);
        cmd_list->SetGraphicsRootConstantBufferView(0, cb->GetGPUVirtualAddress());
    }

    // Step 2: Bind resources
    ID3D12DescriptorHeap* heap = AllocateDescriptorHeap(metadata->resource_members.size());
    uint32_t heap_index = 0;

    for (const auto& member : metadata->resource_members) {
        void* resource_ptr = (uint8_t*)params_data + member.offset;

        if (member.type == EShaderParameterType::Texture) {
            ID3D12Resource** tex_ptr = (ID3D12Resource**)resource_ptr;
            ID3D12ShaderResourceView* srv = CreateSRV(*tex_ptr);
            heap->SetDescriptor(heap_index++, srv);
        }
        // ... handle SRV, UAV, Sampler similarly
    }

    cmd_list->SetGraphicsRootDescriptorTable(1, heap->GetGPUHandle(0));
}
```

**Key differences from direct binding:**
- One `SetShaderParameters()` call instead of separate texture/sampler calls
- Metadata drives the binding — DX11 vs DX12 differences hidden in implementation
- Same C++ struct works on both APIs

#### Phase 4: HLSL Code Generation

For shader hot-reload, generate HLSL cbuffer declarations:

```cpp
// uz/render/shader_compiler.h

std::string GenerateShaderCbuffer(
    const char* cbuffer_name,
    const ShaderParameterMetadata* metadata)
{
    std::string result;
    result += "cbuffer " + std::string(cbuffer_name) + " : register(b0) {\n";

    for (const auto& member : metadata->cb_members) {
        // e.g., "    float4 ViewProj;"
        result += "    " + GetHLSLType(member) + " " + member.name + ";\n";
    }
    for (const auto& member : metadata->resource_members) {
        // e.g., "    Texture2D DiffuseMap;"
        result += "    " + std::string(member.shader_type) + " " + member.name + ";\n";
    }

    result += "};\n";
    return result;
}
```

Then inject this into shader source before DXC compilation.

---

## Answers to Original Questions

### Q1: How are textures packed into the uniform buffer?

**Answer: They aren't.** The C++ struct holds *pointers* to resources, not the resources themselves. UE5's macro system encodes metadata that says "this is a resource, not CB data." At runtime, `SetShaderParameters()` reads the metadata and:
- Uploads only CB data to GPU
- Binds resources separately via RHI calls

### Q2: How does the HLSL side access struct members?

**Answer: Both ways work:**

**DX11 (direct access):**
```hlsl
float MyScalar = MyParams.MyScalar;     // CB data
float4 Color = DiffuseMap.Sample(s0, uv); // SRV binding
```

**DX12 (descriptor tables):**
```hlsl
float MyScalar = g_Params.MyScalar;    // CB register b0
float4 Color = g_DiffuseMap.Sample(g_Sampler, uv);  // SRV/Sampler from heap
```

UE5 generates **identical HLSL syntax** for both — the difference is *where* textures and samplers come from at the RHI level.

### Q3: What's the DX11 translation?

**Answer:**
```cpp
// C++
Params.SkyLightCubemap = MyTexture;

// UE5 RHI (DX11 backend)
D3D11DeviceContext->PSSetShaderResources(0, 1, &MyTexture->GetD3D11SRV());
D3D11DeviceContext->PSSetSamplers(0, 1, &MipSampler->GetD3D11Sampler());

// Also:
RHICreateUniformBuffer(&Params, ... ) → ID3D11Buffer for CB data only
D3D11DeviceContext->PSSetConstantBuffers(0, 1, &ConstantBuffer);
```

Slot assignments (t0, s0, etc.) come from **shader reflection**, not manual C++.

### Q4: What's the DX12 translation?

**Answer:**
```cpp
// C++
Params.SkyLightCubemap = MyTexture;

// UE5 RHI (DX12 backend)
// 1. Create descriptor heap
ID3D12DescriptorHeap* heap = ...

// 2. Copy textures/samplers into heap
D3D12Device->CopyDescriptorsSimple(..., MyTexture->GetD3D12SRV(), ...);
D3D12Device->CopyDescriptorsSimple(..., MipSampler->GetD3D12Sampler(), ...);

// 3. Set descriptor table
CmdList->SetGraphicsRootDescriptorTable(1, HeapGPUHandle);

// 4. Upload CB data (CPU-visible, no copy needed if persistent)
UploadBuffer->Write(&Params, SizeOfCBData);
CmdList->SetGraphicsRootConstantBufferView(0, UploadBuffer->GetGPUAddr());
```

**Same C++ code**, different RHI implementation details.

---

## Key Takeaways for UzEngine

1. **Separate CB data from resources at the metadata level**, not the C++ struct level
2. **Use metadata to drive binding** — one `SetShaderParameters()` call instead of 5+ API calls
3. **HLSL generation** — synthesize cbuffer declarations from metadata before shader compilation
4. **DX11/DX12 transparency** — hide API differences in RHI implementation, not C++ calling code
5. **Compile-time vs. runtime**: Metadata is generated once at struct definition, used repeatedly at runtime

---

## Related UE5 Files (Full Paths)

**Public Headers:**
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterMacros.h` (1700+ lines)
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterMetadata.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameterStruct.h`
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Public\ShaderParameters.h`

**Implementation:**
- `W:\UE_5.6_Source\Engine\Source\Runtime\RenderCore\Private\ShaderParameterStruct.cpp`
- `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Private\RHICommandList.cpp` (SetShaderParameters RHI dispatch)

**Related Specs in UzEngine:**
- `docs/specs/shader-system.md` — shader compilation & hot-reload
- `docs/specs/renderer.md` — render passes using parameter structs
- `docs/architecture/rhi-abstraction.md` — RHI-level binding differences

---

**Analysis Date:** March 16, 2026
**UE5 Version:** 5.6 Source
**Document Version:** 1.0

