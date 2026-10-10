# Proposal: phase10_10.6.0-render-graph-mrt-blend-helper

## Why
Phase 9.10 (volumetric RG), 9.11 (OIT RG), and 9.12 (stencil PSO) all need MRT passes with per-RT independent blend states and transient 3D/2D RT allocation. The render-graph today only offers single-RT convenience helpers; MRT passes require boilerplate that each task would duplicate.

## What Changes
Add `RgBuilder::AddMRTPass(name, colorFormats[], depthFormat, callback)` convenience that allocates transient RTs, sets up per-RT blend state from a `MrtBlendDesc[]` array, and wires in the standard color+depth attachment binding. Also expose `RgBuilder::AllocateTransient3D(w, h, d, format)` for the volumetric density volume.

## Impact
- Affected specs: docs/engine/renderer/render_graph/
- Affected code: runtime/engine/renderer/render_graph/rg_builder.*
- Breaking change: NO
- User benefit: Downstream 9.10 / 9.11 / 9.12 ship without boilerplate duplication.
- Estimated effort: M

## Source
UE5 RDG MRT pass helpers.
