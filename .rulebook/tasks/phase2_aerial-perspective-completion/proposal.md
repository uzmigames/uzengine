# Proposal: Aerial Perspective Completion (Phase 2)

## Why
The aerial perspective 3D volume pass exists (AerialPerspectiveVolumePass) but is only partially integrated — distant objects do not properly blend with atmospheric scattering. This makes the scene look like objects are "cut out" against the sky instead of naturally fading. Completing this is high ROI (2-3 days for a dramatic improvement in outdoor scenes) since the compute pass already generates the 3D texture. We just need to sample it during deferred lighting.

## What Changes

### New Systems
1. Aerial perspective sampling function in deferred lighting — sample 3D LUT based on view distance and altitude
2. Aerial perspective integration for transparent/forward objects

### Refactoring
- aerial_perspective_volume_pass.cpp: verify 3D texture dimensions and depth slicing are correct per Hillaire 2020
- deferred_lighting_ps.hlsl: add aerial perspective contribution after direct + IBL lighting
- height_fog_ps.hlsl: blend aerial perspective with existing height fog (they are complementary, not redundant)

## Impact
- Affected specs: docs/specs/rendering/shader-library.md
- Affected code: runtime/engine/renderer/passes/atmosphere/aerial_perspective_volume_pass.cpp/.h, runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl, runtime/shaders/runtime/atmosphere/height_fog_ps.hlsl
- Breaking change: NO — additive visual improvement
- User benefit: Natural atmospheric depth in outdoor scenes, objects fade into sky at distance
- Enables: Physically-correct large-scale outdoor rendering
