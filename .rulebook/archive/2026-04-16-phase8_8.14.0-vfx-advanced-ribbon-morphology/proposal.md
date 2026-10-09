# Proposal: phase8_8.14.0-vfx-advanced-ribbon-morphology

## Why
The multi-strand ribbon system (3.75) has uniform width per strand. Niagara supports per-point width curves, per-point twist, per-point opacity/thickness, and morph-target-style per-vertex attribute overrides. Enables more expressive trails (tapered lightning, twisted energy).

## What Changes
Extend TrailComponent with per-point VfxTangentCurve (7.16.0) for width / twist / opacity. TrailMeshBuilder samples the curve per ribbon point when generating the quad strip. Material unchanged.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/trails/trail_component.h, trail_mesh_builder.cpp
- Breaking change: NO
- User benefit: Tapered / twisted / fading trails without hand-authored meshes.
- Estimated effort: M

## Source
UE5 FNiagaraRibbonShapeCustomVertex + FNiagaraRibbonPerVertexData.
