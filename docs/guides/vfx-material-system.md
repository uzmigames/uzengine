<!-- doc-meta
title: VFX Material System
category: guides
theme: vfx
created: 2026-04-13
updated: 2026-04-13
status: active
author: docs-operator
-->

# VFX Material System

VFXMaterial controls how particles are textured and blended. Each emitter can reference a VFXMaterial that sets sprite texture, blend mode, emissive intensity, and flipbook animation.

## VFXMaterial Struct

Defined in `runtime/engine/vfx/material/vfx_material.h`. Inherits from `MaterialInterface` so VFX particles integrate with the engine's shared material pipeline.

### Key Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `spriteTexturePath` | string | "" | Path to sprite sheet (TGA/PNG). |
| `spriteTextureSrvIndex` | uint32 | UINT32_MAX | Bindless SRV index (set by LoadTexture). |
| `blendMode` | uint32 | 1 | 0=AlphaBlend, 1=Additive, 2=PremulAlpha. |
| `emissiveIntensity` | float | 1.0 | Additive emissive multiplier. |
| `softFadeDistance` | float | 100.0 | Soft particle fade distance (cm). |
| `alphaErosion` | float | 0.0 | Alpha erosion threshold [0, 1]. |
| `baseColorTintR/G/B` | float | 1.0 | Per-channel color tint multiplier. |
| `isUnlit` | bool | true | Particles default to unlit shading. |
| `isTwoSided` | bool | true | Billboards are always double-sided. |
| `flipbookRows/Cols` | uint32 | 1 | Sprite sheet grid dimensions. |
| `flipbookTotalFrames` | uint32 | 1 | Total valid frames. |
| `flipbookFPS` | float | 0.0 | Playback speed. 0 = static. |
| `flipbookBlendMode` | uint32 | 0 | 0=None, 1=Linear, 2=Random. |
| `colorRampLutSrvIndex` | uint32 | UINT32_MAX | Bindless SRV for 256x1 color ramp LUT. |

## Blend Modes

### AlphaBlend (mode 0)

Standard alpha blending: `SrcAlpha / InvSrcAlpha`. Best for smoke, dust, and volumetric effects where particles should darken the background proportionally to their opacity.

```
output.rgb = particle.rgb * particle.a + background.rgb * (1 - particle.a)
```

### Additive (mode 1)

Additive blending: `One / One`. Best for fire, sparks, magic, and light-emitting effects. Particles brighten the background without darkening.

```
output.rgb = particle.rgb + background.rgb
```

### PremulAlpha (mode 2)

Pre-multiplied alpha: `One / InvSrcAlpha`. The default mode. Combines alpha-blended edges with additive cores. Best for effects that need both opacity and brightness (glowing fire with semi-transparent edges).

```
output.rgb = particle.rgb + background.rgb * (1 - particle.a)
// Where particle.rgb is pre-multiplied: rgb *= alpha before output.
```

## Assigning Materials to Emitters

In the VFX viewport demo, materials are assigned at the demo level:

```cpp
VFXMaterial material;
material.blendMode = 1;          // Additive
material.emissiveIntensity = 2.0f;
material.baseColorTintR = 1.0f;
material.baseColorTintG = 0.6f;
material.baseColorTintB = 0.2f;

// Load sprite texture (auto-detects flipbook from filename).
material.spriteTexturePath = "content/textures/fire_6x6.tga";
material.LoadTexture(device, cmdList);

// Use material's SRV index in the render pass.
billboardCB.spriteTextureIdx = material.spriteTextureSrvIndex;
billboardCB.blendModeHint = material.blendMode;
```

## Texture Caching

`VFXMaterial::LoadTexture()` caches loaded textures by file path. If multiple emitters reference the same sprite, only one GPU upload occurs. The cache persists for the process lifetime.

## Flipbook Auto-Detection

If the sprite filename contains a `_NxM_` or `_NxM.` pattern (e.g., `fire_6x6.tga`), LoadTexture automatically sets `flipbookRows`, `flipbookCols`, and `flipbookTotalFrames`.

## Demo Controls

| Key | Action |
|-----|--------|
| M | Cycle through 6 material presets (Fire/Smoke/Sparks/Magic/Debris/Distortion) |
