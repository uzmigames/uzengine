<!-- doc-meta -->
# Shadow Depth Z-Convention Audit

**Status**: CRITICAL BUG FOUND

## Executive Summary

The point light shadow depth passes are executing correctly (78 draw calls logged), but shadows are not visible because of a **Z-convention mismatch**:

- **Shadow depth pipeline**: Configured for **standard-Z** (near=0, far=1)
- **Shadow camera projection**: Uses **reversed-Z** (near=1, far=0)
- **Result**: Depth comparison always fails → shadows inverted/invisible

## Root Cause Analysis

### 1. Shadow Depth Pipeline Configuration
**File**: `runtime/engine/renderer/core/pipeline/pipeline_shadow.cpp` lines 59-65

```cpp
desc.depthStencil.depthEnable         = true;
desc.depthStencil.depthWrite          = true;
desc.depthStencil.depthFunc           = RHICompareFunc::LessEqual;
// ...
desc.depthStencilFormat               = RHIFormat::D32Float;
```

Comment on line 61-62 claims "standard-Z (0=near, 1=far)" and references UE5 CF_LessEqual.

**Clear value** (frame_shadows.cpp line 375): `cmd->ClearDepthStencil(localAtlasTex, 1.0f, 0);`

For standard-Z, clearing to 1.0 is correct (far plane = no occlusion).

### 2. Shadow Camera Projection
**File**: `runtime/engine/renderer/frame/frame_constants.cpp` line 640

```cpp
faceCamera.SetHorizontalFOV(kCubeFovDeg, 1.0f, kPointShadowNear, ptShadow.radius);
const Mat4 faceViewProj = faceCamera.GetViewProjection();
```

### 3. Camera Projection Matrix
**File**: `runtime/engine/math/mat4.cpp` lines 51-65

```cpp
Mat4 Mat4::Perspective(float fov, float aspect, float near_plane, float far_plane) {
    // Reversed-Z perspective (standard): near maps to 1.0, far maps to 0.0.
    // Source: FReversedZPerspectiveMatrix in PerspectiveMatrix.h.
    // ...
    result.m._33 = -near_plane / range;                // Reversed-Z: maps near→1, far→0
    result.m._43 = (near_plane * far_plane) / range;   // Reversed-Z: positive offset
```

**CRITICAL**: `Mat4::Perspective()` explicitly uses **reversed-Z** (line 52 comment confirms this).

### 4. Depth Comparison

When sampling shadows in `shadow_common.hlsli`, the comparison uses:
- Comparison function: `LessEqual` (from PSO)
- Depth range: reversed-Z (near=1.0, far=0.0) from camera
- Atlas clear value: 1.0 (standard-Z convention)

The atlas is cleared to 1.0 (no occlusion in standard-Z), but camera output is in reversed-Z. When a pixel is at near plane, the camera outputs 1.0 (reversed-Z near). The comparison `1.0 LessEqual 1.0` passes, so the pixel appears **lit** (not shadowed).

For a pixel at far plane: camera outputs 0.0 (reversed-Z far). Comparison `0.0 LessEqual 1.0` passes — still lit. All pixels are lit.

## The Fix

**Option 1 (RECOMMENDED): Use Reversed-Z in shadow pipeline**

- Change PSO depth function to `RHICompareFunc::GreaterEqual` (matches reversed-Z)
- Change clear value to `0.0f` (reversed-Z far = no occlusion)
- Cite reversed-Z as the standard convention in UzEngine

**Option 2: Use standard-Z in shadow cameras**

- Add `Mat4::OrthographicReversedZ()` as `Mat4::OrthographicStandardZ()`
- Create a variant `Camera::SetPerspectiveStandardZ()` for shadow cameras only
- More complex, requires surgery on multiple systems

**Option 3 (NOT RECOMMENDED): Invert the comparison in shader**

- Flip the depth test in `shadow_common.hlsli`
- Fragile and hard to debug

## Recommendation

**Use Option 1**: The entire codebase already uses reversed-Z (main camera, deferred lighting, all shaders). The shadow pass should use the same convention.

## Changes Required

1. **pipeline_shadow.cpp**:
   - Change `depthFunc` to `RHICompareFunc::GreaterEqual`
   - Change comments to cite reversed-Z convention
   - Change `clearDepth` from `1.0f` to `0.0f`

2. **frame_shadows.cpp**:
   - Change `ClearDepthStencil()` call to use `0.0f`

3. **shadow_common.hlsli** (if sampling logic exists):
   - Verify comparison logic matches reversed-Z

## Verification

After fix:
- Rebuild runtime
- Run local shadows demo with `--camera-z 500` (test at various distances)
- Capture screenshots showing point light shadows on geometry
- Verify shadows darken surfaces correctly (not inverted)
