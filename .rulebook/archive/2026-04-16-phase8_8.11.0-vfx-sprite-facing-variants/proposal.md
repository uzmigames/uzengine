# Proposal: phase8_8.11.0-vfx-sprite-facing-variants

## Why
VFXBillboardCB exposes a facingMode uint32 + customFacingX/Y/Z but the shader only implements FaceCamera (mode 0). The other modes (VelocityAligned, PlaneAxis, CustomAxis, WorldZ) have no shader path. Cheap unlock since CB infrastructure is already in place.

## What Changes
Add shader branches / permutations for the 4 missing modes in particle_billboard_vs.hlsl. Authored emitters that already set facingMode immediately pick them up on the next shader recompile.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl
- Breaking change: NO
- User benefit: Velocity-aligned streaks, plane-pinned sprites, custom-axis sprites all work.
- Estimated effort: M

## Source
UE5 ENiagaraBillboardType.
