# Proposal: 3.5b — Rendering Bridge (MERGE POINT)

## Why
This is the merge point where Track A (rendering) and Track B (ECS/scene) connect. PrimitiveComponent bridges ECS entities to the renderer via RenderProxy → RenderWorld. After this, loaded .uzscene files render with deferred PBR + shadows + post-processing.

## What Changes
- PrimitiveComponent: base for all renderable components, registers in RenderWorld
- StaticMeshComponent: references .uzmesh + material
- LightComponent: point, directional, spot with color/intensity/range
- CameraComponent: FOV, near/far, active camera selection
- RenderProxy: ECS → render-thread data sync via command queue
- RenderSubsystem: WorldSubsystem that collects visible entities → generates draw calls

## Impact
- Affected specs: `docs/engine/12-rendering/01-render-world.md`
- Affected code: `runtime/core/renderer/bridge/`, `runtime/core/ecs/components/rendering/`
- Breaking change: YES — rendering no longer driven by demo code, driven by ECS
- User benefit: Scene entities render automatically via deferred pipeline

## Demo Gate
Load a .uzscene → entities render with deferred PBR + shadows + post-processing.
