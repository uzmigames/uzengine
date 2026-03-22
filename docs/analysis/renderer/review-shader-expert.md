<!-- doc-meta
title: Shader Expert Review — UzEngine Deferred Lighting, PBR, Post-Process
description: Independent shader quality assessment validating consensus analysis claims against actual HLSL source
author: shader-engineer
date: 2026-03-22
-->

# Shader Expert Review — UzEngine Render Quality

**Reviewed files**:
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` (1514 lines)
- `runtime/shaders/runtime/include/pbr_common.hlsli` (321 lines)
- `runtime/shaders/runtime/include/shadow_common.hlsli` (495 lines)
- `runtime/shaders/runtime/include/contact_shadows.hlsli` (229 lines)
- `runtime/shaders/runtime/include/film_tonemap.hlsli` (297 lines)
- `runtime/shaders/runtime/include/math_util.hlsli` (109 lines)
- `runtime/shaders/runtime/postprocess/gtao_cs.hlsl` (526 lines)
- `runtime/shaders/runtime/postprocess/bloom_downsample_cs.hlsl` (142 lines)

**Methodology**: Read every line of shader code, validated math against published references, checked for common GPU pitfalls.

---

## 1. Shader Quality Assessment — Overall Grade: B+

The shaders are **well-written, well-documented, and mathematically correct**. Code quality is above average for a game engine at this stage. Specific strengths:

- Every function has a `@ref` citation to a published paper or standard
- `@constants` and `@deviations` headers are consistent throughout
- Helper functions (`Pow2`/`Pow4`/`Pow5`) correctly avoid FXC/DXC divergence
- `LinearToSrgb` uses the branchless polynomial approximation (correct)
- Octahedral normal encoding/decoding is properly separated
- The `AOMultiBounce` Jimenez fit is exactly right (coefficients verified)
- Bloom uses the 13-tap Jimenez 2014 pattern with correct weights (sum = 1.0, verified)

Weaknesses are limited to calibration and one disabled feature, not algorithmic errors.

---

## 2. BRDF Validation — CORRECT

### 2.1 GGX Normal Distribution Function (`pbr_common.hlsli:74-78`)

```hlsl
float DistributionGGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0f) + 1.0f;
    return a2 / (PI * denom * denom);
}
```

**Verdict**: CORRECT. This is the standard Trowbridge-Reitz GGX NDF:
D(h) = alpha^2 / (PI * ((NdotH^2 * (alpha^2 - 1) + 1)^2))

The `alpha` parameter is `roughness^2` (computed at `deferred_lighting_ps.hlsl:989`), making `a2 = roughness^4`. This matches the convention where the NDF takes alpha (not roughness) and squares it internally.

**One note**: The input `alpha` is `roughness^2`, and the function computes `a2 = alpha * alpha = roughness^4`. This is the correct UE4/UE5 convention where the NDF uses `a2 = Pow4(roughness)`. Confirmed correct.

### 2.2 Smith Height-Correlated Visibility (`pbr_common.hlsli:142-148`)

```hlsl
float VisibilitySmithGGX(float a2, float NoV, float NoL) {
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0f - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0f - a) + a);
    return 0.5f * rcp(Vis_SmithV + Vis_SmithL + 1e-5f);
}
```

**Verdict**: CORRECT. This is the Hammon 2017 approximation to the exact Smith height-correlated visibility term, which is what UE4/UE5 uses. The exact form would be:
`Vis_SmithV = NoL * sqrt(NoV^2 * (1-a2) + a2)` (with sqrt), but the approximation `NoL * (NoV * (1-a) + a)` replaces `sqrt(NoV^2*(1-a2)+a2)` with the linear approximation `NoV*(1-a)+a`. This is the standard optimization used across all major engines. The `1e-5` epsilon prevents NaN at grazing angles. Correct.

**Call site** (`pbr_common.hlsli:268`): `VisibilitySmithGGX(alpha * alpha, NdotV, NdotL)` — passes `alpha^2 = roughness^4` as the `a2` parameter. Correct: the sqrt inside recovers `roughness^2 = alpha`, which is what the approximation expects.

### 2.3 Fresnel (`pbr_common.hlsli:96-98`)

```hlsl
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * Pow5(saturate(1.0f - cosTheta));
}
```

**Verdict**: CORRECT. Standard Schlick approximation. The `saturate` on `(1-cosTheta)` is correct defensive practice.

### 2.4 F90 Micro-Occlusion (`pbr_common.hlsli:270-271`)

```hlsl
float F90 = saturate(50.0f * F0.g);
float3 F  = F0 + (F90 - F0) * Pow5(1.0f - VdotH);
```

**Verdict**: CORRECT. This is the Lagarde 2014 "Moving Frostbite to PBR" F90 micro-occlusion term. `saturate(50*F0.g)` ensures F90 is 1.0 for dielectrics (50*0.04=2.0, saturated to 1) and scales correctly for very dark surfaces. Using `.g` (green channel) for the luminance check is the standard approach since green dominates perceptual brightness.

### 2.5 Burley Diffuse (`pbr_common.hlsli:118-123`)

```hlsl
float3 DiffuseDisneyBurley(float3 albedo, float roughness, float NdotV, float NdotL, float VdotH) {
    float FD90 = 0.5f + 2.0f * VdotH * VdotH * roughness;
    float FdV = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotV);
    float FdL = 1.0f + (FD90 - 1.0f) * Pow5(1.0f - NdotL);
    return albedo * (1.0f / PI) * FdV * FdL;
}
```

**Verdict**: CORRECT. This is the exact Disney Burley 2012 diffuse formula. `FD90 = 0.5 + 2 * VdotH^2 * roughness` matches the published paper equation. The Fresnel terms `FdV` and `FdL` are correctly computed. The `1/PI` normalization is correct.

### 2.6 Multiscatter Energy Compensation (`pbr_common.hlsli:175-223`)

The `EnergyAlbedoFitGGX` and `MultiscatterCompensation` functions implement the Kulla-Conty 2017 + Turquin 2018 analytic fit. I verified:
- The `E` (single-scatter directional albedo) fit formula matches the Turquin 2018 paper
- The `Ef` (Fresnel-weighted) fit matches the same reference
- The compensation weight `W = 1 + F0 * ((1-E)/E)` is the correct Kulla-Conty formula
- The diffuse attenuation `(1 - specAlbedoLuminance)` correctly preserves total energy

**Verdict**: CORRECT.

### 2.7 Energy Conservation Chain

In `EvaluateBRDF` (`pbr_common.hlsli:253-289`):
1. Specular = D * Vis * F (Cook-Torrance)
2. Specular *= energyComp.xyz (multiscatter compensation)
3. kD = (1 - specAlbedoLuminance) * (1 - metallic)
4. Diffuse = kD * Burley(...)
5. Output = (diffuse + specular) * radiance * NdotL

This is the correct order and the correct energy conservation chain. The diffuse is attenuated by the *total* specular albedo (including multiscatter), not just the Fresnel term. This is more physically correct than the simpler `(1-F)*(1-metallic)` approach.

**Verdict**: CORRECT.

---

## 3. Lighting Equation Validation — CORRECT (with caveats)

### 3.1 Directional Light (`deferred_lighting_ps.hlsl:1035-1049`)

The directional light path is straightforward: `Lo = EvaluateBRDF(...) * shadow`. Correct.

### 3.2 Point Light Attenuation (`deferred_lighting_ps.hlsl:1068-1077`)

```hlsl
float distRatio4 = distOverRadius * distOverRadius * distOverRadius * distOverRadius;
float windowing  = saturate(1.0f - distRatio4);
attenuation      = (windowing * windowing) / (dist * dist + 1.0f);
```

**Verdict**: CORRECT. This is the UE4 inverse-square with windowing function:
`saturate(1 - (d/r)^4)^2 / (d^2 + 1)`. The `+1` prevents division by zero at distance=0 and matches the reference implementation exactly.

### 3.3 Spot Light Cone Attenuation (`deferred_lighting_ps.hlsl:409-418`)

```hlsl
float ConeAngleFalloff = saturate((dot(L, -spotDir) - spotAngles.x) * spotAngles.y);
return ConeAngleFalloff * ConeAngleFalloff;
```

**Verdict**: CORRECT. Standard squared smooth-step cone falloff. `spotAngles.x = cosOuterAngle`, `spotAngles.y = 1/(cosInner - cosOuter)`. Matches UE4/UE5 exactly.

### 3.4 IBL Compositing (`deferred_lighting_ps.hlsl:1184-1377`)

The IBL path computes:
1. Diffuse = kD * albedo * irradiance (SH L2)
2. Specular = prefilteredColor * (F0 * envBRDF.x + F90 * envBRDF.y)
3. Multiscatter IBL = FmsEms * irradiance * 0.5
4. Horizon occlusion = saturate(1 + dot(R, N))^2
5. Specular occlusion = saturate(pow(NdotV + ao, exp2(-16*r - 1)) - 1 + ao)
6. Ambient = (diffuseIBL * AOMultiBounce(ao) + specularIBL * specOcc) * skyLightIntensity

**Verdict**: CORRECT. The split-sum IBL with F90 micro-occlusion (line 1245-1246) is right. The Marmoset horizon occlusion is standard. The specular occlusion formula (`pow(NdotV + ao, exp2(-16*r-1)) - 1 + ao`) matches the Jimenez GTAO special occlusion term.

### 3.5 Final Composition (`deferred_lighting_ps.hlsl:1405`)

```hlsl
float3 color = ambient + Lo + emissiveContrib;
```

Then aerial perspective, then output as linear HDR (tonemapping in separate pass). **Correct pipeline**.

---

## 4. Post-Process Quality Assessment

### 4.1 GTAO (`gtao_cs.hlsl`) — Grade: B+

The GTAO implementation is algorithmically correct:
- Horizon search with dual directions (positive + negative) — correct
- Thickness heuristic blend — correct (0.5 default matches reference)
- Inner integral (`IntegrateHorizonArc`) — verified against Jimenez 2016 formulas
- Temporal rotation via `GetRandomVector` with per-frame angle cycling — correct
- Distance fade — correct (linear fade beyond 50000 cm)
- `acosFast` polynomial — correct (Penner 2011, max error ~0.018 rad)

**One issue found**: `GTAO_AO_POWER = 0.3f` (line 107).

The comment at line 103-106 says:
```hlsl
/// AO power curve exponent. reference default AmbientOcclusionPower = 2.0 (Scene.cpp:528),
/// GTAO path multiplies by 0.5, effective = 1.0.
#define GTAO_AO_POWER  0.3f
```

The comment describes the correct value (1.0) but the actual define is 0.3. This means:
```hlsl
ao = 1.0f - (1.0f - pow(abs(ao), 0.3f)) * 0.5f;
```

With power=0.3, `pow(ao, 0.3)` will be MUCH higher than `pow(ao, 1.0)` for values in [0,1]. For example, `pow(0.5, 0.3) = 0.812` vs `pow(0.5, 1.0) = 0.5`. Combined with intensity=0.5, this makes the AO **much weaker** than the reference default. AO with power 0.3 barely darkens anything — it flattens the contrast that GTAO should provide.

**Fix**: Change `GTAO_AO_POWER` from `0.3f` to `1.0f` to match the reference default.

### 4.2 Film Tonemap (`film_tonemap.hlsli`) — Grade: A

This is a complete ACES RRT+ODT pipeline implementation:
- AP0/AP1 color space matrices — exact values from ACES CTL Reference (verified)
- Glow module (RRT_GLOW_GAIN=0.05, RRT_GLOW_MID=0.08) — correct
- Red modifier (RRT_RED_SCALE=0.82, RRT_RED_PIVOT=0.03, RED_WIDTH=135) — correct
- Pre-desaturation factor 0.96 — correct
- Post-desaturation factor 0.93 — correct
- Custom S-curve with 5 artist parameters (Slope/Toe/Shoulder/BlackClip/WhiteClip) — correct
- Piecewise sigmoid construction with Hermite blending — correct

**Default parameters** (from `post_process_volume.h:128-134`):
- filmSlope = 0.88 (UE5 default: 0.88) — MATCH
- filmToe = 0.55 (UE5 default: 0.55) — MATCH
- filmShoulder = 0.26 (UE5 default: 0.26) — MATCH
- filmBlackClip = 0.0 (UE5 default: 0.0) — MATCH
- filmWhiteClip = 0.04 (UE5 default: 0.04) — MATCH

**Verdict**: EXCELLENT. The consensus document questioned whether the ACES parameters match UE5 — **they do, exactly**.

Note: The consensus document cited UzEngine defaults as "Slope=0.91, Toe=0.45, Shoulder=0.22" — this is WRONG. The actual code has Slope=0.88, Toe=0.55, Shoulder=0.26, which exactly match UE5.

### 4.3 Bloom Downsample (`bloom_downsample_cs.hlsl`) — Grade: A

The 13-tap Jimenez 2014 downsample filter is implemented correctly:
- 13 bilinear taps covering a 4x4 texel region
- Weights: corners=0.03125, edges=0.0625, inner=0.125, center=0.125
- Weight sum = 4*0.03125 + 4*0.0625 + 4*0.125 + 0.125 = 1.0 (verified)
- `max(0, color)` prevents negative values from propagating

**Verdict**: CORRECT.

### 4.4 Contact Shadows (`contact_shadows.hlsli`) — Grade: B+

The ray casting is correct:
- Clip-space ray construction matches the reference
- Depth tolerance with `compareToleranceScale = 2.0` matches the reference
- Interleaved gradient noise for temporal dithering — correct
- 8 steps per ray — matches the reference default

**One minor deviation**: The contact shadow result uses a smooth distance-based falloff (`smoothstep(0, 1, t)` based on hit position along the ray) instead of the reference's binary hit result. This is documented at line 199-203 and produces smoother results. Acceptable deviation — produces better visual quality than the binary approach.

### 4.5 Shadow Sampling (`shadow_common.hlsli`) — Grade: A-

- 5x5 PCF with hardware comparison sampler — correct
- 16-tap Poisson disk with IGN per-pixel rotation — correct
- Cascade blending with linear interpolation at fade boundaries — correct
- Shadow sharpening (disabled by default at identity 1.0) — correct
- Static/dynamic shadow cache compositing with min() — correct
- Distance field shadow compositing — correct

The PCF over-blur correction (`shadow * shadow`) is commented out at line 424-425. The comment says it makes shadows darker than expected. This is a known issue — the squared shadow term is aggressive and may need a softer version (e.g., `lerp(shadow, shadow*shadow, 0.5)`) for visual parity.

---

## 5. Specific Shader Fixes Needed

### Fix 1: GTAO_AO_POWER — CRITICAL
**File**: `runtime/shaders/runtime/postprocess/gtao_cs.hlsl:107`
**Current**: `#define GTAO_AO_POWER  0.3f`
**Correct**: `#define GTAO_AO_POWER  1.0f`
**Why**: 0.3 makes AO nearly invisible. The comment on line 103-106 correctly describes the reference default as effectively 1.0 but the code uses 0.3. This single value dramatically weakens the entire GTAO contribution.

### Fix 2: Micro-Shadow TODO — HIGH
**File**: `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl:1038-1040`
**Current**: Micro-shadow code is disabled with a TODO comment about GTAO texture state.
**Analysis**: Looking at the code, the GTAO texture IS already being sampled successfully at lines 960-968 (the `gtaoSrvIndex` branch reads it without issues). The micro-shadow block that was planned here would use the same GTAO texture. Since the GTAO texture is demonstrably readable at line 962, the "SRV conflict" that the TODO mentions appears to have been resolved.
**Fix**: Implement the micro-shadow term. Reference approach:
```hlsl
// Micro-shadow: AO-based directional shadow for surface detail
// @ref Chan 2018 "The Technical Art of Uncharted 4" — micro-shadows
float microShadow = saturate(abs(dot(N, L_dir)) + 2.0f * ao * ao - 1.0f);
shadow *= microShadow;
```
This uses the GTAO `ao` value that is already computed and available at this point in the shader.

### Fix 3: Exposure Default — CRITICAL (C++ side)
**File**: `runtime/engine/renderer/frame/frame_setup.cpp:52`
**Current**: `UZ_CVAR_FLOAT("r.Exposure", 1.4f, ...)`
**Correct**: `UZ_CVAR_FLOAT("r.Exposure", 1.0f, ...)`
**Why**: 1.4 is 40% above neutral. Combined with ACES tonemapping, this overexposes highlights and washes out midtones.

### Fix 4: SkyLight Intensity Default — CRITICAL (C++ side)
**File**: `runtime/engine/renderer/frame/frame_setup.cpp:40`
**Current**: `UZ_CVAR_FLOAT("r.SkyLight.Intensity", 3.5f, ...)`
**Correct**: `UZ_CVAR_FLOAT("r.SkyLight.Intensity", 1.0f, ...)`
**Why**: 3.5x ambient floods shadows with light, destroying contrast.

### Fix 5: SkyLight MinAmbient Default — MEDIUM (C++ side)
**File**: `runtime/engine/renderer/frame/frame_setup.cpp:41`
**Current**: `UZ_CVAR_FLOAT("r.SkyLight.MinAmbient", 0.1f, ...)`
**Correct**: `UZ_CVAR_FLOAT("r.SkyLight.MinAmbient", 0.02f, ...)`
**Why**: 0.1 (10%) ambient floor prevents deep shadows. Reference has no ambient floor. 0.02 allows near-black shadows while preventing pure black.

---

## 6. Consensus Document Corrections

After reading the actual code, I found the following errors in `consensus-analysis.md`:

1. **ACES parameter claim is WRONG**: The consensus says UzEngine has "Slope=0.91, Toe=0.45, Shoulder=0.22". The actual code has Slope=0.88, Toe=0.55, Shoulder=0.26, which **exactly match** UE5 defaults. P1.4 in the strategic plan is unnecessary — parameters are already correct.

2. **Micro-shadow root cause is likely stale**: The TODO says "GTAO texture state conflict" but the GTAO texture is successfully sampled at line 960-968 of the same shader. The conflict may have been resolved in a later commit without re-enabling the micro-shadow path.

3. **GTAO weakness is NOT mentioned**: Neither document identifies the `GTAO_AO_POWER = 0.3` issue, which dramatically weakens AO contrast. This is arguably a larger visual impact than the micro-shadow TODO.

---

## 7. TOP 3 Shader-Level Recommendations

### Recommendation 1: Fix GTAO Power + Re-enable Micro-Shadows (HIGHEST IMPACT)

The GTAO power of 0.3 makes ambient occlusion nearly invisible, and the disabled micro-shadow removes surface contact detail. Together, these two issues explain much of the "flat, plastic" look.

**Changes**:
- `gtao_cs.hlsl:107` — change `GTAO_AO_POWER` from `0.3f` to `1.0f`
- `deferred_lighting_ps.hlsl:1038-1040` — replace the TODO with the Chan 2018 micro-shadow formula
- These are purely shader changes, no C++ changes needed

**Expected visual impact**: Dramatically improved surface contact detail. Crevices, grooves, and edges will show proper darkening. Combined with the CVar fixes (exposure + skylight), this will produce the most visible improvement.

### Recommendation 2: Implement SSR (Hi-Z Ray March)

SSR is the single largest missing feature. Without it, every metallic/reflective surface falls back to a blurry 128x128 sky cubemap. The Hi-Z pyramid already exists (`hiz_pyramid_cs.hlsl`), and the SSR SRV slot is already plumbed through the cbuffer (`ssrSrvIndex` at line 140) with compositing code already written (`deferred_lighting_ps.hlsl:1228-1240`). The GPU-side integration is already 80% done — only the two compute shaders (trace + resolve) are missing.

**Expected visual impact**: Transformative for reflective surfaces.

### Recommendation 3: PCF Over-Blur Correction (Quick Win)

The `shadow * shadow` PCF over-blur correction at `shadow_common.hlsli:424-425` is commented out. A softer version — `lerp(shadow, shadow * shadow, 0.5)` — would add shadow edge definition without making shadows too dark. This is a 1-line change.

**Expected visual impact**: Subtle but noticeable — sharper shadow penumbrae, slightly better contact shadow definition. Cost: zero.

---

## 8. Summary

| Area | Grade | Notes |
|------|-------|-------|
| GGX NDF | A | Exact match to published reference |
| Smith Visibility | A | Hammon 2017 approximation, correct |
| Burley Diffuse | A | Exact Disney 2012 formula |
| Fresnel (Schlick + F90) | A | Lagarde 2014 micro-occlusion, correct |
| Multiscatter Compensation | A | Kulla-Conty + Turquin 2018, correct |
| Energy Conservation | A | Correct chain: specAlbedo attenuates diffuse |
| IBL Split-Sum | A | F90 in EnvBRDF, horizon occlusion, spec occlusion |
| ACES Tonemap | A | Full RRT+ODT, exact parameters match UE5 |
| Bloom 13-tap | A | Jimenez 2014, energy-preserving weights |
| Shadow PCF/Poisson | A- | Correct, PCF over-blur correction disabled |
| Contact Shadows | B+ | Smooth falloff deviation (acceptable), 8 steps |
| GTAO | B+ | Algorithm correct, power 0.3 too weak |
| **Calibration (CVars)** | **D** | **Exposure 1.4, SkyLight 3.5 — both wrong** |
| **Micro-Shadow** | **F** | **Disabled (TODO)** |

**Bottom line**: The shader math is correct. The render looks bad because of 3 numeric values (exposure, skylight intensity, GTAO power) and 1 disabled feature (micro-shadows). Fix those four things and the render will look dramatically better before any new features are implemented.
