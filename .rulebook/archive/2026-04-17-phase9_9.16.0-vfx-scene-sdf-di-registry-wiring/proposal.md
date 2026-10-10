# Proposal: phase9_9.16.0-vfx-scene-sdf-di-registry-wiring

## Why
Phase 8.22.0 shipped `VfxSceneSDFAtlasDesc` + HLSL helpers + collision math, but there's no atlas manager that combines per-mesh SDF bricks at scene load, and the compositor doesn't know about the DI yet.

## What Changes
- **Atlas manager:** new `VfxSceneSDFAtlas` service. Scene-load iterates every static mesh with an SDF brick and composites them (per-voxel min) into the atlas Texture3D.
- **Registry entry:** compositor registers the `scene_sdf` DI; emitter assets opt in via a collision module that uses it.

## Impact
- Affected specs: docs/engine/25-vfx/39-scene-sdf.md
- Affected code: runtime/engine/vfx/modules/ + scene load path + module compositor
- Breaking change: NO
- User benefit: Particles collide / glow-through geometry.
- Estimated effort: L

## Source
UE5 UNiagaraDataInterfaceDistanceField atlas path.
