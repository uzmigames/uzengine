# UE5 Uniform Buffer Layout Analysis: DX12 vs Vulkan/SPIRV

## Problem Statement

When compiling HLSL cbuffers to SPIRV for Vulkan, uniform buffer members (especially `uint` values after `row_major float4x4` matrices) arrive with incorrect values or offsets in the shader. This suggests a layout mismatch between DX12's HLSL semantics and Vulkan's SPIRV std140 layout semantics.

## Key Finding: `row_major` is DISABLED in Vulkan

**File**: `W:\UE_5.6_Source\Engine\Source\Developer\VulkanShaderFormat\Private\SpirVShaderCompiler.inl`
**Line 1416**:

```cpp
Input.Environment.SetDefine(TEXT("row_major"), TEXT(""));
```

This is the **critical pattern UE5 uses**: The `row_major` keyword is defined as an **empty macro** in Vulkan compilation.

### What This Means

1. **DX12**: `row_major float4x4` → rows stored contiguously, column-major in memory
2. **Vulkan**: `row_major` expands to nothing → becomes just `float4x4` → std140 layout (column-major, 4 floats per column, each column 16-byte aligned)
3. **No SPIRV modifier**: DXC does NOT emit `ColMajor` decoration on the matrix type

This avoids layout issues because SPIRV defaults to std140 (column-major), which is what the preprocessor now generates after stripping `row_major`.

## UE5's Scalar Block Layout Strategy

**File**: `W:\UE_5.6_Source\Engine\Source\Developer\VulkanShaderFormat\Private\SpirVShaderCompiler.inl`
**Lines 1334-1335**:

```cpp
// VK_EXT_scalar_block_layout is required by raytracing and by Nanite (so expect it to be present in SM6/Vulkan_1_3)
Options.bDisableScalarBlockLayout = !(InternalState.IsRayTracingShader() || InternalState.IsSM6());
```

### Two Layout Modes

| Mode | When Used | Behavior |
|------|-----------|----------|
| **std140** (default) | ES3.1, SM5, SM5_Android, SM6 (graphics) | Scalar block layout DISABLED. Column-major matrices, 16-byte column padding. |
| **Scalar block layout** | Ray tracing, Nanite (SM6 compute/RT) | ENABLED. Tightly-packed scalars and matrices, no alignment padding. |

### Why Two Modes?

- **std140**: Portable, compatible with older Vulkan 1.1/1.2, predictable offset calculations
- **Scalar block layout**: SM6 + RT require denser packing (e.g., short uniform buffers in shader records)

## cbuffer Member Ordering Pattern

UE5 does **NOT** reorder cbuffer members. Instead, it ensures:

1. **Matrices (float4x4) placed first** (implicitly, via shader authoring conventions)
2. **Scalars/uints follow after** (they pack tightly in std140)
3. **No padding macros** — std140 layout handles it automatically

### std140 Layout for Mixed Types

When a `float4x4` is followed by `uint` in a cbuffer:

```hlsl
cbuffer MyBuffer {
    row_major float4x4 ViewProj;  // (becomes `float4x4` in Vulkan after macro expansion)
    uint InstanceID;
    uint2 Padding;
    uint Flags;
};
```

**DX12 layout** (row_major semantics):
```
Offset 0-15:   Row 0 (float4)
Offset 16-31:  Row 1 (float4)
Offset 32-47:  Row 2 (float4)
Offset 48-63:  Row 3 (float4)
Offset 64-67:  InstanceID (uint)
Offset 68-71:  (next uint or padding)
Offset 72-75:  Flags (uint)
```

**Vulkan std140 layout** (column-major after removing row_major):
```
Offset 0-15:   Column 0 (float4) — 16-byte aligned
Offset 16-31:  Column 1 (float4) — 16-byte aligned
Offset 32-47:  Column 2 (float4) — 16-byte aligned
Offset 48-63:  Column 3 (float4) — 16-byte aligned
Offset 64-67:  InstanceID (uint) — 4-byte aligned
Offset 68-71:  (packing occurs here in std140)
Offset 72-75:  Flags (uint) — 4-byte aligned
```

**Key difference**: The matrix **occupies the same space** (64 bytes), but the interpretation of row vs column order changes. The offsets for following `uint` members are **identical**, but the matrix data layout is **transposed** relative to DX12.

## How Data Mismatch Happens (Your Symptom)

If you experience `uint` values showing as 0 or garbage after `float4x4`:

**Root cause**: The cbuffer is being created/updated with DX12 row-major semantics in C++, but the Vulkan shader is reading it as column-major std140.

**Example failure**:
```cpp
// C++ code (DX12 semantics)
struct CBuffer {
    DirectX::XMMATRIX ViewProj;  // 64 bytes, row-major in DX12
    uint32_t InstanceID;         // Offset 64 in DX12
};

// But Vulkan shader with `row_major` → empty macro, std140 kicks in
// Offset 64 is still valid, but if the matrix interpretation is off...
```

## Solution: UzEngine Adaptation

### 1. **Strip `row_major` Keyword**

Define in your shader preprocessor/compiler:
```hlsl
#define row_major  // Empty macro for Vulkan targets
#define column_major   // Explicit column-major (default in std140)
```

### 2. **Use std140 Explicitly in SPIRV Compilation**

When targeting Vulkan, ensure DXC emits std140-compliant SPIRV:
- Do NOT use `VK_EXT_scalar_block_layout` for graphics shaders (reserve for RT/compute)
- Matrices become column-major by default
- Scalars follow with tight packing rules

### 3. **Reorder cbuffer Members: Matrices First**

**Bad** (will have offset issues):
```hlsl
cbuffer Data {
    uint ID;           // Offset 0-3, but then padding to 16-byte boundary
    float4x4 Matrix;   // Offset 16-79, expected offset 4 in DX12
};
```

**Good** (std140-safe):
```hlsl
cbuffer Data {
    float4x4 Matrix;   // Offset 0-63 (always 16-byte cols, std140 safe)
    uint ID;           // Offset 64-67 (tight-packed after matrix)
    uint Flags;        // Offset 68-71
};
```

### 4. **Ensure C++ Side Matches Vulkan Layout**

In your C++ cbuffer struct:

```cpp
struct MyCBuffer {
    // Matrix first (64 bytes, std140 column-major)
    DirectX::XMMATRIX ViewProj;

    // Then scalars (tight-packed)
    uint32_t InstanceID;
    uint32_t Flags;
    // Total: 72 bytes (no padding needed in std140)
};
```

**Critical**: When you update the cbuffer on CPU for Vulkan, the matrix must be **column-major** (transposed from DX12).

### 5. **Matrix Transpose Pattern**

If your game code uses row-major matrices (DX12-style):

```cpp
// Game code (row-major semantics)
auto matrix = ComputeViewProj();  // Row-major XMMATRIX

// For DX12, use directly:
dx12_cbuffer.ViewProj = matrix;

// For Vulkan, transpose:
vulkan_cbuffer.ViewProj = XMMatrixTranspose(matrix);
```

OR: Use a utility macro:

```hlsl
#if PLATFORM_VULKAN
    #define row_major  // Expand to nothing
    #define APPLY_MATRIX_TRANSPOSE(M) M  // Hint: SPIRV compiler will handle column-major
#else
    #define row_major row_major
    #define APPLY_MATRIX_TRANSPOSE(M) M
#endif
```

### 6. **Validate Offsets with Reflection**

After compiling to SPIRV, use reflection to verify member offsets:

```cpp
// Parse SPIRV reflection (using spirv-reflect or glslang)
// Verify: ViewProj.offset == 0, InstanceID.offset == 64, Flags.offset == 68
```

## File References

| File | Key Content |
|------|-------------|
| `W:\UE_5.6_Source\Engine\Source\Developer\VulkanShaderFormat\Private\SpirVShaderCompiler.inl` | Lines 1334-1335: Scalar block layout control; Line 1416: `row_major` macro definition |
| `W:\UE_5.6_Source\Engine\Source\Developer\VulkanShaderFormat\Private\VulkanShaderCompiler.cpp` | Vulkan-specific shader compilation flags |
| `W:\UE_5.6_Source\Engine\Shaders\Public\Platform\Vulkan\VulkanCommon.ush` | Vulkan shader declarations and bindless patterns |
| `W:\UE_5.6_Source\Engine\Source\Runtime\VulkanRHI\Private\VulkanShaders.cpp` | Vulkan SPIRV module creation |

## Anti-Patterns Found in UzEngine

1. ❌ **Using `row_major` keyword without macro stripping** — will cause layout mismatch in Vulkan SPIRV
2. ❌ **Interleaving scalars and matrices** — violates std140 padding rules
3. ❌ **Assuming DX12 row-major semantics in Vulkan** — SPIRV defaults to column-major (std140)
4. ❌ **Not transposing matrices when cross-compiling** — D3D row-major ≠ SPIRV column-major
5. ❌ **Enabling scalar block layout globally** — should only be on for RT/compute, breaks std140 portability

## Summary

UE5's solution is **elegant and minimal**:

1. **Empty `row_major` macro** → strips modifier, lets std140 handle layout
2. **std140 default** → predictable, column-major matrices, tight scalar packing
3. **Scalar block layout on RT/SM6 only** → denser packing where needed
4. **Member ordering convention** → matrices first, then scalars
5. **CPU-side matrix transpose** → adjust before upload to GPU for Vulkan

Adopt this pattern in UzEngine for cross-platform uniform buffer compatibility.
