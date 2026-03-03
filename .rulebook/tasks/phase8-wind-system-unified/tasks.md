## 1. WindSystem Singleton

- [ ] 1.1 Create `src/scene/wind_system.h` declaring `WindSystem` singleton with fields: `base_dir` (Vec3, normalized XZ direction), `base_speed` (float), `turbulence_scale` (float), `turbulence_frequency` (float), and `std::vector<GustDescriptor> active_gusts_`
- [ ] 1.2 Create `src/scene/wind_system.cpp` implementing `WindSystem::get()` singleton accessor
- [ ] 1.3 Define `struct GustDescriptor` in `wind_system.h` with fields: `origin` (Vec3), `direction` (Vec3), `speed` (float), `radius` (float), `duration_ms` (uint32_t), `elapsed_ms` (uint32_t), `falloff_curve` (enum: Linear, EaseOut)
- [ ] 1.4 Implement `WindSystem::update(float delta_ms)`: advance `elapsed_ms` on all gusts; remove gusts where `elapsed_ms >= duration_ms`; sample Perlin turbulence noise at current time to modulate `base_speed`
- [ ] 1.5 Implement `WindSystem::get_wind_at(Vec3 world_pos)` returning the effective wind vector (base + turbulence + sum of active gust contributions at that world position)

## 2. CWindReceiver ECS Component

- [ ] 2.1 Add `CWindReceiver` struct to `src/scene/components.h` with fields: `wind_multiplier` (float, default 1.0), `shelter_factor` (float 0.0–1.0, where 1.0 = fully sheltered = no wind)
- [ ] 2.2 Register `CWindReceiver` with the ECS component registry

## 3. GlobalParams cbuffer Integration

- [ ] 3.1 Add `windDir` (float3), `windStrength` (float), and `windTurbulence` (float) to the `GlobalParams` constant buffer struct in `src/renderer/global_params.hlsli` at slot `b15`
- [ ] 3.2 In the renderer's per-frame cbuffer update, write the current `WindSystem::get_wind_at(Vec3{0,0,0})` base wind data into `GlobalParams.windDir` and `GlobalParams.windStrength`
- [ ] 3.3 Default values when `WindSystem` has not been configured: `windDir = {0,0,0}`, `windStrength = 0.0` (zero wind, no behavioral change for existing projects)

## 4. Vegetation Shader Integration

- [ ] 4.1 In the vegetation vertex shader (`shaders/vegetation.hlsl`), replace any hardcoded wind offset with a read from `GlobalParams.windDir * GlobalParams.windStrength` for vertex animation
- [ ] 4.2 Apply Perlin turbulence phase offset per vertex using `GlobalParams.windTurbulence` and the vertex's world-space XZ position as UV

## 5. Particle System Integration

- [ ] 5.1 In `src/particle/particle_system.cpp`, replace any hardcoded wind force with `WindSystem::get_wind_at(particle_world_pos)` applied as an acceleration each tick for CPU-simulated particles
- [ ] 5.2 For GPU-simulated particles, ensure the compute shader reads `GlobalParams.windDir` and `GlobalParams.windStrength` directly

## 6. Cloth and Soft Body Integration

- [ ] 6.1 In `src/physics/physics_system.cpp`, each physics tick query `WindSystem::get_wind_at(body_world_pos)` for each cloth/soft body entity; apply the result as a per-vertex body force
- [ ] 6.2 Scale the applied wind force by the entity's `CWindReceiver::wind_multiplier * (1.0 - shelter_factor)` if a `CWindReceiver` component is present

## 7. Gust Event System

- [ ] 7.1 Implement `WindSystem::add_gust(Vec3 dir, float speed, float radius, uint32_t duration_ms, FalloffCurve curve)` appending a `GustDescriptor` to `active_gusts_`
- [ ] 7.2 In `get_wind_at(world_pos)`, for each active gust, compute distance from `world_pos` to `gust.origin`; if within `radius`, add `gust.direction * gust.speed * falloff(distance) * (1.0 - elapsed/duration)` to the result
- [ ] 7.3 Implement `WindSystem::clear_wind()` removing all active gusts and resetting base speed to 0

## 8. Lua API

- [ ] 8.1 Add `ctx.weather:set_wind(dir_x, dir_z, speed, turbulence)` Lua binding in a new `src/scripting/lua_weather.cpp` calling `WindSystem` setters
- [ ] 8.2 Add `ctx.weather:add_gust({dir_x, dir_z, speed, radius, duration_ms, falloff})` Lua binding calling `WindSystem::add_gust()`
- [ ] 8.3 Add `ctx.weather:clear_wind()` Lua binding

## 9. Editor Panel

- [ ] 9.1 Add a wind direction compass widget and speed slider to the Scene Settings panel in `vscode-uzengine/src/panels/SceneSettingsPanel.ts`
- [ ] 9.2 Poll current wind state via an IPC handler `scene.get_wind` returning `{ "dir_x", "dir_z", "speed", "turbulence" }` at 5 Hz for the panel display

## 10. Testing

- [ ] 10.1 Create `tests/scene/test_wind_system.cpp` testing `WindSystem::get_wind_at()` with `base_speed=5.0` and `base_dir={1,0,0}`: verify the returned vector X component is approximately 5.0
- [ ] 10.2 Test gust contribution: add a gust at origin, query `get_wind_at({0,0,0})`; verify returned speed > `base_speed`
- [ ] 10.3 Test gust falloff: query at distance > gust radius; verify gust contributes zero additional wind
- [ ] 10.4 Test gust expiry: after advancing `WindSystem::update(duration_ms + 1)`, verify the gust is removed from `active_gusts_`
- [ ] 10.5 Test `clear_wind()` removes all gusts and resets `base_speed` to 0
- [ ] 10.6 Test `CWindReceiver` with `shelter_factor=1.0`: applied wind force is 0 regardless of global wind strength
- [ ] 10.7 Test `CWindReceiver` with `wind_multiplier=2.0`: applied wind force is 2x the global wind vector
- [ ] 10.8 Test `GlobalParams.windStrength` is 0 by default when `WindSystem` is not configured (no regression on existing projects)
- [ ] 10.9 Test Lua `ctx.weather:set_wind()` changes `WindSystem` base direction and speed; verify via `get_wind_at()`
- [ ] 10.10 Achieve >= 95% line and branch coverage across `wind_system.cpp`, particle wind force paths, and Lua weather bindings

## 11. Documentation

- [ ] 11.1 Update `docs/specs/particle-system.md` documenting wind force integration via `GlobalParams.windDir`
- [ ] 11.2 Update `docs/specs/shader-library.md` documenting `GlobalParams.windDir`, `windStrength`, `windTurbulence` fields and vegetation shader usage
- [ ] 11.3 Update `docs/specs/renderer.md` documenting `GlobalParams` cbuffer layout at `b15` with new wind fields
- [ ] 11.4 Update `docs/specs/lua-scripting.md` documenting `ctx.weather` API: `set_wind()`, `add_gust()`, `clear_wind()`
