## 1. CPhysicsRegion ECS Component

- [ ] 1.1 Add `CPhysicsRegion` struct to `src/scene/components.h` with fields: `gravity` (Vec3), `linear_damping` (float), `angular_damping` (float), `priority` (int), and `trigger_shape` (tagged union: Box{half_extents}, Sphere{radius}, Capsule{radius, half_height})
- [ ] 1.2 Register `CPhysicsRegion` with the ECS component registry in `src/scene/scene_system.cpp` so it serializes/deserializes via the scene save system

## 2. Jolt Sensor Body Creation

- [ ] 2.1 In `PhysicsRegionSystem::on_component_added(entity, CPhysicsRegion&)`, create a Jolt sensor body (non-colliding trigger) using `JPH::BodyCreationSettings` with `IsSensor = true` and shape matching `trigger_shape`
- [ ] 2.2 In `PhysicsRegionSystem::on_component_removed(entity)`, remove and destroy the sensor body from the Jolt world
- [ ] 2.3 Register a `JPH::ContactListener` subclass `RegionContactListener` that fires `OnEnter(body_id)` and `OnExit(body_id)` callbacks when a dynamic body contacts or leaves a sensor body

## 3. PhysicsRegionSystem

- [ ] 3.1 Create `src/physics/physics_region_system.h` declaring `PhysicsRegionSystem` with methods: `on_component_added()`, `on_component_removed()`, `update(entity_id body_id, bool entered, RegionHandle region)`
- [ ] 3.2 Create `src/physics/physics_region_system.cpp` implementing a per-body `std::vector<RegionHandle>` sorted descending by `priority`; update on each `OnEnter`/`OnExit` contact event
- [ ] 3.3 After updating the active region list for a body, call `rebuild_effective_params(body_id)` to recompute the active gravity and damping
- [ ] 3.4 For equal-priority regions, blend gravity and damping linearly by the body's distance to each region center (approximated normalized blend)

## 4. Per-Body Override Application

- [ ] 4.1 In `PhysicsSystem::pre_step()`, for each body with active region overrides, call `JPH::Body::SetGravityFactor()` to scale global gravity by the effective override factor
- [ ] 4.2 Call `JPH::MotionProperties::SetLinearDamping()` and `SetAngularDamping()` with the effective per-body damping values
- [ ] 4.3 Restore global defaults for bodies that have left all regions (empty active region list)

## 5. Lua API

- [ ] 5.1 Add `ctx.physics:set_region_gravity(entity, {x, y, z})` Lua binding in `src/scripting/lua_physics.cpp` that updates `CPhysicsRegion::gravity` on the entity and triggers a Jolt sensor body shape refresh if the region size changed
- [ ] 5.2 Add `ctx.physics:get_region_gravity(entity)` Lua binding returning the current gravity vector of a `CPhysicsRegion` entity as a table `{x, y, z}`

## 6. Editor Visualization

- [ ] 6.1 In the DX12 editor pass, render selected entities with `CPhysicsRegion` as a transparent colored box, sphere, or capsule wireframe matching the `trigger_shape`
- [ ] 6.2 Render the gravity direction as an arrow gizmo from the region center using the editor gizmo system in `src/editor/gizmos.cpp`

## 7. Testing

- [ ] 7.1 Create `tests/physics/test_physics_regions.cpp` testing that a body entering a `CPhysicsRegion` with `gravity = {0, 9.81, 0}` has its gravity inverted (upward) while inside the region
- [ ] 7.2 Test body exiting the region: verify global gravity (downward) is restored after the body leaves
- [ ] 7.3 Test priority: body overlapping two regions with different priorities uses the gravity from the higher-priority region
- [ ] 7.4 Test equal-priority blend: body overlapping two equal-priority regions uses a gravity value between the two region gravities
- [ ] 7.5 Test linear damping override: body inside a region with `linear_damping = 5.0` decelerates faster than outside the region
- [ ] 7.6 Test sensor body creation: adding `CPhysicsRegion` to an entity creates a Jolt sensor body; removing the component destroys it
- [ ] 7.7 Test box, sphere, and capsule `trigger_shape` variants each create the correct Jolt sensor shape
- [ ] 7.8 Test `ctx.physics:set_region_gravity()` updates the region's gravity vector and takes effect on the next physics step
- [ ] 7.9 Test nested regions with priorities 0 and 1: confirm the highest-priority region's gravity wins; confirm priority-0 region's gravity applies when the body is only in that region
- [ ] 7.10 Achieve >= 95% line and branch coverage across `physics_region_system.cpp` and the `pre_step()` override application code

## 8. Documentation

- [ ] 8.1 Update `docs/specs/physics-system.md` documenting `CPhysicsRegion` fields, `PhysicsRegionSystem` behavior, priority rules, and Lua API
- [ ] 8.2 Update `docs/specs/scene-system.md` documenting `CPhysicsRegion` as a serializable ECS component with example scene YAML
