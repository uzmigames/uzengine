# Proposal: phase9_9.12.0-vfx-stencil-customdepth-pso-wiring

## Why
Phase 8.17.0 defined `VfxStencilConfig` and the `vfx_customdepth_ps.hlsl` silhouette shader, but no PSO variants exist for stencil writes and no render-graph pass draws into the CustomDepth RT.

## What Changes
- **Stencil writes:** new billboard PSO permutations with `StencilOp::Replace` (mode 1) and `StencilOp::Increment` (mode 2), bound when `config.writeMode != Disabled`.
- **CustomDepth pass:** depth-only RG pass bound to the existing CustomDepth RT; iterates emitters with `customDepthEnabled = true` using the phase 8.17.0 silhouette PS.

## Impact
- Affected specs: docs/engine/25-vfx/34-stencil-customdepth.md
- Affected code: runtime/engine/renderer/passes/vfx/ + pipeline library
- Breaking change: NO
- User benefit: Stencil masks + CustomDepth reveal effects actually fire.
- Estimated effort: M

## Source
UE5 Niagara CustomDepth pass + stencil permutations.
