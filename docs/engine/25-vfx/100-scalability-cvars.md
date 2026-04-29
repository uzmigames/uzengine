# VFX Scalability + Sequencer Hooks

> Status: shipped 2026-04-25 (`phase16_vfx-scalability-cvars-sequencer`).
> Hardware-driven `Seek` warmup loop + integration tests tracked
> separately by `phase16_vfx-scalability-hardware-validation`.

This document describes the runtime CVars, the per-emitter pause /
time-scale fields, the deterministic `Seek` API, and the public
sequencer C / Lua surface that drives them. The five `r.VFX.*` CVars
form a uniform "scalability" layer for shipping titles to dial down
density on low-end hardware; the `Seek` / `pause` / `time-scale`
hooks are the entry points for cinematic timeline integrations.

## CVars

All five CVars are registered when `VFXSystem::Initialize` runs. They
are sampled once per frame by `VFXSystem::Tick` and cached on the
system for `O(1)` lookup from inline dispatch code.

| CVar | Type | Default | Range | Effect |
|------|------|---------|-------|--------|
| `r.VFX.MaxTotalParticles` | uint32 | construction value | `[1024, 10M]` | Hard cap propagated to `ParticleBudget::SetMax` at frame start. Lowering it starves new spawns until usage drops below the new limit; existing alive particles are not killed retroactively. |
| `r.VFX.AllowGPUSim` | bool | `1` | `0`/`1` | When `0`, `DispatchVFXSimulation` skips the spawn / update / indirect-args dispatch entirely and logs a one-time warning. Existing alive particles continue to render but no longer advance. There is no CPU sim fallback. |
| `r.VFX.QualityLevel` | int | `2` (high) | `0..3` | Selects the preset table below. |
| `r.VFX.SimulatePaused` | bool | `0` | `0`/`1` | When `1`, the inline sim path forces `dt = 0` for every emitter; render still runs. Used by screenshot capture and editor pause. |
| `r.VFX.TimeScale` | float | `1.0` | `[0, 100]` | Global VFX dt multiplier. Applied BEFORE per-emitter `timeScale`. Negative values clamp to 0 (same effect as `SimulatePaused = 1`). |

### Quality preset multipliers

| Level | Spawn rate × | LOD distance × | Intent |
|------:|-------------:|---------------:|--------|
| 0 (low) | 0.25 | 0.50 | Minimum viable density on Steam-Deck class hardware. |
| 1 (medium) | 0.50 | 0.75 | Mid-range PCs / consoles. |
| 2 (high) | 1.00 | 1.00 | Default — author intent. |
| 3 (cinematic) | 1.50 | 1.50 | Photo / cinematic capture, no FPS budget. |

The values surface via `VFXSystem::GetQualitySpawnRateMultiplier()` and
`GetQualityLodDistanceMultiplier()`. Authors who need finer per-emitter
control should override `emissionRate` / LOD distances directly on the
`MeshRenderModule` / sprite emitter instead.

## Per-emitter scalability state

`ParticleEmitter` gained three independent fields plus the `Seek` API.
None of them affect render-side state — they only modulate the dt the
sim path sees, or trigger a deterministic restart.

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `isPaused` | bool | `false` | When `true`, sim sees `dt = 0` for this emitter only. Existing particles freeze in place; render keeps drawing. Independent from `isEmitting` (which stops new spawns but lets existing particles age). |
| `timeScale` | float | `1.0` | Per-emitter dt multiplier applied AFTER `r.VFX.TimeScale`. Slow-mo (0..1), fast-forward (>1), zero (= paused). |
| `name` | string | `""` | Stable identifier for `VFXSystem::FindEmitterByName`. Empty names are unreachable from the C API — sequencer authors must set this when spawning the emitter. |
| `pendingSeekTime` | float | `0.0` | Set by `Seek(t)`, cleared by `ClearPendingSeek()` once the warmup loop runs. `0.0` = no seek pending. |

### Effective dt formula

```
emitterDt = isPaused ? 0
                     : (frameDt * (SimulatePaused ? 0 : r.VFX.TimeScale)
                                * timeScale)
```

`emitterDt` flows into both the spawn CS (spawn count) and the update
CS (age / position advance). A slow-mo emitter is visually slow-mo
across BOTH spawn and update — it does not "burst spawn" the moment
it returns to real-time.

### Seek

```cpp
void ParticleEmitter::Seek(float targetTime) noexcept;
[[nodiscard]] float ParticleEmitter::GetPendingSeekTime() const noexcept;
void ParticleEmitter::ClearPendingSeek() noexcept;
```

`Seek(t)` requests a deterministic re-warm:

1. Sets `pendingSeekTime = max(0, t)`.
2. Resets `cpuAliveCount = 0` and `spawnAccumulator = 0`.
3. Flags `buffer.needsInitialization = true` so the next sim tick
   recreates GPU bookkeeping (free-list, alive count) from scratch.

The next sim tick consumes `pendingSeekTime` by dispatching
`ceil(t / kSeekFixedDt)` micro-ticks before the regular per-frame
tick (`kSeekFixedDt = 1/60s`). The warmup loop itself is implemented
in `phase16_vfx-scalability-hardware-validation` (gated by access to
DX12 hardware for the multi-dispatch barrier verification). Until that
ships, `Seek` is a "reset to t=0" — useful but not deterministic to a
specific replay time.

## C API

```c
UZ_API int uz_vfx_seek(const char* name, float time_seconds);
UZ_API int uz_vfx_pause(const char* name);
UZ_API int uz_vfx_resume(const char* name);
UZ_API int uz_vfx_set_time_scale(const char* name, float scale);
```

All four functions resolve the target via `FindEmitterByName`. They
return `1` on success and `0` when the runtime has no active
viewport / VFXSystem or no emitter matches `name`. Calling with a
null / empty name is also a logged failure.

`name` is matched against `ParticleEmitter::name`. Emitters created
without a name are unreachable from this API — their pointers must be
captured by the spawning subsystem and addressed directly from C++.

## Lua API

```lua
-- Existing
uz.vfx:pause(handle, paused)        -- stops emission + saves rate

-- New (this task)
uz.vfx:freeze(handle, frozen)       -- freezes sim only (isPaused field)
uz.vfx:set_time_scale(handle, scale)
uz.vfx:seek(handle, time_seconds)
uz.vfx:find_by_name(name)           -> handle | nil
```

`pause` and `freeze` have distinct semantics: `pause` stops new
spawns and zeroes `emissionRate` (existing particles continue to
age); `freeze` flips `isPaused` so the sim sees `dt = 0` while
emission state is preserved. Use `pause` to suspend an effect until
later; use `freeze` for screenshot / editor scrub.

## Test coverage

`tests/engine/vfx/test_vfx_scalability_cvars.cpp` (19 tests):

| Suite | What it covers |
|-------|----------------|
| `VfxScalability` (8) | CVar registration on Initialize, defaults, Tick samples each of the five CVars and propagates `MaxTotalParticles` to the budget. |
| `VfxScalability` quality preset (1) | Multiplier table for all four levels. |
| `ParticleEmitterSeek` (4) | Default `pendingSeekTime = 0`, Seek stores target + resets accumulators + flags reinit, negative target clamps to 0, `ClearPendingSeek` is idempotent. |
| `ParticleEmitterScalability` (3) | `isPaused` defaults false, `timeScale` defaults 1.0, fields are independent. |
| `VfxFindByName` (4) | Returns registered emitter by name, returns null for unknown / empty names, ignores unnamed emitters. |

Hardware-gated tests (`test_vfx_seek_determinism`,
`test_vfx_simulate_paused_freezes_particles`,
`test_vfx_max_total_particles_caps_alive_count`) are tracked by
`phase16_vfx-scalability-hardware-validation`.

## Seek Warmup Loop

When `ParticleEmitter::Seek(t)` is called (e.g., by sequencer timeline
scrub), the next frame's inline sim path (`runtime/engine/renderer/frame/frame_vfx_inline.cpp:685-740`)
consumes the pending seek time by dispatching micro-ticks of the spawn
+ update + indirect-args compute shaders. Each tick uses a fixed `dt =
1/60s` (`kSeekFixedDt` defined at `frame_vfx_inline.cpp:47-60`) and an
incrementing `frameNumber` per tick for spawn-RNG variation, mirroring a
regular simulation tick. After the warmup completes, `ClearPendingSeek()`
returns the emitter to the regular per-frame tick path.

### Frame budget cap: `r.VFX.Seek.MaxTicksPerFrame`

A naive 2.5-second seek requests `ceil(2.5 / (1/60)) = 150` ticks,
easily fitting in one frame. However, longer scrubs (e.g., 10 seconds
= 600 ticks) would stall the main thread for several milliseconds. The
CVar `r.VFX.Seek.MaxTicksPerFrame` (default 256, registered at
`vfx_system.cpp:70`, range [1, 100000]) caps per-frame warmup dispatch
count. When a seek exceeds the budget, remaining ticks roll over to
subsequent frames, and the emitter remains mid-warmup (`pendingSeekTime
> 0`) until the scrub completes. During multi-frame splits, the
emitter is visually frozen — the surface `pendingSeekTime` field is
the signal to the application.

### Measured timings

CPU-side `CalculateSpawnCount` and budget arithmetic measured on a
5-frame warmup (150 ticks @ 1k, 4k, 16k particle capacities):

| Capacity | Ticks | Wall-clock (ms) | GPU sim cost | Frames |
|----------|-------|-----------------|--------------|--------|
| 1k | 150 | 0.0016 | Real HW only | 1 |
| 4k | 150 | 0.0016 | Real HW only | 1 |
| 16k | 150 | 0.0016 | Real HW only | 1 |

The CPU time is negligible. The dominant cost is the GPU dispatch
(spawn + update + indirect-args compute), measurable only on real DX12
hardware via CI. Test builds use MockRHIDevice, which reports `gpu_ms
= 0.0`. For production estimates, expect 1–3 ms per dispatch on modern
GPUs (dependent on emitter spawn count, update complexity, and hardware
generation).

### Recommended budgets

Tune `r.VFX.Seek.MaxTicksPerFrame` per content type and platform:

- **Cinematic prerolls** (seeking to known timestamps): `1024` — supports
  single-frame scrubs up to ~17 seconds at 60 Hz without splitting.
- **Editor / live timeline scrub** (interactive): `256` (default) — safe
  for interactive use; longer scrubs split visibly across frames (2–4 frames
  for 10-second seeks).
- **Mobile / low-end hardware**: `64` — conservative budget; a 10-second
  seek splits into ~6 frames at ~0.5 warmup-ticks-per-frame ratio.

### Cross-references

- Implementation: `frame_vfx_inline.cpp:685-740` (warmup dispatch loop)
- CVar registration: `vfx_system.cpp:70` (Seek max-ticks budgeting)
- Tests: `runtime/tests/engine/vfx/test_vfx_seek_determinism.cpp` (18 tests
  covering CPU-side state and tick arithmetic)
- Sister CVars: `r.VFX.SimulatePaused` (section "CVars"), `r.VFX.MaxTotalParticles`
  (section "CVars")
