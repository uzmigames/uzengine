# Proposal: phase9_9.11.0-vfx-oit-rendergraph-wiring

## Why
Phase 8.16.0 shipped the Weighted Blended OIT math + resolve shader. Emitters authored with `blendMode=WeightedOIT` (enum value 3) currently render as plain additive because nothing allocates the accum/revealage RTs or routes the draw to the OIT pass.

## What Changes
Add the transient RTs (RGBA16F accum, R8 revealage) to the render graph. Accumulation pass reuses the billboard PSO with two-RT blend state (one=one on accum, zero=invSrcAlpha on revealage). Fullscreen resolve composites via `vfx_oit_resolve_ps.hlsl`. Emitters with `blendMode=3` route into this pass instead of the regular translucent pass.

## Impact
- Affected specs: docs/engine/25-vfx/33-weighted-oit.md
- Affected code: runtime/engine/renderer/passes/vfx/ + billboard PSO permutation
- Breaking change: NO
- User benefit: Dense interpenetrating transparents without sort artefacts.
- Estimated effort: L

## Source
McGuire & Bavoil 2013.
