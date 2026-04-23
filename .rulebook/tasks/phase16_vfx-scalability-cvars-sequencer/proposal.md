# VFX Scalability CVars + Cinematic Sequencer Hooks

## Why

Niagara exposes a first-class scalability layer (`fx.Niagara.MaxParticles`, `fx.Niagara.AllowGPUParticles`, platform quality presets) so shipping games can uniformly scale VFX density for low-end hardware, and a sequencer integration so cinematics can pause/resume/seek particle time deterministically.

We have per-emitter budget allocation, but no global runtime-tunable CVar surface, and no way to drive emitter playback from a timeline. Cinematics and gameplay cutscenes cannot currently sync with VFX.

## What Changes

- CVars:
  - `r.VFX.MaxTotalParticles` — global cap; budget allocator uses this as ceiling.
  - `r.VFX.AllowGPUSim` — 0 disables all GPU emitters (falls back to CPU-sim sprites for platforms without compute).
  - `r.VFX.QualityLevel` — 0..3 scales spawn rate + LOD distance multipliers via preset.
  - `r.VFX.SimulatePaused` — 0/1 pauses all simulation (for screenshot capture).
  - `r.VFX.TimeScale` — multiplier for all emitters' dt (slow-mo / speed-up).
- Sequencer hooks (minimal, C API so any timeline system can call):
  - `uz_vfx_seek(emitterId, timeSeconds)` — rewind/fastforward a specific emitter.
  - `uz_vfx_pause(emitterId, paused)` / `uz_vfx_resume`.
  - `uz_vfx_set_time_scale(emitterId, scale)`.
- Emitter runtime: implement deterministic seek by re-warming from t=0 to target time at the configured warmup tick rate.
- CVar registration at startup; respected by `PerFrameTickVFX`.

## Impact

- Affected specs: CVar registry, VFX system tick, public C API, Lua bindings
- Affected code: `cvar_registry.cpp`, `vfx_system.cpp`, `particle_emitter.cpp`, `public_c_api.h`, Lua bindings
- Breaking change: NO (new CVars default to current behavior; new C API is additive)
- User benefit: shippable scalability surface + cinematic timeline integration
