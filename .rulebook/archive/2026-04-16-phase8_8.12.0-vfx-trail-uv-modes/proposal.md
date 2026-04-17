# Proposal: phase8_8.12.0-vfx-trail-uv-modes

## Why
The trail / ribbon system (3.72 / 3.75) uses arc-length normalised V coordinate exclusively. Niagara offers Stretch (V across segment), Tiled (V repeats every N), and PerRibbon (independent UV per strand) modes. Common in shipping effects (flame throwers, lightning bolts).

## What Changes
Add VfxTrailUVMode enum + per-strand config field. TrailMeshBuilder emits V according to the selected mode; the shader sampler does not need to change.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/trails/trail_mesh_builder.cpp
- Breaking change: NO (new optional mode)
- User benefit: Flame-thrower / lightning-bolt / per-strand-UV effects all possible.
- Estimated effort: M

## Source
UE5 ENiagaraRibbonUVMode.
