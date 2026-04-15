# Proposal: phase7_7.20.0-vfx-velocity-inheritance-spawn-curves

## Why
Small but high-ROI gaps: (a) emitter velocity inheritance - particles spawned while the emitter moves inherit a fraction of the emitter velocity (Niagara's VelocityInheritance). (b) spawn rate as a curve instead of a constant - ramping bursts, pulsing, cooldown. Both currently absent.

## What Changes
Add velocityInheritance (0.0-1.0) field plus an emitterVelocityThisFrame CB field; spawn CS adds inheritance * emitterVel to initial particle velocity. Promote emissionRate to a VfxCurve (once 7.16.0 lands; interim 4-point ramp table).

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/vfx/components/particle_emitter.h, shaders/runtime/vfx/particle_spawn_cs.hlsl
- Breaking change: NO
- User benefit: Realistic velocity inheritance for moving emitters + ramped/pulsed spawn rates without scripts.

## Source
Reference: UE5 FNiagaraEmitterSpawnInfo::SpawnCurve + bInheritVelocity.
