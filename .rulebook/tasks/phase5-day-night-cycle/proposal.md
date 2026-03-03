# Proposal: phase3-day-night-cycle

## Why
No time-of-day system exists in UzEngine. Sun direction, sky color, ambient light, and fog all require manual per-frame setup and cannot animate automatically. Games with outdoor environments need a proper day/night cycle to create atmosphere and drive gameplay mechanics such as enemy spawns or weather transitions. Without this system every outdoor game reimplements fragile ad-hoc time logic inside Lua scripts.

## What Changes
- Add `TimeOfDay` ECS singleton component: 24-hour normalized clock (`float hour` 0–24), `float speed_multiplier` (1.0 = real-time, 0 = paused)
- Directional light direction and color driven by configurable keyframe curves (`CurveFloat3` keyed by hour) stored in `.uzscene`
- Sky atmosphere parameter curves: horizon color, zenith color, sun disk size/intensity — all curve-driven
- Ambient light curves: sky light SH coefficients and ground bounce color keyed by hour
- Moon phase rendering: secondary directional light active from hour 18–6 with configurable albedo and phase angle
- Fog density and fog color curves integrated with the sky atmosphere pass
- Lua API: `ctx.world:set_time(hour)`, `ctx.world:get_time()`, `ctx.world:set_time_speed(multiplier)`
- Editor Inspector panel shows time scrubber with preview rendering
- `.uzscene` `[time_of_day]` section for all curve data (INI-compatible keyframe format)

## Impact
- Affected specs: `docs/specs/renderer.md`, `docs/specs/scene-system.md`, `docs/specs/lua-scripting.md`, `docs/specs/shader-library.md`
- Affected code: `src/renderer/sky_pass.cpp`, `src/scene/time_of_day.cpp`, `src/scene/time_of_day.h`, `src/lua/lua_world_api.cpp`, `src/editor/inspector_time_of_day.cpp`
- Breaking change: NO
- User benefit: Outdoor game environments gain automatic atmospheric animation (sunrise, sunset, night sky) with zero per-frame Lua code; designers control the look via curve editors in the Inspector
