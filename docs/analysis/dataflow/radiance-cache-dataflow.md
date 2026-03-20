<!-- doc-meta
category: rendering
status: complete
author: unreal-architect
date: 2026-03-19
-->

# Radiance Cache Data Flow — CPU to Shader

## Problem Statement

UzEngine's radiance cache produces valid SH coefficients in probes, but the scene renders too dark (1/π darker than expected). This document traces the exact data flow from probe storage → GPU sampling → final compositing to identify where the missing factors are.

## Data Flow Table

| Stage | UE5 File | Function | Input | Output | Format |
|-------|----------|----------|-------|--------|--------|
| **1. Probe Tracing** | `LumenRadianceCache.usf:210-350` | `TraceProbesCS` | World rays from probe | float3 accumulated light | RGB HDR |
| **2. Irradiance Conv** | `LumenRadianceCache.cpp:LumenRadianceCacheUpdate()` | `AccumulateProbeIrradiance()` | Accumulated light | `float3 / π` | Irradiance |
| **3. Atlas Storage** | `LumenRadianceCacheInterpolation.ush:56` | CPU texture upload | Irradiance | `R11G11B10 or float3` | Atlas |
| **4. Probe Sampling** | `LumenRadianceCacheInterpolation.ush:221-269` | `SampleRadianceCacheProbe()` | Direction (normal) | float3 irradiance | RGB HDR |
| **5. Parallax Corr** | `LumenRadianceCacheInterpolation.ush:277-347` | `SampleRadianceCacheProbeWithParallaxCorrection()` | World pos + dir | Irradiance × CorrectionFactor | RGB HDR |
| **6. Trilinear Interp** | `LumenRadianceCacheInterpolation.ush:418-436` | Trilinear lerp of 8 corners | 8 probe samples | Interpolated irradiance | RGB HDR |
| **7. Sky Resolution** | `LumenRadianceCacheInterpolation.ush:358-370` | `RadianceCacheResolveSky()` | SkyVisibility flag | Lerp(cache, sky) | RGB HDR |
| **8. Material Apply** | `DiffuseIndirectComposite.usf:520-530` | Read GBuffer | Roughness, albedo, AO | Occlusion terms | float |
| **9. Diffuse Comp** | `DiffuseIndirectComposite.usf:588` | Main shader | Irradiance × albedo | Diffuse output | RGB HDR |
| **10. Final Blend** | `DiffuseIndirectComposite.usf:620-621` | Accumulate to frame | Scene + diffuse | Final color | RGB LDR |

## Component Breakdown

### Stage 1-3: Probe Generation (CPU-side, already working in UzEngine)

**What happens**:
- Ray trace from each probe position
- Accumulate incoming radiance, weighted by `cos(theta)`
- **Divide by π** to convert to irradiance
- Store in atlas

**UzEngine status**: Assumed working (you have valid SH probes).

**Critical**: If you are storing SH coefficients instead of irradiance, this is the root cause of the `/π` error.

---

### Stage 4: Probe Sampling (Shader-side)

**UE5 Code** (`LumenRadianceCacheInterpolation.ush:221-269`):

```hlsl
FRadianceCacheSample SampleRadianceCacheProbe(
    uint ProbeIndex,
    float3 WorldSpaceDirection,  // ← Surface normal
    float MipLevel)
{
    float2 ProbeUV = InverseEquiAreaSphericalMapping(WorldSpaceDirection);
    uint2 ProbeAtlasCoord = FinalProbeResolution * uint2(
        ProbeIndex & ProbeAtlasResolutionModuloMask,
        ProbeIndex >> ProbeAtlasResolutionDivideShift);
    float2 ProbeTexelCoord = ProbeUV * RadianceProbeResolution + (1u << FinalRadianceAtlasMaxMip);
    float2 ProbeAtlasUV = (ProbeAtlasCoord + ProbeTexelCoord) * InvProbeFinalRadianceAtlasResolution;

    float3 Radiance = RadianceCacheFinalRadianceAtlas.SampleLevel(
        GlobalBilinearClampedSampler,
        ProbeAtlasUV,
        MipLevel)
        * RadianceCacheOneOverCachedLightingPreExposure;  // ← Pre-exposure correction

    return Radiance;
}
```

**What this does**:
1. Convert surface normal → equirectangular UV
2. Convert UV → atlas texture coordinates
3. Sample irradiance texture (bilinear, with mip)
4. Apply pre-exposure correction

**Expected output**: `float3` irradiance (already pre-multiplied by `/π`)

**UzEngine check**:
- Are you sampling an irradiance atlas, or SH atlas?
- Are you applying `InverseEquiAreaSphericalMapping` correctly?
- Are you using the correct mip level?

---

### Stage 5: Parallax Correction

**UE5 Code** (`LumenRadianceCacheInterpolation.ush:277-347`):

```hlsl
float ReprojectionRadius = ReprojectionRadiusScale * ProbeTMin;
float T = RayIntersectSphere(WorldSpacePosition, WorldSpaceDirection,
    float4(ProbeWorldPosition, ReprojectionRadius)).y;
float3 IntersectionPosition = WorldSpacePosition + WorldSpaceDirection * T;
float3 ReprojectedDirection = IntersectionPosition - ProbeWorldPosition;
float CorrectionFactor = T * T / (ReprojectionRadius * dot(ReprojectedDirection, WorldSpaceDirection));

FRadianceCacheSample Sample = SampleRadianceCacheProbe(ProbeIndex, ReprojectedDirection, MipLevel);
Sample.Radiance *= CorrectionFactor;  // ← SCALE the sampled irradiance
```

**What this does**:
1. Compute sphere intersection with reprojection sphere
2. Compute attenuation factor to account for distance mismatch
3. Lookup direction changes slightly
4. **Scale** sampled irradiance by `CorrectionFactor`

**UzEngine check**:
- Are you computing `CorrectionFactor` correctly?
- Are you **multiplying** the sampled value by this factor?
- Are you using the correct `ReprojectionRadiusScale` (typically ~0.5)?

---

### Stage 6: Trilinear Interpolation

**UE5 Code** (`LumenRadianceCacheInterpolation.ush:418-436`):

```hlsl
// 8-corner trilinear interpolation
float3 InterpolatedIrradiance = 0;
for (int corner = 0; corner < 8; corner++) {
    float3 cornerIrrad = SampleRadianceCacheProbeWithParallaxCorrection(...);
    InterpolatedIrradiance += cornerIrrad * trilinearWeights[corner];
}
```

**What this does**: Simple trilinear interpolation in grid space (linear in position, linear in radiance values).

**UzEngine check**:
- Are you interpolating in the correct space?
- Are your weights computed correctly?

---

### Stage 7: Sky Visibility Resolution

**UE5 Code** (`LumenRadianceCacheInterpolation.ush:358-370`):

```hlsl
if (SkyVisibility > 0.0f && ReflectionStruct.SkyLightParameters.y > 0) {
    float3 SkyRadiance = GetSkyLightReflection(WorldSpaceDirection, 0.0f, 1.0f);
    Sample.Radiance = lerp(Sample.Radiance, SkyRadiance, SkyVisibility);
}
```

**What this does**:
- If probe has `SkyVisibility > 0`, the ray didn't hit geometry in that direction
- Evaluate sky environment map at runtime
- **Blend** cache + sky radiance

**Important**: `GetSkyLightReflection()` returns **irradiance** (not radiance), so no conversion needed.

**UzEngine check**:
- Are you evaluating sky lighting at runtime?
- Are you applying `SkyVisibility` as a blend weight correctly?

---

### Stages 8-9: Final Compositing (THE CRITICAL BUG)

**UE5 Code** (`DiffuseIndirectComposite.usf:520-596`):

```hlsl
// Line 520: Read material albedo
float3 DiffuseColor = GBuffer.DiffuseColor;  // ← Albedo, NOT divided by π

// Line 527: Get occlusion (AO, bentNormal visibility)
FShadingOcclusion Occlusion = GetShadingOcclusion(...);  // → DiffuseOcclusion = float or float3

// Line 588: MAIN DIFFUSE EQUATION
IndirectLighting.Diffuse = (DiffuseIndirectLighting * DiffuseColor + BackfaceDiffuseIndirectLighting)
    * Occlusion.DiffuseOcclusion * EnergyPreservationFactor;

// Where:
// DiffuseIndirectLighting     = Irradiance from cache (already /π factored in)
// DiffuseColor                = Material albedo [0, 1]
// Occlusion.DiffuseOcclusion  = AO term [0, 1]
// EnergyPreservationFactor    = 1.0 (usually) or special energy conservation term
```

**Fully expanded for non-Substrate**:

```hlsl
diffuse_lighting = irradiance × albedo × occlusion
```

**NO `/π` at compositing time**.

---

### Stage 10: Final Blend

**UE5 Code** (`DiffuseIndirectComposite.usf:620-621`):

```hlsl
OutColor = SceneColor * OutMultiplyColor + OutAddColor;
// Where OutMultiplyColor = FinalAmbientOcclusion (multiplicative)
//       OutAddColor = IndirectLighting.Diffuse + IndirectLighting.Specular (additive)
```

**What this does**: Blend indirect lighting additively into scene color.

---

## THE BUG IN UZENGINE

Based on your description ("probes produce valid SH but scene is too dark by ~1/π"):

### Root Cause

You are likely **applying `/π` at compositing**, which is incorrect. The correct equation is:

```hlsl
// CORRECT (UE5):
diffuse = cache_irradiance × material_albedo × ao

// INCORRECT (possibly UzEngine):
diffuse = (cache_irradiance / PI) × material_albedo × ao
```

If you divide by π at composition, you get `1/π` darker results, which matches the observed bug.

### Fix Checklist

1. **Probe generation** (CPU):
   - [ ] Accumulate ray radiance weighted by `cos(θ)`
   - [ ] **Divide by π** before storing in atlas
   - [ ] Verify atlas stores irradiance, NOT SH coefficients

2. **Probe sampling** (shader):
   - [ ] Sample irradiance directly from atlas
   - [ ] Apply parallax correction (multiply by CorrectionFactor)
   - [ ] Apply trilinear interpolation
   - [ ] Resolve sky visibility with lerp

3. **Final compositing** (shader):
   - [ ] Multiply sampled irradiance by material albedo
   - [ ] Multiply by occlusion/AO
   - [ ] **DO NOT divide by π again**
   - [ ] Blend additively into scene color

---

## Constant Buffer Layout

If UzEngine has a radiance cache constant buffer, it should contain:

```hlsl
struct RadianceCacheInterpolation {
    // Probe grid
    float3 ClipmapCornerTWS[RADIANCE_PROBE_MAX_CLIPMAPS];      // Clipmap corner position
    float ClipmapCellSize[RADIANCE_PROBE_MAX_CLIPMAPS];        // Grid spacing
    float RadianceProbeTMin[RADIANCE_PROBE_MAX_CLIPMAPS];      // Parallax TMin

    // Atlas metadata
    uint RadianceProbeResolution;        // Octahedral resolution (16 or 32)
    uint FinalProbeResolution;           // Final atlas resolution
    float2 InvProbeFinalRadianceAtlasResolution;  // 1.0 / atlas_dimensions

    // Pre-exposure
    float RadianceCacheOneOverCachedLightingPreExposure;  // 1.0 / pre-exposure
    float ReprojectionRadiusScale;       // Parallax radius scale (~0.5)

    // Mipmapping
    uint FinalRadianceAtlasMaxMip;       // Number of mip levels
};
```

---

## Visual Diagnostic

To verify the fix:

**Before**: Scene is 1/π darker (~0.32x original)
**After**: Scene matches UE5 reference (1.0x)

**Screenshot test**:
```bash
# Capture at default camera
demo.exe --screenshot-stdout --frames 8

# Capture at higher altitude (to test sky visibility)
demo.exe --screenshot-stdout --camera-z 800 --frames 8

# Capture close to surface (to test parallax correction)
demo.exe --screenshot-stdout --camera-z 200 --frames 8
```

All three should show consistent brightness. If one is darker, the bug is in that subsystem.

---

## Summary

**UE5 Lumen radiance cache = irradiance-based, NOT SH-based.**

- Probes store pre-convolved irradiance (already `/π` factored)
- Sampling is direct texture lookup, no SH basis functions
- Parallax correction is an attenuation scale factor, not a basis rotation
- Compositing is simply `irradiance × albedo × ao`
- **NO `/π` at compositing time**

If UzEngine is dividing by π anywhere in the pipeline, remove it.

