<!-- doc-meta
title: UE5 Metallic Behavior Without IBL — Analysis
project: UzEngine
created: 2026-03-23
keywords: metallic, IBL, deferred lighting, UE5, environment lighting
-->

# UE5 Metallic Behavior Without IBL

## Research Question

When `metallic=1.0` and there is NO SkyLight or reflection captures available (no IBL), does UE5:
1. Show pure black on unlit sides of metallic objects?
2. Have any fallback lighting?
3. Apply diffuse compensation?

## Key Findings

### Metallic Color Decomposition (UE5 Engine)

UE5 splits base color into diffuse and specular components based on metallic:

**Source**: `DeferredShadingCommon.ush`, lines 750-751

```hlsl
GBuffer.SpecularColor = ComputeF0(GBuffer.Specular, GBuffer.BaseColor, GBuffer.Metallic);
GBuffer.DiffuseColor = GBuffer.BaseColor - GBuffer.BaseColor * GBuffer.Metallic;
```

**Effect**:
- When `metallic=0`: `DiffuseColor = BaseColor`, `SpecularColor = 0.04` (typical dielectric F0)
- When `metallic=0.5`: `DiffuseColor = BaseColor * 0.5`, `SpecularColor = BaseColor * 0.5` (metal F0)
- When `metallic=1.0`: `DiffuseColor = 0`, `SpecularColor = BaseColor` (pure metal, NO diffuse)

### Direct Lighting (Always Applied)

In deferred lighting, direct light (sun, point, spot) ALWAYS applies to both diffuse and specular:

**Source**: `DeferredLightingCommon.ush`, line 420 and 454

```hlsl
float3 LightingDiffuse = Diffuse_Lambert( GBuffer.DiffuseColor ) * Lighting;
```

This means:
- When `metallic=1.0`, `DiffuseColor=0`, so **no diffuse response from ANY directional or point light**
- Only specular (GGX/Cook-Torrance) responds to direct light
- **Result**: Direct light ONLY shows highlight, no global light distribution

### Indirect Lighting Pipeline (SEPARATE PASS)

Indirect lighting (sky light + reflections) is applied in a completely separate pass via `AddSkyReflectionPass()`.

**Source**: `IndirectLightRendering.cpp`, lines 2146-2159

This pass:
1. Reads the GBuffer (including metallic)
2. Applies sky light diffuse to `DiffuseColor`
3. Applies specular reflections to `SpecularColor`

**What happens with NO IBL**:
- If `Scene->SkyLight == nullptr` OR `!SkyLight->ProcessedTexture`, the sky light contribution is **zero**
- If no reflection captures exist, reflection contribution is **zero**
- **Result**: Metallic surface receives NO indirect lighting

### The Answer: YES, Pure Black Without IBL

When `metallic=1.0` and there is NO SkyLight or reflection captures:

1. **Direct lighting works normally** — specular highlights respond to sun/lights
2. **Indirect lighting is zero** — no sky light diffuse, no reflections
3. **Result**: Backlit side = **PURE BLACK**
4. **Expected behavior in real UE5**:
   - Frontlit side: Bright specular highlight
   - Backlit side: Completely black (physically accurate, no scattered light)
   - Side angle: Specular reflection of sky (if sky texture exists), else nothing

### Why This Happens

UE5 design philosophy:
- **Metallic materials have NO diffuse lobe** — all light comes from specular reflection
- **Indirect light = specular reflection of environment** — no environment = no reflection
- **This is physically correct**: metals only reflect light at grazing angles (Fresnel), they don't scatter like diffuse materials
- There is NO fallback ambient or "fake" diffuse for metals

### Comparison: Non-Metallic (metallic=0)

With `metallic=0` and NO IBL:
- **Direct lighting**: Full diffuse + specular from all lights
- **Indirect lighting**: Zero (same situation)
- **Backlit side**: Receives NO direct light, but appears dark gray (still gets some light spread from side angles)
- **Result**: Much brighter overall, especially on unlit surfaces

## Implementation Notes for UzEngine

If replicating this in UzEngine:

1. **Ensure metallic properly scales diffuse color**:
   ```cpp
   diffuseColor = baseColor * (1.0f - metallic);
   specularColor = ComputeF0(specular, baseColor, metallic);
   ```

2. **Direct lighting must respect this split**:
   - Apply `Diffuse_Lambert(diffuseColor)` for diffuse term
   - Apply `GGX(specularColor, roughness)` for specular term

3. **Indirect lighting is SEPARATE**:
   - Sky light diffuse (SH/cube) applies to `diffuseColor` only
   - Reflections apply to `specularColor` only
   - When IBL is missing, both are zero

4. **Visual consequence**:
   - If your metallic objects look too dark backlit, it's CORRECT (you just need IBL)
   - If they look too bright, your metallic calculation is wrong (probably applying diffuse to metals)

## UE5 Source Files

- **Core deferred lighting**: `Engine/Shaders/Private/DeferredLightingCommon.ush:420,454`
- **GBuffer decomposition**: `Engine/Shaders/Private/DeferredShadingCommon.ush:750-751`
- **Sky + reflection pass**: `Renderer/Private/IndirectLightRendering.cpp:2146-2159`
- **IBL application**: `Renderer/Private/IndirectLightRendering.cpp:1987-2010`

