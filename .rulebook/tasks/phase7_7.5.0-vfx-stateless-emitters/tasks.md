# Phase 7-7.5.0 - VFX Stateless Emitters (Closed-Form Motion)

Emitters with analytical motion evaluated per-frame in the VS instead of a simulation CS. Massive speedup for simple bursts.

Reference: UE5 UNiagaraStatelessEmitter.h

## 1. Mode enum

- [ ] 1.1 Add VfxEmitterMode enum (Stateful, Stateless) to ParticleEmitter.
- [ ] 1.2 Stateless SoA: only spawnPosition, spawnVelocity, spawnTime, lifetime channels (no per-frame writes after spawn).

## 2. Stateless VS

- [ ] 2.1 Create particle_stateless_vs.hlsl evaluating pos = spawnPos + spawnVel*t + 0.5*gravity*t*t where t = currentTime - spawnTime.
- [ ] 2.2 Apply stateless damping analytically: vel *= exp(-damping * t).
- [ ] 2.3 Skip the update CS dispatch for stateless emitters in frame_particles.cpp.

## 3. Preset and demo

- [ ] 3.1 Preset: Muzzle Flash as a stateless spark burst (short lifetime, high initial velocity).
- [ ] 3.2 Demo integration: toggle Stateful vs Stateless to compare.

## 4. Benchmark

- [ ] 4.1 Measure at 10k particles: stateless must be >= 2x cheaper than stateful equivalent.

## 5. Docs and tests

- [ ] 5.1 Documentation: 19-stateless-emitters.md covering the mode, limitations, and benchmark.
- [ ] 5.2 Tests: closed-form motion accuracy vs stateful reference, boundary (t > lifetime).

## 6. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 6.1 Update or create documentation covering the implementation
- [ ] 6.2 Write tests covering the new behavior
- [ ] 6.3 Run tests and confirm they pass
