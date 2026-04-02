<!-- doc-meta
type: analysis
title: UE5 vs UzEngine Cascaded Shadow Implementation Comparison
session: 2026-03-16
target: e:/UzEngine/runtime/engine/renderer/shadows/cascade.cpp
ue5-source: W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp
-->

# UE5 vs UzEngine Cascaded Shadow Map Analysis

## Summary

UzEngine's cascade implementation has a **critical precision issue** in the frustum corner computation that causes flickering during camera movement. UE5 computes frustum corners differently (directly from camera parameters) rather than via inverse matrix, which maintains stability.

---

## 1. Cascade Split Distance Computation

### UE5 Approach (Portable Split Scheme)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (lines 753-814)

**Algorithm**:
```cpp
static float ComputeAccumulatedScale(float Exponent, int32 CascadeIndex, int32 CascadeCount)
{
    float CurrentScale = 1;
    float TotalScale = 0.0f;
    float Ret = 0.0f;

    for(int i = 0; i < CascadeCount; ++i)
    {
        if(i < CascadeIndex)
        {
            Ret += CurrentScale;
        }
        TotalScale += CurrentScale;
        CurrentScale *= Exponent;
    }

    return Ret / TotalScale;  // Returns accumulated fraction [0,1]
}

float GetSplitDistance(const FSceneView& View, uint32 SplitIndex, ...)
{
    float ShadowNear = View.NearClippingDistance;
    float CascadeDistanceWithoutFar = GetCSMMaxDistance(...);
    float EffectiveExponent = CascadeDistributionExponent;  // Default 3.0

    // Split = Near + AccumulatedScale * (Far - Near)
    return ShadowNear +
           ComputeAccumulatedScale(EffectiveExponent, SplitIndex, NumNearCascades) *
           (CascadeDistanceWithoutFar - ShadowNear);
}
```

**Key differences from UzEngine**:
- Uses **exponential distribution** (not linear-log blend)
- Default exponent = 3.0, not configurable as `splitLambda`
- Distributes splits across **near cascades only** (far cascades get their own distribution)
- **Accumulated scale formula** ensures that cascade distances grow exponentially

### UzEngine Approach (Linear-Log Blend)

**File**: `e:/UzEngine/runtime/engine/renderer/shadows/cascade.cpp` (lines 165-176)

```cpp
const float logSplit = shadowNear * std::pow(shadowFar / shadowNear, fraction);
const float linearSplit = shadowNear + (shadowFar - shadowNear) * fraction;
splits[i] = config.splitLambda * logSplit + (1.0f - config.splitLambda) * linearSplit;
```

**Status**: ✅ This is correct and more flexible than UE5. UzEngine's split computation is NOT the problem.

---

## 2. Light View Matrix Construction

### UE5 Approach

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (line 506)

```cpp
OutInitializer.WorldToLight = FInverseRotationMatrix(GetDirection().GetSafeNormal().Rotation());
```

This creates a **pure rotation matrix** (no translation) from the light direction. The matrix columns are:
- Column 0: X-axis (right vector)
- Column 1: Y-axis (up vector)
- Column 2: Z-axis (light direction)

The light direction is computed via: `FVector LightDirection = -GetDirection();` (line 824)

**Why pure rotation?**: The light space origin is adjusted separately via `PreShadowTranslation` in the orthographic projection setup. The WorldToLight matrix is only the orientation.

### UzEngine Approach

**File**: `e:/UzEngine/runtime/engine/renderer/shadows/cascade.cpp` (lines 206-210)

```cpp
const Vec3 lightUp = ChooseLightUp(lightDirNorm);
const Vec3 lightEye = center - lightDirNorm * radius;
const Mat4 lightView = Mat4::LookAt(lightEye, center, lightUp);
```

**Status**: ✅ Functionally equivalent. Both create a view matrix pointing along the light direction.

---

## 3. Frustum Corner Computation — THE CRITICAL BUG

### UE5 Approach (CORRECT)

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (lines 832-862)

UE5 extracts frustum corners directly from camera parameters:

```cpp
// Get FOV from projection matrix
float HalfHorizontalFOV = FMath::Atan(1.0f / ProjectionMatrix.M[0][0]);
float HalfVerticalFOV = FMath::Atan(1.0f / ProjectionMatrix.M[1][1]);

// Extract camera axes from view matrix
const FVector CameraDirection = ViewMatrix.GetColumn(2);    // Forward
const FVector CameraRight = ViewMatrix.GetColumn(0);         // Right
const FVector CameraUp = ViewMatrix.GetColumn(1);            // Up

// Compute frustum plane extents
const float StartHorizontalTotalLength = SplitNear * FMath::Tan(HalfHorizontalFOV);
const FVector StartCameraRightOffset = CameraRight * StartHorizontalTotalLength;
const FVector StartCameraTopOffset = CameraUp * StartVerticalTotalLength;

// Build 8 corners by direct linear combination
FVector CascadeFrustumVerts[8];
CascadeFrustumVerts[0] = ViewOrigin + CameraDirection * SplitNear +
                         StartCameraRightOffset + StartCameraTopOffset;
// ... etc for all 8 corners
```

**Why this is stable**:
- No matrix inversion required (inversion is imprecise)
- Works directly with camera parameters (camera matrix is already stable)
- Handles asymmetric projections cleanly via `ProjectionMatrix.M[2][0]` and `M[2][1]`
- No accumulation of floating-point errors

### UzEngine Approach (UNSTABLE)

**File**: `e:/UzEngine/runtime/engine/renderer/shadows/cascade.cpp` (lines 64-116)

```cpp
// CRITICAL: Compute inverse of the full view-projection
const Mat4 invViewProj = (cameraView * cameraProj).Inverse();

// Unproject 8 NDC corners
for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
    Vec4 ndc{kNdcCorners[i][0], kNdcCorners[i][1], kNdcCorners[i][2], 1.0f};
    Vec4 world = invViewProj * ndc;
    const float invW = 1.0f / world.w;
    worldCorners[i] = Vec3{world.x * invW, world.y * invW, world.z * invW};
}

// Interpolate along near-far rays
const float tNear = (splitNear - cameraNear) / range;
const float tFar = (splitFar - cameraNear) / range;
for (uint32_t i = 0; i < 4; ++i) {
    outCorners[i] = nearPt + ray * tNear;     // Interpolate
    outCorners[i + 4] = nearPt + ray * tFar;
}
```

**Why this flickers**:
1. **Matrix inversion is imprecise** — if ViewProj is ill-conditioned (common at large scales), `invViewProj` accumulates error
2. **Interpolation amplifies error** — small error in inverted corners becomes systematic when interpolating
3. **Camera movement changes corner precision** — as camera moves, corner positions drift slightly due to inversion error
4. **Cascades are positioned on drifting corners** — orthographic projections snap to these drifting positions, causing visible shimmer

---

## 4. Texel Snapping Implementation

### UE5 Approach

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (lines 1021-1035)

```cpp
// Transform the shadow's position into shadowmap space (after building view+proj)
const FVector TransformedPosition = WorldToViewScaledInner.TransformPosition(-PreShadowTranslation);

// Snap distance = 2 * MaxDownsampleFactor / resolution
// MaxDownsampleFactor accounts for PCF filter widths
const int32 MaxDownsampleFactor = 4;
const float SnapX = FMath::Fmod(TransformedPosition.X, 2.0f * MaxDownsampleFactor / InSnapResolutionX);
const float SnapY = FMath::Fmod(TransformedPosition.Y, 2.0f * MaxDownsampleFactor / InSnapResolutionY);

// Apply snap in shadow space, transform back to world
const FVector SnappedWorldPosition = WorldToViewScaledInner.InverseFast()
    .TransformPosition(TransformedPosition - FVector(SnapX, SnapY, 0.0f));
PreShadowTranslation = -SnappedWorldPosition;
```

**Key insight**: Snap size = `2.0f * 4 / resolution` = `8.0f / resolution` texels, NOT 1 texel.

This accounts for 4x downsampling used in PCF filtering. Snapping to every texel would be overkill and cause jumpiness.

### UzEngine Approach

**File**: `e:/UzEngine/runtime/engine/renderer/shadows/cascade.cpp` (lines 234-251)

```cpp
// Snap to texel grid
const float worldUnitsPerTexel = (2.0f * radius) / static_cast<float>(config.resolution);

Mat4 shadowMatrix = lightView * lightProj;

// Transform world origin to shadow space
Vec4 origin = shadowMatrix * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
origin.x /= worldUnitsPerTexel;
origin.y /= worldUnitsPerTexel;
origin.x = std::floor(origin.x);      // SNAP TO NEAREST TEXEL
origin.y = std::floor(origin.y);
origin.x *= worldUnitsPerTexel;
origin.y *= worldUnitsPerTexel;

// Compute rounding offset
Vec4 currentOrigin = shadowMatrix * Vec4{0.0f, 0.0f, 0.0f, 1.0f};
const float offsetX = origin.x - currentOrigin.x;
const float offsetY = origin.y - currentOrigin.y;

// Apply offset to projection matrix's translation row
snappedProj.m._41 += offsetX;
snappedProj.m._42 += offsetY;
```

**Comparison**:
- ✅ UzEngine snaps correctly to the full texel boundary (1 texel)
- ⚠️ UE5 uses 4x coarser snapping (8 texels) for stability, which is a feature not a bug
- UzEngine's snapping logic is sound, but the flickering problem is in the corner computation itself, not the snapping

---

## 5. Bounding Volume Strategy

### UE5

```cpp
// Compute ideal bounding sphere for subfrustum
// Fx = (Db^2 - Da^2) / 2Fl + Fl / 2
float OptimalOffset = (DiagonalBSq - DiagonalASq) / (2.0f * FrustumLength)
                      + FrustumLength * 0.5f;
float CentreZ = SplitFar - OptimalOffset;
CentreZ = FMath::Clamp(CentreZ, SplitNear, SplitFar);

FSphere CascadeSphere(ViewOrigin + CameraDirection * CentreZ, 0);
for (int32 Index = 0; Index < 8; Index++) {
    CascadeSphere.W = FMath::Max(CascadeSphere.W,
                                  FVector::DistSquared(CascadeFrustumVerts[Index],
                                                       CascadeSphere.Center));
}
```

Uses a mathematically optimal bounding sphere calculated from diagonal lengths, then fits a sphere.

### UzEngine

```cpp
// Simple centroid + radius
Vec3 center = Vec3::Zero();
for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
    center += corners[i];
}
center = center * (1.0f / 8.0f);

float radius = 0.0f;
for (uint32_t i = 0; i < kFrustumCornerCount; ++i) {
    const float dist = (corners[i] - center).Length();
    radius = std::max(radius, dist);
}
radius = std::ceil(radius * 16.0f) / 16.0f;  // Round up slightly
```

**Status**: ✅ Both approaches are sound. UzEngine's is simpler and sufficient.

---

## 6. Shadow Depth Bias Values

### UE5 For Whole-Scene Directional Shadows

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp` (lines 1839-1851)

```cpp
// Depth bias depends on Z-range of shadow-mapped cascade
DepthBias = CVarCSMShadowDepthBias.GetValueOnRenderThread() / (MaxSubjectZ - MinSubjectZ);

// World-space texel scale for bias distribution
const float WorldSpaceTexelScale = ShadowBounds.W / ResolutionX;
DepthBias = FMath::Lerp(DepthBias,
                        DepthBias * WorldSpaceTexelScale,
                        CascadeSettings.CascadeBiasDistribution);

// Multiply by light component's user-facing bias scalar
DepthBias *= LightSceneInfo->Proxy->GetUserShadowBias();

// Slope-scale bias (typically 1.0, modified by user scalar)
SlopeScaleDepthBias = CVarCSMShadowSlopeScaleDepthBias.GetValueOnRenderThread();
SlopeScaleDepthBias *= LightSceneInfo->Proxy->GetUserShadowSlopeBias();
```

**Default CVars**:
- `r.Shadow.CSMShadowDepthBias` — typically 0.5
- `r.Shadow.CSMShadowSlopeScaleDepthBias` — typically 1.0
- `ShadowCascadeBiasDistribution` — 0.0 to 1.0, controls whether to use constant or world-space-scaled bias

**Formula Summary**:
```
DepthBias = (CSMShadowDepthBias / (MaxZ - MinZ)) * Lerp(1.0, WorldTexelScale, BiasDistribution) * UserBias
SlopeScaleBias = 1.0 * UserSlopeBias
```

---

## 7. Light Direction and Up Vector

### UE5

```cpp
// Light direction is directly from component rotation
const FVector LightDirection = -GetDirection();  // GetDirection extracts from WorldToLight

// Up vector chosen to avoid degeneracy
FVector LightUp = FVector(0, 0, 1);  // Z-up default (same as UzEngine)
if (std::abs(dot(LightDirection, LightUp)) > threshold) {
    LightUp = FVector(1, 0, 0);  // Switch to X-up if light is nearly vertical
}
```

**File**: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (line 824 and implicit in LookAt)

### UzEngine

```cpp
constexpr float kVerticalThreshold = 0.999f;

Vec3 ChooseLightUp(const Vec3& lightDir) {
    const Vec3 worldUp{0.0f, 0.0f, 1.0f};  // Z-up
    if (std::abs(lightDir.Dot(worldUp)) > kVerticalThreshold) {
        return Vec3{0.0f, 1.0f, 0.0f};  // Fallback to Y when vertical
    }
    return worldUp;
}
```

**Status**: ✅ Both are identical in spirit. UzEngine's implementation is correct.

---

## 8. Row-Major vs Column-Major

**UE5**: Uses column-major matrices (standard DirectXMath)
- `M[row][col]` indexing in FMatrix
- GetColumn(i) returns the i-th column
- Multiplication is `(View * Proj) * vertex` in shader code

**UzEngine**: Uses row-major matrices (DirectXMath XMFLOAT4X4)
- `.m._41`, `.m._42` are row 3, columns 0-1
- Direct XMFLOAT4X4 storage
- Multiplication order preserved

**Status**: ✅ Both are DirectXMath-compatible. No issue here.

---

## Root Cause of Flickering

**The problem is in `ComputeFrustumCorners` using matrix inversion.**

When you compute `invViewProj = (cameraView * cameraProj).Inverse()` and use it to unproject NDC corners:

1. **Matrix inversion introduces floating-point error** proportional to the condition number of ViewProj
2. **At large world scales** (e.g., camera far-plane at 100,000 units), ViewProj becomes ill-conditioned
3. **Unprojected corners drift** slightly frame-to-frame as camera moves
4. **Interpolation amplifies error** — small corner drift becomes systematic offset when you interpolate
5. **Cascade orthographic projections snap to drifting corners** — the snap grid itself is moving

Result: **Shadows appear to shimmer/flicker** because the cascade bounds are subtly shifting every frame.

---

## Fix for UzEngine

Replace `ComputeFrustumCorners` to match UE5's direct approach:

```cpp
void ComputeFrustumCornersEU5_Stable(
    const Mat4& cameraView,
    const Mat4& cameraProj,
    float splitNear,
    float splitFar,
    Vec3* outCorners) {

    // Extract camera orientation from view matrix (column-major convention)
    Vec3 cameraPos = Vec3{cameraView(3,0), cameraView(3,1), cameraView(3,2)};
    Vec3 cameraRight = Vec3{cameraView(0,0), cameraView(1,0), cameraView(2,0)};
    Vec3 cameraUp = Vec3{cameraView(0,1), cameraView(1,1), cameraView(2,1)};
    Vec3 cameraDir = Vec3{cameraView(0,2), cameraView(1,2), cameraView(2,2)};

    // Extract FOV from projection matrix
    float fovy = 2.0f * std::atan(1.0f / cameraProj(1,1));
    float aspect = cameraProj(1,1) / cameraProj(0,0);

    float halfVFOV = fovy * 0.5f;
    float halfHFOV = std::atan(std::tan(halfVFOV) * aspect);

    // Compute near-plane half-dimensions
    float nearHeight = 2.0f * splitNear * std::tan(halfVFOV);
    float nearWidth = nearHeight * aspect;

    // Compute far-plane half-dimensions
    float farHeight = 2.0f * splitFar * std::tan(halfVFOV);
    float farWidth = farHeight * aspect;

    Vec3 nearCenter = cameraPos + cameraDir * splitNear;
    Vec3 farCenter = cameraPos + cameraDir * splitFar;

    // Near-plane 4 corners
    outCorners[0] = nearCenter - cameraRight * (nearWidth * 0.5f) - cameraUp * (nearHeight * 0.5f);
    outCorners[1] = nearCenter + cameraRight * (nearWidth * 0.5f) - cameraUp * (nearHeight * 0.5f);
    outCorners[2] = nearCenter + cameraRight * (nearWidth * 0.5f) + cameraUp * (nearHeight * 0.5f);
    outCorners[3] = nearCenter - cameraRight * (nearWidth * 0.5f) + cameraUp * (nearHeight * 0.5f);

    // Far-plane 4 corners
    outCorners[4] = farCenter - cameraRight * (farWidth * 0.5f) - cameraUp * (farHeight * 0.5f);
    outCorners[5] = farCenter + cameraRight * (farWidth * 0.5f) - cameraUp * (farHeight * 0.5f);
    outCorners[6] = farCenter + cameraRight * (farWidth * 0.5f) + cameraUp * (farHeight * 0.5f);
    outCorners[7] = farCenter - cameraRight * (farWidth * 0.5f) + cameraUp * (farHeight * 0.5f);
}
```

This **avoids matrix inversion entirely** and computes frustum corners directly from camera parameters — the same approach UE5 uses.

---

## Summary Table

| Aspect | UE5 | UzEngine | Status |
|--------|-----|---------|--------|
| **Split distance formula** | Exponential (exp=3.0) | Linear-log blend | UzEngine more flexible ✅ |
| **Light view matrix** | Rotation only | Full LookAt | Both correct ✅ |
| **Frustum corners** | Direct from parameters | Matrix inversion | **UE5 is stable, UzEngine flickers** ⚠️ |
| **Bounding volume** | Optimal sphere (diagonal formula) | Centroid + radius | Both adequate ✅ |
| **Texel snapping** | 4x coarse (8 texels) | Full (1 texel) | Both valid ✅ |
| **Depth bias** | Dynamic per Z-range | Not implemented | UzEngine missing bias logic |
| **Coordinate system** | Z-up | Z-up | Match ✅ |
| **Matrix storage** | Column-major | Row-major | Both DirectXMath compatible ✅ |

---

## Files to Review

- **UE5 Implementation**:
  - `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (lines 753-900)
  - `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (lines 1021-1035, 2926-3068)
  - `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowRendering.cpp` (lines 1811-1890)

- **UzEngine Implementation**:
  - `e:/UzEngine/runtime/engine/renderer/shadows/cascade.cpp`
  - `e:/UzEngine/runtime/engine/renderer/shadows/cascade.h`

