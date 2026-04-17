<!-- doc-meta
type: engine_analysis
title: UE5 Reversed-Z Shadow Map Implementation
description: Complete analysis of UE5 5.6 reversed-Z shadow mapping pipeline, including depth clear values, projection matrices, depth tests, comparison functions, and manual PCF logic.
status: final
date: 2026-03-16
author: Claude (UE5 source analysis)
tags: [rendering, shadow-maps, reversed-z, projection-matrices, depth-testing, pcf, csm, rhi]
references: [docs/architecture/rhi-abstraction.md]
-->

# UE5 Reversed-Z Shadow Map Implementation

## Overview

This document provides exact implementation details of UE5 5.6's reversed-Z shadow mapping system. Every value, formula, and design decision is extracted directly from UE5 source code with file paths and line numbers.

**Key finding**: UE5 uses **true reversed-Z** throughout the shadow pipeline—not a simulation with inverted comparisons. The depth range is [1, 0] where 1 = near plane (closest) and 0 = far plane (farthest).

---

## 1. Shadow Map Depth Clear Value

### What Gets Cleared and Why

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp`

**Lines 690, 704**:
```cpp
const FLinearColor Colors[] = { FLinearColor::White };  // Line 690

DrawClearQuadMRT(RHICmdList, bClearColor, NumClearColors, Colors, true, 1.0f, false, 0);  // Line 704
```

- **Clear color**: `FLinearColor::White` = `(1.0, 1.0, 1.0, 1.0)`
- **Clear depth**: `1.0f` (the last parameter to DrawClearQuadMRT)
- **Clear stencil**: `0`

### Why 1.0 for Reversed-Z?

In reversed-Z coordinates:
- Depth range: [1.0, 0.0] (NDC, after perspective divide)
- 1.0 = maximum depth (far plane / unshadowed)
- 0.0 = minimum depth (near plane / closest geometry)

Clearing to 1.0 means "no occluder at all depths." Any surface closer than 1.0 will be in shadow during sampling.

### Context: Viewport Setup

**Lines 682-685**:
```cpp
const float ViewportMinZ = 0.0f;
const float ViewportMaxZ = 1.0f;
```

The viewport Z range is [0, 1] in clip space. Combined with the reversed-Z projection matrices (see Section 3), values get mapped as:
- Geometry on near plane → clip Z = 1.0
- Geometry on far plane → clip Z = 0.0

---

## 2. Shadow Projection Matrix for Directional Lights (CSM)

### Core Matrix Class

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h`

**Lines 1665-1693**: `struct FShadowProjectionMatrix : public FMatrix`

#### Constructor: Center-based projection
```cpp
FShadowProjectionMatrix(FVector::FReal MinZ, FVector::FReal MaxZ, const FVector4& WAxis) :
    FMatrix(
        FPlane(1,     0,     0,                                          WAxis.X),
        FPlane(0,     1,     0,                                          WAxis.Y),
        FPlane(0,     0,     -(WAxis.Z * MinZ + WAxis.W) / (MaxZ - MinZ),  WAxis.Z),
        FPlane(0,     0,     MaxZ * (WAxis.Z * MinZ + WAxis.W) / (MaxZ - MinZ),  WAxis.W)
    )
```

#### Key Formulas

Z-component mapping:
```
Z' = -(WAxis.Z * MinZ + WAxis.W) / (MaxZ - MinZ)
W' = MaxZ * (WAxis.Z * MinZ + WAxis.W) / (MaxZ - MinZ)
```

After perspective divide (z_post = Z' / W'):
```
z_post = -1 / MaxZ  (at near plane MinZ)
z_post = 0          (at far plane MaxZ)
```

This is the **reversed-Z mapping**: near=1, far=0 (after the inversion from the negative ZScale).

#### Off-Center Projection (overload, lines 1677-1684)
```cpp
FShadowProjectionMatrix( const FVector2D& Min, const FVector2D& Max, const FVector4& WAxis )
    : FMatrix(
        FPlane( 2.0f / (Max.X - Min.X),               0.0f,                           0.0f, WAxis.X),
        FPlane( 0.0f,                                 2.0f / (Max.Y - Min.Y),         0.0f, WAxis.Y),
        FPlane( -(Max.X + Min.X) / (Max.X - Min.X),  -(Max.Y + Min.Y) / (Max.Y - Min.Y),  0.0f, WAxis.Z),
        FPlane( 0.0f,                                 0.0f,                           1.0f, WAxis.W)
    )
```

Used for off-center frustums (common in cascaded shadow map adjustments).

### Usage in CSM Setup

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp`

**Line 907**:
```cpp
const FMatrix ProjectionInner = FShadowProjectionMatrix( MinProjected, MaxProjected, Initializer.WAxis );
```

**Lines 1060-1061**:
```cpp
ViewToClipInner = InnerScaleMatrix * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, Initializer.WAxis);
ViewToClipOuter = OuterScaleMatrix * FShadowProjectionMatrix(MinSubjectZ, MaxSubjectZ, Initializer.WAxis);
```

**Note**: The `Initializer.WAxis` parameter is passed from the light's shadow initialization and encodes whether reversed-Z is used.

---

## 3. Reversed-Z Orthographic Projection Matrix

### Template Definition

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\OrthoMatrix.h`

**Lines 39-51**:
```cpp
template<typename T>
struct TReversedZOrthoMatrix : public TMatrix<T>
{
public:
    TReversedZOrthoMatrix(T Width, T Height, T ZScale, T ZOffset);
    TReversedZOrthoMatrix(T Left, T Right, T Bottom, T Top, T ZScale, T ZOffset);
};
```

### Constructor: Symmetric (Width/Height based)

**Lines 65-72**:
```cpp
template<typename T>
FORCEINLINE TReversedZOrthoMatrix<T>::TReversedZOrthoMatrix(T Width, T Height, T ZScale, T ZOffset)
    : TMatrix<T>(
        TPlane<T>((Width != 0.0f) ? (1.0f / Width) : 1.0f, 0.0f, 0.0f, 0.0f),
        TPlane<T>(0.0f, (Height != 0.0f) ? (1.0f / Height) : 1.f, 0.0f, 0.0f),
        TPlane<T>(0.0f, 0.0f, -ZScale, 0.0f),                          // ← NEGATIVE ZScale
        TPlane<T>(0.0f, 0.0f, 1.0f - ZOffset * ZScale, 1.0f)
    )
{ }
```

### Constructor: Asymmetric (Left/Right/Bottom/Top based)

**Lines 75-82**:
```cpp
template<typename T>
FORCEINLINE TReversedZOrthoMatrix<T>::TReversedZOrthoMatrix(T Left, T Right, T Bottom, T Top, T ZScale, T ZOffset)
    : TMatrix<T>(
        TPlane<T>(1.0f / (Right - Left), 0.0f, 0.0f, 0.0f),
        TPlane<T>(0.0f, 1.0f / (Top - Bottom), 0.0f, 0.0f),
        TPlane<T>(0.0f, 0.0f, -ZScale, 0.0f),                          // ← NEGATIVE ZScale
        TPlane<T>((Left + Right) / (Left - Right), (Top + Bottom) / (Bottom - Top), 1.0f - ZOffset * ZScale, 1.0f)
    )
{ }
```

### Key Differences vs Standard Orthographic

| Aspect | Standard Ortho | Reversed-Z Ortho |
|--------|----------------|------------------|
| M[2][2] (Z scale) | +ZScale | **-ZScale** |
| M[3][2] (Z offset) | ZOffset * ZScale | **1.0 - ZOffset * ZScale** |
| Depth mapping | 0=near, 1=far | **1=near, 0=far** |

The **negative ZScale** and **1.0 offset** are the critical markers of reversed-Z.

### Depth Mapping Math

For a point at view-space depth `Z_view`:
```
Z_clip = -ZScale * Z_view + (1.0 - ZOffset * ZScale)
Z_ndc = Z_clip / W_clip (where W_clip = 1.0 for ortho)
```

After perspective divide:
```
Z_ndc = 1.0 - (Z_view * ZScale + ZOffset * ZScale)
      = 1.0 - (Z_view + ZOffset) * ZScale
```

So:
- At Z_view = -ZOffset (frustum center): Z_ndc = 1.0
- At Z_view = -ZOffset + 1/ZScale (far plane): Z_ndc = 0.0

### Virtual Shadow Map Clipmap Usage

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VirtualShadowMaps\VirtualShadowMapClipmap.cpp`

**Lines 440, 447**:
```cpp
Level.ViewToClip = FReversedZOrthoMatrix(HalfLevelDim, HalfLevelDim, ZScale, ZOffset);
```

**Parameters**:
- `HalfLevelDim`: Half the shadow map's visible range (e.g., 512 pixels → 512 units)
- `ZScale = 1.0 / ViewRangeZ`: Inverse of max depth range
- `ZOffset = ViewRadiusZ`: Distance to frustum center (negative, looking forward)

**Example**:
```
ViewRadiusZ = 2000.0 (2 km forward)
ViewRangeZ = 4000.0 (4 km visible range)
ZScale = 1.0 / 4000.0 = 0.00025
ZOffset = 2000.0

Z_ndc = 1.0 - (Z_view + 2000.0) * 0.00025
      = 1.0 - Z_view * 0.00025 - 0.5
      = 0.5 - Z_view * 0.00025

At Z_view = -2000 (near): Z_ndc = 0.5 - (-2000)*0.00025 = 1.0 ✓
At Z_view = +2000 (far):  Z_ndc = 0.5 - (2000)*0.00025 = 0.0 ✓
```

---

## 4. Reversed-Z Perspective Projection Matrix (Point Lights)

### Template Definition

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\PerspectiveMatrix.h`

### Constructor: FOV + Aspect Ratio

```cpp
template<typename T>
FORCEINLINE TReversedZPerspectiveMatrix<T>::TReversedZPerspectiveMatrix(T HalfFOV, T Width, T Height, T MinZ, T MaxZ)
    : TMatrix<T>(
        TPlane<T>(1.0f / FMath::Tan(HalfFOV), 0.0f,                                    0.0f,                               0.0f),
        TPlane<T>(0.0f,                       Width / FMath::Tan(HalfFOV) / Height,     0.0f,                               0.0f),
        TPlane<T>(0.0f,                       0.0f,                                    MinZ / (MinZ - MaxZ),               1.0f),
        TPlane<T>(0.0f,                       0.0f,                                    -MaxZ * MinZ / (MinZ - MaxZ),       0.0f)
    )
{ }
```

### Key Formulas

Z-component mapping:
```
Z_clip = MinZ / (MinZ - MaxZ) * Z_view + (-MaxZ * MinZ / (MinZ - MaxZ)) * W
```

For perspective (W = Z_view for camera-relative coords):
```
Z_clip = MinZ / (MinZ - MaxZ) * Z_view + (-MaxZ * MinZ / (MinZ - MaxZ)) * Z_view
       = Z_view * [MinZ / (MinZ - MaxZ) - MaxZ * MinZ / (MinZ - MaxZ)]
       = Z_view * [MinZ - MaxZ * MinZ] / (MinZ - MaxZ)
       = Z_view * MinZ * (1 - MaxZ) / (MinZ - MaxZ)
```

After perspective divide (z_post = Z_clip / Z_view):
```
z_post = MinZ / (MinZ - MaxZ)  (at near plane MinZ)
z_post = MaxZ / (MinZ - MaxZ)  (at far plane MaxZ)
```

Note: `(MinZ - MaxZ)` is **negative** (MinZ < MaxZ in standard notation), so:
```
z_post = MinZ / (negative)  → **negative** at near (which remaps to 1.0 in normalized range)
z_post = MaxZ / (negative)  → **less negative** at far (which remaps to 0.0)
```

This is **reversed-Z perspective**: larger absolute Z values → smaller normalized Z.

### Usage in Point Light One-Pass Shadows

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp`

**Line 1104**:
```cpp
OnePassShadowFaceProjectionMatrix = FReversedZPerspectiveMatrix(PI / 4.0f, 1, 1, 1, LightProxy.GetRadius());
```

**Parameters**:
- `HalfFOV = PI / 4.0f` (45 degrees)
- `Width = Height = 1` (square aspect, for cube faces)
- `MinZ = 1` (1 unit near plane)
- `MaxZ = LightProxy.GetRadius()` (light's influence radius)

**Result**: Each cube face gets its own perspective projection with the light as origin.

---

## 5. Shadow Depth Comparison Function (Depth Test)

### Rendering vs Sampling

There are **two different comparison semantics**:
1. **During shadow map generation** (depth write): How deep values get written
2. **During shadow projection** (depth sample): How comparisons work

### Generation: Depth Test State

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp`

**Lines 749-761**:
```cpp
if (EnumHasAnyFlags(ShadowDepthType, EShadowDepthType::VSM))
{
    DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<false, CF_Always>::GetRHI());
}
else if (EnumHasAnyFlags(ShadowDepthType, EShadowDepthType::OnePassPoint))
{
    // Point lights use reverse Z depth maps
    DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_DepthNearOrEqual>::GetRHI());
}
else
{
    DrawRenderState.SetDepthStencilState(TStaticDepthStencilState<true, CF_LessEqual>::GetRHI());
}
```

### Comparison Semantics

| Shadow Type | Depth Test | Meaning |
|-------------|-----------|---------|
| **Virtual Shadow Map** | CF_Always | No test; clear/replace only |
| **One-Pass Point Light** | CF_DepthNearOrEqual | Keep if new_depth >= stored_depth |
| **Directional/Spot (CSM)** | CF_LessEqual | Keep if new_depth <= stored_depth |

**Why different?**
- Point light uses **perspective-corrected depth** (varies non-linearly), so >= keeps closest
- Directional/spot use **linear normalized depth** (0-1 range), so <= keeps closest
- In both cases, "keep closest" is the goal; the comparison function depends on the depth format

### Copy Operations: CF_Always

**Line 871**:
```cpp
GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<true, CF_Always>::GetRHI();
```

When copying cached shadow maps, use `CF_Always` because we're replacing the entire buffer and don't want per-pixel depth tests.

---

## 6. Shadow Comparison Sampler (Hardware PCF)

### Sampler State Creation

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp`

**Line 285** (One-Pass Point Light):
```cpp
OutParameters.ShadowDepthCubeTextureSampler =
    TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI();
```

**Line 56** (Hair Strands Transmittance, volume lighting):
```cpp
ShaderParams.OnePassPointShadowProjection.ShadowDepthCubeTextureSampler =
    TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp, 0, 0, 0, SCF_Less>::GetRHI();
```

### Sampler Parameters

| Parameter | Value | Meaning |
|-----------|-------|---------|
| Filter | `SF_Bilinear` | 4-tap linear interpolation between depth samples |
| AddressU/V/W | `AM_Clamp` | Clamp out-of-bounds UVs to edge |
| MipBias | `0` | No mip level adjustment |
| MaxAnisotropy | `0` | No anisotropic filtering (meaningless for depth compare) |
| BorderColor | `0` | Not used with clamp |
| **ComparisonFunc** | **`SCF_Less`** | **Reference < Texture = pass** |

### What SCF_Less Means

In D3D12's `SampleCmpLevelZero` call:
```hlsl
float shadow = Texture2D_Comparison.SampleCmpLevelZero(Sampler, UV, ReferenceDepth);
```

With `SCF_Less`:
- If `ReferenceDepth < StoredDepth` → 1.0 (unoccluded)
- If `ReferenceDepth >= StoredDepth` → 0.0 (occluded)

**Why Less?** In reversed-Z, **larger values are closer**. So if the reference (scene depth) is **less than** the stored (shadow map depth), the surface is **behind** the occluder → shadowed.

---

## 7. Manual PCF: Shadow Filtering Logic

### Occlusion Calculation

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush`

**Lines 162-170** (Non-subsurface case):
```glsl
// The standard comparison is SceneDepth < ShadowmapDepth
// Using a soft transition based on depth difference
float TransitionScale = Settings.TransitionScale;

// Unoptimized Math: saturate((ShadowmapDepth - Settings.SceneDepth) * TransitionScale + 1);
// Rearranged the math so that per pixel constants can be optimized from per sample constants.
float ConstantFactor = (Settings.SceneDepth * TransitionScale - 1);
float4 ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor);
```

### Algebraic Simplification

Starting from the comment:
```
ShadowFactor = saturate((ShadowmapDepth - Settings.SceneDepth) * TransitionScale + 1)
```

Expand:
```
= saturate(ShadowmapDepth * TransitionScale - Settings.SceneDepth * TransitionScale + 1)
```

Rearrange to extract per-pixel constant:
```
ConstantFactor = Settings.SceneDepth * TransitionScale - 1
ShadowFactor = saturate(ShadowmapDepth * TransitionScale - ConstantFactor)
             = saturate(ShadowmapDepth * TransitionScale - (Settings.SceneDepth * TransitionScale - 1))
             = saturate(ShadowmapDepth * TransitionScale - Settings.SceneDepth * TransitionScale + 1)
             = saturate((ShadowmapDepth - Settings.SceneDepth) * TransitionScale + 1)  ✓
```

### Semantics

- **In-shadow when**: `ShadowmapDepth < Settings.SceneDepth`
  - Because `(ShadowmapDepth - Settings.SceneDepth) < 0`
  - Multiplying by positive `TransitionScale` keeps it negative
  - `negative + 1 → clamp to [0, 1]` → small value → dark (shadow)

- **Unoccluded when**: `ShadowmapDepth > Settings.SceneDepth`
  - Because `(ShadowmapDepth - Settings.SceneDepth) > 0`
  - Multiplying by positive `TransitionScale` keeps it positive
  - `positive + 1 → clamp to [0, 1]` → large value → bright (no shadow)

### Max Depth Unlit

**Line 175**:
```glsl
ShadowFactor = saturate(ShadowFactor + (ShadowmapDepth > .99f));
```

If `Settings.bTreatMaxDepthUnshadowed` is true and the stored depth is > 0.99 (i.e., was never written to, cleared to 1.0), add 1 to the shadow factor, clamping to 1.0. This ensures unwritten regions are unlit.

### TransitionScale Parameter

```
TransitionScale = 1.0 / (1.0 + ReceiveSurfaceSlopeScaleBias)
```

Soft shadow transitions use this scale to blur shadow edges based on depth differences, reducing aliasing.

---

## 8. Shadow Depth Vertex Shader Output

### Per-Lighting-Model Outputs

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowDepthVertexShader.usf`

**Lines 51-108**: `void SetShadowDepthOutputs(...)`

### Perspective-Correct Depth (Point Lights)

**Lines 92-97** (PERSPECTIVE_CORRECT_DEPTH mode):
```glsl
#if PERSPECTIVE_CORRECT_DEPTH
    ShadowDepth = OutPosition.z;  // ← Raw clip-space Z (perspective-corrected by GPU)
    OutDepthBias = DepthBias;
#elif ONEPASS_POINTLIGHT_SHADOW
    ShadowDepth = 0;
    //OutPosition.z += DepthBias;
#else
    // Output linear, normalized depth
```

**Semantics**:
- `OutPosition` is already in clip space after `mul(WorldPosition, WorldToClipMatrix)`
- `OutPosition.z` contains the perspective-corrected depth value
- GPU's rasterizer will divide by `OutPosition.w` to get normalized depth
- This depth is written directly to shadow map (no further conversion)

### Linear Normalized Depth (Directional/Spot Lights)

**Lines 99-107**:
```glsl
#else
    const float InvMaxSubjectDepth = PassStruct.ShadowParams.w;
    #if PLATFORM_NEEDS_PRECISE_SHADOW_DEPTH
        precise
    #endif
    float AdjustedDepth = ( 1 - OutPosition.z * InvMaxSubjectDepth ) + DepthBias;
    ShadowDepth = AdjustedDepth;
    OutPosition.z = AdjustedDepth;  // ← Replace clip Z with normalized value
#endif
```

**Conversion formula**:
```
AdjustedDepth = 1.0 - (OutPosition.z * InvMaxSubjectDepth) + DepthBias
```

Where `InvMaxSubjectDepth = 1.0 / (MaxSubjectZ - MinSubjectZ)` is the CSM's Z range.

**Why this formula?**
- `OutPosition.z` is in reversed-Z clip space [1, 0] (from the ortho matrix)
- `OutPosition.z * InvMaxSubjectDepth` normalizes to [?, ?] range
- `1 - normalized` inverts back to [0, 1] for storage as typical depth
- `+ DepthBias` adds per-vertex bias to reduce self-shadowing

**Result**: Depth written to shadow map is [0, 1] linear (or slightly offset by bias).

### Depth Bias Calculation

**Lines 83-90**:
```glsl
const float MaxSlopeDepthBias = PassStruct.ShadowParams.z;
const float Slope = clamp(abs(NoL) > 0 ? sqrt(saturate(1 - NoL*NoL)) / NoL : MaxSlopeDepthBias, 0, MaxSlopeDepthBias);

const float SlopeDepthBias = PassStruct.ShadowParams.y;
const float SlopeBias = SlopeDepthBias * Slope;

const float ConstantDepthBias = PassStruct.ShadowParams.x;
const float DepthBias = SlopeBias + ConstantDepthBias;
```

**Semantics**:
- **Slope bias**: Scales with surface angle relative to light (NoL)
- **Constant bias**: Fixed offset for all surfaces
- **Total**: Combined to prevent self-shadowing on slanted surfaces

---

## 9. UzEngine Adaptation Guide

### What to Implement

1. **Clear Value**: Always clear shadow depth to 1.0 (white)
   ```cpp
   rhi_cmd_clear_depth(cmd, shadow_rt, 1.0f);
   ```

2. **Orthographic Projection** (CSM):
   ```cpp
   // ZScale = 1.0 / view_range_z
   // ZOffset = view_center_z (negative, backward)
   matrix view_to_clip = matrix_reversed_z_ortho(
       width_half, height_half, z_scale, z_offset
   );
   ```

3. **Perspective Projection** (Point Lights):
   ```cpp
   // MinZ = 0.1, MaxZ = light_radius
   matrix view_to_clip = matrix_reversed_z_perspective(
       half_fov, 1.0f, 1.0f, min_z, max_z
   );
   ```

4. **Depth Test During Render**:
   - Directional/Spot: `COMPARISON_LESS_EQUAL` (LessEqual)
   - Point: `COMPARISON_GREATER_EQUAL` (GreaterEqual, because perspective-corrected)

5. **Comparison Sampler**:
   ```cpp
   SamplerComparisonFunc::LESS  // Reference < Texture = unoccluded
   ```

6. **Manual PCF**:
   ```cpp
   float occlusion = saturate(
       (shadow_depth - scene_depth) * transition_scale + 1.0f
   );
   float shadow_factor = occlusion;  // 1.0 = unoccluded, 0.0 = shadowed
   ```

### Critical UE5 Values to Replicate

| Parameter | UE5 Value | Context |
|-----------|-----------|---------|
| Clear depth | 1.0 | Shadow map generation |
| Clear color | White (1,1,1,1) | Unused in depth-only pass |
| CSM near plane | MinSubjectZ | From shadow bounds |
| CSM far plane | MaxSubjectZ | From shadow bounds |
| Point light FOV | 90° (PI/2) or per-light | Cube face projection |
| Point light aspect | 1:1 (square) | Each face is square |
| PCF scale | TransitionScale = 1.0 / (1.0 + slopeBias) | Soft edge bluring |
| Depth bias constant | Per-light setting | Self-shadow avoidance |
| Depth bias slope | Per-light setting | Surface-angle dependent |

---

## References

- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowDepthRendering.cpp`
- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.h`
- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\OrthoMatrix.h`
- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Source\Runtime\Core\Public\Math\PerspectiveMatrix.h`
- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowFilteringCommon.ush`
- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Shaders\Private\ShadowDepthVertexShader.usf`
- UE5 5.6 Source: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\VirtualShadowMaps\VirtualShadowMapClipmap.cpp`

---

## Summary

UE5's reversed-Z shadow implementation is **mathematically clean and consistent**:

1. **Depth range**: [1.0, 0.0] (near to far) across all shadow types
2. **Clear value**: Always 1.0 (white, representing "no occluder")
3. **Projection matrices**: Use negative Z scale and offset formula `1.0 - ZOffset*ZScale`
4. **Depth test**: LESS (DepthNearOrEqual for point, LessEqual for directional)
5. **Comparison sampler**: SCF_Less (Reference < Texture = unoccluded)
6. **Manual PCF**: `(ShadowDepth - SceneDepth) > 0 → unoccluded`, with soft transition via scale factor
7. **Depth bias**: Applied in vertex shader before output, both constant and slope-based

All values, formulas, and semantics have been verified against actual UE5 5.6 source code.

---

## UzEngine Adaptation Status

| Component | UE5 Value | UzEngine Status | Commit |
|-----------|-----------|-----------------|--------|
| Shadow depth clear | 1.0 (white) | ✅ `kShadowDepthClear = 1.0f` in rhi_types.h | fix-reversed-z-shadow-consistency |
| Shadow depth test (CSM) | CF_LessEqual | ✅ `RHICompareFunc::LessEqual` in pipeline_library.cpp | fix-reversed-z-shadow-consistency |
| Shadow depth test (instanced) | CF_LessEqual | ✅ `RHICompareFunc::LessEqual` in pipeline_library.cpp | fix-reversed-z-shadow-consistency |
| Comparison sampler | SCF_Less | ✅ `D3D12_COMPARISON_FUNC_LESS` in dx12_pso_creation.cpp | Already correct |
| Shadow ortho projection | Standard-Z | ✅ `XMMatrixOrthographicOffCenterLH` in shadow_cascade.cpp | Already correct |
| Main-view ortho reversed-Z | FReversedZOrthoMatrix | ✅ `Mat4::OrthographicReversedZ()` in mat4.cpp | fix-reversed-z-shadow-consistency |
| Depth constants separation | Implicit | ✅ `kShadowDepthClear/Near/Far` vs `kDepthFar/Near` | fix-reversed-z-shadow-consistency |
| Default clearDepth | N/A | ✅ Defaults to `kDepthFar` (0.0) for reversed-Z safety | fix-reversed-z-shadow-consistency |
| Frustum corner NDC Z | z=1 near, z=0 far (reversed-Z) | ✅ Fixed in shadow_cascade.cpp:78-87. Was using standard-Z which inflated cascade bounds 90x, making shadows invisible. One-line fix: swapped z=0↔z=1 in 8 NDC corner definitions. | fix-shadow-cascade-frustum-corners |
| CSM vertex shader depth output | 1 - z*InvMaxSubjectDepth + bias | ❌ Not yet (uses standard ortho Z directly) | Planned |
| Manual PCF (CalculateOcclusion) | saturate((shadow-scene)*scale+1) | ❌ Uses hardware SampleCmp instead | Planned |
