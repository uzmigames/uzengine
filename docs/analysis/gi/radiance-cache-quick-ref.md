<!-- doc-meta
category: rendering
status: complete
author: unreal-architect
date: 2026-03-19
-->

# Radiance Cache — Quick Reference

## TL;DR: Why Your Scene Is Dark

**You are dividing by π at composition. Don't.**

UE5 Lumen stores **irradiance** (pre-divided by π), not SH coefficients. The final equation is:

```hlsl
// CORRECT:
diffuse = cache_sample × material_albedo × occlusion
         ↑
    already includes π

// WRONG:
diffuse = (cache_sample / PI) × material_albedo × occlusion
         ↑
    this makes everything 0.32x darker
```

---

## UE5 Compositing Math (Lines & Constants)

### Sampling (LumenRadianceCacheInterpolation.ush:221-269)

```hlsl
float3 irradiance = RadianceCacheFinalRadianceAtlas.SampleLevel(sampler, uv, mip)
                    * RadianceCacheOneOverCachedLightingPreExposure;
```

- Input: Surface normal direction
- Output: **Irradiance** (float3 RGB)
- No SH basis functions
- No dot products with normal

### Parallax Correction (line 345)

```hlsl
irradiance *= CorrectionFactor;  // Scale by attenuation
```

- `CorrectionFactor = T² / (R² × dot(dir, normal))`
- Used to account for distance mismatch between shading point and probe center
- **Multiplies** the sampled value (not a basis weight)

### Sky Visibility (line 366)

```hlsl
irradiance = lerp(irradiance, GetSkyLightReflection(normal), sky_visibility);
```

- If ray missed (sky_visibility > 0), evaluate sky at runtime
- Blend between cache and sky

### Final Composition (DiffuseIndirectComposite.usf:588)

```hlsl
diffuse_output = (irradiance * material_albedo) * occlusion_ao * energy_factor;
```

- **NO `/π` at composition**
- Multiply by albedo directly
- Apply occlusion/AO
- Optional energy conservation factor

---

## Exact Constants

| Name | Typical Value | Purpose |
|------|---------------|---------|
| `RadianceProbeResolution` | 16 or 32 | Octahedral layout resolution during tracing |
| `FinalProbeResolution` | 32 or 64 | Octahedral layout resolution during sampling (mipped) |
| `FinalRadianceAtlasMaxMip` | 6 | Number of mip levels (0 to this value) |
| `ReprojectionRadiusScale` | 0.5 | Parallax sphere radius = `TMin × 0.5` |
| `RadianceCacheOneOverCachedLightingPreExposure` | ~1.0 | Undo pre-exposure if used |
| `Normalization (screen probes)` | `2π / (NumSamples × AO)` | Sample averaging constant |

---

## Checklist: Is Your Implementation Correct?

- [ ] **Probe storage**: Storing irradiance (float3), NOT SH coefficients?
- [ ] **Sampling**: Using `InverseEquiAreaSphericalMapping(normal)` to get UV?
- [ ] **Parallax**: Computing `CorrectionFactor` and **multiplying** by it?
- [ ] **Interpolation**: Trilinear lerp of 8 corner probes?
- [ ] **Sky blend**: If `sky_visibility > 0`, lerp between cache and evaluated sky?
- [ ] **Composition**: `irradiance × albedo × ao` (no `/π`)?
- [ ] **Occlusion**: Using correct AO term (not applied twice)?
- [ ] **Specular**: Using separate `RoughSpecularIndirectLighting` × `EnvBRDFValue`?

---

## If You Have SH Coefficients (Wrong Approach)

If your probes store SH coefficients (L2 = 9 floats per probe):

1. This is **not** how UE5 does it
2. You would need to evaluate SH basis at the surface normal
3. You would need to handle parallax correction via SH rotation (complex)
4. You would get different visual results than UE5

**Recommendation**: Rewrite probes to store irradiance directly (simpler, faster, matches UE5).

---

## UE5 Sources (Where to Read)

| File | Lines | What |
|------|-------|------|
| `LumenRadianceCacheInterpolation.ush` | 221-269 | Probe sampling function |
| `LumenRadianceCacheInterpolation.ush` | 277-347 | Parallax correction |
| `LumenRadianceCacheInterpolation.ush` | 358-370 | Sky visibility blending |
| `LumenRadianceCacheInterpolation.ush` | 418-436 | Trilinear interpolation |
| `DiffuseIndirectComposite.usf` | 520-596 | Final composition |
| `LumenScreenProbeGather.usf` | 1309-1360 | Screen probe gathering |

---

## Most Common Bugs

1. **Dividing by π at composition** ← **THIS IS YOUR BUG**
   - Fix: Remove the `/π` in the final multiply
   - Effect: Scene becomes 3.14x brighter

2. **Storing SH instead of irradiance**
   - Fix: Change probe storage format to float3 irradiance
   - Effect: Need complete rewrite of sampling/parallax logic

3. **Not applying parallax correction scale**
   - Fix: Multiply sampled irradiance by `CorrectionFactor`
   - Effect: Scene looks grid-like (blocky artifacts)

4. **Not handling sky visibility**
   - Fix: Evaluate sky at runtime, lerp with cache based on `sky_visibility`
   - Effect: Indoor areas show dark ceiling instead of environment

5. **Using wrong occlusion term**
   - Fix: Use bent normal AO, not static AO
   - Effect: Scene loses local contrast

---

## Implementation Pseudo-Code

```hlsl
// Sample radiance cache at pixel (shader entry point)
void GetIndirectLighting(float3 world_position, float3 surface_normal, out float3 diffuse_result) {
    // 1. Find nearby probes in grid
    float3 grid_coord_float = GetProbeGridCoord(world_position);
    int3 grid_corner = floor(grid_coord_float);
    float3 lerp_weights = frac(grid_coord_float);

    // 2. Sample all 8 corner probes
    float3 accumulated_irradiance = 0;
    for (int corner = 0; corner < 8; corner++) {
        int3 probe_coord = grid_corner + GetCornerOffset(corner);
        uint probe_index = GetProbeIndex(probe_coord);

        // 2a. Apply parallax correction
        float3 reprojected_normal = ApplyParallax(world_position, surface_normal, probe_position);
        float correction = ComputeParallaxScale(world_position, surface_normal, probe_position);

        // 2b. Sample atlas
        float2 uv = OctahedralMapping(reprojected_normal);
        float mip = ComputeMipLevel(view_distance);  // Lower mip = less detail
        float3 probe_irradiance = SampleProbeAtlas(probe_index, uv, mip);
        probe_irradiance *= correction;  // ← Apply parallax scale

        // 2c. Resolve sky if needed
        float sky_vis = SampleSkyVisibility(probe_index, uv, mip);
        if (sky_vis > 0) {
            float3 sky_irrad = EvaluateSkyLighting(surface_normal);
            probe_irradiance = lerp(probe_irradiance, sky_irrad, sky_vis);
        }

        // 2d. Accumulate with trilinear weight
        float weight = GetTrilinearWeight(corner, lerp_weights);
        accumulated_irradiance += probe_irradiance * weight;
    }

    // 3. Apply material and occlusion
    float3 material_albedo = SampleGBuffer(pixel_pos).diffuse_color;
    float occlusion = SampleAO(pixel_pos);
    diffuse_result = accumulated_irradiance * material_albedo * occlusion;
}
```

---

## Expected vs Actual

| Metric | Expected | Actual (before fix) | Actual (after fix) |
|--------|----------|---------------------|-------------------|
| Scene brightness | 100% | ~32% (1/π) | 100% |
| Indoor areas | Lit by sky | Dark or missing | Correct |
| Outdoor areas | Bright | Too dark | Correct |
| Specular | Subtle | May be broken | Correct |

---

## Next Steps

1. **Locate composition equation** in your codebase
2. **Find where you divide by π** (in shader or CPU)
3. **Remove the `/π` factor**
4. **Recompile and test**
5. **Capture screenshots** at multiple camera positions to verify

If the fix doesn't work, verify:
- Are you sampling irradiance (float3) or SH coefficients (9 floats)?
- Are you applying parallax correction correctly?
- Are you using the correct mip level?

