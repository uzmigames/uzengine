<!-- doc-meta
type: index
category: analysis
theme: post-process
title: Post-Processing Analysis — Tone Mapping, Bloom, Volumetrics
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Post-Processing & Advanced Effects Analysis

Complete analysis of UE5 post-processing effects including tone mapping, bloom, FXAA, volumetric rendering, auto-exposure, color grading, and frame composition.

## Files

- [auto-exposure-summary.md](auto-exposure-summary.md) — Summary of auto-exposure system components and parameters
- [ue5-auto-exposure-histogram.md](ue5-auto-exposure-histogram.md) — Auto-exposure histogram computation, luminance weighting, range remapping
- [ue5-auto-exposure-implementation-guide.md](ue5-auto-exposure-implementation-guide.md) — Step-by-step guide to implement histogram-based auto-exposure
- [ue5-auto-exposure-index.md](ue5-auto-exposure-index.md) — Index of auto-exposure concepts and parameters
- [ue5-auto-exposure-quick-reference.md](ue5-auto-exposure-quick-reference.md) — Quick reference for auto-exposure settings and formulas
- [volumetric-clouds-implementation-guide.md](volumetric-clouds-implementation-guide.md) — Guide to implement volumetric clouds using ray marching
- [volumetric-clouds.md](volumetric-clouds.md) — Complete volumetric clouds analysis
- [ue5/README.md](ue5/README.md) — Post-processing detailed analysis entry point
- [ue5/frostbite-volumetric-rendering.md](ue5/frostbite-volumetric-rendering.md) — Frostbite engine volumetric rendering architecture
- [ue5/godot4-tonemap-fxaa-analysis.md](ue5/godot4-tonemap-fxaa-analysis.md) — Godot 4 tone mapping and FXAA implementation
- [ue5/ue5-tonemap-fxaa-analysis.md](ue5/ue5-tonemap-fxaa-analysis.md) — UE5 tone mapping (ACES) and FXAA anti-aliasing analysis

## Quick Navigation

### Implementing Auto-Exposure?
- [ue5-auto-exposure-histogram.md](ue5-auto-exposure-histogram.md) — Histogram computation
- [ue5-auto-exposure-implementation-guide.md](ue5-auto-exposure-implementation-guide.md) — Implementation steps
- [ue5-auto-exposure-quick-reference.md](ue5-auto-exposure-quick-reference.md) — Parameters and constants

### Tone Mapping & Color Grading?
- [ue5/ue5-tonemap-fxaa-analysis.md](ue5/ue5-tonemap-fxaa-analysis.md) — ACES tone mapping
- [ue5/godot4-tonemap-fxaa-analysis.md](ue5/godot4-tonemap-fxaa-analysis.md) — Godot approach

### Volumetric Rendering?
- [volumetric-clouds.md](volumetric-clouds.md) — Cloud rendering analysis
- [volumetric-clouds-implementation-guide.md](volumetric-clouds-implementation-guide.md) — Implementation guide
- [ue5/frostbite-volumetric-rendering.md](ue5/frostbite-volumetric-rendering.md) — Volumetric fog patterns

## Key Concepts

### Auto-Exposure (Eye Adaptation)
- **Histogram**: Compute luminance distribution across screen
- **Eyeadaptation**: Blend old exposure toward new exposure over time
- **Range**: Min/max EV values for clipping behavior
- **Metering**: Weighted histogram (center-weighted, spot, full-screen)

### Tone Mapping
- **ACES (Academy Color Encoding System)**: Industry-standard 3-segment curve
- **Filmic tone mapping**: Mimics film camera response
- **Color grading**: 3D LUT (32³) for final color correction

### Volumetric Rendering
- **Froxel grid**: 3D grid cells for volume lighting
- **Ray marching**: Step through grid sampling density and lighting
- **Phase functions**: Rayleigh/Mie scattering approximations
- **Temporal reprojection**: Reduce flicker via history blending

### Bloom
- **Pyramid**: Downsample chain (6 mips) for blur at different scales
- **Threshold**: Luminance cutoff to isolate bright areas
- **Per-stage tints**: Color adjustment at each blur level
- **Karis average**: Weighted average for smooth falloff
