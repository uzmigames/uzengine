# Proposal: phase8_8.21.0-vfx-advection-velocity-field-di

## Why
The vector-field DI (4.15) samples a procedural or texture-based force field (adds force to velocity). Advection is different: a 3D velocity texture directly SETS particle velocity instead of adding a force. Common for author-driven flow effects (smoke following a painted path).

## What Changes
Add VfxAdvectionFieldDI backed by a bindless Texture3D<float3>. HLSL helper SampleAdvection(worldPos) returns velocity; the update CS lerps between current vel and advected vel by a tightness factor.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/data_interfaces/vfx_advection_field_di.h/.cpp
- Breaking change: NO
- User benefit: Artist-painted flow paths + smoke plume authoring.
- Estimated effort: M

## Source
UE5 UNiagaraDataInterfaceVelocityGrid.
