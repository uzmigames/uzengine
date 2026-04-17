<!-- doc-meta
type: analysis
category: rendering/clouds
title: three-geospatial Clouds Adaptation Plan for UzEngine
created: 2026-03-20
updated: 2026-03-20
status: proposed
-->

# three-geospatial Clouds — Adaptation Plan for UzEngine

Analysis of [takram/three-geospatial clouds](https://github.com/takram-design-engineering/three-geospatial/tree/main/packages/clouds) and adaptation strategy for UzEngine's DX12/HLSL compute pipeline.

## Source: MIT License

The three-geospatial package is MIT-licensed. We can freely study, adapt, and reimplement the techniques. Key papers referenced by the implementation are all publicly available.

---

## 1. Gap Analysis: Current UzEngine vs three-geospatial

| Feature | UzEngine Current | three-geospatial | Gap |
|---------|-----------------|-------------------|-----|
| **Ray march steps** | 64 fixed uniform | 200-500 adaptive + perspective scale | CRITICAL |
| **Step size** | Uniform `(tExit-tEntry)/N` | Adaptive: `minStep + perspectiveScale * distance` | CRITICAL |
| **Blue noise jitter** | None | STBN 3D texture, per-pixel temporal jitter | CRITICAL |
| **Empty space skip** | None | Skip when density < threshold, enlarge step | HIGH |
| **Multi-scattering** | Ambient approx (single term) | 8-octave production volume rendering (Oz/Kulla 2017) | HIGH |
| **Phase function** | Dual HG only | Dual HG + Draine (NVIDIA approx Mie) option | MEDIUM |
| **Cloud shadows** | None (self-shadow only) | Beer Shadow Maps (BSM) + cascade + TAA | HIGH |
| **Temporal upscale** | Half-res EMA blend | 1/4 res + 4x4 Bayer 16-frame cycling + variance clipping | HIGH |
| **Aerial perspective** | None on clouds | Bruneton atmosphere per-sample transmittance | MEDIUM |
| **Cloud layers** | 1 fixed layer | Up to 4 layers with per-layer density profiles | MEDIUM |
| **Density profile** | Fixed parabolic | Configurable `exp + linear + constant` per layer | MEDIUM |
| **Cloud type** | Parameter exists but unused | Shapes height gradient (stratus/cumulus) | LOW |
| **Powder effect** | None | `1 - powderScale * exp(-extinction * powderExp)` | LOW |
| **Haze / light shafts** | None | Analytical height fog + shadow separation | LOW |
| **Weather map** | Procedural only | Supports artist-authored textures | LOW |
| **SVS for shadows** | N/A | Structured Volume Sampling (icosahedron) | LOW |
| **Ground bounce** | None | Optional downward march + albedo 0.3 | LOW |

### Priority Ranking

**P0 — Visual Quality (biggest bang for effort):**
1. Adaptive stepping + blue noise jitter (eliminates banding)
2. Multi-scattering octaves (eliminates dark flat clouds)
3. Cloud shadow maps (ground shadows = instant realism)

**P1 — Performance:**
4. Quarter-res + Bayer temporal upscale (4x perf improvement)
5. Empty space skipping (reduces wasted samples)

**P2 — Completeness:**
6. Aerial perspective on clouds
7. Multi-layer support + density profiles
8. Powder effect
9. Draine phase function option

---

## 2. Adaptation Plan — Phase by Phase

### Phase 1: Adaptive Stepping + Blue Noise Jitter
**Files**: `cloud_raymarch_cs.hlsl`, `cloud_pass.h`, `cloud_pass.cpp`

Current uniform stepping produces visible banding artifacts. The fix has two parts:

**A) Perspective-scaled step size:**
```hlsl
// CURRENT (uniform):
float stepT = (tExit - tEntry) / float(primarySteps);

// NEW (adaptive):
float stepT = minStepSize;
for (uint i = 0; i < primarySteps && t < tExit; ++i) {
    // Sample at current position
    float3 posKm = rayOrigin + rayDir * t;
    // ... density/lighting ...

    // Grow step with distance (distant clouds need less precision)
    stepT = min(stepT * perspectiveStepScale, maxStepSize);

    // Empty space skip: if density is zero, jump further
    if (density <= 0.0) {
        stepT = lerp(stepT, maxStepSize, min(1.0, mipLevel));
    }

    t += stepT;
}
```

**B) STBN jitter:**
- Generate or load a 128x128x64 R8 3D blue noise texture (Christoph Peters STBN)
- Initial ray offset: `t += stepT * stbnSample * 2.0` where `stbnSample = stbnTexture[pixel.xy % 128, frameIndex % 64]`
- This spreads sampling error into high-frequency noise that the temporal filter averages out

**New CB fields:**
- `float minStepSize` — default 0.01 km
- `float maxStepSize` — default 0.5 km
- `float perspectiveStepScale` — default 1.006
- `uint stbnSrvIndex` — SRV for blue noise 3D texture

---

### Phase 2: Multi-Scattering Octaves
**Files**: `cloud_raymarch_cs.hlsl`

Replace the ambient approximation with the Oz/Kulla 2017 octave approach.

**Current:**
```hlsl
float3 ambient = ambientColor * ambientMultiplier * saturate(0.3 + heightFrac);
float3 scattered = (sunScattered + ambient) * scatteringCoeff * density;
```

**New (8 octaves):**
```hlsl
float3 scatteredLuminance = 0;
float a = 1.0;  // scattering attenuation
float b = 1.0;  // extinction attenuation
float c = 1.0;  // phase eccentricity attenuation

for (uint octave = 0; octave < MULTI_SCATTERING_OCTAVES; ++octave) {
    float beerLambert = exp(-opticalDepthToSun * b);
    float phase = DualHG(cosTheta, phaseG * c, phaseG2 * c, phaseBlend);
    scatteredLuminance += a * beerLambert * phase * sunIrradiance;

    a *= 0.5;  // each bounce loses half the energy
    b *= 0.5;  // extinction weakens (photons scatter more freely)
    c *= 0.5;  // phase function becomes more isotropic
}
// Add sky irradiance term
scatteredLuminance += skyIrradiance * RECIPROCAL_4PI * skyGradient;
```

This produces bright silver linings and softer, more realistic cloud interiors without the "dark flat" look.

---

### Phase 3: Beer Shadow Maps (BSM)
**Files**: NEW `cloud_shadow_cs.hlsl`, NEW `cloud_shadow_resolve_cs.hlsl`, `cloud_pass.h/cpp`, `cloud_raymarch_cs.hlsl`, `deferred_lighting_ps.hlsl`

**Shadow pass** (compute shader, sun's perspective):
- Orthographic projection from sun direction
- 1-3 cascades covering the camera frustum
- Ray march through cloud layer, accumulate 4-channel BSM:
  - R: front depth (transmittance-weighted average)
  - G: mean extinction
  - B: max optical depth
  - A: tail optical depth (exponential estimate)
- Output: `RWTexture2DArray<float4>` RGBA16F, one slice per cascade

**Shadow resolve** (temporal AA per cascade):
- Variance clipping to reduce flickering
- 3x3 neighborhood for clip bounds

**Shadow sampling** (in `cloud_raymarch_cs.hlsl` for self-shadow AND in `deferred_lighting_ps.hlsl` for ground shadow):
```hlsl
float SampleCloudShadow(float3 worldPos, float3 sunDir) {
    // Project into shadow cascade UV
    float4 shadowClip = mul(cloudShadowVP[cascade], float4(worldPos, 1));
    float2 shadowUV = shadowClip.xy * 0.5 + 0.5;
    float4 bsm = cloudShadowMap.SampleLevel(linearClamp, float3(shadowUV, cascade), 0);

    float distToFront = max(0, distToTop - bsm.r);
    float opticalDepth = min(bsm.b + bsm.a, bsm.g * distToFront);
    return exp(-opticalDepth);
}
```

**Integration with deferred lighting:**
- Multiply `sunShadow *= SampleCloudShadow(worldPos, sunDir)` in `deferred_lighting_ps.hlsl`
- This gives ground shadows from clouds for free

---

### Phase 4: Quarter-Res Temporal Upscale with Bayer Pattern
**Files**: `cloud_temporal_cs.hlsl`, `cloud_pass.h/cpp`

Replace half-res EMA with 1/4 res + 16-frame Bayer cycling:

**Ray march at 1/4 resolution** (e.g., 480x270 for 1080p):
- Each frame, only 1/16th of pixels are traced (4x4 block, one pixel per frame)
- Pixel selection: `bayerIndex = BayerMatrix4x4[pixel.x % 4][pixel.y % 4]`
- Current frame traces pixel where `bayerIndex == frameCount % 16`

**Resolve at full resolution:**
```hlsl
if (bayerIndex == frameCount % 16) {
    // This pixel was traced this frame — use current result
    output = current;
} else {
    // Reproject from history
    float2 historyUV = ReprojectUV(pixel, velocity);
    float4 history = historyTexture.SampleLevel(linearClamp, historyUV, 0);

    // Variance clipping (3x3 neighborhood of current-frame results)
    float3 mean, stddev;
    ComputeNeighborhoodStats(currentTexture, pixel, mean, stddev);
    float3 clipped = ClipToAABB(history.rgb, mean, stddev, varianceGamma);

    output = float4(clipped, history.a);
}
```

**Performance impact**: 4x fewer ray march invocations. At 64 steps per pixel, this reduces total step count from ~33M to ~2M per frame at 1080p.

---

### Phase 5: Aerial Perspective on Clouds
**Files**: `cloud_raymarch_cs.hlsl`

After accumulating cloud radiance, apply atmosphere transmittance and inscattering:

```hlsl
// Compute average cloud depth (transmittance-weighted)
float cloudDepth = weightedDepthSum / max(transmittanceSum, 0.001);

// Sample atmosphere transmittance from camera to cloud
float3 atmTransmittance = SampleTransmittanceLUT(cameraPos, cloudDir, cloudDepth);
float3 atmInscatter = SampleSkyRadianceToPoint(cameraPos, cloudDir, cloudDepth, sunDir);

// Apply to cloud radiance
cloudRadiance = cloudRadiance * atmTransmittance + atmInscatter * (1.0 - cloudTransmittance);
```

This integrates with our existing transmittance LUT and sky atmosphere system.

---

### Phase 6: Multi-Layer + Density Profiles
**Files**: `volumetric_clouds_component.h`, `cloud_raymarch_cs.hlsl`, `cloud_pass.h/cpp`

Extend to support up to 4 cloud layers with per-layer:
- Altitude + height
- Density profile: `expTerm * exp(exponent * h) + linearTerm * h + constantTerm`
- Coverage channel from weather texture (R/G/B/A = layer 0/1/2/3)
- Shape/detail erosion amounts
- Shadow casting flag

---

## 3. Key Papers Referenced

| Paper | Technique | Used In |
|-------|-----------|---------|
| Schneider & Vos 2015, "Real-time Volumetric Cloudscapes of Horizon: Zero Dawn" | Noise composition, density model | Shape/detail noise, weather coverage |
| Hillaire 2016, "Physically Based & Unified Volumetric Rendering (Frostbite)" | Energy-conserving integration, section 5.6.3 | Ray march luminance integral |
| Oz & Kulla 2017, "Production Volume Rendering" (Pixar) | Multi-scattering octaves | Lighting model |
| Bowles 2017, "Structured Volume Sampling" | Icosahedron-aligned planes for temporal stability | Shadow map sampling |
| Wrenninge et al. 2013, "Oz: The Great and Volumetric" | Beer shadow maps | Shadow encoding |
| Bruneton & Neyret 2008, "Precomputed Atmospheric Scattering" | Transmittance/inscattering LUTs | Aerial perspective on clouds |
| Peters 2019, "Spatio-Temporal Blue Noise" | STBN 3D textures | Jitter patterns |
| NVIDIA 2018, "Approximate Mie Scattering (Draine)" | Phase function for large particles | Optional Draine phase function |

---

## 4. Implementation Order & Estimated Complexity

| Phase | Files Modified | New Files | Complexity |
|-------|---------------|-----------|------------|
| 1. Adaptive step + jitter | 3 | 0 (+ 1 texture asset) | Medium |
| 2. Multi-scattering octaves | 1 | 0 | Low |
| 3. Beer Shadow Maps | 2 + 1 deferred lighting | 2 new shaders | High |
| 4. Quarter-res Bayer upscale | 2 | 0 | Medium |
| 5. Aerial perspective | 1 | 0 | Low |
| 6. Multi-layer + profiles | 3 | 0 | Medium |

**Recommended order**: Phase 1 → 2 → 4 → 3 → 5 → 6

Phases 1+2 give the biggest visual improvement with least risk. Phase 4 gives the biggest performance win. Phase 3 (shadows) is the most complex but adds dramatic visual impact.
