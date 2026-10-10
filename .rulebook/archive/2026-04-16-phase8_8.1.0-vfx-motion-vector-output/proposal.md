# Proposal: phase8_8.1.0-vfx-motion-vector-output

## Why
Particles need per-pixel motion vectors written into the velocity buffer so TAA / DLSS / FSR can reproject them correctly. VFXBillboardCB already carries a prevViewProj matrix; the shader just never emits the delta. Small quick-win that improves upscaler quality across every effect.

## What Changes
Extend the particle billboard PS to compute clip-space delta using prevViewProj, pack as screen-space vector, output to MRT slot 1 (velocity buffer). Mirror in mesh / trail / ribbon shaders. No new CB fields required.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl + mesh/trail variants
- Breaking change: NO
- User benefit: No more particle ghosting under TAA / DLSS / FSR reprojection.
- Estimated effort: S

## Source
UE5 NiagaraSpriteVertexFactory + FNiagaraMeshVertexFactory motion-vector output.
