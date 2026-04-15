# Phase 7-7.8.0 - VFX Fixed Timestep Accumulator

Deterministic sub-stepped update CS dispatch. FrameRate mode (default) preserves current behavior; FixedRate runs N fixed sub-steps until the accumulator drains.

Reference: UE5 UNiagaraEmitter::FixedTickDeltaTime

## 1. Configuration

- [ ] 1.1 Add timestepMode enum (FrameRate, FixedRate) and fixedDt field to ParticleEmitter.
- [ ] 1.2 Expose via .uzfx JSON (fixed_dt field).

## 2. Accumulator loop

- [ ] 2.1 Per-frame: accumulator += frameDt.
- [ ] 2.2 While accumulator >= fixedDt and steps < maxSteps (8) - dispatch update CS with fixedDt; accumulator -= fixedDt.
- [ ] 2.3 Anti-spiral clamp: drop any remaining accumulator when maxSteps hit.

## 3. Demo

- [ ] 3.1 Side-by-side FrameRate vs FixedRate at 30/60/144 Hz - visual confirmation of determinism.

## 4. Docs and tests

- [ ] 4.1 Tests: accumulator drains correctly, maxSteps clamp fires, identical particle positions across framerates in FixedRate mode.
- [ ] 4.2 Documentation on timestep modes and trade-offs.

## 5. Tail (mandatory - enforced by rulebook v5.3.0)
- [ ] 5.1 Update or create documentation covering the implementation
- [ ] 5.2 Write tests covering the new behavior
- [ ] 5.3 Run tests and confirm they pass
