## 1. CWaterVolume ECS Component

- [ ] 1.1 Add `CWaterVolume` struct to `src/scene/components.h` with fields: `aabb_min` (Vec3), `aabb_max` (Vec3), `surface_y` (float), `fluid_density` (float, kg/m3, water default 1000), `wave_amplitude` (float), `wave_frequency` (float), `linear_drag` (float), `angular_drag` (float)
- [ ] 1.2 Register `CWaterVolume` with the ECS component registry for scene serialization
- [ ] 1.3 Add `on_water_enter` and `on_water_exit` Lua callback fields to `CWaterVolume` settable via scene YAML and Lua

## 2. Buoyancy Force Computation

- [ ] 2.1 In `src/physics/physics_system.cpp`, in `pre_step()`, iterate all `CWaterVolume` components and all dynamic rigid bodies; for each body whose AABB intersects a water volume, compute the submerged volume fraction
- [ ] 2.2 Implement submerged volume fraction as `clamp((surface_y - body_min_y) / body_height_y, 0.0, 1.0)` where `surface_y` is the animated wave surface Y at the body's XZ center
- [ ] 2.3 Compute buoyancy upward force: `F_buoyancy = fluid_density * 9.81 * V_submerged` where `V_submerged = body_volume * submerged_fraction`; estimate `body_volume` from the body's AABB
- [ ] 2.4 Apply `F_buoyancy` as an upward `AddForce({0, F_buoyancy, 0})` on the Jolt body each physics step

## 3. Wave Surface Animation

- [ ] 3.1 Implement `CWaterVolume::sample_surface_y(float world_x, float world_z, float time)` using a sine function: `surface_y + sin(world_x * wave_frequency + time) * wave_amplitude`
- [ ] 3.2 Use the per-step physics time (accumulated from `PhysicsSystem::step()` calls) as the `time` input for deterministic wave sampling

## 4. Fluid Drag

- [ ] 4.1 For each body with non-zero `submerged_fraction`, apply linear drag: `drag_force = -velocity * linear_drag * submerged_fraction`; call `body.AddForce(drag_force)`
- [ ] 4.2 Apply angular drag: `angular_drag_torque = -angular_velocity * angular_drag * submerged_fraction`; call `body.AddTorque(angular_drag_torque)`

## 5. Splash Events

- [ ] 5.1 Track the previous frame's `submerged_fraction` for each body-volume pair; emit `EventBus` event `"water_enter"` when fraction transitions from 0 to > 0
- [ ] 5.2 Emit `EventBus` event `"water_exit"` when fraction transitions from > 0 to 0
- [ ] 5.3 Fire the `on_water_enter` and `on_water_exit` Lua callbacks on the `CWaterVolume` entity when the corresponding event fires; pass the entering/exiting entity ID as the callback argument

## 6. Lua API

- [ ] 6.1 Add `ctx.physics:is_submerged(entity)` Lua binding in `src/scripting/lua_physics_bindings.cpp` returning the submerged fraction [0.0, 1.0] of the entity across all overlapping water volumes
- [ ] 6.2 Add Lua callback registration: `ctx.physics:on_water_enter(volume_entity, fn)` and `ctx.physics:on_water_exit(volume_entity, fn)`

## 7. Multiple Volume Support

- [ ] 7.1 Support multiple overlapping `CWaterVolume` entities (e.g., a pool inside a flooded room): sum buoyancy forces from all volumes the body overlaps; use the highest-priority volume's drag coefficients (or average if equal priority)

## 8. Debug Draw

- [ ] 8.1 When `r_debug_physics` cvar is enabled, draw the water surface plane as a transparent blue quad at `surface_y` spanning the `aabb_min`/`aabb_max` XZ extents
- [ ] 8.2 Draw a green upward arrow at each body's world center scaled by the buoyancy force magnitude, when `r_debug_physics_bodies` cvar is enabled

## 9. Testing

- [ ] 9.1 Create `tests/physics/test_buoyancy.cpp` testing that a body fully submerged (AABB entirely below `surface_y`) has `submerged_fraction == 1.0`
- [ ] 9.2 Test a body with its center at `surface_y` has `submerged_fraction` approximately 0.5
- [ ] 9.3 Test buoyancy force equals `fluid_density * 9.81 * body_volume * submerged_fraction` for a known body volume
- [ ] 9.4 Test `is_submerged()` returns 0.0 for a body entirely above `surface_y`
- [ ] 9.5 Test wave surface: `sample_surface_y(0, 0, 0)` equals `base_surface_y`; `sample_surface_y(0, 0, pi/(2*frequency))` equals `base_surface_y + amplitude`
- [ ] 9.6 Test `water_enter` event fires exactly once when a body crosses from above to below `surface_y`
- [ ] 9.7 Test `water_exit` event fires exactly once when a body crosses from below to above `surface_y`
- [ ] 9.8 Test multiple overlapping `CWaterVolume` components: buoyancy forces from both volumes are summed
- [ ] 9.9 Test linear drag is applied in the direction opposing velocity, scaled by `submerged_fraction`
- [ ] 9.10 Achieve >= 95% line and branch coverage across buoyancy force computation, wave sampling, drag application, and splash event paths

## 10. Documentation

- [ ] 10.1 Update `docs/specs/physics-system.md` documenting `CWaterVolume` fields, buoyancy computation, drag coefficients, Lua API, and splash events
- [ ] 10.2 Update `docs/specs/lua-scripting.md` documenting `ctx.physics:is_submerged()` and water callback registration
- [ ] 10.3 Update `docs/specs/event-bus.md` documenting `water_enter` and `water_exit` event types and payload schema
