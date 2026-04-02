# Phase 4d — 4.24.0 VFX Warmup / Pre-warm Simulation

Simulate N seconds of particles instantly at emitter spawn so looping effects
(smoke, fire, dust) appear fully populated from frame 1. Niagara reference:
`WarmupTime` + `WarmupTickRate` on UNiagaraSystem. No new shaders needed —
reuses existing spawn/update compute dispatches in a tight loop.

---

## 1. Emitter Data

- [ ] 1.1 Add `float warmupTime = 0.0f` to `ParticleEmitter` in `particle_emitter.h`.
      Default 0 means no warmup (backwards compatible). Units: seconds.
- [ ] 1.2 Add `float warmupTickRate = 0.0333f` to `ParticleEmitter` (1/30s default).
      Controls simulation granularity during warmup. Lower = more accurate but slower.
- [ ] 1.3 Add `bool warmupComplete = false` runtime flag to `ParticleEmitter`.
      Set to true after warmup loop finishes. Prevents re-warmup on hot reload.

## 2. .uzvfx Format Support

- [ ] 2.1 Add `warmup_time` and `warmup_tick_rate` optional JSON fields to emitter
      section in `uzfx_loader.cpp`. Default to 0 and 1/30 if absent.
- [ ] 2.2 Add serialization in `vfx_graph.cpp` Load/Save for warmup fields.
- [ ] 2.3 Update `vfx_graph_validator.cpp` — warn if warmupTime > 10s (likely mistake),
      error if warmupTickRate <= 0.

## 3. Warmup Dispatch Loop

- [ ] 3.1 In `VFXSystem::SpawnEmitter()` (or `frame_particles.cpp` first-frame path),
      detect `warmupTime > 0 && !warmupComplete`. Calculate tick count:
      `uint32_t ticks = uint32_t(warmupTime / warmupTickRate)`.
- [ ] 3.2 Execute a tight loop of `ticks` iterations, each dispatching:
      (a) spawn CS with deltaTime = warmupTickRate,
      (b) update CS with deltaTime = warmupTickRate.
      No render dispatches during warmup — only simulation.
- [ ] 3.3 After the loop, set `warmupComplete = true` and proceed to normal rendering.
- [ ] 3.4 Clamp max ticks to 1000 (33s at 1/30 rate) to prevent accidental GPU hang
      from misconfigured warmupTime.
- [ ] 3.5 Insert a single UAV barrier after the warmup loop before the first render
      dispatch to ensure all warmup writes are visible.

## 4. Budget Integration

- [ ] 4.1 During warmup, particles spawned count against the global ParticleBudget
      normally — no special case. If budget is exhausted mid-warmup, remaining
      ticks still run (update only, no more spawns).
- [ ] 4.2 Log `UZ_LOG(VFX, Info, "Warmup: %u ticks, %u particles alive", ticks, aliveCount)`
      after warmup completes.

## 5. Hot Reload Interaction

- [ ] 5.1 On VFX hot reload (`vfx_hot_reload.cpp`), reset `warmupComplete = false`
      so the emitter re-warms with updated parameters.
- [ ] 5.2 On hot reload, also reset spawn accumulator to 0 to prevent double-spawn burst.

## 6. Demo Preset

- [ ] 6.1 Create `sample/content/vfx/presets/ambient_dust.uzvfx` — slow-moving dust motes
      with `warmup_time: 5.0`, low emission rate (20/s), long lifespan (8s). Should appear
      fully filled from frame 1.
- [ ] 6.2 Add ambient_dust to vfx_showcase.uzscene as a new emitter instance.

## 7. Tests

- [ ] 7.1 Unit test: warmup tick count calculation (warmupTime=3, tickRate=1/30 → 90 ticks).
- [ ] 7.2 Unit test: warmup clamp (warmupTime=100, tickRate=1/30 → capped at 1000 ticks).
- [ ] 7.3 Unit test: warmupTime=0 → no warmup loop executed, warmupComplete stays false.
- [ ] 7.4 Integration test: after warmup, alive count > 0 (particles were spawned).
