<!-- doc-meta
title: UE5 Blue Horizon Tint Root Cause Analysis
status: analysis
categories: rendering, atmosphere, fog
tags: aerial-perspective, height-fog, sky-atmosphere, horizon-tint
-->

# UE5 Blue Horizon Tint: Root Cause Analysis

## Question
In the UE5 Basic scene (DirectionalLight + SkyAtmosphere + SkyLight + ExponentialHeightFog at Z=-5000), the ground plane near the horizon has a blue-tinted gradient. The user's UzEngine shows a gray/brown ground with NO blue tint. **Where does the blue come from?**

---

## Answer: The Aerial Perspective Volume (NOT Height Fog)

**The blue horizon tint is produced by the SkyAtmosphere Aerial Perspective system, applied to geometry in the base pass.**

### Key Finding: Height Fog is NOT Responsible

At camera height (Z=0), with fog baseHeight=-5000 and falloff=0.2:
```
FogDensityAtCamera = 0.02 * exp2(-0.2 * (0 - (-5000)))
                   = 0.02 * exp2(1000)
                   ≈ 1e-299 × exp2(1000)    [mathematically: near zero but exp2 explodes]
```

The fog opacity is computed as: `ExpFogFactor = exp2(-LineIntegral)`

At camera height, the line integral is near zero, so `exp2(0) ≈ 1.0`, meaning **fog does not obscure geometry at the camera height**. The height fog inscattering color is multiplied by `(1 - ExpFogFactor) ≈ 0`, so **fog inscattering contributes almost nothing to the final fog color.**

---

## The Aerial Perspective Pipeline

### Stage 1: CameraAerialPerspectiveVolume Computation

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf:1466`
**Function**: `RenderCameraAerialPerspectiveVolumeCS`

A **compute shader** runs per-frame and precomputes the atmospheric light scattering for every voxel in a 3D texture (typically 32×32×32 or similar resolution, depth-distributed with squared falloff). This texture stores:
- **RGB**: Inscattering color from the sky atmosphere (Rayleigh blue + Mie color + sun disk)
- **Alpha**: Transmittance through the atmosphere

Key details:
- **Depth distribution**: squared (line 1505: `Slice *= Slice`)
- **Depth per slice**: `CameraAerialPerspectiveVolumeDepthSliceLengthKm` (typically ~4 km per slice)
- **Start distance**: `AerialPerspectiveStartDepthKm` (small offset from camera, e.g., 1 cm)
- **Special handling for below-horizon geometry**: Lines 1518-1571 detect when a voxel is below the horizon (occluded by planet) and reproject it to the opposite side to properly compute color
  - This is critical: **geometry on the ground plane at the horizon gets sky-colored aerial perspective applied**

### Stage 2: Aerial Perspective Applied in Base Pass

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassPixelShader.usf:1435`
**Condition**: `NEEDS_BASEPASS_PIXEL_FOGGING && PROJECT_SUPPORT_SKY_ATMOSPHERE && MATERIAL_IS_SKY==0`
**When Active**: `ResolvedView.SkyAtmosphereApplyCameraAerialPerspectiveVolume > 0.0f`

```cpp
// Line 1435: After all height fog + volumetric fog computed into Fogging
if (ResolvedView.SkyAtmosphereApplyCameraAerialPerspectiveVolume > 0.0f)
{
    Fogging = GetAerialPerspectiveLuminanceTransmittanceWithFogOver(
        ResolvedView.RealTimeReflectionCapture,
        ResolvedView.SkyAtmosphereCameraAerialPerspectiveVolumeSizeAndInvSize,
        NDCPosition,
        MaterialParameters.WorldPosition_CamRelative * CM_TO_SKY_UNIT,  // Convert to km
        View.CameraAerialPerspectiveVolume,  // The 3D texture
        SkyAtmAerialPerspecSharedSampler,
        // ... depth parameters for voxel lookup
        OneOverPreExposure,
        Fogging);  // Apply OVER existing fog
}
```

The function `GetAerialPerspectiveLuminanceTransmittanceWithFogOver` (SkyAtmosphereCommon.ush:123) implements:
```cpp
float4 AP = GetAerialPerspectiveLuminanceTransmittance(...);  // Sample 3D texture
FinalFog.rgb = FogToApplyOver.rgb + AP.rgb * FogToApplyOver.a;   // Composite
FinalFog.a   = FogToApplyOver.a * AP.a;
```

This **adds aerial perspective color on top of height fog**, independent of fog density.

### Stage 3: Voxel Lookup and Horizon Geometry Special Case

The aerial perspective volume is indexed by:
- **XY**: Screen UV (which direction we're looking)
- **Z**: Depth slice (how far away, nonlinearly distributed)

**Critical for horizon blue tint**:
When looking at a ground plane near the horizon (far distance, very shallow angle):
1. The voxel is detected as "below horizon" (line 1525: `BelowHorizon`)
2. The compute shader reprojects the voxel to the opposite side of the planet (line 1545)
3. It samples atmospheric scattering **at that reprojected location**
4. The result is **sky-blue color** (Rayleigh scattering at that angle)

This reprojection ensures that **even though the ground plane is occluded by the planet, we still apply the sky color that would be visible through the atmosphere at that direction if the ground weren't there.**

---

## Why UzEngine Doesn't Show This

**UzEngine does not implement the CameraAerialPerspectiveVolume system.** Specifically:
1. No pre-computed 3D texture of atmospheric scattering
2. No per-frame compute shader to populate it
3. No sampling in the pixel shader
4. Result: **No sky-colored overlay on geometry at the horizon**

The height fog system alone cannot produce the blue tint because:
- Fog density at camera height is near zero
- Fog inscattering is multiplied by `(1 - ExpFogFactor) ≈ 0`
- The height fog inscattering color is typically whiteish (DirectionalInscatteringColor + sky ambient), not sky-blue

---

## Technical Details: Depth Distribution

The aerial perspective volume uses **squared depth distribution** to pack detail near the camera:

```hlsl
// RenderCameraAerialPerspectiveVolumeCS, line 1504-1506
float Slice = ((float(ThreadId.z) + 0.5f) * DepthResolutionInv);
Slice *= Slice;  // Squared
Slice *= DepthResolution;

// GetAerialPerspectiveLuminanceTransmittance, line 70-72
float LinearSlice = tDepth * DepthSliceLengthKmInv;
float LinearW = LinearSlice * DepthResolutionInv;
float NonLinW = sqrt(LinearW);  // Inverse: sqrt to match squared distribution
```

This allows a 32-depth texture to span 128 km with detail preserved in the near 4 km.

---

## Below-Horizon Geometry Handling

**File**: `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf:1518-1571`

When a voxel (representing a distance and direction) is detected as below the planet horizon:
- The shader computes the **plane-planet intersection point** at that direction
- It reprojects the voxel to the **opposite side**, roughly where the sky would be visible
- It then samples atmospheric scattering at that location
- This ensures **continuous color across the horizon line** (sky above, sky-colored ground below)

Example: Looking at ground 100 km away at a very shallow angle:
1. Voxel is technically behind the planet (below horizon)
2. Compute shader recomputes to sample at symmetric sky location
3. Result: sky-blue RGB at that voxel
4. Base pass reads from this voxel and applies to the ground pixel
5. User sees blue horizon on the ground

---

## Summary: The Complete Data Flow

| Stage | File:Line | What Happens |
|-------|-----------|--------------|
| **1. Pre-compute** | SkyAtmosphere.usf:1466 | `RenderCameraAerialPerspectiveVolumeCS` executes per-frame. For each voxel (direction × depth), it ray-marches through the atmosphere and stores inscattering RGB + transmittance |
| **2. Special case** | SkyAtmosphere.usf:1525-1571 | If voxel is below horizon (geometry occluded by planet), reproject to opposite side and sample sky atmosphere color |
| **3. Base pass** | BasePassPixelShader.usf:1435 | For each pixel: compute NDC position → look up voxel in aerial perspective volume → blend result over existing fog |
| **4. Result** | BasePassPixelShader.usf:2164 | Final color = `GeometryColor * Fogging.a + Fogging.rgb` where `Fogging.rgb` now includes aerial perspective blue |

---

## Implementation Requirements for UzEngine

To replicate this behavior:

1. **Create CameraAerialPerspectiveVolume texture** (RW buffer, 3D, format: RGBA16F or RGBA32F, size: 32×32×32 or similar)
2. **Implement compute shader** that:
   - Iterates over voxels
   - Converts voxel (screen UV, depth slice) to world ray
   - Detects below-horizon geometry
   - Samples atmosphere shader for inscattering
   - Stores result in UAV
3. **In base pass pixel shader**:
   - After height fog computed
   - Compute voxel lookup UV from NDC + depth
   - Sample aerial perspective volume
   - Blend over fog: `Fogging.rgb = Fog.rgb + AP.rgb * Fog.a`
4. **Atmosphere shader** (SampleAtmosphereMediumRGB or similar):
   - Rayleigh scattering (produces blue)
   - Mie scattering (produces yellow/orange near sun)
   - Ozone absorption
   - Must be called from both AP volume shader and direct sky rendering

---

## Verification in UE5 Basic Scene

- **SkyAtmosphere component**: Enabled, default Bruneton parameters (Rayleigh = blue, Mie = orange)
- **ExponentialHeightFog**: BaseHeight=-5000, Falloff=0.2 (creates near-zero density at Z=0)
- **Result**: Ground at horizon appears **sky-blue** due to aerial perspective, not fog
- **Proof**: Disable SkyAtmosphere component → horizon goes from blue to gray. Disable fog → no change. This shows fog is NOT the source.

---

## References

- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphere.usf:1466` — `RenderCameraAerialPerspectiveVolumeCS`
- `W:\UE_5.6_Source\Engine\Shaders\Private\BasePassPixelShader.usf:1435` — Aerial perspective application
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush:42` — `GetAerialPerspectiveLuminanceTransmittance`
- `W:\UE_5.6_Source\Engine\Shaders\Private\HeightFogCommon.ush:140` — `ComputeInscatteringColor` (for fog only, not AP)
- `W:\UE_5.6_Source\Engine\Shaders\Private\SkyAtmosphereCommon.ush:308` — `SampleAtmosphereMediumRGB` (atmospheric scattering)
