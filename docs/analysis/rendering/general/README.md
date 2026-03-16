<!-- doc-meta
type: index
category: rendering
theme: general
title: General Rendering Analysis — Architecture & Systems
created: 2026-03-16
updated: 2026-03-16
status: complete
author: Claude
-->

# General Rendering Analysis — Architecture & Systems

Overview and architecture analysis of complete rendering systems, including rendering pipeline design, shader patterns, and general rendering practices.

## Files

- [ue5-default-scene-rendering-systems.md](ue5-default-scene-rendering-systems.md) — Default rendering systems in UE5 scene rendering (forward, deferred, motion vectors, etc.)
- [ue5-rendering-systems-analysis.md](ue5-rendering-systems-analysis.md) — Complete analysis of UE5's rendering architecture and system design
- [ue5-shader-patterns-analysis.md](ue5-shader-patterns-analysis.md) — Common shader patterns and best practices from UE5

## Quick Navigation

### Understanding UE5 Rendering Architecture?
- [ue5-rendering-systems-analysis.md](ue5-rendering-systems-analysis.md) — Start here for high-level overview
- [ue5-default-scene-rendering-systems.md](ue5-default-scene-rendering-systems.md) — Scene rendering specifics
- [ue5-shader-patterns-analysis.md](ue5-shader-patterns-analysis.md) — Shader implementation details

### Learning Shader Patterns?
- [ue5-shader-patterns-analysis.md](ue5-shader-patterns-analysis.md) — Common patterns and practices

## Key Concepts

### Rendering Pipeline Stages
1. **Depth Prepass** — Depth-only rendering for occlusion culling and depth pyramid
2. **Shadow Passes** — Render shadow maps from light viewpoints
3. **Base Pass** — Primary lighting with GBuffers (deferred) or direct output (forward)
4. **Lighting Pass** — Deferred: compute lighting from GBuffers; Forward: already in base
5. **Transparency** — Render translucent geometry with proper blending
6. **Post-Processing** — Apply effects (bloom, tone map, color grading, etc.)
7. **UI** — Render UI overlay on top of final image

### Forward vs. Deferred Rendering
- **Forward**: Light per object, suitable for few lights, complex materials
- **Deferred**: Light per pixel from GBuffers, suitable for many lights, simple materials
- **UE5**: Uses both — forward for small objects/transparency, deferred for complex scenes

### GBuffer Layout (Deferred)
- **Target 0**: Albedo RGB + Alpha
- **Target 1**: Normal XY + metallic + roughness
- **Target 2**: Ambient occlusion + depth or other data
- **Depth**: Separate depth buffer for reconstruction

### Shader Organization
- **Header Files**: Reusable functions (lighting, math, normal mapping, etc.)
- **Include System**: Modular shader composition
- **Conditional Compilation**: Platform-specific variants or feature toggles
- **Optimization**: Common subexpressions, branch elimination

## Related Documentation

- **Shadow Mapping**: `docs/analysis/rendering/shadows/`
- **Sky & Atmosphere**: `docs/analysis/rendering/atmosphere/`
- **Post-Processing**: `docs/analysis/rendering/post-process/`
- **Materials & Parallax**: `docs/analysis/rendering/materials/`
- **Debug Visualization**: `docs/analysis/rendering/debug-visualization/`
- **UzEngine Rendering Specs**: `docs/specs/rendering/`
