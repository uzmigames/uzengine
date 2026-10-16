# Proposal: Indoor Lighting System (Phase 0)

## Why

UzEngine currently has no way to light enclosed/semi-enclosed interiors. The rendering pipeline (directional sun -> CSM -> deferred lighting) correctly shadows everything inside a building, but provides no supplemental lighting to replace the blocked sunlight. The Sponza atrium -- a standard benchmark with 138 mesh nodes and 24 materials -- renders as a uniformly dark interior with zero shadow variation from columns, arches, or windows.

Every production engine (UE5, Frostbite, CryEngine, Godot, Three.js) solves this with a layered approach: SkyLight ambient fill + irradiance probes + artist fill lights + screen-space GI + optional RT GI. UzEngine has some pieces (SkyLightComponent, SSGI, RadianceCache/SDF probes, GTAO) but they are not wired together for indoor use. See `docs/analysis/indoor-lighting/` for complete multi-engine analysis.

## What Changes

### New Systems

1. **Irradiance Probe Grid** -- 3D grid of L2 SH probes placed around the camera. Captures irradiance from sky cubemap + scene via GPU ray march (SDF) or RT rays (DXR). Deferred lighting shader samples probes via trilinear SH interpolation for per-pixel diffuse GI.

2. **LightProbeVolumeComponent** -- ECS component defining a box region for probe placement. Manual artist control + auto-placement from scene bounds.

3. **SkyLight Indoor Enhancements** -- Wire existing `indirectLightingIntensity`, `minOcclusion`, and `lowerHemisphereIsBlack=false` into the deferred lighting shader. Currently these fields exist on the component but are not read by the GPU.

4. **DDGI (Dynamic Diffuse GI)** -- GPU ray tracing probe update using DXR infrastructure (BLAS/TLAS already implemented). Traces rays from probe positions, accumulates irradiance, stores in 3D probe texture atlas.

5. **Interior Shadow Enhancement** -- Screen-space contact shadows + GTAO tuning for indoor depth perception. Shadow cascade interior detection to avoid wasting resolution on fully-shadowed interior pixels.

### Refactoring

- `deferred_lighting_ps.hlsl`: Add irradiance probe sampling alongside existing SH
- `frame_constants.cpp`: Wire SkyLight indoor params to PerFrameCB
- `scene_sync.cpp`: Propagate probe volume SRV to draw commands

## Impact
- Affected specs: `docs/specs/rendering/ibl-pipeline.md`, `docs/specs/rendering/renderer.md`
- Affected code: `runtime/engine/renderer/gi/`, `runtime/engine/renderer/passes/lighting/`, `runtime/engine/ecs/environment/`, `runtime/shaders/runtime/lighting/`
- Breaking change: NO -- all additive, existing scenes unchanged
- User benefit: Indoor scenes render with proper lighting and shadow variation
- Enables: RT GI (DDGI), baked lightmaps, light probe editor, VoxelGI
