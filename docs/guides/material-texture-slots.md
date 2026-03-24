<!-- doc-meta
title: Material Texture Slots and Defaults
category: rendering
theme: material-system
status: done
last-updated: 2026-03-24
-->

# Material Texture Slots and Defaults

This guide documents all texture slots available in UzEngine's material system, their purpose, default textures, and GPU resource binding.

## Overview

UzEngine's PBR material system uses a fixed set of texture slots bound at SRV indices `t0` through `t6` in the pixel shader. Each slot has a semantic purpose (albedo, normal, roughness, etc.) and a fallback texture used when no artist-authored texture is provided.

The texture slots are populated by the renderer during the scene loading phase. Game code specifies texture paths in the Material asset (or MaterialInstance override), and the renderer uploads them to GPU memory, storing the SRV heap index in the corresponding PerObjectCB field.

## Standard Texture Slots

| Slot | C++ Field | SRV Index | Default Texture | Format | Usage |
|------|-----------|-----------|-----------------|--------|-------|
| **Albedo** | `albedoSrvIndex` | `t0` | White (1, 1, 1, 1) | sRGB RGBA8 | Base color RGB; A channel unused |
| **Normal** | `normalSrvIndex` | `t1` | Flat (0.5, 0.5, 1.0) | Linear RGB8 | Tangent-space normal (XY stored, Z reconstructed) |
| **Roughness** | `roughnessSrvIndex` | `t2` | White (1.0) | Linear R8 | Surface roughness; 1.0 = fully rough |
| **Metallic** | `metallicSrvIndex` | `t3` | Black (0.0) | Linear R8 | Metallic reflectance; 0.0 = dielectric (default) |
| **Ambient Occlusion** | `aoSrvIndex` | `t4` | White (1.0) | Linear R8 | Contact shadow darkening; 1.0 = no occlusion |
| **Emissive** | `emissiveSrvIndex` | `t5` | Black (0, 0, 0) | Linear RGB8 | Self-illuminated color; combined with emissiveIntensity |
| **Opacity/Alpha Mask** | `opacitySrvIndex` | `t6` | White (1.0) | Linear R8 | Alpha for masked/translucent blend modes |
| **Heightmap** | `heightmapSrvIndex` | — | Black (0.0) | Linear R8 | POM displacement height; indirect binding |

## Default Texture Semantics

### Color vs. Data Textures

UzEngine distinguishes between two categories of textures:

**Color textures** (sRGB):
- **Albedo** — stored in sRGB and automatically converted to linear on GPU sample
- Contains color information meant for direct visual display
- Default white = unmodified texture color when no texture is assigned

**Data textures** (Linear):
- **Normal, Roughness, Metallic, AO, Emissive, Opacity** — stored in linear color space
- Contain numerical parameters (surface properties), not visual colors
- Default varies by semantic meaning

### Why These Defaults?

| Slot | Default | Reasoning |
|------|---------|-----------|
| Albedo | White (1,1,1) | Neutral tint; unmodified texture color |
| Normal | Flat (0.5, 0.5, 1.0) | No perturbation; straight up in tangent space (Z=1) |
| Roughness | White (1.0) | Fully rough surface; no specular highlights |
| Metallic | Black (0.0) | Dielectric (non-metal); no metallic reflections |
| AO | White (1.0) | No occlusion; full ambient lighting contribution |
| Emissive | Black (0, 0, 0) | No self-illumination |
| Opacity | White (1.0) | Fully opaque; no transparency or masking |
| Heightmap | Black (0.0) | No displacement offset |

## Packed ORM Mode

When `useORM = true` (set in Material or via MaterialInstance), the material system operates in **packed ORM mode**. In this mode:

- A single **packed ORM texture** replaces the three separate Roughness/Metallic/AO textures
- The ORM texture is sampled from `aoIndex` SRV (reinterpreted as ORM)
- Channel mapping:
  - **R channel** = Ambient Occlusion (0.0–1.0)
  - **G channel** = Roughness (0.0–1.0)
  - **B channel** = Metallic (0.0–1.0)

### When to Use ORM

**Packed ORM is beneficial when:**
- Artist-authored texture atlases already use the ORM packing standard (ArtStation, Quixel, Substance Designer exports)
- Reducing texture memory footprint (1 texture vs. 3 separate textures)
- Improving GPU cache locality (single texture fetch instead of three)

**Use separate textures when:**
- Individual channels vary in quality/resolution (e.g., low-res AO, high-res roughness)
- Content is already split into separate files and repacking is expensive
- Channel precision differs (some channels need higher bit depth)

### Shader Implementation

In the pixel shader, the ORM mode is detected via the `materialFlags` bitfield (bit 2). The GBuffer pass conditionally unpacks:

```hlsl
float roughness;
float metallic;
float ao;

if (materialFlags & 0x4u) {
    // ORM packed mode: sample single texture
    float3 orm = Sample_Linear(t_ao, uv).rgb;
    ao = orm.r;
    roughness = orm.g;
    metallic = orm.b;
} else {
    // Separate textures mode
    ao = Sample_Linear(t_ao, uv).r;
    roughness = Sample_Linear(t_roughness, uv).r;
    metallic = Sample_Linear(t_metallic, uv).r;
}
```

## Custom Texture Paths and Asset Loading

### Setting Texture Paths

Material texture paths are VFS-relative strings (e.g., `"textures/stone/stone_albedo.png"`):

```cpp
Material mat;
mat.albedoPath = "textures/stone/stone_albedo.png";
mat.normalPath = "textures/stone/stone_normal.png";
mat.roughnessPath = "textures/stone/stone_roughness.png";
mat.metallicPath = "textures/stone/stone_metallic.png";
mat.aoPath = "textures/stone/stone_ao.png";
```

Empty string (`""`) means no texture — the default fallback is used.

### SRV Index Population

Game code **should NOT** set SRV indices directly. The renderer handles this:

1. Scene renderer scans all referenced Materials/MaterialInstances
2. For each non-empty texture path, loads texture from disk (or uses fallback)
3. Uploads to GPU heap, records SRV index
4. Sets `material->albedoSrvIndex = heapIndex` (or equivalent for other slots)
5. Marks material with `SetGpuResourcesLoaded(true)` when complete

The PerObjectCB is then populated from these indices when building draw commands.

## PerObjectCB Texture Index Fields

The PerObjectCB structure holds the SRV indices used in rendering:

```cpp
struct PerObjectCB {
    uint32_t albedoIndex;      // t0 (SRV heap index, 0 = white default)
    uint32_t normalIndex;      // t1 (SRV heap index, 0 = flat normal)
    uint32_t roughnessIndex;   // t2 (SRV heap index, 0 = white/1.0)
    uint32_t metallicIndex;    // t3 (SRV heap index, 0 = black/0.0)
    uint32_t aoIndex;          // t4 (SRV heap index, 0 = white/1.0)
    uint32_t emissiveIndex;    // t5 (SRV heap index, 0 = no texture)
    uint32_t opacitySrvIndex;  // t6 (SRV heap index, 0 = white/1.0)
    uint32_t heightmapSrvIndex;// (not bound to a SRV slot, used in POM)
    // ... other CB fields ...
};
```

When a slot's index is `0`, the shader uses the hardcoded default texture. This allows:
- Efficient fallback without extra texture allocations
- Per-slot override capability (e.g., albedoIndex=42, roughnessIndex=0 uses custom albedo + default roughness)
- Dynamic material variations without full reload

## Texture Format and Precision

| Slot | Internal Format | Precision | Range | Gamma |
|------|-----------------|-----------|-------|-------|
| Albedo | RGBA8 | 8-bit per channel | 0–1 (sRGB encode/decode) | sRGB |
| Normal | RGB8 | 8-bit per channel | 0–1 (stored 0.5–1.0 for center) | Linear |
| Roughness | R8 | 8-bit | 0–1 | Linear |
| Metallic | R8 | 8-bit | 0–1 | Linear |
| AO | R8 | 8-bit | 0–1 | Linear |
| Emissive | RGB8 | 8-bit per channel | 0–∞ (HDR via intensity multiplier) | Linear |
| Opacity | R8 | 8-bit | 0–1 (alpha test via threshold) | Linear |

All formats use BC7 GPU compression when authored in DCC tools, reducing VRAM footprint by 4x.

## Channel Usage Examples

### Albedo (t0) — Color Information

Stores base color in sRGB. Alpha channel is typically unused (fully opaque), but can optionally encode a second parameter (e.g., subsurface scattering control) when custom shading models are used.

```
RGB: base color (sRGB, auto-converted to linear on sample)
A: unused (default 1.0 = opaque)
```

### Normal (t1) — Surface Detail

Tangent-space normal map. Packing: RG channels store (X, Y) normalized normal, Z reconstructed from `sqrt(1 - X² - Y²)`.

```
RG: tangent-space normal XY (0.5 = center, 1.0 = +max)
B: ignored (reconstructed in shader)
```

### Roughness/Metallic/AO (t2/t3/t4) — Material Parameters

Single-channel maps encoding scalar parameters used directly in the lighting equation.

```
R: parameter value (0–1)
GBA: unused (discarded)
```

## Material Parameter Multipliers

Beyond texture values, the Material class provides **multipliers** that scale texture samples:

```cpp
// In Material or PerObjectCB
float roughnessMultiplier = 1.0f;  // scales roughness texture sample
float metallicMultiplier = 1.0f;   // scales metallic texture sample
float aoIntensity = 1.0f;          // lerp(1.0, ao_texture, aoIntensity)
float normalIntensity = 1.0f;      // scale normal map perturbation
float baseColorTintR/G/B = 1.0f;   // multiplies albedo texture RGB
```

These parameters allow artists to adjust material properties without reauthoring textures, useful for:
- Material variations (rust overlay on base metallic surface)
- Environmental wear (mud/dust multipliers)
- Dynamic material swaps (same mesh, different tints)

## Summary

All material textures route through a fixed 8-slot system with predictable defaults. The renderer automatically handles texture loading, compression, and SRV heap management. Artists specify texture paths and optional multipliers; the shader reads precomputed SRV indices from the PerObjectCB and applies the configured parameters.

For custom shading models beyond the standard PBR set, additional texture slots can be added via the packed ORM mechanism or future texture array extensions, but the core system remains fixed to optimize for the common case.
