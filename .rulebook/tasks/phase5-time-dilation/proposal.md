# Proposal: phase3-time-dilation

## Why
Bullet time, slow-motion abilities, and time-freeze powers are common game mechanics that require dilating global simulation speed independently of real wall-clock time. No time dilation system exists in UzEngine — games must manually scale every delta_time consumer, which is error-prone and does not integrate with the physics, animation, or particle subsystems. Audio pitch scaling during slow-motion also requires a configurable coupling factor so that slowed audio lowers pitch slightly rather than fully following the dilation ratio (full pitch-drop sounds unnatural).

## What Changes
- Add `TimeSystem::set_dilation(float factor)` where 0.0 = frozen, 1.0 = normal, and values above 1.0 produce fast-forward
- `TimeSystem::delta_time()` returns `raw_delta * dilation_factor`; all engine subsystems (physics step, animation update, particle simulation, timer system) consume the dilated delta
- Audio pitch scaling: `pitch = 1.0 + (dilation - 1.0) * coupling_factor`; `coupling_factor` defaults to 0.3 and is configurable in `.uzproj` `[time]` section
- ECS `CTimeDilationOverride` component allows individual entities to opt out of global dilation (e.g., UI elements, HUD animations, player input)
- `TimeSystem` exposes `raw_delta_time()` for systems that must run at wall-clock speed regardless of dilation (networking, input polling)
- Lua API: `ctx.time:set_dilation(factor)`, `ctx.time:get_dilation()`, `ctx.time:get_delta()`, `ctx.time:get_raw_delta()`
- Integration with `timer-system` — timers respect dilation by default; `schedule_after_realtime()` variant ignores dilation

## Impact
- Affected specs: `docs/specs/timer-system.md`, `docs/specs/animation-system.md`, `docs/specs/particle-system.md`, `docs/specs/physics-system.md`, `docs/specs/audio-system.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/core/time_system.cpp`, `src/core/time_system.h`, `src/physics/physics_world.cpp`, `src/animation/animation_system.cpp`, `src/particle/particle_system.cpp`, `src/audio/audio_system.cpp`, `src/lua/lua_time_api.cpp`
- Breaking change: NO
- User benefit: Slow-motion and time-freeze abilities can be implemented with a single Lua call; all engine subsystems automatically respect the dilation without any per-system code changes
