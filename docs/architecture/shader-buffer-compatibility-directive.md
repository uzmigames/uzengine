# Shader Buffer Compatibility Directive (SBC-1 to SBC-5)

## Context

UE5 solves uniform buffer layout differences between DX12 (row-major HLSL) and Vulkan (column-major SPIRV std140) through a **single critical pattern**: stripping the `row_major` keyword and letting SPIRV's default column-major std140 layout handle alignment.

This document codifies that pattern as enforceable architecture rules for UzEngine.

## SBC-1: `row_major` Macro Stripping for Vulkan

**Status**: MANDATORY
**Applies To**: All shader files, all platforms

### Rule

Define `row_major` as an empty macro when compiling shaders for Vulkan targets:

```hlsl
// Shaders/Platform/Vulkan/BufferCompat.hlsli

#if PLATFORM_VULKAN
    // SPIRV always uses column-major std140 layout; row_major is not supported
    // Expand to empty macro so shaders compile without the modifier
    #define row_major
#endif

// Shaders/Platform/DX12/BufferCompat.hlsli

#if PLATFORM_DX12
    // DX12 supports both row_major and column_major
    // Prefer column_major to match Vulkan std140 layout
    #define column_major
    // row_major can be used for optimization if needed, but prefer column_major
#endif
```

### Why

- **DX12**: `row_major float4x4` tells HLSL to store matrix rows contiguously (optimized for row-vector multiplication)
- **Vulkan**: `row_major` keyword is ignored by DXC → SPIRV, which defaults to column-major std140
- **Risk**: If a shader author uses `row_major` expecting DX12 row-major semantics, Vulkan SPIRV will silently produce column-major, causing data misinterpretation

### Implementation

1. Create `Shaders/Platform/Vulkan/BufferCompat.hlsli` with the macro definition
2. Include it in every shader file that declares cbuffers
3. Lint rule: Flag any use of `row_major` in shader source without the macro definition

### Verification

```hlsl
// Shaders/Common/FrameData.hlsli

#if PLATFORM_VULKAN
    #include "Platform/Vulkan/BufferCompat.hlsli"
#elif PLATFORM_DX12
    #include "Platform/DX12/BufferCompat.hlsli"
#endif

cbuffer FrameData
{
    // Do NOT use row_major here; use column_major or rely on default (column_major in DX12 too)
    column_major float4x4 ViewProj;  // Explicitly column-major = safe for both platforms
    uint FrameIndex;
};
```

## SBC-2: Member Ordering: Matrices First, Scalars Last

**Status**: MANDATORY
**Applies To**: All cbuffer and structured buffer declarations

### Rule

In any constant buffer or structured buffer, order members by size (largest first):

```
Order 1: Matrices (float4x4, etc.) — 64+ bytes
Order 2: Large vectors (float4) — 16 bytes
Order 3: Medium vectors (float3, float2) — 8-12 bytes
Order 4: Scalars (float, uint, int) — 4 bytes
Order 5: Explicit padding (if needed) — 1-16 bytes
```

### Why

**std140 Layout** (Vulkan default, and matching DX12 column-major):

- Each matrix column = float4 = 16-byte aligned
- Scalars and small vectors pack **tightly** after the matrix ends
- If you interleave matrix and scalar, the scalar may force padding that shifts subsequent offsets

**Example (WRONG)**:
```hlsl
cbuffer Data {
    uint ID;            // Offset 0-3
                        // Offset 4-15: padding (std140 pads to 16-byte boundary for next member)
    float4x4 Matrix;    // Offset 16-79 (NOT offset 4)
    uint Flags;         // Offset 80-83 (off by 16 from expected)
};
```

**Example (CORRECT)**:
```hlsl
cbuffer Data {
    float4x4 Matrix;    // Offset 0-63
    uint ID;            // Offset 64-67 (tightly packed, no padding between Matrix and ID)
    uint Flags;         // Offset 68-71
};
```

### Implementation

1. Add linting rule to shader preprocessor: Warn if scalar appears before matrix in cbuffer
2. Add to shader authoring guide: Always declare matrices first
3. Add static assert in C++ struct: Verify size == expected std140 size

```cpp
// In shader compiler

void LintCBufferMembers(const ShaderAst& Ast)
{
    for (const auto& Member : Ast.cbuffer.members)
    {
        if (Member.type.isMatrix && !PrevMemberIsMatrixOrEmpty)
        {
            WARN("Matrix members should come first in cbuffer; found matrix after scalar");
        }
    }
}
```

## SBC-3: CPU-Side Matrix Transpose for Vulkan

**Status**: MANDATORY for Vulkan targets
**Applies To**: All cbuffer upload code targeting Vulkan

### Rule

When uploading a row-major matrix (standard in game engines) to a constant buffer for Vulkan:

1. Compute the matrix normally (row-major semantics)
2. **Transpose** before copying to the GPU buffer for Vulkan
3. For DX12, copy without transpose (or use row-major semantics)

```cpp
// Upload helper

void UploadMatrixToBuffer(
    FRenderContext* Ctx,
    FFrameConstantBuffer* Buffer,
    const DirectX::XMMATRIX& RowMajorMatrix)
{
    if (Ctx->GetRHIBackend() == ERHIBackend::Vulkan)
    {
        // Vulkan uses column-major std140; transpose the row-major matrix
        DirectX::XMStoreFloat4x4(&Buffer->Matrix, DirectX::XMMatrixTranspose(RowMajorMatrix));
    }
    else if (Ctx->GetRHIBackend() == ERHIBackend::DX12)
    {
        // DX12 uses row-major (or can handle column_major); store directly
        DirectX::XMStoreFloat4x4(&Buffer->Matrix, RowMajorMatrix);
    }
}
```

### Why

- **DX12**: Expects row-major matrices (historical HLSL convention)
- **Vulkan**: std140 layout is column-major (GLSL convention)
- **Transposing**: Converts between the two without changing semantics (viewer sees the same result)

### Implementation

1. Create utility function `MatrixForLayout()` in Math library
2. Use in all matrix-upload paths: camera, transform, light matrices, etc.
3. Add test: Create a matrix, transpose/no-transpose, verify shader reads correct values

```cpp
// File: Runtime/Core/Math/MatrixLayout.h

namespace uz {

/// Returns a matrix in the correct layout for the current graphics backend.
/// For Vulkan (std140 column-major), transposes; for DX12, returns as-is.
inline DirectX::XMMATRIX MatrixForLayout(const DirectX::XMMATRIX& RowMajorMatrix)
{
#if UZ_VULKAN
    return DirectX::XMMatrixTranspose(RowMajorMatrix);
#else
    return RowMajorMatrix;  // DX12
#endif
}

} // namespace uz
```

## SBC-4: std140 Explicit Layout Verification

**Status**: MANDATORY for constant buffer structs
**Applies To**: C++ structs that mirror cbuffer declarations

### Rule

Every C++ struct that corresponds to a GPU constant buffer must:

1. Have explicit `#pragma pack(push, 1)` and `#pragma pack(pop)`
2. Include a `static_assert` verifying the total size matches expected std140 size
3. Include offset assertions for critical members

```cpp
#pragma pack(push, 1)
struct alignas(16) FFrameData
{
    // Matrices (64 bytes each)
    DirectX::XMFLOAT4X4 ViewMatrix;       // Offset 0-63
    DirectX::XMFLOAT4X4 ProjectionMatrix; // Offset 64-127

    // Vectors (16 bytes each)
    DirectX::XMFLOAT4 CameraPos;          // Offset 128-143

    // Scalars (4 bytes each, tightly packed)
    float DeltaTime;                       // Offset 144-147
    uint32_t FrameIndex;                   // Offset 148-151
    uint32_t Flags;                        // Offset 152-155
    float Padding0;                        // Offset 156-159 (explicit padding to 16-byte boundary)

    // Size validation
    static_assert(sizeof(FFrameData) == 160, "Size mismatch: expected 160 bytes");
    static_assert(offsetof(FFrameData, ViewMatrix) == 0, "ViewMatrix offset mismatch");
    static_assert(offsetof(FFrameData, CameraPos) == 128, "CameraPos offset mismatch");
    static_assert(offsetof(FFrameData, DeltaTime) == 144, "DeltaTime offset mismatch");
};
#pragma pack(pop)
```

### Why

std140 layout is deterministic, but subtle:
- Matrix columns are 16-byte aligned
- Scalars pack tightly after the matrix
- If your C++ struct doesn't match, GPU reads garbage

### Implementation

1. After declaring each cbuffer in shaders, calculate expected size using std140 rules
2. Document it in code comment
3. Add `static_assert` in C++ struct
4. Add unit test: Serialize struct to binary, deserialize, verify offsets in SPIRV reflection

## SBC-5: No Scalar Block Layout for Graphics Shaders

**Status**: MANDATORY for graphics pipelines
**Applies To**: Vertex, pixel, and geometry shaders

### Rule

Do **NOT** enable `VK_EXT_scalar_block_layout` for graphics shaders. This extension allows tighter packing but breaks std140 portability and makes offset calculations non-deterministic.

**Only enable for**:
- Ray tracing shaders (inline raytracing, closest-hit, etc.)
- Compute shaders with specific performance requirements
- Nanite-style GPU-driven rendering

```cpp
// In shader compiler options

struct FShaderCompilationOptions
{
    bool bEnableScalarBlockLayout = false;  // Default: std140 for graphics

    void ConfigureForShaderType(EShaderFrequency Frequency, bool bIsRayTracing)
    {
        if (bIsRayTracing || Frequency == SF_Compute)
        {
            // Ray tracing and compute can use denser packing
            bEnableScalarBlockLayout = true;
        }
        else
        {
            // Graphics shaders stick with std140 for portability
            bEnableScalarBlockLayout = false;
        }
    }
};
```

### Why

- **Scalar block layout**: Removes padding, allows 1-byte alignment for scalars
- **std140**: Requires 4-byte alignment for scalars, predictable padding
- **Risk**: If you enable scalar block layout globally, shaders compiled for RT will have different offsets than graphics shaders, causing data corruption

## Enforcement Checklist

| SBC | What to Check | Tool/Location |
|-----|---------------|---------------|
| SBC-1 | `row_major` macro defined for Vulkan | `Shaders/Platform/Vulkan/BufferCompat.hlsli` |
| SBC-2 | Matrices appear before scalars in all cbuffers | Shader linter (preprocessor) |
| SBC-3 | All matrix uploads use `MatrixForLayout()` | Grep codebase for `DirectX::XMStoreFloat4x4` |
| SBC-4 | All GPU structs have `static_assert(sizeof(...))` | C++ compiler (runs all asserts) |
| SBC-5 | Scalar block layout disabled for graphics | Shader compiler config; visual test (wrong offsets = visible artifacts) |

## Related Documents

- **Implementation Guide**: `docs/architecture/UNIFORM_BUFFER_LAYOUT_GUIDE.md`
- **UE5 Analysis**: `docs/analysis/UE5_CBUFFER_LAYOUT_DX12_VULKAN_ANALYSIS.md`
- **Rendering Architecture**: `docs/architecture/render-pipeline.md` (RHI abstraction)

## Example: Complete End-to-End Cbuffer

### Shader Declaration
```hlsl
// Shaders/Common/CommonParams.hlsli
#include "Platform/Vulkan/BufferCompat.hlsli"

cbuffer CommonParams
{
    column_major float4x4 ViewProj;    // 0-63: Column-major for both DX12 and Vulkan
    float4 SunDirection;               // 64-79
    uint FrameIndex;                   // 80-83
    float DeltaTime;                   // 84-87
    uint2 Padding;                     // 88-95: Explicit padding for alignment
};
```

### C++ Mirror
```cpp
// Runtime/Renderer/CommonParams.h
#pragma pack(push, 1)
struct alignas(16) FCommonParams
{
    DirectX::XMFLOAT4X4 ViewProj;  // 0-63
    DirectX::XMFLOAT4 SunDirection; // 64-79
    uint32_t FrameIndex;            // 80-83
    float DeltaTime;                // 84-87
    uint32_t Padding[2];            // 88-95

    static_assert(sizeof(FCommonParams) == 96, "Size mismatch");
};
#pragma pack(pop)
```

### Upload Code
```cpp
// Runtime/Renderer/CommonParamsUpload.cpp
void UploadCommonParams(FRenderContext* Ctx, const FSceneView& View)
{
    auto* CB = Ctx->MapConstantBuffer<FCommonParams>();

    // Use MatrixForLayout to handle DX12 vs Vulkan transpose
    DirectX::XMMATRIX ViewProj = DirectX::XMMatrixMultiply(View.ViewMatrix, View.ProjMatrix);
    DirectX::XMStoreFloat4x4(&CB->ViewProj, uz::MatrixForLayout(ViewProj));

    // Other fields (same for all platforms)
    CB->SunDirection = View.SunDirection;
    CB->FrameIndex = View.FrameIndex;
    CB->DeltaTime = View.DeltaTime;
    CB->Padding[0] = CB->Padding[1] = 0;

    Ctx->UnmapConstantBuffer();
}
```

## Anti-Patterns to Flag (Code Review)

- ❌ Using `row_major` without the macro guard
- ❌ Scalar member before matrix in cbuffer
- ❌ Mixing std140 and scalar block layout in same shader pass
- ❌ Matrix upload without `MatrixForLayout()` helper
- ❌ No `static_assert(sizeof(...))` in GPU struct
- ❌ Hardcoded offsets in shaders (should use reflection or HLSL structure)

## Success Criteria

- All shaders compile without layout warnings for both DX12 and Vulkan
- Uniform buffer data reads correctly on both platforms (verified by visual test or unit test comparing GPU results)
- No "random" null/garbage values in shader uniforms after first matrix member
- Matrix operations produce identical results on DX12 and Vulkan (same pixel output)
