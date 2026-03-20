<!-- doc-meta
category: rendering
status: complete
author: unreal-architect
date: 2026-03-19
-->

# UE5 Lumen Radiance Cache Compositing Math — EXACT SPECIFICATION

## Executive Summary

UE5's Lumen radiance cache does NOT use spherical harmonics for final compositing. It:

1. **Directly stores pre-convolved radiance** in octahedral atlas (not SH coefficients)
2. **Samples radiance directly** from the atlas via parallax-corrected direction
3. **Applies a single `/PI` factor** to convert irradiance → diffuse contribution
4. **Uses `DiffuseColor` (albedo) to scale** the irradiance value
5. **Applies occlusion/visibility terms** (AO, bentNormal, sky visibility) AFTER computing base diffuse
6. **Sky visibility** resolves sky radiance via `GetSkyLightReflection()` at runtime

This is fundamentally different from SH-based approaches. The probe stores **pre-integrated irradiance** (L2 cosine-weighted radiance), not coefficients.

---

## 1. Probe Storage Format

**Source**: `LumenScreenProbeGather.usf` line 1360, `DiffuseIndirectComposite.usf` line 588

The radiance cache does NOT store SH coefficients. Instead:

```hlsl
// What's actually stored in each probe:
// RadianceCacheFinalRadianceAtlas — float3 RGB irradiance (pre-convolved with cosine lobe)
// RadianceCacheFinalSkyVisibilityAtlas — float visibility factor [0, 1]
// RadianceCacheDepthAtlas — float hit distance for parallax correction

// Probe resolution: typically 16x16 octahedral layout per probe
// Mip levels: up to 6 levels (FinalRadianceAtlasMaxMip)
```

**Key insight**: The radiance stored is **NOT raw incoming light**. It is **pre-integrated hemispherical irradiance** — the integral of incoming radiance weighted by cosine lobe.

---

## 2. Radiance Cache Sampling (No SH Evaluation)

**Source**: `LumenRadianceCacheInterpolation.ush` lines 221-269

### The Exact Sampling Function

```hlsl
FRadianceCacheSample SampleRadianceCacheProbe(
    uint ProbeIndex,
    float3 WorldSpaceDirection,  // The surface normal direction
    float MipLevel)
{
    FRadianceCacheSample Sample;
    Sample.Radiance = 0.0f;
    Sample.SkyVisibility = 0.0f;

    // Step 1: Convert normal direction to equirectangular UV
    float2 ProbeUV = InverseEquiAreaSphericalMapping(WorldSpaceDirection);

    // Step 2: Convert UV to atlas coordinates
    uint2 ProbeAtlasCoord = FinalProbeResolution * uint2(
        ProbeIndex & ProbeAtlasResolutionModuloMask,
        ProbeIndex >> ProbeAtlasResolutionDivideShift);
    float2 ProbeTexelCoord = ProbeUV * RadianceProbeResolution + (1u << FinalRadianceAtlasMaxMip);
    float2 ProbeAtlasUV = (ProbeAtlasCoord + ProbeTexelCoord) * InvProbeFinalRadianceAtlasResolution;

    // Step 3: Sample from atlas (bilinear, multiple mips)
    if (ProbeIndex != INVALID_PROBE_INDEX) {
        Sample.Radiance = RadianceCacheFinalRadianceAtlas.SampleLevel(
            GlobalBilinearClampedSampler,
            ProbeAtlasUV,
            MipLevel) * RadianceCacheOneOverCachedLightingPreExposure;  // ← PRE-EXPOSURE CORRECTION

        Sample.SkyVisibility = RadianceCacheFinalSkyVisibilityAtlas.SampleLevel(
            GlobalBilinearClampedSampler,
            ProbeAtlasUV,
            MipLevel);
    }

    return Sample;
}
```

**Critical observations**:
- **No SH basis functions** (no `SHBasisFunction3`, no `DotSH3`)
- **No dot products with normal** (the direction IS the normal)
- **Direct texture lookup** → the value sampled IS the irradiance for that direction
- **Sky visibility blending** happens AFTER sampling (see next section)

---

## 3. Sky Visibility Resolution

**Source**: `LumenRadianceCacheInterpolation.ush` lines 358-370

When a ray misses (probe doesn't have data in that direction), sky visibility can be > 0:

```hlsl
FRadianceCacheSample RadianceCacheResolveSky(
    FRadianceCacheSample Sample,
    float3 WorldSpaceDirection)
{
#if RADIANCE_CACHE_SKY_VISIBILITY
    if (Sample.SkyVisibility > 0.0f && ReflectionStruct.SkyLightParameters.y > 0) {
        // Evaluate sky radiance for this direction (NOT cache)
        float SkyAverageBrightness = 1.0f;
        float3 SkyRadiance = GetSkyLightReflection(
            WorldSpaceDirection,
            /*Roughness*/ 0.0f,  // Perfect clarity for sky
            SkyAverageBrightness);

        // LERP between cache and sky
        Sample.Radiance = lerp(Sample.Radiance, SkyRadiance, Sample.SkyVisibility);
        // ↑ When SkyVisibility=1.0, use pure sky radiance
        // ↑ When SkyVisibility=0.0, use pure cache radiance
    }
#endif
    return Sample;
}
```

**The sky radiance itself is ALSO irradiance**, not raw directional radiance. It comes from `GetSkyLightReflection()`.

---

## 4. Parallax Correction (Affects Scale Only)

**Source**: `LumenRadianceCacheInterpolation.ush` lines 277-347

The parallax correction uses sphere intersection to reproject the lookup direction:

```hlsl
FRadianceCacheSample SampleRadianceCacheProbeWithParallaxCorrection(
    uint3 ProbeCoord,
    uint ProbeClipmapIndex,
    float3 WorldSpacePosition,  // Shading point (not direction)
    float3 WorldSpaceDirection, // Initial lookup direction (surface normal)
    float MipLevel)
{
    float ProbeTMin = GetRadianceProbeTMin(ProbeClipmapIndex);
    uint ProbeIndex = GetProbeIndexFromIndirectionTexture(ProbeCoord, ProbeClipmapIndex);
    float3 ProbeWorldPosition = GetProbeWorldPosition(ProbeCoord, ProbeClipmapIndex, ProbeIndex);

    float3 ReprojectedDirection = WorldSpaceDirection;
    float CorrectionFactor = 1.0f;

    // Sphere intersection to move probe "closer" to shading point
    float ReprojectionRadius = ReprojectionRadiusScale * ProbeTMin;
    float T = RayIntersectSphere(
        WorldSpacePosition,
        WorldSpaceDirection,
        float4(ProbeWorldPosition, ReprojectionRadius)).y;
    float3 IntersectionPosition = WorldSpacePosition + WorldSpaceDirection * T;
    ReprojectedDirection = IntersectionPosition - ProbeWorldPosition;

    // ← This changes lookup direction slightly, but more importantly:
    // ← Applies attenuation to account for distance differences
    CorrectionFactor = T * T / (ReprojectionRadius * dot(ReprojectedDirection, WorldSpaceDirection));

    // Sample using reprojected direction
    FRadianceCacheSample RadianceCacheSample = SampleRadianceCacheProbe(
        ProbeIndex,
        ReprojectedDirection,
        MipLevel);

    // ← SCALE the sampled radiance
    RadianceCacheSample.Radiance *= CorrectionFactor;

    return RadianceCacheSample;
}
```

**Key**: The `CorrectionFactor` is a **scale factor only**, not a basis function weight.

---

## 5. Interpolation Between 8 Probes

**Source**: `LumenRadianceCacheInterpolation.ush` lines 418-436

When a shading point is between probes, UE5 performs trilinear interpolation:

```hlsl
#if STOCHASTIC_INTERPOLATION
    // Fast version: sample 1 of 8 corners stochastically
    uint3 StochasticOffset = GetStochasticTrilinearOffset(
        RandomScalarForStochasticInterpolation,
        InterpolationWeights0,
        InterpolationWeights1);
    FRadianceCacheSample InterpolatedSample = SampleRadianceCacheProbeWithParallaxCorrection(
        CornerProbeCoord + StochasticOffset,  // ← Pick ONE corner
        Coverage.ClipmapIndex,
        WorldSpacePosition,
        WorldSpaceDirection,
        MipLevel);
#else
    // Full version: sample all 8 corners, lerp results
    FRadianceCacheSample Sample000 = SampleRadianceCacheProbeWithParallaxCorrection(
        CornerProbeCoord + int3(0,0,0), ..., MipLevel);
    FRadianceCacheSample Sample001 = SampleRadianceCacheProbeWithParallaxCorrection(
        CornerProbeCoord + int3(0,0,1), ..., MipLevel);
    // ... 6 more samples ...

    // Trilinear interpolation of radiance
    FRadianceCacheSample ZLerp00 = LerpRadianceCacheSample(Sample000, Sample001, LerpAlphas.z);
    FRadianceCacheSample ZLerp01 = LerpRadianceCacheSample(Sample010, Sample011, LerpAlphas.z);
    FRadianceCacheSample ZLerp10 = LerpRadianceCacheSample(Sample100, Sample101, LerpAlphas.z);
    FRadianceCacheSample ZLerp11 = LerpRadianceCacheSample(Sample110, Sample111, LerpAlphas.z);

    FRadianceCacheSample YLerp0 = LerpRadianceCacheSample(ZLerp00, ZLerp01, LerpAlphas.y);
    FRadianceCacheSample YLerp1 = LerpRadianceCacheSample(ZLerp10, ZLerp11, LerpAlphas.y);

    FRadianceCacheSample InterpolatedSample = LerpRadianceCacheSample(
        YLerp0, YLerp1, LerpAlphas.x);
#endif
```

The interpolation is **purely linear in radiance space** — no SH basis work.

---

## 6. Final Compositing (The Key Equation)

**Source**: `DiffuseIndirectComposite.usf` lines 588, 593-596

### For Legacy GBuffer (Non-Substrate):

```hlsl
// Line 588: Core diffuse equation
IndirectLighting.Diffuse = (DiffuseIndirectLighting * DiffuseColor + BackfaceDiffuseIndirectLighting)
    * Occlusion.DiffuseOcclusion * EnergyPreservationFactor;

// Where:
// DiffuseIndirectLighting       = Sampled cache radiance (per direction)
// DiffuseColor                   = Material albedo (from GBuffer)
// BackfaceDiffuseIndirectLighting = Cache backface component (if enabled, line 552)
// Occlusion.DiffuseOcclusion     = AO term from screen-space (lines 174, 177-179)
// EnergyPreservationFactor       = 1.0 if NOT using ComputeGGXSpecEnergyTerms, else special value
```

**Fully expanded**:

```glsl
diffuse_indirect = (cache_radiance * albedo + backface_component) * occlusion * energy_factor
```

### For Specular:

```hlsl
// Line 593: Rough specular
IndirectLighting.Specular = CombineRoughSpecular(
    GBuffer,
    HasBackfaceDiffuse(Material),
    NoV,
    SpecularIndirectLighting,
    RoughSpecularIndirectLighting,
    SpecularColor,
    EnvBRDFValue);

// Inside CombineRoughSpecular (line 218-230):
float3 Lighting = MixSpecularAndRoughReflections(
    GBuffer.Roughness,
    bHasBackfaceDiffuse,
    RayTracedReflections,           // ← Lumen specular cache
    RoughReflections * SpecularOcclusion); // ← Lumen rough specular
return AddContrastAndSpecularScale(Lighting) * EnvBRDFValue;
```

**Critical for specular**: The `EnvBRDFValue` is the **pre-integrated Fresnel-Schlick term**, NOT part of the cache.

### Final Accumulation:

```hlsl
// Line 620-621: Final blend to scene color
OutColor = SceneColor * OutMultiplyColor + OutAddColor;
// Where OutMultiplyColor = FinalAmbientOcclusion (line 618)
//       OutAddColor = DiffuseIndirectLighting (lines 606-612)
```

---

## 7. CRITICAL DIFFERENCES FROM SH-BASED APPROACHES

| Aspect | UE5 Lumen | Generic SH |
|--------|-----------|-----------|
| **Probe Storage** | Irradiance texture (float3 RGB) | SH coefficients (9×float4 for L2) |
| **Lookup Method** | Direct texture sample via direction → UV | Evaluate SH basis at normal dir |
| **Normal Integration** | `DiffuseColor` × `CacheRadiance` | `DotSH(SH_coeffs, Normal_SH)` |
| **Cosine Lobe** | **Pre-integrated into cache** | Applied during SH evaluation |
| **Division by π** | **NOT APPLIED** (cache is already irradiance) | Not applicable (SH is radiance) |
| **Sky Fallback** | Evaluated at runtime if `SkyVisibility > 0` | Would need separate SH or fallback |
| **Parallax Correction** | Scale factor only, not basis change | Would need full SH rotation |

**The /π that you think should exist**: In UE5, the `/π` is **baked into the probe during tracing**. The cache stores `∫ L(ω) cos(θ) dω / π`, not `∫ L(ω) cos(θ) dω`.

---

## 8. Screen Probe Gather Integration

**Source**: `LumenScreenProbeGather.usf` lines 1309, 1331-1333

The screen probes also output irradiance (not SH):

```hlsl
// Line 1309: Accumulate radiance samples
DiffuseLighting += InterpolatedRadiance * SampleWeight * DirectionalOcclusion;

// Line 1331-1332: Normalize by sample count and AO
float NormalizeTerm = 2 * PI / (NumValidSamples * AO);
DiffuseLighting *= NormalizeTerm;

// Line 1360: Final output is irradiance, NOT divided by π again
float3 DiffuseIndirectOutput = DiffuseLighting * Diffuse_Lambert(float3(1, 1, 1));
```

**Key**: `Diffuse_Lambert(float3(1,1,1))` = `1.0 / π`, so the final output is:

```
output = (∑ radiance / sample_count) × (2π / AO) × (1/π)
       = (∑ radiance / sample_count) × (2 / AO)
```

This is **probe-generated irradiance**, not radiance.

---

## 9. Exact Constants & Scale Factors

| Parameter | Value | Purpose |
|-----------|-------|---------|
| `RadianceCacheOneOverCachedLightingPreExposure` | Render frame variable | Undo lighting pre-exposure |
| `ReprojectionRadiusScale` | Typically ~0.5 | Parallax sphere size scale |
| `RadianceProbeResolution` | Typically 16 or 32 | Octahedral atlas resolution |
| `FinalProbeResolution` | ~32-128 | Mip-mapped atlas resolution |
| `FinalRadianceAtlasMaxMip` | ~6 | Number of mip levels |
| `NormalizeTerm` (screen probes) | `2π / (NumSamples × AO)` | Sample averaging |
| `PreintegratedTwoSidedBxDF` | `1.0 / π` | Two-sided foliage subsurface |

---

## 10. Does UE5 Apply `/π`?

**Short answer: No, NOT at compositing time.**

The cache stores **pre-convolved irradiance**. The `/π` is implicit:

1. **During probe tracing** (in `LumenRadianceCache.cpp/usf`), rays are **weighted by cosine lobe**
2. The result is **divided by π** during accumulation
3. The cache stores **irradiance** (not radiance)
4. At compositing, **multiply by albedo directly** (no `/π`)

If you divide by π at compositing, **you will get 1/π darker results** — which is exactly what's happening in UzEngine.

---

## 11. UzEngine Adaptation

To match UE5 exactly:

1. **Probe storage**: Store irradiance (R11G11B10 or float3), NOT SH coefficients
2. **Sampling**: Direct atlas lookup via octahedral UV of surface normal
3. **Parallax correction**: Apply parallax sphere intersection, get CorrectionFactor, **scale** sampled value
4. **Interpolation**: Trilinear lerp of 8 corner probes in clip-space
5. **Compositing**:
   ```hlsl
   diffuse = cache_sample × material_albedo × visibility_ao
   ```
   **NO `/π` at compositing time**
6. **Sky visibility**: If `sky_visibility > 0`, lerp between cache and evaluated sky
7. **Pre-exposure**: Apply `RadianceCacheOneOverCachedLightingPreExposure` after sampling

---

## 12. Pseudo-Code: Complete Flow

```cpp
// CPU: Gather environment + render probes
void RenderRadianceProbes() {
    for (auto& probe : probes) {
        // Trace rays from probe, accumulate incoming radiance weighted by cos(θ)
        float3 accum = RayTrace(probe.position);
        // Divide by π to get irradiance
        probe.irradiance = accum / PI;
        // Store in atlas
        atlas[probe.atlas_coord] = probe.irradiance;
    }
}

// Shader: Sample probes and apply to scene
void ComputeDiffuseIndirect() {
    // 1. Find 8 nearby probes
    int3 probe_corner = GetProbeGridCoord(pixel_position);
    float3 probe_lerp_weights = frac(GetProbeGridCoordFloat(pixel_position));

    // 2. Sample 8 probes (or 1 randomly)
    float3 irradiance = 0;
    for (int i = 0; i < 8; i++) {
        // 2a. Get probe position and apply parallax
        float3 corrected_dir = ParallaxCorrect(normal, probe_position, pixel_position);
        float correction_scale = ComputeCorrectionFactor(...);

        // 2b. Sample atlas
        float2 uv = OctahedralMapping(corrected_dir);
        float3 probe_irrad = sample_atlas(probe_index, uv, mip_level);

        // 2c. Apply parallax scale
        probe_irrad *= correction_scale;

        // 2d. Accumulate with trilinear weight
        irradiance += probe_irrad * trilinear_weight[i];
    }

    // 3. Resolve sky visibility
    if (sky_visibility > 0) {
        float3 sky_irrad = EvaluateSkyLighting(normal);
        irradiance = lerp(irradiance, sky_irrad, sky_visibility);
    }

    // 4. Apply to scene
    float ao = SampleAOTexture(pixel_position);
    float3 diffuse = irradiance * material_albedo * ao;

    // 5. Composite to frame
    out_color = scene_color + diffuse;
}
```

---

## References

- **UE5 Source**: `LumenRadianceCacheInterpolation.ush:221-269` (sampling)
- **UE5 Source**: `LumenRadianceCacheInterpolation.ush:372-440` (interpolation)
- **UE5 Source**: `DiffuseIndirectComposite.usf:588-596` (compositing)
- **UE5 Source**: `LumenScreenProbeGather.usf:1309-1360` (screen probe integration)
- **UE5 Source**: `IndirectLightRendering.cpp:118-222` (C++ shader definition)

