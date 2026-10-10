# Proposal: phase8_8.13.0-vfx-trail-decal-streaks

## Why
Particle death decals (3.82) spawn a single decal at the death position. Niagara's trail-decal renderer emits a swept decal mesh along the particle path (tire marks, blood trails, magic grooves). Different visual from single-point decals.

## What Changes
Add VfxTrailDecalEmitter that watches a source emitter's particle paths, accumulates ribbon segments, emits swept decals at a configurable cadence. Shares the ribbon mesh builder (3.72) and the decal pass (3.82).

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/decals/ + trails/trail_mesh_builder.cpp
- Breaking change: NO
- User benefit: Tire marks, blood trails, magic grooves as proper VFX modules.
- Estimated effort: M

## Source
UE5 UNiagaraDecalRendererProperties trail mode.
