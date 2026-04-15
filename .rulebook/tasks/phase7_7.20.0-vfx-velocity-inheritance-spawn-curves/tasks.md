# Phase 7-7.20.0 - VFX Velocity Inheritance + Spawn-Rate Curves

Two low-cost gaps: emitter velocity inheritance and ramped spawn rates.

Reference: UE5 FNiagaraEmitterSpawnInfo::SpawnCurve + bInheritVelocity

## 1. Velocity tracking

- [ ] 1.1 Track per-emitter frame velocity (world-space delta / dt).
- [ ] 1.2 Expose via CB field emitterVelocityThisFrame.

## 2. Inheritance field

- [ ] 2.1 Add velocityInheritance (0.0-1.0) to ParticleEmitter config.
- [ ] 2.2 Spawn CS: initialVel = spawnVel + velocityInheritance * emitterVel.

## 3. Spawn rate curve (interim)

- [ ] 3.1 4-point ramp table with (time, rate) pairs + linear lerp.
- [ ] 3.2 Preserve emissionRate scalar as the fallback default.

## 4. Full curve migration

- [ ] 4.1 Once 7.16.0 curve editor ships, migrate ramp tables to VfxCurve.
- [ ] 4.2 Keep the interim ramp table path for backward compat.

## 5. Demo

- [ ] 5.1 Moving-emitter preset showing inherited velocity trail.
- [ ] 5.2 Pulsed-spawn preset showing ramped emission bursts.

## 6. Docs and tests

- [ ] 6.1 Documentation on velocity inheritance semantics and spawn-rate curve format.
- [ ] 6.2 Tests: velocity inheritance math, ramp table lerp correctness.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
