# Proposal: phase7_7.7.0-vfx-local-space-simulation

## Why
Niagara emitters can simulate in local space (positions relative to the component transform) so forces/trails follow the owner without world-space decoupling. UzEngine currently simulates in world space for every emitter - fast-moving attached effects (weapons, vehicles) leave trails that lag behind. The VfxSystemEmitterRef::localSpace field exists but is only used for transform propagation, not true local-space simulation.

## What Changes
Add a simulationSpace mode (LocalSpace | WorldSpace). In LocalSpace, positions/velocities are stored and simulated in the emitter's local frame; the billboard VS applies worldFromLocal at draw time. Gravity remains world-space (converted per-emitter at spawn).

## Impact
- Affected specs: docs/engine/25-vfx/18-vfx-system-compound.md
- Affected code: runtime/engine/vfx/components/, shaders/runtime/vfx/particle_update_cs.hlsl, particle_billboard_vs.hlsl
- Breaking change: NO (default WorldSpace preserves current behavior)
- User benefit: Attached effects (muzzle flashes, vehicle exhaust) stay correctly anchored to fast-moving entities.

## Source
Reference: UE5 UNiagaraEmitter::bLocalSpace.
