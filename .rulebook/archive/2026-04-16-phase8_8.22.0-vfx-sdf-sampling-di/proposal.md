# Proposal: phase8_8.22.0-vfx-sdf-sampling-di

## Why
Niagara can sample the scene's global Signed Distance Field for fast particle collision (walls, meshes) or glow-through-geometry effects. UzEngine has per-mesh SDF bakes for DDGI but particles never query them. Cheaper than the 4.23 depth-trace for coarse collision.

## What Changes
Add VfxSceneSDFDI that aggregates per-mesh SDF bakes into a bindless atlas. HLSL helper SampleSceneSDF(worldPos) returns signed distance + gradient; modules use the gradient for collision response or glow attenuation.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/vfx_scene_sdf_di.h/.cpp
- Breaking change: NO
- User benefit: Coarse scene collision without per-mesh compute; glow-through-geometry look.
- Estimated effort: M

## Source
UE5 UNiagaraDataInterfaceDistanceField.
