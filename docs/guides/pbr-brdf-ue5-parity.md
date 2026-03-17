# PBR BRDF — UE5 Parity Reference

<!-- doc-meta
title: PBR BRDF — UE5 Parity Reference
category: guide
engine: uzengine
theme: rendering
created: 2026-03-17
updated: 2026-03-17
status: active
author: docs-operator
-->

## Overview

UzEngine implements a physically-based rendering (PBR) pipeline using the Cook-Torrance microfacet model with exact formula parity to UE5. The BRDF decomposes light reflection into diffuse (Lambertian + Burley) and specular (GGX + Smith + Schlick Fresnel) components, with energy compensation to prevent over-darkening at grazing angles.

## BRDF Components

### Diffuse: Disney Burley BRDF

Burley diffuse models roughness-dependent edge darkening (retroreflection loss at grazing angles):

```hlsl
float3 Diffuse_Burley(float3 baseColor, float roughness, float NdotL, float NdotV, float LdotH) {
    float FD90 = 0.5 + 2.0 * LdotH * LdotH * roughness;
    float lightScatter  = 1.0 + (FD90 - 1.0) * Pow5(saturate(1.0 - NdotL));
    float viewScatter   = 1.0 + (FD90 - 1.0) * Pow5(saturate(1.0 - NdotV));
    return baseColor * lightScatter * viewScatter / PI;
}
```

**Parameters**:
- `baseColor`: Diffuse reflectance (0–1).
- `roughness`: Surface roughness (0–1). Higher = broader retroreflection falloff.
- `NdotL`: Dot(N, L), dot product of normal and light direction.
- `NdotV`: Dot(N, V), dot product of normal and view direction.
- `LdotH`: Dot(L, H), dot product of light and half-vector.

**Derivation**: Disney's principled shader (Burley 2012). Accounts for subsurface scattering at shallow angles; more realistic than pure Lambertian (1/π) for matte surfaces.

**Source**: UE5 `BRDF.ush:183-188` — `Diffuse_Burley`.

### Specular Distribution: GGX Normal Distribution Function (NDF)

GGX describes the statistical distribution of microfacet normals relative to the macrosurface normal:

```hlsl
float D_GGX(float NdotH, float alpha) {
    float a2    = alpha * alpha;
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}
```

**Parameters**:
- `NdotH`: Dot(N, H), dot product of normal and half-vector.
- `alpha`: Perceptual roughness squared (r²). Range [MIN_ROUGHNESS², 1].
- `MIN_ROUGHNESS`: 0.04 (prevents singularity at fully smooth surfaces).

**Interpretation**: Specifies the probability density of microfacets aligned to the half-vector H. Broader NDF (higher roughness) spreads the specular lobe.

**Source**: UE5 `BRDF.ush:71-80` — `D_GGX`.

### Specular Visibility: Smith Joint Approximation

The Smith geometry function models microfacet shadowing and masking (self-occlusion at grazing angles):

```hlsl
float Vis_SmithJointApprox(float NdotL, float NdotV, float alpha) {
    float a2 = alpha * alpha;
    float vis_L = NdotV * sqrt(NdotL * (NdotL - NdotL * a2) + a2);
    float vis_V = NdotL * sqrt(NdotV * (NdotV - NdotV * a2) + a2);
    return 0.5 / max(vis_L + vis_V, 0.001);
}
```

**Parameters**:
- `NdotL`, `NdotV`: Dot products of normal with light and view directions.
- `alpha`: Same as in GGX NDF.

**Interpretation**: Returns a visibility scalar (0–1) that attenuates the specular lobe as both light and view angles approach grazing. At grazing angles, microfacets occlude each other, reducing reflectance.

**Source**: UE5 `BRDF.ush:101-113` — `Vis_SmithJointApprox`.

### Specular Fresnel: Schlick Approximation

The Fresnel equations describe the fraction of light reflected (vs. refracted) based on the angle of incidence:

```hlsl
float3 FresnelSchlick(float cosTheta, float3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}
```

**Parameters**:
- `cosTheta`: Dot(V, H), angle between view and half-vector.
- `F0`: Reflectance at normal incidence (0° angle).
  - **Dielectric (non-metal)**: F0 ≈ (0.04, 0.04, 0.04) regardless of base color.
  - **Metal**: F0 = base color (reflectance depends on material).

**Interpretation**: At normal incidence (V aligned to N), reflectance = F0. As the angle increases (grazing angles, cosTheta → 0), reflectance → 1 (nearly full reflection).

**Source**: UE5 `BRDF.ush:58-66` — `FresnelSchlick`.

## Energy Compensation (Kulla-Conty / Turquin 2018)

A critical correction for high-roughness surfaces: the Cook-Torrance BRDF loses energy at grazing angles due to the Smith visibility function. Without compensation, rough metallic surfaces appear artificially dark.

### GGX Energy Lookup (Analytic Fit)

UE5 uses an analytic polynomial fit to approximate the energy correction:

```hlsl
float GGXEnergyLookup(float NdotV, float roughness) {
    // Polynomial approximation of GGX energy integral
    // (derived from tabulated lookup in UE5)
    float r = saturate(roughness);
    float c = (1.0 - 1.0 * exp2((-34.55f * r * r - 46.0f * r - 10.0f) * NdotV * NdotV / (1.0 - NdotV * NdotV)));
    return c;  // 0-1, correction factor
}
```

**Purpose**: Returns a per-pixel energy correction scalar that modulates the specular contribution to prevent under-darkening at grazing angles.

**Source**: UE5 `BRDF.ush:206-216` — `GGXEnergyLookup`.

### Per-Pixel Compensation (Not Per-Light)

**Critical Detail**: Energy compensation is applied *per-pixel* (based on the view direction), not per-light. This ensures consistency regardless of light count.

```hlsl
// In deferred_lighting_ps.hlsl:
float3 specular_light = computeSpecularLighting(...);  // Sum over all lights
float energyCompensation = GGXEnergyLookup(NdotV, roughness);
specular_light *= (1.0 + energyCompensation);  // Boost high-grazing angles
```

**Reasoning**: The view direction determines the viewing angle (NdotV). A surface seen at grazing loses energy to *all* lights equally, so correction is per-pixel, not per-light.

## Metal vs Dielectric

The metallic parameter controls F0 and base color:

### Dielectric (Metallic = 0)
```hlsl
float3 F0 = lerp((0.04, 0.04, 0.04), baseColor, metallic);
```

- Dielectrics have fixed F0 (0.04); base color is diffuse reflectance.
- Very little specular color variation (always grayish at normal incidence).

### Metal (Metallic = 1)
```hlsl
float3 F0 = baseColor;
```

- Metals have high F0 that matches the material color (gold, copper, iron).
- No diffuse term (metals are pure specular).

### Blended (0 < Metallic < 1)
```hlsl
float3 F0 = lerp((0.04, 0.04, 0.04), baseColor, metallic);
float3 diffuse = baseColor * (1.0 - metallic);  // Diffuse fades with metallic
```

Standard lerp blend for mixed metallic values.

**Source**: UE5 `DeferredShadingCommon.ush:400-430` — Metal shading model.

## Roughness Remapping

Perceptual roughness (artist input, 0–1) is remapped to alpha (microfacet roughness squared) to decorrelate visual non-linearity:

```hlsl
float perceptualRoughness = 0.5;  // Artist input
float alpha = perceptualRoughness * perceptualRoughness;  // Squared for microfacet
alpha = max(alpha, MIN_ROUGHNESS);  // Clamp to prevent singularity
```

**Reasoning**: A perceptual roughness slider from 0 to 1 should feel linear to artists, but the GGX NDF's visual non-linearity requires squaring to compensate.

**MIN_ROUGHNESS = 0.04** prevents a fully smooth surface (alpha = 0) which would create a delta-function specular lobe (numerical instability on GPU).

**Source**: UE5 `BRDF.ush:1-20` — Roughness constant definitions.

## Complete Cook-Torrance BRDF

The final BRDF is computed as:

```hlsl
float3 BRDF(float3 baseColor, float metallic, float roughness,
           float3 N, float3 V, float3 L) {
    float3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0001);
    float NdotV = max(dot(N, V), 0.0001);
    float LdotH = saturate(dot(L, H));
    float NdotH = saturate(dot(N, H));

    // Fresnel
    float3 F0 = lerp(0.04, baseColor, metallic);
    float3 F = FresnelSchlick(LdotH, F0);

    // Diffuse (Burley)
    float3 diffuse = Diffuse_Burley(baseColor * (1.0 - metallic), roughness,
                                     NdotL, NdotV, LdotH);

    // Specular (GGX + Smith + Schlick)
    float alpha = roughness * roughness;
    float D = D_GGX(NdotH, alpha);
    float Vis = Vis_SmithJointApprox(NdotL, NdotV, alpha);
    float3 specular = D * Vis * F;

    return diffuse + specular;
}
```

**Application in Deferred Lighting**:
```hlsl
float3 lighting = 0;
for (uint i = 0; i < lightCount; ++i) {
    float3 L = normalize(lights[i].pos - pixelPos);
    float3 brdf = BRDF(baseColor, metallic, roughness, normal, viewDir, L);
    lighting += brdf * lightIntensity * max(0, dot(normal, L));
}
lighting *= energyCompensationFactor;  // Per-pixel, not per-light
```

## Default Material Values

| Parameter | Dielectric | Metal |
|-----------|-----------|-------|
| Base Color | Artist-defined (0–1) | Reflectance color (gold/copper) |
| Metallic | 0 | 1 |
| Roughness | 0.5 | 0.2–0.5 |
| F0 @ Normal Incidence | 0.04 | baseColor |

**Common Material Presets**:
- **Plastic**: Metallic=0, Roughness=0.9, BaseColor=(0.8, 0.8, 0.8)
- **Polished Metal**: Metallic=1, Roughness=0.1, BaseColor=(0.972, 0.960, 0.915) [gold]
- **Brushed Metal**: Metallic=1, Roughness=0.4, BaseColor=(0.8, 0.8, 0.8) [steel]
- **Rust**: Metallic=0.3, Roughness=0.8, BaseColor=(0.8, 0.3, 0.1)

## Key Takeaways

- **Exact UE5 Formula Parity**: D_GGX, Vis_SmithJointApprox, FresnelSchlick, Diffuse_Burley are bit-for-bit compatible.
- **Roughness Squaring**: Perceptual roughness is squared before use in microfacet functions.
- **Energy Compensation**: Per-pixel (based on NdotV), not per-light. Critical for high-roughness metals.
- **Metal vs Dielectric**: F0 and diffuse term are lerped by metallic parameter.
- **MIN_ROUGHNESS = 0.04**: Prevents singularity at zero roughness.

## References

- UE5 `BRDF.ush` — Complete BRDF implementation
- UE5 `DeferredShadingCommon.ush:400-430` — Metal/non-metal shading
- UE5 `BRDF.ush:206-216` — `GGXEnergyLookup` analytic fit
- Burley 2012 — "Physically-Based Shading at Disney" (SIGGRAPH)
- Walter et al. 2007 — "Microfacet Models for Refraction through Rough Surfaces"
- Kulla & Conty 2017 — "Revisiting Physically Based Shading for Game Engines"
- Turquin 2018 — "Energy-Preserving Wrapping Diffuse and Specular" (GDC)
- [PBR Common Header](../../runtime/shaders/runtime/include/pbr_common.hlsli)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-17 | Initial creation — BRDF formulas, energy compensation, metal vs dielectric, material presets |
