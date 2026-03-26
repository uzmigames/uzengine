<!-- doc-meta
title: Metallic Shading Reference — Implementation Patterns from UE5
project: UzEngine
created: 2026-03-23
keywords: metallic, PBR, F0, DiffuseColor, SpecularColor, deferred shading
-->

# Metallic Shading Reference

## Color Decomposition

### UE5 Pattern

```hlsl
// From DeferredShadingCommon.ush:750-751
GBuffer.SpecularColor = ComputeF0(GBuffer.Specular, GBuffer.BaseColor, GBuffer.Metallic);
GBuffer.DiffuseColor = GBuffer.BaseColor - GBuffer.BaseColor * GBuffer.Metallic;
```

This creates a clean split where metallic interpolates between two materials:

### Semantics

- **BaseColor**: Artist-provided material color (linear RGB)
- **Metallic**: [0, 1] blend factor
  - `0.0` = Dielectric (plastic, wood, stone)
  - `1.0` = Metal (gold, copper, iron)
- **Specular**: F0 coefficient for dielectrics (unused for metals)

### Color Output

| Metallic | DiffuseColor | SpecularColor |
|----------|--------------|---------------|
| 0.0 | BaseColor | ~0.04 (typical dielectric) |
| 0.5 | BaseColor × 0.5 | BaseColor × 0.5 + dielectric |
| 1.0 | 0 | BaseColor (metal F0) |

### Why Subtract?

```
DiffuseColor = BaseColor * (1 - Metallic)
```

Ensures energy conservation: as metallic increases, diffuse decreases. A fully metallic surface reflects ALL light specularly and has NO diffuse scattering.

## Shading Application

### Direct Lighting (Directional/Point/Spot)

```hlsl
// DeferredLightingCommon.ush:420
float3 LightingDiffuse = Diffuse_Lambert(GBuffer.DiffuseColor) * Lighting;

// And separately:
float3 LightingSpecular = GGX_BRDF(GBuffer.SpecularColor, GBuffer.Roughness, N, L, V) * Lighting;

// Combined:
float3 DirectLighting = LightingDiffuse + LightingSpecular;
```

**Effect on metallic=1.0**:
- `DiffuseColor = 0` → `LightingDiffuse = 0`
- `LightingSpecular` = Full GGX response (mirror-like)
- **Result**: Only specular highlights visible from direct light

### Indirect Lighting (Sky Light + Reflections)

These are applied in a separate pass (NOT part of deferred light pixel shaders):

```cpp
// IndirectLightRendering.cpp:2146-2159
AddSkyReflectionPass(
    // ... applies BOTH:
    // 1. Sky light diffuse → multiplied by DiffuseColor
    // 2. Reflection capture/probe → multiplied by SpecularColor
);
```

**Effect on metallic=1.0 with NO IBL**:
- Sky light diffuse contribution: `0 * DiffuseColor = 0`
- Reflection contribution: `0 (no env texture)`
- **Result**: Zero indirect lighting

## C++ Pattern (for UzEngine)

```cpp
// Material shading computation
struct FPBRMaterial {
    float3 baseColor;
    float metallic;      // [0, 1]
    float roughness;     // [0, 1]
    float specular;      // 0.04 for typical dielectrics
};

struct FDecomposedColor {
    float3 diffuse;
    float3 specular;
};

FDecomposedColor DecomposeMaterial(FPBRMaterial mat) {
    FDecomposedColor out;

    // Specular color = either metal F0 or dielectric F0
    float3 f0_dielectric = float3(specular, specular, specular);
    float3 f0_metal = mat.baseColor;
    out.specular = lerp(f0_dielectric, f0_metal, mat.metallic);

    // Diffuse = baseColor modulated by (1 - metallic)
    out.diffuse = mat.baseColor * (1.0f - mat.metallic);

    return out;
}
```

## Shading Math

### Diffuse Term

```hlsl
float3 Diffuse_Lambert(float3 diffuseColor) {
    return diffuseColor * INV_PI;  // Normalized for area lights
}
```

**Only applied when**: `DiffuseColor > 0` (i.e., `metallic < 1.0`)

### Specular Term (GGX Cook-Torrance)

```hlsl
float3 GGX_BRDF(
    float3 specularColor,  // F0
    float roughness,
    float3 N, float3 L, float3 V) {

    float NoL = max(0.0, dot(N, L));
    float3 H = normalize(L + V);
    float NoH = max(0.0, dot(N, H));
    float VoH = max(0.0, dot(V, H));

    // Distribution (GGX)
    float D = GGX_D(roughness, NoH);

    // Geometry (Smith)
    float G = GGX_G(roughness, NoL, NoV);

    // Fresnel (Schlick)
    float3 F = Fresnel_Schlick(specularColor, VoH);

    return (F * D * G) / (4.0 * NoL * NoV + 1e-5);
}
```

**Applied to**: Both diffuse (metallic=0) and metallic (metallic=1) materials
- For dielectrics, `specularColor = 0.04` → small specular highlight
- For metals, `specularColor = baseColor` → bright mirror-like reflection

## Test Cases for UzEngine

### Case 1: Metallic Gold Sphere, Directional Light Only

**Setup**:
- Material: baseColor=(1.0, 0.84, 0.0), metallic=1.0, roughness=0.3
- Lighting: Single directional light, NO sky light
- Expected:
  - **Frontlit side**: Bright golden specular highlight (mirror-like)
  - **Backlit side**: Pure black (metallic has NO diffuse)
  - **Silhouette**: Sharp boundary, no light wraparound

**Wrong behavior** (common mistakes):
- Backlit side has gray tone → DiffuseColor not properly zeroed
- Highlight too broad → roughness too high or GGX wrong
- Highlight looks plastic → F0 not set to baseColor for metals

### Case 2: Metallic vs Dielectric, Same Base Color

**Setup**:
- Two spheres, same baseColor=(0.5, 0.5, 0.5)
- Sphere 1: metallic=0.0 (gray plastic)
- Sphere 2: metallic=1.0 (chrome)
- Lighting: Directional light, NO IBL

**Expected**:
- **Plastic**: Bright on frontlit side (full diffuse), dim gray on backlit (still receives some light from shallow angles)
- **Chrome**: Bright highlight on frontlit, **pure black backlit** (zero diffuse)
- **Key difference**: Backlit is much darker for chrome

### Case 3: With IBL Added

**Setup**: Same as Case 2, but add sky light

**Expected**:
- **Plastic**: Already bright, sky light makes backlit side visible (light blue tint)
- **Chrome**: Backlit NOW VISIBLE (reflects sky blue), **not black anymore**
- **Key insight**: Metal surfaces come alive with IBL (reflections)

## Validation Checklist

For UzEngine shader/math verification:

- [ ] `DiffuseColor = BaseColor * (1 - Metallic)` — exact formula
- [ ] When metallic=1.0, DiffuseColor=0 — verify in deferred pass
- [ ] When metallic=0.0, SpecularColor approaches 0.04 — verify F0 computation
- [ ] Direct lighting applies BOTH diffuse and specular — verify accumulation
- [ ] Indirect lighting (IBL) is SEPARATE from direct — verify pass order
- [ ] Backlit metallic = black without IBL — visual test
- [ ] Backlit non-metallic = dim gray without IBL — visual test (plastic should be brighter)

## Related UE5 Functions

From `Engine/Shaders/Private/BRDF.ush`:

```hlsl
float3 ComputeF0(float Specular, float3 BaseColor, float Metallic) {
    return lerp(Specular.xxx * 0.08, BaseColor, Metallic);
}
```

This is the authoritative UE5 implementation of metal F0 computation:
- Dielectric: `Specular * 0.08` (0.04 is the default)
- Metal: `BaseColor`
- The 0.08 factor accounts for normalized dielectric F0

