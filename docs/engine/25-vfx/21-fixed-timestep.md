# Deterministic Fixed-Timestep VFX Simulation

Frame-rate-independent sub-stepped update dispatch for particle emitters.
Niagara-parity feature critical for competitive multiplayer parity,
deterministic replay systems, and stable physics/collision across
hardware tiers.

## Problem

Particle simulation at wildly different frame rates drifts in behaviour:

- A curl-advection solver tuned at 60 Hz explodes at 144 Hz (too large
  per-tick turbulence kick).
- Gravity-driven debris reaches different apexes at 30 vs 60 Hz because
  Euler integration with larger `dt` overshoots.
- Collision events fire at different timestamps on different hardware,
  breaking replay sync.

## Solution — `VfxTimestepAccumulator`

A CPU-side accumulator that converts variable `frameDt` into a
deterministic sequence of fixed sub-step dispatches:

```cpp
#include "engine/vfx/core/vfx_timestep.h"

VfxTimestepAccumulator acc;
acc.SetFixedRate(1.0f / 60.0f, /* maxSubSteps = */ 8);

// Per frame:
const uint32_t steps = acc.Advance(frameDt);
for (uint32_t i = 0; i < steps; ++i) {
    DispatchParticleUpdate(acc.SubStepDt());  // always 1/60 s
}
```

`SubStepDt()` returns the size of each sub-step; `Advance` returns how
many to dispatch this frame. Residual frame time is carried into the
next frame so long-term average simulation time matches real time.

## Anti-spiral clamp

When the frame time spikes (debugger pause, hitch, app minimised), the
accumulator could demand arbitrarily many sub-steps, which would make
the next frame even longer — the classic spiral-of-death. `Advance`
clamps at `maxSubSteps` (default 8) and **drops** any remaining
accumulator. Simulation progresses slightly slower than real time
during the hitch, but the spiral is broken immediately.

## Emitter fields

`ParticleEmitter` gains three fields authored per emitter:

| Field           | Type    | Default       | Meaning                                |
|-----------------|---------|---------------|----------------------------------------|
| `timestepMode`  | uint8   | 0 (FrameRate) | 0 = FrameRate, 1 = FixedRate.          |
| `fixedDt`       | float   | 1/60          | Sub-step size when FixedRate.          |
| `maxSubSteps`   | uint32  | 8             | Anti-spiral clamp when FixedRate.      |

FrameRate mode matches pre-7.8.0 behaviour — one dispatch per frame,
`subStepDt = frameDt`. Emitters only opt in to FixedRate when
determinism is required.

## Edge cases

- **Negative / NaN / Inf `frameDt`** → returns 0 sub-steps; simulation
  does not rewind or blow up.
- **`fixedDt <= 0`** → `SetFixedRate` silently falls back to FrameRate
  mode (defensive fallback rather than a crash).
- **`maxSubSteps == 0`** → clamped to 1 so the simulation never stalls
  permanently.
- **Sub-`fixedDt` frame** → returns 0 sub-steps, accumulator grows;
  next frame will usually trigger a step.

## Tests

`runtime/tests/engine/vfx/test_vfx_timestep.cpp` — 12 tests covering:

- FrameRate default + 1-step-per-frame + dt mirroring.
- FrameRate rejection of NaN / Inf / non-positive dt.
- FixedRate exact multiples, sub-`fixedDt` frames, residual carry-over.
- Anti-spiral clamp behaviour + excess-accumulator drop.
- Degenerate `fixedDt` fall-back to FrameRate.
- Reset clears residual.
- Cross-framerate determinism (30Hz vs 60Hz input → same total sub-steps).
- Mode-switch clearing the accumulator.

## Future work

Wiring the accumulator into the render-graph dispatch loop (consuming
`ParticleEmitter::timestepMode` / `fixedDt` / `maxSubSteps` in
`frame_particles.cpp`) is tracked in
`phase7_7.8.1-vfx-fixed-timestep-dispatch-integration`. The CPU
contract shipped in 7.8.0 is the freeze point — the follow-up only adds
the per-emitter runtime accumulator instance and the sub-step loop
around the existing update CS dispatch call.

## References

- `runtime/engine/vfx/core/vfx_timestep.h` / `.cpp`
- `runtime/engine/vfx/components/particle_emitter.h` — emitter fields.
- UE5 Niagara `UNiagaraEmitter::FixedTickDeltaTime`.
