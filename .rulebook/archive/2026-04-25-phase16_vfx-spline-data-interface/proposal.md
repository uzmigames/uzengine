# Spline Data Interface for VFX

## Why

Niagara's `Spline Data Interface` lets emitters sample a spline's position/tangent/normal at parametric u ∈ [0,1] on the GPU, enabling spawn-along-path, force-toward-spline, and follow-path behaviors without per-frame CPU readback. It is the backbone of magic trails, cinematic streams, bullet paths, blood drips on terrain, and traffic/crowd VFX.

We have no spline data interface today. Adding one opens a whole category of VFX that currently requires bespoke scripting.

## What Changes

- Define `SplineDataInterface` abstraction: an ordered set of control points (position + tangent) uploaded to a StructuredBuffer each frame.
- Engine already has spline components (check `runtime/engine/` for existing Spline work); if none, add a minimal Catmull-Rom spline evaluator (position / tangent / arc-length).
- GPU HLSL helpers: `SplineSampleAtU(splineIdx, u) -> (position, tangent)`.
- New modules:
  - `SpawnAlongSplineModule` — distributes spawned particles uniformly along arc length.
  - `ForceTowardSplineModule` — spring force pulling particles toward nearest spline point.
  - `FollowSplineModule` — per-particle u-parameter advances over lifetime, position = spline sample(u).
- Editor: UI to pick/bind a spline entity per emitter.

## Impact

- Affected specs: VFX data interface architecture
- Affected code: new `runtime/engine/vfx/data_interfaces/spline_di.*`, new modules, HLSL helpers, editor binding UI
- Breaking change: NO (additive modules, no change to existing emitters)
- User benefit: enables path-following VFX authoring without gameplay scripting
