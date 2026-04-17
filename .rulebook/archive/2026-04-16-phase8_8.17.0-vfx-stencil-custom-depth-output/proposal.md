# Proposal: phase8_8.17.0-vfx-stencil-custom-depth-output

## Why
Particles currently cannot write to the stencil buffer or CustomDepth channel. Niagara tags emitters for stencil output (post-process masking like heat haze only where fire is) and CustomDepth output (reveal masks, outlines, wallhacks).

## What Changes
Add stencilRef + customDepthEnabled flags on the emitter config. Shader permutation writes stencil via SV_StencilRef (requires D3D12 SV_StencilRefOutput) and CustomDepth via a dedicated depth-only prepass.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/components/particle_emitter.h, shaders/runtime/vfx/
- Breaking change: NO
- User benefit: VFX participate in post-process masking + reveal effects.
- Estimated effort: M

## Source
UE5 UNiagaraRendererProperties::bRenderCustomDepth + stencil controls.
