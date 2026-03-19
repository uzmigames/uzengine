<!-- doc-meta
type: quick-reference
title: Cascaded Shadow Flickering — Root Cause & Fix
tags: shadows, rendering, stability
-->

# Cascaded Shadow Flickering — Root Cause & Fix

## The Problem

UzEngine shadows flicker (appear/disappear) when moving the camera. The flicker is most visible at large world scales or with certain camera angles.

## Root Cause

**Matrix inversion precision loss** in `ComputeFrustumCorners()`.

The current implementation:
```cpp
const Mat4 invViewProj = (cameraView * cameraProj).Inverse();  // IMPRECISE
for (uint32_t i = 0; i < 8; ++i) {
    Vec4 world = invViewProj * ndc;  // Error amplified here
    worldCorners[i] = Vec3{world.x / world.w, world.y / world.w, world.z / world.w};
}
// Then interpolates along rays — small errors become systematic
```

**Why it fails**:
1. Matrix inversion is numerically imprecise, especially at large world scales
2. Unprojected corner positions drift slightly frame-to-frame as camera moves
3. Interpolation along near-far rays amplifies this drift
4. Cascade orthographic projections snap to these drifting positions
5. Result: visible shimmer as shadow boundaries jitter

## The Solution (UE5 Approach)

Compute frustum corners directly from camera parameters — **no matrix inversion**:

```cpp
// Extract camera axes (stable — these come from stable matrices)
Vec3 cameraDir = viewMatrix[2];  // Forward
Vec3 cameraRight = viewMatrix[0];  // Right
Vec3 cameraUp = viewMatrix[1];  // Up
Vec3 cameraPos = viewMatrix[3];  // Position (or extract from view matrix inverse)

// Extract FOV from projection (stable — simple atan operations)
float halfVFOV = atan(1.0f / proj(1,1));
float aspect = proj(1,1) / proj(0,0);
float halfHFOV = atan(tan(halfVFOV) * aspect);

// Compute half-dimensions at split planes
float nearH = 2.0f * splitNear * tan(halfVFOV);
float nearW = nearH * aspect;
float farH = 2.0f * splitFar * tan(halfVFOV);
float farW = farH * aspect;

// Build 8 corners via direct linear combination (no inversion)
Vec3 nearCenter = cameraPos + cameraDir * splitNear;
Vec3 farCenter = cameraPos + cameraDir * splitFar;

corners[0] = nearCenter - cameraRight * (nearW/2) - cameraUp * (nearH/2);  // Near BL
corners[1] = nearCenter + cameraRight * (nearW/2) - cameraUp * (nearH/2);  // Near BR
corners[2] = nearCenter + cameraRight * (nearW/2) + cameraUp * (nearH/2);  // Near TR
corners[3] = nearCenter - cameraRight * (nearW/2) + cameraUp * (nearH/2);  // Near TL
corners[4] = farCenter - cameraRight * (farW/2) - cameraUp * (farH/2);   // Far BL
corners[5] = farCenter + cameraRight * (farW/2) - cameraUp * (farH/2);   // Far BR
corners[6] = farCenter + cameraRight * (farW/2) + cameraUp * (farH/2);   // Far TR
corners[7] = farCenter - cameraRight * (farW/2) + cameraUp * (farH/2);   // Far TL
```

**Why this works**:
- No matrix inversion (no precision loss)
- Uses camera parameters directly (already stable)
- Avoids interpolation (no error amplification)
- Handles asymmetric projections naturally
- Frame-to-frame stability guaranteed

## Implementation Checklist

- [ ] Rewrite `ComputeFrustumCorners()` in `cascade.cpp` to use direct parameter approach
- [ ] Extract camera axes from view matrix directly (GetColumn or equivalent)
- [ ] Extract FOV from projection using atan operations
- [ ] Build corners via linear combination (8 direct Vec3 computations)
- [ ] Remove all references to `invViewProj`
- [ ] Test with camera movement at various scales
- [ ] Verify shadows stay stable during rapid camera rotation
- [ ] Commit and document the fix

## References

- UE5 Implementation: `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\Private\Components\DirectionalLightComponent.cpp` (lines 832-862)
- UE5 Shadow Setup: `W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private\ShadowSetup.cpp` (lines 2926-2965)
- Full Analysis: `.claude/agent-memory/cascade-shadow-analysis.md`

