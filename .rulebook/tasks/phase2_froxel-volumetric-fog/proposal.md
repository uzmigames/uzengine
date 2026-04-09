# Proposal: Froxel Volumetric Fog (Phase 2)

## Why
UzEngine only has analytical exponential height fog — a flat color blend with no volumetric light scattering. Every modern AAA title (UE5, Frostbite, God of War) uses froxel-based volumetric fog for light shafts, volumetric shadows, and depth-aware atmospheric scattering. This is the single highest-impact visual gap identified in the renderer AAA analysis (P0.1). The froxel approach (Hillaire 2015) rasterizes participating media into a 3D grid, injects light, and composites over the scene.

## What Changes

### New Systems
1. `VolumetricFogPass` — New render pass managing the froxel fog pipeline (3 compute dispatches + 1 composite)
2. `volumetric_fog_inject_cs.hlsl` — Light injection compute shader: scatters point/spot/directional light into froxel grid
3. `volumetric_fog_scatter_cs.hlsl` — Temporal accumulation + front-to-back scattering integration along each froxel column
4. `volumetric_fog_composite_ps.hlsl` — Screen-space composite: sample final froxel inscattering over lit scene
5. `VolumetricFogComponent` — ECS component with density, albedo, phase asymmetry, height falloff

### Refactoring
- scene_renderer_init.cpp: register volumetric fog passes in render graph (after shadows, before deferred lighting composite)
- Height fog pass: should blend with froxel fog (froxel fog replaces height fog for covered depth range)

## Impact
- Affected specs: docs/specs/rendering/renderer.md, docs/specs/rendering/shader-library.md
- Affected code: NEW runtime/engine/renderer/passes/atmosphere/volumetric_fog_pass.cpp/.h, NEW runtime/shaders/runtime/atmosphere/volumetric_fog_*.hlsl, runtime/engine/renderer/scene/scene_renderer_init.cpp
- Breaking change: NO — additive feature, height fog remains as fallback
- User benefit: Volumetric light shafts, fog with depth, light scattering through fog volumes
- Enables: Atmospheric outdoor/indoor scenes, god rays through fog, volumetric shadows
