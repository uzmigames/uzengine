<!-- doc-meta
type: index
category: analysis
theme: rendering-pipeline
title: Rendering Pipeline Analysis — Deferred, Forward, Architecture
created: 2026-03-19
updated: 2026-03-19
status: complete
author: Claude
-->

# Rendering Pipeline & Architecture Analysis

Complete analysis of UE5 rendering pipeline architecture, deferred and forward rendering, GBuffer layouts, shader patterns, velocity generation, and debug visualization.

## Files

- [debug-visualization/README.md](debug-visualization/README.md) — Debug visualization entry point
- [debug-visualization/godot-debug-modes-quick-reference.md](debug-visualization/godot-debug-modes-quick-reference.md) — Quick reference for Godot's debug visualization modes
- [debug-visualization/godot-debug-modes-taxonomy.md](debug-visualization/godot-debug-modes-taxonomy.md) — Taxonomy and classification of Godot debug modes
- [debug-visualization/godot-vs-ue5-debug-visualization.md](debug-visualization/godot-vs-ue5-debug-visualization.md) — Comparison of debug visualization between Godot and UE5
- [debug-visualization/godot4-debug-visualization-analysis.md](debug-visualization/godot4-debug-visualization-analysis.md) — Detailed analysis of Godot 4 debug visualization implementation
- [debug-visualization/ue5-view-mode-buffer-visualization-analysis.md](debug-visualization/ue5-view-mode-buffer-visualization-analysis.md) — UE5's view mode system for visualizing intermediate rendering buffers
- [general/README.md](general/README.md) — General rendering systems entry point
- [general/ue5-default-scene-rendering-systems.md](general/ue5-default-scene-rendering-systems.md) — Default rendering systems in UE5 scene rendering
- [general/ue5-rendering-systems-analysis.md](general/ue5-rendering-systems-analysis.md) — Complete analysis of UE5's rendering architecture and system design
- [general/ue5-shader-patterns-analysis.md](general/ue5-shader-patterns-analysis.md) — Common shader patterns and best practices from UE5

## Quick Navigation

### Understanding Rendering Architecture?
1. [general/ue5-rendering-systems-analysis.md](general/ue5-rendering-systems-analysis.md) — Start with architecture overview
2. [general/ue5-default-scene-rendering-systems.md](general/ue5-default-scene-rendering-systems.md) — Scene rendering specifics
3. [general/ue5-shader-patterns-analysis.md](general/ue5-shader-patterns-analysis.md) — Shader implementation details

### Debug Visualization?
- [debug-visualization/ue5-view-mode-buffer-visualization-analysis.md](debug-visualization/ue5-view-mode-buffer-visualization-analysis.md) — UE5 view modes
- [debug-visualization/godot-debug-modes-taxonomy.md](debug-visualization/godot-debug-modes-taxonomy.md) — Godot modes taxonomy
- [debug-visualization/godot-vs-ue5-debug-visualization.md](debug-visualization/godot-vs-ue5-debug-visualization.md) — Comparison

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

### Debug View Modes
- **Unlit** — Raw geometry color without lighting
- **Normals** — Surface normal directions (RGB color coding)
- **Albedo** — Base color only (no lighting)
- **Roughness** — Material roughness values
- **Metallic** — Material metallic values
- **Depth** — Scene depth (logarithmic or linear)
- **Lighting** — Final lighting result only
- **Ambient Occlusion** — AO texture visualization
- **Shadow Mask** — Shadow contribution visualization
- **View Velocity** — Motion vector visualization (for TAA/motion blur)
