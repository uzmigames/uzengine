# Cbuffer Layout Quick Reference Card

## The Problem in 30 Seconds

- **DX12**: `row_major float4x4` = rows stored contiguously, matrix is row-major in memory
- **Vulkan**: Ignores `row_major`, uses std140 = columns stored contiguously, matrix is column-major in memory
- **Result**: If you use `row_major` in shader + don't transpose on CPU, data is transposed in GPU memory → garbage values

## The Solution in 30 Seconds

1. ❌ Don't use `row_major` in Vulkan shaders
2. ✅ Use `column_major` explicitly (or rely on std140 default)
3. ✅ Place matrices **before** scalars in cbuffer
4. ✅ Transpose matrices on CPU when uploading to Vulkan
5. ✅ Add `static_assert(sizeof(...))` to C++ structs

## Shader Template (CORRECT)

```hlsl
#include "Platform/Vulkan/BufferCompat.hlsli"  // Strips row_major for Vulkan

cbuffer FrameData
{
    column_major float4x4 ViewProj;     // Matrix first (0-63)
    float4 CameraPos;                   // Vector (64-79)
    float DeltaTime;                    // Scalar (80-83)
    uint FrameIndex;                    // Scalar (84-87)
    uint Padding;                       // Explicit padding (88-91)
};
```

## C++ Template (CORRECT)

```cpp
#pragma pack(push, 1)
struct alignas(16) FFrameData
{
    DirectX::XMFLOAT4X4 ViewProj;   // 0-63
    DirectX::XMFLOAT4 CameraPos;    // 64-79
    float DeltaTime;                 // 80-83
    uint32_t FrameIndex;             // 84-87
    uint32_t Padding;                // 88-91

    // SIZE CHECK: std140 = 92 bytes → round to 16 = 96
    static_assert(sizeof(FFrameData) == 96, "Size mismatch!");
};
#pragma pack(pop)
```

## Upload Template (CORRECT)

```cpp
auto* CB = MapBuffer<FFrameData>();

// Compute matrix (row-major, game engine convention)
DirectX::XMMATRIX ViewProj = ComputeViewProj();

// Transpose for Vulkan, keep as-is for DX12
if (IsVulkan())
    DirectX::XMStoreFloat4x4(&CB->ViewProj, XMMatrixTranspose(ViewProj));
else
    DirectX::XMStoreFloat4x4(&CB->ViewProj, ViewProj);

CB->CameraPos = CameraPosition;
CB->DeltaTime = DeltaTime;
CB->FrameIndex = FrameIndex;
CB->Padding = 0;

UnmapBuffer();
```

## Memory Layout Comparison

### std140 Layout (Vulkan, used for graphics)

| Member | Type | Offset | Size | Notes |
|--------|------|--------|------|-------|
| ViewProj | float4x4 | 0 | 64 | 4 cols × 16 bytes |
| CameraPos | float4 | 64 | 16 | 16-byte aligned |
| DeltaTime | float | 80 | 4 | 4-byte aligned |
| FrameIndex | uint | 84 | 4 | Tightly packed |
| Padding | uint | 88 | 4 | Tightly packed |
| | | 92 | | → Rounds to 96 (16-byte aligned) |

### Key Rules

1. **Matrix**: Takes 64 bytes (4 columns, 16 bytes each)
2. **Vector (float4)**: Takes 16 bytes, 16-byte aligned
3. **Scalar (float, uint, int)**: Takes 4 bytes, **tightly packed** (no padding between them)
4. **Total cbuffer size**: Must be 16-byte aligned

## What Breaks (Anti-Patterns)

### ❌ Scalar Before Matrix
```hlsl
cbuffer Data {
    uint ID;            // Offset 0-3
                        // Offset 4-15: PADDING (std140 pads to 16 bytes)
    float4x4 Matrix;    // Offset 16-79 (not 4!)
};
```
Result: C++ struct has `ID` at 0, `Matrix` at 4, but shader has them at 0 and 16 → CRASH

### ❌ Using row_major Without Guard
```hlsl
cbuffer Data {
    row_major float4x4 Matrix;  // Vulkan ignores row_major → becomes column-major
    uint ID;
};
// DX12 reads row-major, Vulkan reads column-major → Transposed!
```

### ❌ Not Transposing on Vulkan
```cpp
DirectX::XMMATRIX M = GetMatrix();  // Row-major
XMStoreFloat4x4(&CB->Matrix, M);    // Store directly
// DX12: Correct (row-major)
// Vulkan: WRONG (interpreted as column-major, reads transposed)
```

### ❌ No sizeof Assert
```cpp
struct CBData { float4x4 M; uint ID; };
// C++ sizeof = 68 (matrix 64 + uint 4)
// std140 sizeof = 96 (matrix 64 + uint 4 + padding 12)
// → GPU reads garbage at offset 68-96
```

## Checklist Before Shipping

- [ ] All cbuffers use `column_major` (explicit) or rely on std140 default
- [ ] No `row_major` in Vulkan shader code
- [ ] Matrices appear **before** all scalars in cbuffer
- [ ] C++ struct has `#pragma pack(push, 1)` and `static_assert(sizeof(...))`
- [ ] All matrix uploads use transpose-for-Vulkan logic
- [ ] Cbuffer size is 16-byte aligned
- [ ] Tested on both DX12 and Vulkan (same visual output)

## Why This Matters

**Symptom**: "uint values in my cbuffer show as 0 or garbage after a matrix"

**Why**: std140 layout after a float4x4 is different from HLSL row-major layout. The matrix may be transposed (data is there, but interpreted wrong), or the scalars may be at a different offset than expected.

**Fix**: Follow the template above. It's battle-tested in UE5 (used by thousands of games).

## Real-World Example: ViewProj Matrix

### Wrong (Fails on Vulkan)
```hlsl
cbuffer Matrices { row_major float4x4 ViewProj; };  // ← row_major ignored in Vulkan
```
```cpp
DirectX::XMMATRIX VP = XMMatrixMultiply(View, Proj);
XMStoreFloat4x4(&CB->ViewProj, VP);  // ← No transpose, DX12 works, Vulkan shows inverted view
```

### Right (Works on Both)
```hlsl
cbuffer Matrices { column_major float4x4 ViewProj; };  // ← Explicit, safe
```
```cpp
DirectX::XMMATRIX VP = XMMatrixMultiply(View, Proj);
if (IsVulkan())
    XMStoreFloat4x4(&CB->ViewProj, XMMatrixTranspose(VP));  // ← Transpose for column-major std140
else
    XMStoreFloat4x4(&CB->ViewProj, VP);  // ← DX12 uses row-major as-is
```

## Further Reading

- **Full Analysis**: `docs/analysis/UE5_CBUFFER_LAYOUT_DX12_VULKAN_ANALYSIS.md`
- **Implementation Guide**: `docs/architecture/UNIFORM_BUFFER_LAYOUT_GUIDE.md`
- **Architecture Rules**: `docs/architecture/SHADER_BUFFER_COMPATIBILITY_DIRECTIVE.md`
- **UE5 Source**: `W:\UE_5.6_Source\Engine\Source\Developer\VulkanShaderFormat\Private\SpirVShaderCompiler.inl` (Line 1416)
