<!-- doc-meta
type: index
category: rendering
theme: materials
title: Materials & Parallax Analysis — POM, Displacement
created: 2026-03-16
updated: 2026-03-16
status: complete
author: Claude
-->

# Materials & Parallax Analysis — POM, Displacement

Analysis of material systems and parallax occlusion mapping (POM), displacement mapping, and related surface detail techniques.

## Files

- [godot-pom-heightmap-analysis.md](godot-pom-heightmap-analysis.md) — Godot's parallax occlusion mapping implementation with heightmap sampling
- [godot-pom-hlsl-reference.md](godot-pom-hlsl-reference.md) — HLSL code reference for POM from Godot engine
- [parallel-ox-mapping-analysis.md](parallel-ox-mapping-analysis.md) — Analysis of parallel occlusion mapping technique (optimization variant)
- [ue5-pom-code-reference.md](ue5-pom-code-reference.md) — UE5 POM implementation code snippets and patterns

## Quick Navigation

### Understanding Parallax Occlusion Mapping (POM)?
1. [godot-pom-heightmap-analysis.md](godot-pom-heightmap-analysis.md) — Godot's approach
2. [ue5-pom-code-reference.md](ue5-pom-code-reference.md) — UE5's approach
3. [parallel-ox-mapping-analysis.md](parallel-ox-mapping-analysis.md) — Optimization variants

### Implementing POM from Scratch?
- [ue5-pom-code-reference.md](ue5-pom-code-reference.md) — Start with UE5's code
- [godot-pom-hlsl-reference.md](godot-pom-hlsl-reference.md) — HLSL shader templates
- [godot-pom-heightmap-analysis.md](godot-pom-heightmap-analysis.md) — Heightmap sampling details

### Optimizing POM Performance?
- [parallel-ox-mapping-analysis.md](parallel-ox-mapping-analysis.md) — Parallel occlusion mapping (faster)

## Key Concepts

### Parallax Occlusion Mapping (POM)
Adds per-pixel depth detail to surface geometry via height sampling:
- **Height Texture**: Grayscale texture storing relative height (displacement)
- **Ray Marching**: Step through height layers to find surface intersection
- **View-Space Parallax**: Offset texture coordinates based on view angle and height
- **Occlusion**: Self-shadows and occlusion from neighboring height pixels
- **Quality**: Trade-off between layers (performance) and visual detail

**Key Parameters**:
- **Height Scale**: Multiplier for displacement amount (in cm)
- **Layer Count**: Number of ray march steps (8-32 typical)
- **Parallax Bias**: Offset to tune visual appearance

### Height Texture Sampling
- **R Channel**: Height value (0=lowest, 1=highest)
- **Range**: Usually [0, 1], remapped to [-scale, +scale] in shader
- **Filtering**: Usually bilinear for smooth results
- **Precision**: 8-bit or 16-bit, sufficient for detail

### Ray Marching Algorithm
1. **Initial Position**: World position of pixel
2. **View Direction**: Direction from eye to pixel (in tangent space)
3. **Step Loop**: Advance along ray, sampling height at each step
4. **Intersection Test**: Check if ray is below terrain height
5. **Refinement**: Linear interpolation for sub-step precision
6. **Output**: Offset texture coordinates for final sampling

### Self-Shadowing and Occlusion
- **Shadow Rays**: Cast rays from surface point toward light
- **Occlusion Check**: Determine if neighboring points block light
- **Contact Shadows**: Occlusion from small surface details
- **Cavity Darkening**: Darken deep crevasses and valleys

## Related Documentation

- **Rendering Overview**: `docs/analysis/rendering/general/`
- **Shadow Mapping**: `docs/analysis/rendering/shadows/`
- **Post-Processing**: `docs/analysis/rendering/post-process/`
- **UzEngine Materials Specs**: `docs/specs/rendering/`
