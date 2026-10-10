# Proposal: phase8_8.15.0-vfx-volumetric-particle-renderer

## Why
Sprite overdraw is the current only option for volumetric effects (smoke clouds, dust). Niagara ships a dedicated volumetric renderer that raymarches a density field driven by particle positions, producing higher quality for low-density / large-volume effects.

## What Changes
Add VfxVolumetricRenderer: splats particles into a 3D density volume, then a fullscreen raymarch pass samples the volume with absorption + scattering. Volume lives in a bindless RWTexture3D<float>; resolution configurable.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/modules/ + shaders/runtime/vfx/
- Breaking change: NO
- User benefit: Large-volume effects without billboard overdraw.
- Estimated effort: L

## Source
UE5 FNiagaraVolumetricRenderer.
