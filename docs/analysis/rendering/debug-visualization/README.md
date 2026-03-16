<!-- doc-meta
type: index
category: rendering
theme: debug-visualization
title: Debug Visualization Analysis — View Modes, Buffers
created: 2026-03-16
updated: 2026-03-16
status: complete
author: Claude
-->

# Debug Visualization Analysis — View Modes, Buffers

Analysis of debug visualization systems, including buffer visualization, view modes, and rendering diagnostics.

## Files

- [godot-debug-modes-quick-reference.md](godot-debug-modes-quick-reference.md) — Quick reference for Godot's debug visualization modes
- [godot-debug-modes-taxonomy.md](godot-debug-modes-taxonomy.md) — Taxonomy and classification of Godot debug modes
- [godot-vs-ue5-debug-visualization.md](godot-vs-ue5-debug-visualization.md) — Comparison of debug visualization between Godot and UE5
- [godot4-debug-visualization-analysis.md](godot4-debug-visualization-analysis.md) — Detailed analysis of Godot 4 debug visualization implementation
- [ue5-view-mode-buffer-visualization-analysis.md](ue5-view-mode-buffer-visualization-analysis.md) — UE5's view mode system for visualizing intermediate rendering buffers

## Quick Navigation

### Understanding View Modes (UE5 Style)?
- [ue5-view-mode-buffer-visualization-analysis.md](ue5-view-mode-buffer-visualization-analysis.md) — UE5's complete view mode system

### Understanding Debug Modes (Godot Style)?
- [godot-debug-modes-taxonomy.md](godot-debug-modes-taxonomy.md) — Start with taxonomy
- [godot-debug-modes-quick-reference.md](godot-debug-modes-quick-reference.md) — Quick reference of modes
- [godot4-debug-visualization-analysis.md](godot4-debug-visualization-analysis.md) — Full implementation details

### Comparing Approaches?
- [godot-vs-ue5-debug-visualization.md](godot-vs-ue5-debug-visualization.md) — Side-by-side comparison

## Key Concepts

### Debug Visualization Purposes
- **Development**: Real-time inspection of shader outputs, textures, and intermediate results
- **Profiling**: Identify bottlenecks and rendering inefficiencies
- **Debugging**: Diagnose visual artifacts and incorrect lighting/shading
- **Optimization**: Verify which features contribute most to performance cost
- **Artist Review**: Quick visual toggle between different rendering techniques

### View Mode Types
1. **Unlit** — Raw geometry color without any lighting
2. **Normals** — Surface normal directions (RGB color coding)
3. **Albedo** — Base color/diffuse only (no lighting)
4. **Roughness** — Material roughness values (grayscale)
5. **Metallic** — Material metallic values (grayscale)
6. **Depth** — Scene depth (logarithmic or linear)
7. **Lighting** — Final lighting result only
8. **Ambient Occlusion** — AO texture visualization
9. **Shadow Mask** — Shadow contribution (lit/shadowed)
10. **View Velocity** — Motion vector visualization (for TAA/motion blur)

### Implementation Patterns
- **Shader Variants**: Compile alternate versions for each debug view
- **Cvar Gating**: Toggle views via console variables (cvars)
- **Pass Replacement**: Replace main rendering pass with debug-only pass
- **Color Coding**: Standard conventions for visualizing numeric data (heat maps, normals as RGB)
- **Real-Time Toggle**: Instant mode switch without recompilation

## Related Documentation

- **Rendering Overview**: `docs/analysis/rendering/general/`
- **Shaders & Materials**: `docs/analysis/rendering/materials/`
- **Post-Processing**: `docs/analysis/rendering/post-process/`
- **UzEngine Debug Specs**: `docs/specs/debug-profiling.md`
