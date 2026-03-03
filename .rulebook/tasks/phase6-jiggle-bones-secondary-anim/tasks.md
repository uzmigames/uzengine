## 1. CJiggleBone ECS Component
- [ ] 1.1 Add `CJiggleBone` struct to `src/scene/components.h` with fields: `bone_name` (string, 64-char max), `stiffness` (float, spring constant, default 80.0f), `damping` (float, 0-1, default 0.4f), `follow_weight` (float, 0-1, how strongly bone tracks animated position, default 0.5f), `max_angle_degrees` (float, cone constraint, default 45.0f), `collision_sphere_radius` (float, 0 = disabled, default 0.0f)
- [ ] 1.2 Support multiple `CJiggleBone` entries per entity by storing them as a `std::vector<CJiggleBoneConfig>` inside a single `CJiggleBoneSet` component, where `CJiggleBoneConfig` holds all the fields from 1.1 plus `bone_index` (int, resolved at registration time)
- [ ] 1.3 Register `CJiggleBoneSet` in the ECS component registry (`src/scene/ComponentRegistry.cpp`) with reflection metadata
- [ ] 1.4 Serialize `CJiggleBoneSet` in `src/scene/SceneSerializer.cpp` as `[[jiggle_bone]]` INI array blocks, one block per configured bone

## 2. JiggleBoneSystem
- [ ] 2.1 Create `src/animation/JiggleBoneSystem.h` declaring `JiggleBoneSystem` with `update(float delta_time, EntityWorld& world)` and per-bone simulation state stored as `JiggleBoneState` (position: float3, velocity: float3) in a parallel array indexed by entity and bone config index
- [ ] 2.2 Create `src/animation/JiggleBoneSystem.cpp` implementing the spring-damper integration per bone: compute driving force = `parent_world_delta * stiffness`, apply damping = `velocity * damping`, integrate `velocity += (force - damping_force) * delta_time`, integrate `position += velocity * delta_time`
- [ ] 2.3 Apply `follow_weight`: blend between the simulated position and the animated bone position (`lerp(simulated_pos, animated_pos, follow_weight)`) to keep the bone from drifting too far from its authored pose
- [ ] 2.4 Apply the cone constraint: clamp the simulated bone position so that the angle between the bone vector and its parent bone vector does not exceed `max_angle_degrees`; use axis-angle clamping, not Euler decomposition
- [ ] 2.5 Apply the optional collision sphere: if `collision_sphere_radius > 0`, clamp the simulated bone world position to the outside of a sphere of that radius centered at the bone's attachment point (the parent bone's world tip)
- [ ] 2.6 Write the final simulated position back to the bone's local transform relative to its parent before the bone palette is finalized; convert world position back to local space using the parent's inverse world matrix

## 3. AnimatorSystem Integration
- [ ] 3.1 In `AnimatorSystem::update()` (`src/animation/AnimatorSystem.cpp`), call `JiggleBoneSystem::update()` as a post-process pass after all other animation evaluation but before bone palette upload
- [ ] 3.2 Skip `JiggleBoneSystem` when the entity's animation LOD level is LOD2 or LOD3 (consistent with phase6-animation-lod specification)
- [ ] 3.3 Add `PROFILE_SCOPE("JiggleBoneSystem")` profiling marker around the jiggle bone update call

## 4. Lua Scripting API
- [ ] 4.1 Expose `ctx.anim:set_jiggle_config(bone_name, params)` in `src/scripting/LuaAnimBindings.cpp` where `params` is a Lua table with keys: `stiffness`, `damping`, `follow_weight`, `max_angle_degrees`, `collision_sphere_radius`; all fields optional with existing values used for unspecified keys
- [ ] 4.2 Validate numeric range on each param: stiffness >= 0, damping in [0,1], follow_weight in [0,1], max_angle_degrees in [0,180], collision_sphere_radius >= 0; log an error and skip the assignment for any out-of-range value
- [ ] 4.3 Update `docs/specs/lua-scripting.md` with `ctx.anim:set_jiggle_config()` documentation, parameter table keys, types, ranges, and an example for a pouch bone

## 5. Testing
- [ ] 5.1 Create `tests/animation/test_jiggle_bone_system.cpp` with Google Test cases: spring-damper integration with zero velocity produces displacement proportional to stiffness and delta_time, damping reduces velocity each step, follow_weight = 1.0 returns exactly the animated position, follow_weight = 0.0 returns full simulation position
- [ ] 5.2 Add cone constraint tests: bone within max_angle is unchanged, bone beyond max_angle is clamped to cone boundary, max_angle = 0 forces bone to parent direction
- [ ] 5.3 Add collision sphere tests: bone outside sphere is unchanged, bone inside sphere is pushed to sphere surface, `collision_sphere_radius = 0` disables constraint
- [ ] 5.4 Add integration tests: `JiggleBoneSystem` is skipped at LOD2 (mock call count = 0), called at LOD1 and LOD0
- [ ] 5.5 Add serialization round-trip tests for `CJiggleBoneSet` with two bones: verify field values survive save/load
- [ ] 5.6 Add Lua API tests: valid `set_jiggle_config()` updates the component fields, out-of-range `damping = 2.0` logs error and does not update the field
- [ ] 5.7 Verify line coverage >= 95% and branch coverage >= 95% across `JiggleBoneSystem.cpp` and `LuaAnimBindings.cpp` jiggle paths

## 6. Documentation
- [ ] 6.1 Update `docs/specs/animation-system.md` with `CJiggleBoneSet`/`CJiggleBoneConfig` field reference, spring-damper integration algorithm description, post-process pass order, and LOD skip behavior
- [ ] 6.2 Update `docs/specs/skeletal-mesh.md` with a note that jiggle bones require a valid `bone_name` that exists in the skeleton; invalid bone names are ignored with a warning at registration time
- [ ] 6.3 Update `docs/specs/lua-scripting.md` with `ctx.anim:set_jiggle_config()` full parameter reference and example
