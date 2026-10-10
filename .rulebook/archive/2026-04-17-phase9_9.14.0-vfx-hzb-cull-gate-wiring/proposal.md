# Proposal: phase9_9.14.0-vfx-hzb-cull-gate-wiring

## Why
Phase 8.20.0 shipped the HZB occlusion math + hysteresis state machine, but the frustum-cull path (phase 3.89) doesn't call it. Emitters fully behind opaque geometry still sim + render.

## What Changes
Extend the 3.89 frustum-cull gate: after visibility passes, sample the HZB pyramid at the mip selected by `VfxSelectHZBMip`, run `VfxIsOccludedByHZB`, and push the vote through a per-emitter `VfxOcclusionHysteresis`. Only emitters whose committed state is "visible" reach the sim + render dispatch.

## Impact
- Affected specs: docs/engine/25-vfx/37-hzb-occlusion.md
- Affected code: runtime/engine/renderer/scene/vfx cull path
- Breaking change: NO
- User benefit: 30-50% cost saving on VFX in occlusion-heavy scenes.
- Estimated effort: M

## Source
Niemeyer 2007 HZB.
