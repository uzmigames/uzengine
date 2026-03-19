<!-- doc-meta
type: index
category: rendering
theme: post-process
title: Post-Processing Analysis — Tonemap, FXAA, Volumetrics
created: 2026-03-16
updated: 2026-03-16
status: complete
author: Claude
-->

# Post-Processing Analysis — Tonemap, FXAA, Volumetrics

Analysis of post-processing effects including tone mapping, anti-aliasing (FXAA), and volumetric fog rendering.

## Files

- [frostbite-volumetric-rendering.md](frostbite-volumetric-rendering.md) — Volumetric fog and lighting from Frostbite engine (reference for froxel-based approach)
- [godot4-tonemap-fxaa-analysis.md](godot4-tonemap-fxaa-analysis.md) — Godot 4 implementation of tone mapping and FXAA anti-aliasing
- [ue5-tonemap-fxaa-analysis.md](ue5-tonemap-fxaa-analysis.md) — UE5's tone mapping (ACES, Filmic) and FXAA anti-aliasing implementation

## Quick Navigation

### Implementing Tone Mapping?
- [ue5-tonemap-fxaa-analysis.md](ue5-tonemap-fxaa-analysis.md) — UE5's ACES and Filmic tone mapping algorithms
- [godot4-tonemap-fxaa-analysis.md](godot4-tonemap-fxaa-analysis.md) — Godot's implementation for comparison

### Implementing FXAA (Fast Approximate Anti-Aliasing)?
- [ue5-tonemap-fxaa-analysis.md](ue5-tonemap-fxaa-analysis.md) — UE5's FXAA variant with luma-based edge detection
- [godot4-tonemap-fxaa-analysis.md](godot4-tonemap-fxaa-analysis.md) — Godot's FXAA for comparison

### Implementing Volumetric Fog?
- [frostbite-volumetric-rendering.md](frostbite-volumetric-rendering.md) — Froxel-based volumetric fog and lighting

## Key Concepts

### Tone Mapping
Converts HDR (high dynamic range) color values to LDR (low dynamic range) for display:
- **ACES (Academy Color Encoding System)**: Industry-standard tone mapper used by UE5, preserves color grading fidelity
- **Filmic**: Perceptual curve that mimics film camera response
- **Applied at end of frame**: After all lighting, post-processing, color grading
- **Preserves blacks and whites**: Carefully designed curve maintains detail in shadows and highlights

### FXAA (Fast Approximate Anti-Aliasing)
Real-time edge-aware anti-aliasing:
- **Luma-based edge detection**: Detects edges using luminance discontinuity
- **Pattern detection**: Identifies edge orientation and contrast
- **Blend along edge**: Applies directional blur to smooth edge transitions
- **Cheap**: Single full-screen pass, GPU-friendly
- **Trade-off**: Less effective than MSAA or TAA on fine details, but minimal cost

### Volumetric Fog (Froxel-Based)
Volumetric effects using 3D grid (froxel = volume pixel):
- **3D Grid**: Depth-sliced volume, e.g., 128 × 72 × 64 froxels
- **Light Injection**: Inject direct/indirect light into each froxel
- **Scattering**: Light scatters as it travels through volume (ray marching or texture splatting)
- **Lookup During Shading**: Sample volumetric volume during scene shading for atmospheric scattering
- **Uses**: Fog, haze, god rays, atmospheric effects

## Related Documentation

- **Sky & Atmosphere**: `docs/analysis/rendering/atmosphere/`
- **Rendering Overview**: `docs/analysis/rendering/general/`
- **Shadow Mapping**: `docs/analysis/rendering/shadows/`
- **UzEngine Post-Processing Specs**: `docs/specs/rendering/`
