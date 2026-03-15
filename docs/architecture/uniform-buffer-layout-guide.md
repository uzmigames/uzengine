# Uniform Buffer Layout Guide for UzEngine

## Cross-Platform Compatibility: DX12 vs Vulkan

This guide explains how to structure uniform buffers (cbuffers) to work correctly on both DX12 and Vulkan without manual packing or offset calculations.

## The Core Issue

| Backend | Matrix Semantics | Alignment Rules | Default Layout |
|---------|------------------|-----------------|-----------------|
| DX12 | `row_major` (customizable) | HLSL packing rules | Row-major by default |
| Vulkan/SPIRV | Column-major (fixed) | std140 (or scalar block) | Column-major, 16-byte column alignment |

**Problem**: If you use `row_major float4x4` in a shader, DX12 interprets it row-wise, but Vulkan/SPIRV ignores the `row_major` modifier and uses column-major std140 instead. This causes matrix data to be **transposed** and offsets of following members to shift unexpectedly.

## Solution: Vulkan-First Design

### Step 1: Define the Shader Preprocessor Macro

In your shader system, strip `row_major` for Vulkan compilation:

```hlsl
// File: Shaders/Platform/Common/BufferLayouts.hlsli

#if PLATFORM_VULKAN
    // SPIRV defaults to column-major std140 layout
    // Do not use row_major; it expands to nothing
    #define row_major       // Empty macro
    #define column_major    // Explicit (already default)
    #define SPIRV_LAYOUT 1
    #define STD140_LAYOUT 1
#elif PLATFORM_DX12
    // DX12 supports both row_major and column_major
    // Use column_major to match Vulkan std140
    #define row_major       // Allowed, but prefer column_major
    #define column_major    // Matches std140
    #define DX12_LAYOUT 1
#endif
```

### Step 2: Structure cbuffer Members in Standard Order

**GOLDEN RULE**: Place larger types before smaller types to minimize padding.

```hlsl
// File: Shaders/Common/FrameConstants.hlsli

cbuffer FrameData
{
    // Group 1: Matrices (64 bytes each)
    column_major float4x4 ViewMatrix;       // Offset 0-63   (4 cols × 16 bytes)
    column_major float4x4 ProjMatrix;       // Offset 64-127 (4 cols × 16 bytes)

    // Group 2: Large vectors (16 bytes each)
    float4 CameraPosition;                  // Offset 128-143 (4 floats)
    float4 LightDirection;                  // Offset 144-159 (4 floats)

    // Group 3: Smaller vectors (8 bytes each)
    float2 ScreenSize;                      // Offset 160-167 (2 floats)
    float2 ScreenSizeInv;                   // Offset 168-175 (2 floats)

    // Group 4: Scalars (4 bytes each) — pack tightly
    float DeltaTime;                        // Offset 176-179 (1 float)
    uint FrameIndex;                        // Offset 180-183 (1 uint) — std140: int/uint = 4 bytes
    uint DebugFlags;                        // Offset 184-187 (1 uint)
    float Padding0;                         // Offset 188-191 (explicit padding for alignment)
};
// Total: 192 bytes (aligned to 16-byte boundary for std140)
```

### Step 3: CPU-Side Struct (C++)

Match the shader layout exactly. Use alignment directives:

```cpp
// File: Runtime/Engine/Renderer/FrameConstants.h

#pragma pack(push, 1)

struct alignas(256) FFrameConstantBuffer
{
    // Group 1: Matrices (64 bytes each)
    DirectX::XMFLOAT4X4 ViewMatrix;        // Offset 0-63
    DirectX::XMFLOAT4X4 ProjMatrix;        // Offset 64-127

    // Group 2: Vectors (16 bytes each)
    DirectX::XMFLOAT4 CameraPosition;      // Offset 128-143
    DirectX::XMFLOAT4 LightDirection;      // Offset 144-159

    // Group 3: Vectors (8 bytes each)
    DirectX::XMFLOAT2 ScreenSize;          // Offset 160-167
    DirectX::XMFLOAT2 ScreenSizeInv;       // Offset 168-175

    // Group 4: Scalars (4 bytes each)
    float DeltaTime;                        // Offset 176-179
    uint32_t FrameIndex;                    // Offset 180-183
    uint32_t DebugFlags;                    // Offset 184-187
    float Padding0;                         // Offset 188-191 (explicit padding)

    // Total size: 192 bytes
    static_assert(sizeof(FFrameConstantBuffer) == 192, "Cbuffer size mismatch!");
};

#pragma pack(pop)
```

### Step 4: Matrix Transpose for Vulkan

**Critical**: DX12 uses row-major by convention; Vulkan/SPIRV uses column-major. Matrices must be transposed when uploading to GPU for Vulkan.

```cpp
// File: Runtime/Engine/Renderer/FrameConstantsUpload.cpp

void UploadFrameConstants(FRenderContext* RenderCtx, const FFrameView& View)
{
    FFrameConstantBuffer* MappedBuffer = RenderCtx->MapConstantBuffer<FFrameConstantBuffer>();

    if (RenderCtx->GetRHIBackend() == ERHIBackend::DX12)
    {
        // DX12: Row-major (standard for D3D)
        // Compute row-major matrices
        DirectX::XMMATRIX ViewMat = DirectX::XMLoadFloat4x4(&View.ViewMatrix);
        DirectX::XMMATRIX ProjMat = DirectX::XMLoadFloat4x4(&View.ProjectionMatrix);

        // Store directly
        DirectX::XMStoreFloat4x4(&MappedBuffer->ViewMatrix, ViewMat);
        DirectX::XMStoreFloat4x4(&MappedBuffer->ProjMatrix, ProjMat);
    }
    else if (RenderCtx->GetRHIBackend() == ERHIBackend::Vulkan)
    {
        // Vulkan: Column-major std140 (SPIRV default)
        // Compute matrices and TRANSPOSE before storing
        DirectX::XMMATRIX ViewMat = DirectX::XMLoadFloat4x4(&View.ViewMatrix);
        DirectX::XMMATRIX ProjMat = DirectX::XMLoadFloat4x4(&View.ProjectionMatrix);

        // Transpose for column-major layout
        DirectX::XMStoreFloat4x4(&MappedBuffer->ViewMatrix, DirectX::XMMatrixTranspose(ViewMat));
        DirectX::XMStoreFloat4x4(&MappedBuffer->ProjMatrix, DirectX::XMMatrixTranspose(ProjMat));
    }

    // Other fields (same for both)
    MappedBuffer->CameraPosition = View.CameraPosition;
    MappedBuffer->LightDirection = View.LightDirection;
    MappedBuffer->ScreenSize = View.ScreenSize;
    MappedBuffer->ScreenSizeInv = View.ScreenSizeInv;
    MappedBuffer->DeltaTime = View.DeltaTime;
    MappedBuffer->FrameIndex = View.FrameIndex;
    MappedBuffer->DebugFlags = View.DebugFlags;
    MappedBuffer->Padding0 = 0.0f;

    RenderCtx->UnmapConstantBuffer();
}
```

### Alternative: Single Upload Path with Matrix Wrapper

If you want a single upload path, use a helper function:

```cpp
// File: Runtime/Core/Math/MatrixLayout.h

namespace uz {

/// Stores a matrix in the requested layout (row-major for DX12, column-major for Vulkan)
inline DirectX::XMMATRIX MatrixForLayout(const DirectX::XMMATRIX& RowMajorMatrix)
{
#if PLATFORM_VULKAN
    return DirectX::XMMatrixTranspose(RowMajorMatrix);
#else
    return RowMajorMatrix;
#endif
}

} // namespace uz
```

Usage:
```cpp
DirectX::XMStoreFloat4x4(&MappedBuffer->ViewMatrix, uz::MatrixForLayout(ViewMatrix));
```

## Standard Memory Layout Reference

### std140 Layout Rules (Vulkan Default)

1. **Scalars (int, uint, float)**: 4 bytes, 4-byte alignment
2. **Vectors (float2, float3, float4)**: Natural alignment (2, 4, 8, 16 bytes)
3. **Matrices (float4x4)**: Each column = float4 = 16 bytes; 4 columns = 64 bytes total
4. **Arrays**: Element size rounded up to 16-byte boundary
5. **Padding**: Inserted automatically; explicit padding fields are your friend

### Example: Offset Calculation (std140)

```hlsl
cbuffer Data {
    float4x4 M;     // Offset 0-63   (4 × 16-byte columns)
    float2 V;       // Offset 64-71  (2 floats)
    uint U;         // Offset 72-75  (int/uint = 4 bytes)
    uint U2;        // Offset 76-79  (another uint, tight-packed)
    float F;        // Offset 80-83  (float, tight-packed)
};
```

**Key**: In std140, scalars and small vectors pack tightly after larger structures, but are always at least aligned to their own type size (not 16 bytes).

## Pitfalls to Avoid

### ❌ Pitfall 1: Interleaving Matrices and Scalars

```hlsl
// WRONG
cbuffer Data {
    uint ID;                // Offset 0-3 ... but std140 pads next member to 16-byte!
    float4x4 Matrix;        // Offset 16-79 (not 4!)
    uint Flags;             // Offset 80-83 (off by 16 bytes from what you'd expect)
};
```

### ❌ Pitfall 2: Relying on `row_major` in SPIRV

```hlsl
// WRONG for Vulkan
cbuffer Data {
    row_major float4x4 M;   // row_major is IGNORED in SPIRV; becomes column-major
    uint ID;
};
```

### ❌ Pitfall 3: Not Transposing Matrices for Vulkan

```cpp
// WRONG
DirectX::XMMATRIX ViewMat = ComputeViewMatrix();  // Row-major
// ... upload to Vulkan without transpose ...
// Result: Matrix is transposed; view space is inverted
```

### ❌ Pitfall 4: Assuming Same CPU and GPU Layout

```cpp
// WRONG
struct CBData {
    float4x4 M;  // 64 bytes in std140
    uint ID;     // Offset 64, not 68 (std140 doesn't pad uint to 16 bytes)
};
```

## Validation Checklist

- [ ] All matrices are `column_major` (explicit or relying on default)
- [ ] Matrices come **before** scalars in struct declaration
- [ ] CPU struct size matches expected SPIRV size (use `static_assert`)
- [ ] For Vulkan, matrices are **transposed** before upload
- [ ] For DX12, matrices are **not** transposed (or are row-major)
- [ ] No interleaved scalars and matrices
- [ ] Explicit padding fields added where needed
- [ ] Cbuffer size is 16-byte aligned (for std140 safety)

## Example: Complete End-to-End

### Shader Code
```hlsl
// Shaders/Common/MaterialParams.hlsli
cbuffer MaterialData
{
    column_major float4x4 ObjectToWorld;    // 0-63
    float4 AlbedoColor;                     // 64-79
    float Roughness;                        // 80-83
    float Metallic;                         // 84-87
    uint TextureIndex;                      // 88-91
    uint Padding;                           // 92-95
};

float4 main(PSInput input) : SV_Target
{
    float4 worldPos = mul(float4(input.position, 1.0f), ObjectToWorld);
    return AlbedoColor * Roughness;
}
```

### C++ Code
```cpp
// Runtime/Engine/Renderer/MaterialConstants.h
#pragma pack(push, 1)
struct alignas(16) FMaterialConstantBuffer
{
    DirectX::XMFLOAT4X4 ObjectToWorld;  // 0-63
    DirectX::XMFLOAT4 AlbedoColor;      // 64-79
    float Roughness;                     // 80-83
    float Metallic;                      // 84-87
    uint32_t TextureIndex;               // 88-91
    uint32_t Padding;                    // 92-95

    static_assert(sizeof(FMaterialConstantBuffer) == 96, "Size mismatch!");
};
#pragma pack(pop)

// Runtime/Engine/Renderer/MaterialConstants.cpp
void UpdateMaterialBuffer(FRenderContext* Ctx, const FMaterial& Mat)
{
    auto* CB = Ctx->MapConstantBuffer<FMaterialConstantBuffer>();

    // For Vulkan, transpose the matrix
    DirectX::XMMATRIX ObjToWorld = DirectX::XMLoadFloat4x4(&Mat.ObjectToWorld);
    if (Ctx->GetRHIBackend() == ERHIBackend::Vulkan)
    {
        DirectX::XMStoreFloat4x4(&CB->ObjectToWorld, DirectX::XMMatrixTranspose(ObjToWorld));
    }
    else
    {
        DirectX::XMStoreFloat4x4(&CB->ObjectToWorld, ObjToWorld);
    }

    CB->AlbedoColor = Mat.AlbedoColor;
    CB->Roughness = Mat.Roughness;
    CB->Metallic = Mat.Metallic;
    CB->TextureIndex = Mat.TextureIndex;
    CB->Padding = 0;

    Ctx->UnmapConstantBuffer();
}
```

## References

- **UE5 Implementation**: `W:\UE_5.6_Source\Engine\Source\Developer\VulkanShaderFormat\Private\SpirVShaderCompiler.inl` (line 1416: `row_major` macro)
- **Vulkan SPIRV Spec**: Column-major by default, std140 layout
- **HLSL Documentation**: `row_major` and `column_major` qualifiers
- **Analysis Document**: `docs/analysis/UE5_CBUFFER_LAYOUT_DX12_VULKAN_ANALYSIS.md`
