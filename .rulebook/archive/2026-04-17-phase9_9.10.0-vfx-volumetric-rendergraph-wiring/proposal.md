# Proposal: phase9_9.10.0-vfx-volumetric-rendergraph-wiring

## Why
Phase 8.15.0 shipped the volumetric density-splat + raymarch shaders plus CPU math helpers. No render-graph pass registers them yet, so a volumetric-marked emitter currently falls back to regular billboards.

## What Changes
Register two render-graph passes: `VfxVolumetricSplat` (compute — writes R32UINT aliased density volume) and `VfxVolumetricRaymarch` (fullscreen PS — reads density, writes HDR scene colour via alpha blend). Allocate the 128³ R32F transient volume from the RG allocator. Wire in after lighting, before fog (or after — configurable).

## Impact
- Affected specs: docs/engine/25-vfx/32-volumetric-renderer.md
- Affected code: runtime/engine/renderer/passes/vfx/ + render_graph integration
- Breaking change: NO
- User benefit: Emitters with `rendererType=Volumetric` actually render.
- Estimated effort: L

## Source
UE5 FNiagaraVolumetricRenderer pass graph.
