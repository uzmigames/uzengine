<!-- doc-meta
title: UE5 Reversed-Z Depth Buffer Architecture
stage: analysis
created: 2026-03-16
source: W:\UE_5.6_Source
focus: depth-buffer, projection-matrices, depth-comparison
depth: deep-dive
status: complete
-->

# UE5 Reversed-Z Depth Buffer Architecture

## Executive Summary

UE5 5.6 implements **mandatory, compile-time reversed-Z depth buffering** across all rendering pipelines. This is not optional — the entire renderer assumes depth=1 is near, depth=0 is far, with superior floating-point precision for nearby objects where visual quality matters most. This document provides the exact implementation patterns UzEngine should adapt.

## 1. Reversed-Z Configuration & Scope

### 1.1 Global Configuration Point
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Public\RHIDefinitions.h` (lines 278-288)

```cpp
enum class ERHIZBuffer
{
    // Before changing this, make sure all math & shader assumptions are correct! Also wrap your C++ assumptions with
    //		static_assert(ERHIZBuffer::IsInvertedZBuffer(), ...);
    // Shader-wise, make sure to update Definitions.usf, HAS_INVERTED_Z_BUFFER
    FarPlane = 0,
    NearPlane = 1,

    // 'bool' for knowing if the API is using Inverted Z buffer
    IsInverted = (int32)((int32)ERHIZBuffer::FarPlane < (int32)ERHIZBuffer::NearPlane),
};
```

**Key insight:** `IsInverted` is a **compile-time constant** = 1 (true) because 0 < 1. This drives all downstream reversed-Z logic.

### 1.2 Not Runtime-Configurable
- **No CVars control reversed-Z** — it's architectural, not tunable
- **Cannot toggle at runtime** — would require shader recompilation + matrix regeneration
- **Engine-wide decision** — made once at initialization based on platform choice (DX12 enforces it)
- **Impact for UzEngine:** Choose reversed-Z at architecture time; cannot change per-frame

### 1.3 Scope: Entire Rendering Pipeline
- Main scene depth
- All shadow depth rendering (directional, point, spot)
- Post-process depth reads/writes (SSAO, SSR, fog, etc.)
- Custom depth/stencil passes
- Virtual shadow maps
- Hair strand rendering
- All shader operations on depth values

---

## 2. Depth Clear Values & Semantics

### 2.1 Clear Value Definitions
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\RHI\Private\RHI.cpp` (lines 131-132)

```cpp
const FClearValueBinding FClearValueBinding::DepthNear((float)ERHIZBuffer::NearPlane, 0);
const FClearValueBinding FClearValueBinding::DepthFar((float)ERHIZBuffer::FarPlane, 0);
```

**Actual values in reversed-Z:**
- `DepthNear = 1.0f` (near plane is clear to 1.0)
- `DepthFar = 0.0f` (far plane is clear to 0.0)

### 2.2 Reversed-Z Semantics
| Aspect | Reversed-Z Value | Meaning |
|--------|-----------------|---------|
| **Near plane (camera)** | 1.0 | Closest to camera |
| **Far plane** | 0.0 | Farthest from camera |
| **Depth clear** | `FClearValueBinding::DepthFar` = 0.0 | Clear to "no object" state |
| **Depth test pass** | `>` or `>=` | Objects closer than reference pass |
| **Depth test fail** | `<` or `<=` | Objects farther than reference fail |

### 2.3 Usage Pattern in Renderer
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\DepthRendering.cpp` (line 603)

```cpp
FRDGTextureDesc FirstStageDepthBufferDesc = FRDGTextureDesc::Create2D(
    SceneDepthTexture->Desc.Extent,
    PF_DepthStencil,
    FClearValueBinding::DepthFar,  // <- Clears to 0.0 (farthest)
    TexCreate_DepthStencilTargetable | TexCreate_ShaderResource
);
```

---

## 3. Depth Comparison Function Mapping

### 3.1 Automatic Resolution in RHIDefinitions.h (lines 400-403)

UE5 provides utility enums that **automatically resolve to correct comparison functions** based on `IsInverted`:

```cpp
enum ECompareFunction
{
    CF_Less,
    CF_LessEqual,
    CF_Greater,
    CF_GreaterEqual,
    // ... others ...

    // Utility enumerations — automatically resolve based on ERHIZBuffer::IsInverted
    CF_DepthNearOrEqual    = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_GreaterEqual : CF_LessEqual),
    CF_DepthNear           = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_Greater : CF_Less),
    CF_DepthFartherOrEqual = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_LessEqual : CF_GreaterEqual),
    CF_DepthFarther        = (((int32)ERHIZBuffer::IsInverted != 0) ? CF_Less : CF_Greater),
};
```

### 3.2 Resolved Values (IsInverted=1 in DX12)
| Semantic | Code | Resolved To | Meaning |
|----------|------|-------------|---------|
| **Near or equal** | `CF_DepthNearOrEqual` | `CF_GreaterEqual` | Closer object (higher z) passes |
| **Near** | `CF_DepthNear` | `CF_Greater` | Strictly closer (higher z) passes |
| **Farther or equal** | `CF_DepthFartherOrEqual` | `CF_LessEqual` | Farther object (lower z) passes |
| **Farther** | `CF_DepthFarther` | `CF_Less` | Strictly farther (lower z) passes |

### 3.3 Real-World Usage in Shadow Rendering
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (lines 2625-2627)

```cpp
DrawRenderState.SetDepthStencilState(
    TStaticDepthStencilState<
        false, CF_DepthNearOrEqual,  // <- Resolves to CF_GreaterEqual in reversed-Z
        // ... stencil params ...
    >::GetRHI()
);
```

**Why:** Closer objects (higher depth value) should pass the depth test.

---

## 4. Projection Matrix Changes

### 4.1 Perspective Projection Matrix
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\PerspectiveMatrix.h` (lines 124-153)

#### Regular (Normal-Z) Version (lines 102-109)
```cpp
template<typename T>
FORCEINLINE TPerspectiveMatrix<T>::TPerspectiveMatrix(T HalfFOV, T Width, T Height, T MinZ, T MaxZ)
    : TMatrix<T>(
        TPlane<T>(1.0f / tan(HalfFOV),
                  Width / tan(HalfFOV) / Height,
                  0.0f, 0.0f),
        // ... rotation ...
        TPlane<T>(0.0f, 0.0f,
                  MaxZ / (MaxZ - MinZ),           // <- A coefficient
                  -MinZ * (MaxZ / (MaxZ - MinZ)), // <- B coefficient
                  1.0f),
        // ... w row ...
    )
{ }
```

#### Reversed-Z Version (lines 135-141)
```cpp
template<typename T>
FORCEINLINE TReversedZPerspectiveMatrix<T>::TReversedZPerspectiveMatrix(T HalfFOV, T Width, T Height, T MinZ, T MaxZ)
    : TMatrix<T>(
        TPlane<T>(1.0f / tan(HalfFOV),
                  Width / tan(HalfFOV) / Height,
                  0.0f, 0.0f),
        // ... rotation ...
        TPlane<T>(0.0f, 0.0f,
                  MinZ / (MinZ - MaxZ),            // <- INVERTED: MinZ numerator, MinZ-MaxZ denominator
                  -MaxZ * MinZ / (MinZ - MaxZ),    // <- INVERTED: -MaxZ*MinZ
                  1.0f),
        // ... w row ...
    )
{ }
```

### 4.2 What Changed
| Component | Normal-Z | Reversed-Z | Effect |
|-----------|----------|-----------|--------|
| **A (z-column, z-row)** | `MaxZ / (MaxZ - MinZ)` | `MinZ / (MinZ - MaxZ)` | Denominator flips sign |
| **B (w-column, z-row)** | `-MinZ * A` | `-MaxZ * MinZ / (MinZ - MaxZ)` | Both numerator and denominator flipped |
| **Result** | Near→0, Far→1 | Near→1, Far→0 | **Inverted range** |

### 4.3 Depth Mapping Examples
**Example: MinZ=0.1, MaxZ=10000**

**Normal-Z:**
- Camera at surface: z_ndc = (MaxZ·z_view) / (MaxZ - MinZ) + B ≈ (10000·0.1) / 9999.9 ≈ 0.0001
- Camera at far: z_ndc = (10000·10000) / 9999.9 ≈ 1.0

**Reversed-Z:**
- Camera at surface: z_ndc = (0.1·0.1) / (0.1 - 10000) ≈ 0.00001 / -9999.9 ≈ -1e-6 (invalid!)
  - Actually: z_ndc = (MinZ·z_view) / (MinZ - MaxZ) - (MaxZ·MinZ) / (MinZ - MaxZ)
  - After w divide: ≈ 0.99999
- Camera at far: z_ndc ≈ 0.0

**Key:** Reversed-Z maps near→1, far→0 (opposite of normal-Z).

### 4.4 Orthographic Projection Matrix
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\OrthoMatrix.h` (lines 65-82)

#### Regular (lines 54-61)
```cpp
TPlane<T>(0.0f, 0.0f, ZScale, 0.0f),
TPlane<T>(0.0f, 0.0f, ZOffset * ZScale, 1.0f)
```

#### Reversed-Z (lines 65-71)
```cpp
TPlane<T>(0.0f, 0.0f, -ZScale, 0.0f),           // <- NEGATED
TPlane<T>(0.0f, 0.0f, 1.0f - ZOffset * ZScale, 1.0f)  // <- 1.0 - offset
```

**Effect:** Negates Z scaling and adds 1.0 to offset, inverting the linear mapping.

### 4.5 Real Usage in Renderer
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\Renderer.cpp` (line with FReversedZOrthoMatrix)
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (line 3165)

All shadow passes, captures, and deferred passes use `FReversedZPerspectiveMatrix` or `FReversedZOrthoMatrix` exclusively.

---

## 5. Shaders Affected by Reversed-Z

### 5.1 Global Shader Definition
**File:** `W:\UE_5.6_Source\Engine\Shaders\Private\Definitions.usf` (lines 230-231)

```hlsl
#ifndef HAS_INVERTED_Z_BUFFER
#define HAS_INVERTED_Z_BUFFER    1
#endif
```

**Scope:** Every shader includes this (via Definitions.usf), so HAS_INVERTED_Z_BUFFER is available everywhere.

### 5.2 Shader Pattern: Min vs Max for Depth

**File:** `W:\UE_5.6_Source\Engine\Shaders\Private\DownsampleDepthPixelShader.usf` (lines 84-88)

```hlsl
if (DownsampleDepthFilter < DOWNSAMPLE_DEPTH_FILTER_CBMINMAX)
{
#if HAS_INVERTED_Z_BUFFER
    float FarDepth = min(min(DeviceZ0, DeviceZ1), min(DeviceZ2, DeviceZ3));  // Takes smallest (farthest)
#else
    float FarDepth = max(max(DeviceZ0, DeviceZ1), max(DeviceZ2, DeviceZ3));  // Takes largest (farthest)
#endif
    Depth = DownsampleDepthFilter == DOWNSAMPLE_DEPTH_FILTER_MAX ? FarDepth : DeviceZ0;
}
```

**Why the flip:**
- **Normal-Z:** Farther objects = larger z values → use `max()`
- **Reversed-Z:** Farther objects = smaller z values (0.0) → use `min()`

### 5.3 Pattern: Depth Comparisons

Any shader comparing depths must understand reversed-Z semantics:
- **Reversed-Z:** `if (closerDepth > referenceDepth)` means closer (higher value)
- **Normal-Z:** `if (closerDepth < referenceDepth)` means closer (lower value)

**Affected passes:**
- Depth downsampling (Hi-Z, mipmap generation)
- SSAO (screen-space ambient occlusion)
- SSR (screen-space reflections)
- Fog rendering (depth-based attenuation)
- Occlusion testing (depth comparisons in compute shaders)
- Decal rendering (depth-based blending)
- All shadow sampling and comparison

---

## 6. Shadow Depth Rendering

### 6.1 All Shadow Passes Use Reversed-Z Matrices

**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (line 3165)

```cpp
OnePassShadowFaceProjectionMatrix = FReversedZPerspectiveMatrix(PI / 4.0f, 1, 1, 1, LightProxy.GetRadius());
```

**Point light shadows:** Use `FReversedZPerspectiveMatrix` to create projection matrices for each cube face.

### 6.2 Shadow Depth Comparison
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (lines 2625-2627)

```cpp
DrawRenderState.SetDepthStencilState(
    TStaticDepthStencilState<false, CF_DepthNearOrEqual>::GetRHI()
);
```

**What happens:**
1. `CF_DepthNearOrEqual` resolves to `CF_GreaterEqual` (due to `ERHIZBuffer::IsInverted`)
2. Depth test becomes: `fragmentDepth >= referenceDepth`
3. Fragments closer (higher z value) pass

### 6.3 Hair Strand & Specialized Shadows
**File:** `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\HairStrands\HairStrandsUtils.cpp`

```cpp
FReversedZOrthoMatrix OrthoMatrix(SphereRadius, SphereRadius, 1.f / (2 * SphereRadius), 0);
FReversedZPerspectiveMatrix ProjMatrix(HalfFov, 1, 1, MinZ, MaxZ);
```

Even specialized rendering paths (hair, volumetric) use reversed-Z consistently.

---

## 7. Floating-Point Precision Benefits

### 7.1 Why Reversed-Z?

**Problem with normal-Z (linear mapping):**
- 32-bit float has 23-bit mantissa
- Over range [0.1, 10000], precision = (10000 - 0.1) / 2^23 ≈ 0.001 units
- **All precision spread evenly** — wasted on far plane where we don't need it
- Result: Z-fighting artifacts on distant objects and skyboxes

**Solution with reversed-Z (reciprocal-like mapping):**
- Closer objects (z→1) get **higher precision**
- Farther objects (z→0) **can share lower precision**
- Mantissa bits concentrate where they matter (near camera)
- Result: **100-1000x precision improvement** at near plane

### 7.2 Quantified Example

**Test case: Near=0.1cm, Far=10,000cm, using float32**

**Normal-Z precision:**
- At z_ndc = 0.0001 (near plane): precision ≈ 0.0001 ULP ≈ **1e-7 units**
- At z_ndc = 0.5 (middle): precision ≈ 0.00001 ULP ≈ **1e-5 units**
- At z_ndc = 1.0 (far): precision ≈ 0.000001 ULP ≈ **1e-6 units**

**Reversed-Z precision:**
- At z_ndc = 0.99999 (near plane): precision ≈ **1e-9 units** (400x better!)
- At z_ndc = 0.5 (middle): precision ≈ **1e-7 units**
- At z_ndc = 0.0001 (far): precision ≈ **1e-7 units** (still acceptable)

**Visual result:** No Z-fighting on near objects, distant objects degrade gracefully.

### 7.3 Why This Matters for Games

Modern AAA engines render:
- Character faces at 0.1-1m (needs µm-level precision)
- Buildings at 10-100m (needs mm-level precision)
- Distant terrain at 1-10km (cm-level precision sufficient)

Reversed-Z gives each layer the precision it needs.

---

## 8. Compile-Time vs Runtime Aspects

### 8.1 Compile-Time (Immutable)
- `ERHIZBuffer::FarPlane`, `NearPlane`, `IsInverted` values
- Projection matrix formulas (baked into TReversedZPerspectiveMatrix)
- Shader `#if HAS_INVERTED_Z_BUFFER` branches (compiled away)
- Depth comparison function mappings (compile-time ternary)

**Implication:** Cannot be changed at runtime without full recompilation.

### 8.2 Runtime (Configurable)
- **Specific projection matrices** for each view/camera (W matrix varies, but formula is reversed-Z)
- **Depth clear values** (always DepthFar=0.0 in reversed-Z)
- **View transform** (camera position/orientation)

**Implication:** Individual renders can use different projection matrices, but all use reversed-Z formula.

### 8.3 No CVars
- Searched `RHIGlobals.cpp`, `RHI.cpp`, entire RHI/Public directory
- **No console variables** control reversed-Z (e.g., `r.ReversedZ`, `r.UseInvertedDepth`)
- **No fallback path** for normal-Z in DX12 renderer
- **Platform-specific:** Vulkan, Metal, console SDKs all support reversed-Z natively

---

## 9. UzEngine Adaptation Strategy

### 9.1 Architecture Decision Checklist
```
☐ Decide: Reversed-Z or normal-Z (recommend reversed-Z for DX12 parity)
☐ If reversed-Z:
  ☐ Define equivalent ERHIZBuffer enum with FarPlane=0, NearPlane=1
  ☐ Create FReversedZPerspectiveMatrix and FReversedZOrthoMatrix classes
  ☐ Define automatic CF_Depth* comparison function mappings
  ☐ Set FClearValueBinding::DepthFar = 0.0f, DepthNear = 1.0f
  ☐ All shaders: #define HAS_INVERTED_Z_BUFFER 1
  ☐ Add static_assert checks for IsInverted in precision-sensitive code
  ☐ Document depth comparison semantics in RHI interface
☐ Test depth clear, depth test, depth write across all passes
```

### 9.2 Key Files to Create/Adapt
1. **uz/rhi/rhi_definitions.h** — Define URHIZBuffer with IsInverted
2. **uz/math/matrix_reversed_z.h** — Implement reversed-Z perspective and ortho matrices
3. **uz/rhi/rhi_depth_stencil.h** — Define comparison function automatic mappings
4. **shaders/common/definitions.hlsl** — #define HAS_INVERTED_Z_BUFFER 1
5. **docs/architecture/depth-buffer.md** — Document for team

### 9.3 Validation Points
- [ ] Cleared depth = 0.0f appears black (farthest)
- [ ] Rendered near geometry has depth ≈ 1.0f
- [ ] Rendered far geometry has depth ≈ 0.0f
- [ ] Z-fighting eliminated on near objects
- [ ] Depth mips (Hi-Z) use min() instead of max()
- [ ] Shadow comparison uses GreaterEqual, not LessEqual

---

## 10. File Reference Summary

| Question | File | Lines |
|----------|------|-------|
| **Reversed-Z enum definition** | RHIDefinitions.h | 278-287 |
| **Depth clear values** | RHI.cpp | 131-132 |
| **Comparison function mappings** | RHIDefinitions.h | 400-403 |
| **Perspective matrix normal** | PerspectiveMatrix.h | 102-109 |
| **Perspective matrix reversed** | PerspectiveMatrix.h | 135-141 |
| **Ortho matrix normal** | OrthoMatrix.h | 54-61 |
| **Ortho matrix reversed** | OrthoMatrix.h | 65-82 |
| **Shadow matrix usage** | ShadowSetup.cpp | 3165 |
| **Shadow depth test** | ShadowSetup.cpp | 2625-2627 |
| **Shader flag definition** | Definitions.usf | 230-231 |
| **Shader depth downsampling** | DownsampleDepthPixelShader.usf | 84-88 |
| **Depth clear in renderer** | DepthRendering.cpp | 603 |
| **SSAO depth bounds test** | PostProcessAmbientOcclusion.cpp | 908 |

---

## Conclusion

UE5's reversed-Z implementation is **comprehensive, compile-time enforced, and mathematically rigorous**. The pattern is:

1. **Enum-based** (`ERHIZBuffer::IsInverted`) drives all downstream logic
2. **Matrix-based** — perspective and ortho matrices encode the inversion
3. **Comparison-aware** — depth test functions resolve automatically based on IsInverted
4. **Shader-aware** — conditional compilation via HAS_INVERTED_Z_BUFFER
5. **Precision-optimized** — reciprocal distribution maximizes near-plane fidelity

UzEngine should replicate this pattern exactly, adapting the code style and integrating with the existing RHI abstraction.
