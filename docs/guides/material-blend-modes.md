<!-- doc-meta
title: Material Blend Modes
category: rendering
theme: material-system
status: done
last-updated: 2026-03-24
-->

# Material Blend Modes

This guide explains UzEngine's four blend modes: **Opaque**, **Masked**, **Translucent**, and **Additive**. Each mode determines how pixels are blended with the framebuffer, which render pass evaluates the material, and the underlying pipeline state.

## Blend Mode Overview

| Mode | Enum | Value | Pass | Blend Equation | Depth Write | Cull | Use Case |
|------|------|-------|------|----------------|-----------__|------|----------|
| **Opaque** | `EBlendMode::Opaque` | `0` | Deferred G-Buffer | None | Yes | Back | Standard surfaces |
| **Masked** | `EBlendMode::Masked` | `1` | Deferred G-Buffer | None | Yes | None | Foliage, fences, gratings |
| **Translucent** | `EBlendMode::Translucent` | `2` | Forward (post-lighting) | SrcAlpha / InvSrcAlpha | No | None | Glass, water, fog |
| **Additive** | `EBlendMode::Additive` | `3` | Forward (post-lighting) | One / One | No | Back | Particles, emissive glows |

## Opaque (blendMode=0)

The default and most efficient blend mode. Opaque materials are rendered in the **Deferred G-Buffer pass** and fully participate in deferred lighting.

### Characteristics

- **Pass**: Deferred geometry (GBuffer pass)
- **Blend equation**: None (replace framebuffer)
- **Depth**: Write enabled, comparison = Less
- **Alpha test**: Disabled (all pixels written)
- **Cull mode**: Back-face cull (CullMode::Back)
- **Rendering order**: Early in frame (depth prepass candidate)
- **PSO**: `PipelineLibrary::GetGBuffer()`

### When to Use

- Standard static geometry (walls, floors, structures)
- Opaque character meshes
- Any surface that does not need transparency
- Default choice when in doubt

### Performance Notes

- Most efficient blend mode (minimal overdraw, full deferred benefits)
- Supports full deferred lighting (shadows, IBL, point/spot lights)
- Benefit from depth prepass optimization (Z-only rendering for early rejection)

### Example

```cpp
Material stoneMat;
stoneMat.blendMode = EBlendMode::Opaque;  // default
stoneMat.albedoPath = "textures/stone/albedo.png";
stoneMat.normalPath = "textures/stone/normal.png";
// ... no alpha test needed
```

## Masked (blendMode=1)

Masked materials support **alpha testing** — pixels below a threshold are discarded, creating cutout/stencil effects. Still rendered in the **Deferred G-Buffer pass** but with a two-sided PSO.

### Characteristics

- **Pass**: Deferred geometry (GBuffer pass, two-sided variant)
- **Blend equation**: None (discard or replace)
- **Depth**: Write enabled, comparison = Less
- **Alpha test**: Enabled (discard if opacity < alphaClipThreshold)
- **Cull mode**: No cull (CullMode::None) — two-sided rendering
- **Threshold**: `alphaClipThreshold` (default 0.3333, configurable)
- **PSO**: `PipelineLibrary::GetGBufferMasked()`

### How Alpha Testing Works

When a pixel's opacity value (from the opacity texture or scalar) falls below the threshold, the pixel shader executes `discard`, preventing any write to the framebuffer or depth buffer. This creates sharp cutout edges without smooth transparency.

The alpha test condition is:

```glsl
if (opacity < alphaClipThreshold) discard;
```

Where `opacity` comes from:
- **Opacity texture sample** (if `opacitySrvIndex > 0`)
- **`opacity` scalar multiplier** (if no texture)

### When to Use

- Foliage (trees, grass, bushes)
- Fences, gratings, chain-link
- Hair cards (layered planes)
- Torn/ragged geometry (rags, damaged surfaces)
- Any geometry that needs cutout silhouettes without smooth blending

### Performance Notes

- Two-sided rendering = reduced culling efficiency (both faces rendered)
- Still gets full deferred lighting (same as Opaque)
- Depth write enabled, so masked surfaces occlude translucent surfaces behind them
- Alpha test incurs small pixel shader cost (one comparison + discard)

### Adjusting the Threshold

```cpp
Material leafMat;
leafMat.blendMode = EBlendMode::Masked;
leafMat.opacitySrvIndex = heapIndex;  // opacity texture
leafMat.alphaClipThreshold = 0.5f;     // stricter (more pixels discarded)

Material wireFenceMat;
wireFenceMat.blendMode = EBlendMode::Masked;
wireFenceMat.alphaClipThreshold = 0.1f; // lenient (fewer pixels discarded)
```

### Comparison to Translucent

| Aspect | Masked | Translucent |
|--------|--------|-------------|
| Pass | Deferred | Forward |
| Alpha test | Binary (discard or keep) | Smooth (blends 0–1) |
| Edges | Sharp | Soft |
| Lighting | Full deferred | Forward (limited) |
| Depth write | Yes | No |
| Performance | High | Medium |

## Translucent (blendMode=2)

Translucent materials support smooth **alpha blending** and are rendered in a **separate forward pass** after deferred lighting. Used for glass, water, and semi-transparent effects.

### Characteristics

- **Pass**: Forward geometry (post-deferred-lighting)
- **Blend equation**: `SrcAlpha / InvSrcAlpha` (standard alpha compositing)
- **Depth**: Read enabled, write disabled
- **Alpha range**: 0.0–1.0 (smooth interpolation)
- **Cull mode**: No cull (CullMode::None) — see-through surfaces
- **Sorting**: Back-to-front (painter's algorithm)
- **PSO**: `PipelineLibrary::GetTranslucent()`

### Blend Equation Explanation

```glsl
// Translucent blend: SrcAlpha / InvSrcAlpha
output.rgb = src.rgb * src.a + dst.rgb * (1.0 - src.a);
```

This is **Porter-Duff over compositing** — the source color's alpha controls how much it occludes the destination (background). An alpha of 1.0 is fully opaque, 0.0 is fully transparent.

### Rendering Order

Translucent surfaces must be sorted back-to-front (farthest to nearest camera) to render correctly:

1. Deferred pass completes (all opaque/masked surfaces)
2. Deferred lighting pass lights the G-Buffer
3. Translucent surfaces sorted by depth (Z)
4. Translucent pass renders in order (each blends over previous)

### When to Use

- Glass and windows
- Water surfaces (lakes, oceans, swimming pools)
- Smoke and fog volumes
- Particle effects with alpha blending
- Neon signs (glowing transparent surfaces)
- Any surface requiring smooth partial transparency

### Performance Notes

- Forward rendering = reduced efficiency (no deferred lighting benefits)
- Depth read prevents overdraw (translucent surfaces behind opaque don't render)
- Depth write disabled (translucent surfaces don't occlude each other)
- Sorting cost is O(n log n) — keep translucent surface count reasonable
- Ideal for localized transparent objects; avoid full-screen translucency with complex geometry

### Example

```cpp
Material glassMat;
glassMat.blendMode = EBlendMode::Translucent;
glassMat.albedoPath = "textures/glass/glass_albedo.png";
glassMat.opacity = 0.3f;  // 30% transparent

Material waterMat;
waterMat.blendMode = EBlendMode::Translucent;
waterMat.albedoPath = "textures/water/water_normal.png";
waterMat.opacity = 0.8f;  // 80% opaque (20% transparent)
```

## Additive (blendMode=3)

Additive materials use **additive blending** (One / One) and are rendered in the **forward pass** alongside translucent surfaces. Used for emissive effects, particles, and screen-space brightening.

### Characteristics

- **Pass**: Forward geometry (post-deferred-lighting)
- **Blend equation**: `One / One` (additive)
- **Depth**: Read enabled, write disabled
- **Color accumulation**: `output = src.rgb + dst.rgb` (no alpha involved)
- **Sorting**: Not required (addition is commutative)
- **Cull mode**: Back-face cull (CullMode::Back)
- **PSO**: `PipelineLibrary::GetAdditive()`

### Blend Equation Explanation

```glsl
// Additive blend: One / One
output.rgb = src.rgb + dst.rgb;
// (Alpha is ignored; only RGB is added)
```

Each pixel **adds** the source color to the destination. Overlapping additive surfaces brightens the result without occlusion.

### When to Use

- Particle effects (fire, sparks, explosions)
- Glow/bloom effects (neon lights, energy fields)
- Light shafts and volumetric rays
- Screen-space distortions (heat haze, shimmer)
- Any effect that should **brighten** the scene rather than blend

### Performance Notes

- No sorting required (addition is commutative — order doesn't matter)
- Efficient: simple blend state, depth read prevents overdraw
- Ideal for particle emitters and repeated small effects
- Accumulation of many additive surfaces can blow out to white (control via opacity)

### Comparison: Additive vs. Translucent

| Aspect | Additive | Translucent |
|--------|----------|-------------|
| Blend | Addition (src + dst) | Alpha blend (src over dst) |
| Alpha role | Ignored | Controls opacity |
| Visual | Brightens | Colors over background |
| Sorting | Not required | Required (back-to-front) |
| Use case | Particles, glow | Glass, water |

### Example

```cpp
Material fireParticleMat;
fireParticleMat.blendMode = EBlendMode::Additive;
fireParticleMat.emissiveColorR = 1.0f;
fireParticleMat.emissiveColorG = 0.5f;
fireParticleMat.emissiveColorB = 0.0f;  // orange-yellow
fireParticleMat.emissiveIntensity = 2.0f;

Material energyBallMat;
energyBallMat.blendMode = EBlendMode::Additive;
energyBallMat.albedoPath = "textures/energy/glow.png";
energyBallMat.baseColorTintR = 0.0f;
energyBallMat.baseColorTintG = 1.0f;
energyBallMat.baseColorTintB = 1.0f;  // cyan
```

## Blend Mode Selection Guide

### Decision Tree

```
Is the surface opaque?
├─ Yes (no transparency needed)
│   └─ Standard geometry → OPAQUE
│
├─ Needs cutout silhouette (sharp edges)?
│   └─ Yes → MASKED (foliage, fences, hair)
│   └─ No, smooth blending needed
│       ├─ Should brighten the scene?
│       │   └─ Yes → ADDITIVE (particles, glow)
│       │   └─ No → TRANSLUCENT (glass, water)
```

### Quick Reference

**Choose Opaque if:**
- Surface fully blocks light (opaque material)
- Geometry has solid silhouette
- Rendering performance is critical

**Choose Masked if:**
- Geometry is cutout (fences, foliage, hair)
- Need binary alpha (on/off pixels)
- Want full deferred lighting benefits

**Choose Translucent if:**
- Surface is partially see-through (glass, water)
- Need smooth transparency gradient
- Can afford forward rendering cost

**Choose Additive if:**
- Effect should brighten screen (fire, glow)
- Order doesn't matter (particles)
- Want efficient post-lighting overlay

## Blend Mode and PSO Caching

The render engine maintains separate PSOs (Pipeline State Objects) for each blend mode:

```cpp
// In PipelineLibrary
RHIPipelineHandle GetGBuffer();          // Opaque
RHIPipelineHandle GetGBufferMasked();    // Masked
RHIPipelineHandle GetTranslucent();      // Translucent
RHIPipelineHandle GetAdditive();         // Additive
```

When rendering, the engine selects the appropriate PSO based on the material's `blendMode` field. This ensures each mode's render state (blend factors, cull mode, depth behavior) is correct without runtime state changes.

## Troubleshooting

### Masked surfaces have see-through black areas

- **Cause**: `alphaClipThreshold` too high (discarding pixels that should be opaque)
- **Fix**: Lower `alphaClipThreshold` (e.g., from 0.5 to 0.3)
- **Verify**: Opacity texture is loaded (check `opacitySrvIndex > 0`)

### Translucent surfaces appear behind opaque surfaces

- **Cause**: Translucent surfaces not sorted back-to-front
- **Fix**: Ensure scene renderer sorts translucent draw commands by depth
- **Note**: UzEngine handles this automatically; verify surfaces use correct blend mode

### Additive particles don't accumulate brightness

- **Cause**: Using `EBlendMode::Translucent` instead of `Additive`
- **Fix**: Set `material->blendMode = EBlendMode::Additive`
- **Verify**: PSO bound is `GetAdditive()`, not `GetTranslucent()`

### Two-sided rendering missing (masked surfaces show single side)

- **Cause**: Using `EBlendMode::Opaque` instead of `Masked`
- **Fix**: Set `material->blendMode = EBlendMode::Masked`
- **Note**: Opaque uses back-face cull for performance; masked uses no cull

## Summary

UzEngine's four blend modes cover the full spectrum from opaque solids to emissive glows. Choose based on transparency requirements and visual intent:

- **Opaque** — Fast, deferred-friendly, for solid geometry
- **Masked** — Cutout silhouettes with full lighting
- **Translucent** — Smooth blending for glass/water
- **Additive** — Screen-brightening effects for particles and glow

Each mode has dedicated PSO and rendering path, optimizing for its specific use case.
