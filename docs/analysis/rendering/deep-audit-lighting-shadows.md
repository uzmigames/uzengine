<!-- doc-meta
title: Deep Audit — Lighting & Shadow Shaders vs UE5 5.6
author: shader-engineer
date: 2026-03-18
status: complete
-->

# Deep Audit: Lighting & Shadow Shaders vs UE5 5.6

Line-by-line comparison of UzEngine deferred lighting, PBR BRDF, shadow sampling,
PCSS, and contact shadows against UE5 5.6 source at `W:\UE_5.6_Source`.

---

## Summary

| Severity | Count |
|----------|-------|
| HIGH     | 3     |
| MEDIUM   | 5     |
| LOW      | 4     |
| Total    | 12    |

---

## HIGH Severity Issues

### ISSUE 1: Vis_SmithJointApprox receives wrong `a2` parameter

**UE5**: `ShadingModels.ush:215,220`
```hlsl
float a2 = Pow4( Roughness );               // a2 = roughness^4
float Vis = Vis_SmithJointApprox( a2, ... ); // passes roughness^4
// Inside (BRDF.ush:348): float a = sqrt(a2) = roughness^2
```

**UzEngine**: `pbr_common.hlsli:264-265` + `deferred_lighting_ps.hlsl:631`
```hlsl
float alpha = roughness * roughness;                    // alpha = roughness^2
float Vis = Vis_SmithJointApprox(alpha, NdotV, NdotL);  // passes roughness^2
// Inside (pbr_common.hlsli:141): float a = sqrt(a2) = roughness (NOT roughness^2)
```

**FIX**: In `EvaluateBRDF()`, pass `alpha * alpha` (i.e. `roughness^4`) to
`Vis_SmithJointApprox`, matching UE5's `Pow4(Roughness)`. The `DistributionGGX`
call is correct because it squares `alpha` internally to get `roughness^4`.

**IMPACT**: HIGH — The visibility/geometry term is computed with `a = roughness`
instead of `a = roughness^2`. This makes specular highlights on rough surfaces
too bright and narrow. At roughness=0.5, UE5 uses `a=0.25` but UzEngine uses
`a=0.5` — a 2x difference in the masking-shadowing function. At roughness=1.0,
both converge to 1.0 so the error is less visible on very rough surfaces.

---

### ISSUE 2: F_Schlick missing micro-occlusion (F90 != 1.0)

**UE5**: `BRDF.ush:376-382`
```hlsl
float3 F_Schlick( float3 SpecularColor, float VoH )
{
    float Fc = Pow5( 1 - VoH );
    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    return saturate( 50.0 * SpecularColor.g ) * Fc + (1 - Fc) * SpecularColor;
}
```

**UzEngine**: `pbr_common.hlsli:94-96`
```hlsl
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0f - F0) * Pow5(saturate(1.0f - cosTheta));
}
```

UE5 uses `F90 = saturate(50.0 * SpecularColor.g)` which clamps the grazing-angle
reflectance for very low F0 values. For dielectrics with F0=0.04: `F90 = saturate(50*0.04) = 1.0`
(unchanged). But for specular values below 0.02 (sub-2% which UE5 considers
micro-occlusion/shadowing), F90 is reduced, preventing physically impossible
bright rims.

**FIX**: Change `FresnelSchlick` to:
```hlsl
float3 FresnelSchlick(float cosTheta, float3 F0) {
    float Fc = Pow5(1.0f - cosTheta);
    return saturate(50.0f * F0.g) * Fc + (1.0f - Fc) * F0;
}
```

**IMPACT**: HIGH — For standard PBR materials (F0=0.04), the visual difference
is minimal since `saturate(50*0.04)=1.0`. But for any material with specular < 0.02,
UzEngine will show bright grazing-angle reflections that UE5 suppresses. This
affects materials like cloth, certain foliage, and any artistic overrides with
very low specular.

---

### ISSUE 3: Per-light SpecularScale and DiffuseScale are ignored

**UE5**: `DeferredLightingCommon.ush:447-448`
```hlsl
Lighting.Specular *= LightData.SpecularScale;
Lighting.Diffuse  *= LightData.DiffuseScale;
```

**UzEngine**: `deferred_lighting_ps.hlsl:148-156` (struct) + lines 688-726 (loop)

The `PointLight` and `SpotLight` structs declare `specularScale` and `diffuseScale`
fields, but the lighting loops at lines 688-726 (points) and 731-793 (spots)
never apply them. All lights use implicit scale=1.0.

**FIX**: After `EvaluateBRDF()` returns `(diffuse + specular) * radiance * NdotL`,
refactor to return diffuse and specular separately, then multiply:
```hlsl
// Until refactor, apply as a combined scale after BRDF:
float3 brdf = EvaluateBRDF(...);
// Approximate: scale total by diffuseScale for NdotL-dominant term
Lo += brdf * pointLights[i].diffuseScale; // simplified
```

Or better: split `EvaluateBRDF` into diffuse + specular returns and scale each
independently, matching UE5's `FDirectLighting` struct pattern.

**IMPACT**: HIGH — Artists cannot control per-light specular/diffuse contribution.
Any light with non-1.0 SpecularScale or DiffuseScale will render incorrectly.
This blocks fill lights (diffuseScale=1, specularScale=0), rim lights (opposite),
and any artistic light setups.

---

## MEDIUM Severity Issues

### ISSUE 4: NdotV clamped with `max(dot, 0.001)` instead of UE5's `saturate(abs(NoV) + 1e-5)`

**UE5**: `ShadingModels.ush:295`
```hlsl
Context.NoV = saturate(abs( Context.NoV ) + 1e-5);
```

**UzEngine**: `pbr_common.hlsli:258`
```hlsl
float NdotV = max(dot(N, V), 0.001f);
```

UE5 uses `abs()` to handle back-facing normals (negative NdotV), then adds `1e-5`
to prevent exact zero. UzEngine uses `max(..., 0.001)` which is 100x larger than
UE5's epsilon and does not handle negative NdotV (it clamps them to 0.001 instead
of using their absolute value).

**FIX**: Change to `saturate(abs(dot(N, V)) + 1e-5f)`.

**IMPACT**: MEDIUM — Back-facing pixels (interpolated normals crossing over) will
have wrong specular. The 0.001 vs 1e-5 difference is negligible visually but
`abs()` vs `max(0)` matters for surfaces seen from behind (e.g., thin vegetation,
double-sided materials). Also affects energy compensation calculations.

---

### ISSUE 5: UE5 default diffuse is Lambert, not Burley

**UE5**: `ShadingModels.ush:302`
```hlsl
#if MATERIAL_ROUGHDIFFUSE
    Lighting.Diffuse = Diffuse_Chan(...); // or Diffuse_Burley
#else
    Lighting.Diffuse = Diffuse_Lambert(GBuffer.DiffuseColor);
#endif
```

**UzEngine**: `pbr_common.hlsli:281` — always uses `DiffuseBurley()`.

UE5's default DefaultLit shading model uses **Lambert** diffuse, not Burley. Burley
(or Chan in UE5.6) is only used when `MATERIAL_ROUGHDIFFUSE` is explicitly enabled
per-material. This is a deliberate UE5 choice: Burley is more expensive and the
visual difference vs Lambert is subtle for most materials.

**FIX**: Use Lambert as default, add `MATERIAL_ROUGHDIFFUSE` variant for materials
that explicitly request Burley/Chan diffuse.

**IMPACT**: MEDIUM — Burley diffuse adds retroreflective brightening at grazing
angles on rough surfaces. While not "wrong", it doesn't match UE5's default
visual result. The difference is most visible on very rough diffuse surfaces
(roughness > 0.7) at grazing angles.

---

### ISSUE 6: IBL EnvBRDF missing micro-occlusion term

**UE5**: `BRDF.ush:565-572`
```hlsl
half3 EnvBRDF( half3 SpecularColor, half Roughness, half NoV )
{
    float2 AB = Texture2DSampleLevel(PreIntegratedGF, ..., float2(NoV, Roughness), 0).rg;
    float3 GF = SpecularColor * AB.x + saturate(50.0 * SpecularColor.g) * AB.y;
    return GF;
}
```

**UzEngine**: `deferred_lighting_ps.hlsl:828`
```hlsl
float3 specularIBL = prefilteredColor * (F * envBRDF.x + envBRDF.y);
```

UE5's `EnvBRDF` uses `saturate(50.0 * SpecularColor.g)` as the F90 term for the
split-sum bias (AB.y). UzEngine uses `F` (full Fresnel-Schlick-Roughness) for
AB.x and plain `1.0` (implicitly, via `envBRDF.y` unweighted) for AB.y.

**FIX**: Change to:
```hlsl
float F90 = saturate(50.0f * F0.g);
float3 specularIBL = prefilteredColor * (F0 * envBRDF.x + F90 * envBRDF.y);
```

**IMPACT**: MEDIUM — Affects IBL specular on materials with very low specular
values (< 0.02). For standard materials this difference is invisible.

---

### ISSUE 7: IBL diffuse uses Fresnel-Schlick-Roughness instead of energy preservation

**UE5**: `ShadingModels.ush:324-327`
```hlsl
FBxDFEnergyTerms EnergyTerms = ComputeGGXSpecEnergyTerms(Roughness, NoV, SpecularColor);
Lighting.Diffuse *= ComputeEnergyPreservation(EnergyTerms);
// ComputeEnergyPreservation = 1 - GetEnergyLuminance(EnergyTerms.E)
```

**UzEngine**: `deferred_lighting_ps.hlsl:806-807`
```hlsl
float3 F = FresnelSchlickRoughness(NdotV, F0, roughness);
float3 kD = (1.0f - F) * (1.0f - metallic);
```

UE5 uses `ComputeEnergyPreservation()` which computes `1 - luminance(specularAlbedo)`
from the energy conservation terms. UzEngine uses `(1-F)*(1-metallic)` which is
the pre-energy-conservation formulation.

**FIX**: Since UzEngine already computes `ComputeEnergyCompensation()` in the
direct lighting path, use the same approach for IBL:
```hlsl
float4 ec = ComputeEnergyCompensation(F0, roughness, NdotV);
float3 kD = (1.0f - ec.w) * (1.0f - metallic);
```

**IMPACT**: MEDIUM — The old `(1-F)*(1-metallic)` approach overestimates diffuse
contribution on smooth metals and underestimates it on rough dielectrics. The
energy compensation system is already implemented for direct lighting but not
applied to IBL.

---

### ISSUE 8: Contact shadow CompareToleranceScale uses ViewToClip instead of TranslatedWorldToClip for depth tolerance

**UE5**: `ScreenSpaceShadowRayCast.ush:23-24`
```hlsl
const float4 RayDepthClip = RayStartClip + mul(float4(0, 0, RayLength, 0), View.ViewToClip);
```

**UzEngine**: `contact_shadows.hlsli:87`
```hlsl
float4 rayDepthClip = rayStartClip + mul(float4(0, 0, rayLength, 0), vp);
```

UE5 uses `View.ViewToClip` (just the projection matrix, no view transform) for the
depth tolerance computation. UzEngine uses `vp` (the full view-projection matrix).
Since the offset is `(0,0,RayLength,0)` (a pure Z offset with w=0), using the full
view-projection vs just projection gives different results because the view rotation
will rotate the Z-offset vector.

**FIX**: Pass the projection matrix separately, or compute the depth tolerance
differently. For perspective projection, the depth tolerance can be approximated
as `abs(rayLength * vp[2][2]) * step * 2.0`.

**IMPACT**: MEDIUM — The depth comparison tolerance will be incorrect, potentially
causing false positives (contact shadows appearing on flat surfaces) or false
negatives (missing contact shadows on fine detail). The error depends on camera
orientation relative to the light direction.

---

## LOW Severity Issues

### ISSUE 9: AOMultiBounce parameter order swapped (cosmetic)

**UE5**: `DeferredShadingCommon.ush:1220`
```hlsl
half3 AOMultiBounce(half3 BaseColor, half AO)
```

**UzEngine**: `deferred_lighting_ps.hlsl:351`
```hlsl
float3 AOMultiBounce(float ao, float3 baseColor)
```

Parameter order is swapped but the body and call sites use them correctly. The
math is identical. This is purely cosmetic.

**IMPACT**: LOW — No functional difference. Consider swapping for consistency.

---

### ISSUE 10: DistributionGGX parameter naming — `alpha` vs `a2`

**UE5**: `BRDF.ush:284`
```hlsl
float D_GGX( float a2, float NoH )
{
    float d = ( NoH * a2 - NoH ) * NoH + 1;
    return a2 / ( PI*d*d );
}
```

**UzEngine**: `pbr_common.hlsli:72`
```hlsl
float DistributionGGX(float NdotH, float alpha) {
    float a2 = alpha * alpha;
    ...
}
```

UE5's `D_GGX` expects `a2` (alpha squared = roughness^4) directly. UzEngine's
`DistributionGGX` expects `alpha` (roughness^2) and squares it internally. Both
produce the same result because the call site passes `roughness^2` as alpha,
which gets squared to `roughness^4`. However, the naming inconsistency with
`Vis_SmithJointApprox` (which also takes `a2` in its name but UzEngine passes
`alpha`) is the root cause of **Issue 1**.

**IMPACT**: LOW — The NDF result is mathematically correct. But the parameter
naming confusion led to the Vis bug.

---

### ISSUE 11: NdotL clamped to 0.001 instead of 0.0

**UE5**: `ShadingModels.ush:260,302-304`
```hlsl
BRANCH if (NoL > 0.0f) { ... }
Lighting.Diffuse *= AreaLight.FalloffColor * (Falloff * NoL);
```

**UzEngine**: `pbr_common.hlsli:257`
```hlsl
float NdotL = max(dot(N, L), 0.001f);
```

UE5 branches on `NoL > 0.0` and only evaluates lighting when the surface faces
the light. UzEngine clamps NdotL to 0.001, which means back-facing pixels still
get a tiny amount of light (0.1% of the full contribution). While this prevents
division-by-zero in some formulas, it also causes light leaking on back faces.

**FIX**: Use `max(dot(N, L), 0.0f)` for the NdotL used in the final
`(diffuse + specular) * radiance * NdotL` multiplication, and only use the 0.001
epsilon inside the Vis function where division by NdotL occurs.

**IMPACT**: LOW — The 0.001 floor is barely visible but technically incorrect.
It can cause faint light leaking on the dark side of objects.

---

### ISSUE 12: IBL multiscatter uses Karis 2013 approximation instead of UE5 energy conservation system

**UE5**: `ShadingModels.ush:324-330`
```hlsl
FBxDFEnergyTerms EnergyTerms = ComputeGGXSpecEnergyTerms(Roughness, NoV, SpecularColor);
Lighting.Specular *= ComputeEnergyConservation(EnergyTerms);
```

**UzEngine**: `deferred_lighting_ps.hlsl:831-837`
```hlsl
float Ems = 1.0f - (envBRDF.x + envBRDF.y);
float3 Favg = F0 + (1.0f - F0) / 21.0f;
specularIBL *= 1.0f + Favg * Ems / (1.0f - Favg * Ems);
```

UzEngine uses the Karis 2013 multiscatter approximation from the original "Real
Shading in Unreal Engine 4" talk, while UE5 5.6 now uses the Turquin 2018 /
Kulla-Conty system via `ComputeGGXSpecEnergyTerms`. The direct lighting path
in UzEngine already uses the Turquin system (`ComputeEnergyCompensation`), but
the IBL path uses the older Karis approximation.

**FIX**: Replace the Karis IBL multiscatter block with the same
`ComputeEnergyCompensation` system used for direct lighting.

**IMPACT**: LOW — The Karis and Turquin approaches produce similar results for
most materials. The difference is most visible on very rough metals at grazing
angles where Turquin more accurately recovers lost energy.

---

## Verified Correct (No Issues)

| Feature | UE5 Reference | Status |
|---------|---------------|--------|
| D_GGX NDF formula | BRDF.ush:284-288 | CORRECT (alpha squared internally) |
| Vis_SmithJointApprox formula | BRDF.ush:346-351 | CORRECT (formula matches, but input parameter wrong — see Issue 1) |
| Diffuse_Burley formula | BRDF.ush:183-188 | CORRECT (FD90, FdV, FdL all match) |
| AOMultiBounce polynomial | DeferredShadingCommon.ush:1220-1226 | CORRECT (same coefficients) |
| SpotAttenuation formula | DynamicLightingCommon.ush:58-65 | CORRECT (squared smooth-step) |
| Inverse-square windowing | DeferredLightingCommon.ush:265 | CORRECT ((1-(d/r)^4)^2 windowing) |
| PCSS blocker search (16 Sobol) | ShadowPercentageCloserFiltering.ush:42-43 | CORRECT |
| PCSS filter (32 Sobol) | ShadowPercentageCloserFiltering.ush:46-47 | CORRECT |
| PCSS RandomFilterScale=0.75 | ShadowPercentageCloserFiltering.ush:142 | CORRECT |
| PCSS MaxDepthBiasSine=sin(5deg) | ShadowPercentageCloserFiltering.ush:126 | CORRECT |
| PCSS sharpening | ShadowPercentageCloserFiltering.ush:437-459 | CORRECT |
| Contact shadow ray march (8 steps) | DeferredLightingCommon.ush:197 | CORRECT |
| Contact shadow CompareToleranceScale=2.0 | DeferredLightingCommon.ush:68 | CORRECT |
| InterleavedGradientNoise constants | RandomInterleavedGradientNoise.ush:11-18 | CORRECT |
| GGXEnergyLookup analytic fit | ShadingEnergyConservation.ush:58-63 | CORRECT |
| ComputeEnergyCompensation (direct) | ShadingEnergyConservationTemplate.ush:44-64 | CORRECT |
| Shadow post-process (sharpen + square) | ShadowProjectionPixelShader.usf:375-387 | CORRECT |
| Cascade blend/fade logic | Similar to ShadowRendering.cpp cascade system | CORRECT |
| Sobol direction numbers | SobolRandom.ush:38-51 | CORRECT |
| UniformSampleDiskConcentricApprox | MonteCarlo.ush:139-153 | CORRECT |
| Emissive additive pass-through | BasePassPixelShader.usf:1541 | CORRECT |
| Aerial perspective integration order | SkyAtmosphereCommon.ush:107-108 | CORRECT |
| HeightFogContribution anti-double-fog | HeightFogCommon.ush:175 | CORRECT |

---

## Priority Fix Order

1. **Issue 1** (Vis_SmithJointApprox `a2` parameter) — affects ALL specular on ALL surfaces
2. **Issue 3** (SpecularScale/DiffuseScale ignored) — blocks artistic lighting control
3. **Issue 2** (F_Schlick micro-occlusion) — affects low-specular materials
4. **Issue 4** (NdotV clamping) — affects back-facing and grazing-angle rendering
5. **Issue 6** (EnvBRDF micro-occlusion) — affects IBL on low-specular materials
6. **Issue 7** (IBL diffuse energy preservation) — affects diffuse/specular balance
7. **Issue 8** (Contact shadow depth tolerance) — affects contact shadow accuracy
8. **Issue 5** (Default Lambert vs Burley) — UE5 visual parity choice
9. **Issue 11** (NdotL 0.001 floor) — minor light leak
10. **Issue 12** (IBL multiscatter method) — minor quality difference
11. **Issue 10** (Parameter naming) — cosmetic
12. **Issue 9** (AOMultiBounce order) — cosmetic
