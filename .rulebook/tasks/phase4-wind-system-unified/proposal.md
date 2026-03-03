# Proposal: phase4-wind-system-unified

## Why
Vegetation shaders, the cloth/soft body physics system, the particle system, and hair simulation all respond to wind independently with hardcoded or duplicated wind vectors. A gust event in a storm cutscene would require updating four separate systems manually. A unified wind system provides one authoritative global wind field sampled by all subsystems through a shared shader constant and C++ API, ensuring consistent behavior and enabling weather scripting from a single Lua call.

## What Changes
- `WindSystem` singleton: constant base vector, Perlin turbulence noise layer, discrete gust events (direction, magnitude, duration, falloff curve)
- `CWindReceiver` ECS component: per-entity wind multiplier and local shelter factor to attenuate global wind (e.g., inside a building)
- Wind vector and turbulence packed into `GlobalParams` cbuffer at `b15` alongside existing camera/light params, available to all shaders without extra binds
- Vegetation shader: reads `GlobalParams.windDir` and `GlobalParams.windStrength` for vertex offset animation
- Particle system integration: wind force applied to CPU-sim and GPU-sim particles via the same global buffer
- Cloth / soft body integration: `WindSystem` force queried each physics tick and applied as per-vertex body force
- Gust event system: `WindSystem::add_gust(dir, speed, radius, duration)` creates a time-limited localized wind zone
- Lua API: `ctx.weather:set_wind(dir_x, dir_z, speed, turbulence)`, `ctx.weather:add_gust(options)`, `ctx.weather:clear_wind()`
- Editor panel: real-time wind direction compass and strength slider in the Scene Settings panel

## Impact
- Affected specs: `docs/specs/particle-system.md`, `docs/specs/shader-library.md`, `docs/specs/renderer.md`, `docs/specs/lua-scripting.md`
- Affected code: `src/scene/wind_system.cpp`, `src/scene/wind_system.h`, `src/renderer/global_params.hlsli`, `src/physics/physics_system.cpp`, `src/particle/particle_system.cpp`
- Breaking change: NO (existing hardcoded wind values in shaders become reads from `GlobalParams`; default value is zero wind)
- User benefit: Scripting a wind change or storm in a cutscene requires one Lua call and all affected subsystems (trees, cloth, particles, hair) respond consistently with no per-system coordination.
