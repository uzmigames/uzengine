## 1. Physics Queries

- [ ] 1.1 Create `runtime/engine/physics/physics_queries.h` — Raycast(origin, direction, max_dist, layer_mask) → RaycastHit { entity, point, normal, distance, hit }. RaycastAll → vector<RaycastHit> sorted by distance. SphereCast, BoxCast, CapsuleCast. OverlapSphere, OverlapBox → vector<Entity> (capped at 256).
- [ ] 1.2 Create `runtime/engine/physics/physics_queries.cpp` — Implementation via JPH::NarrowPhaseQuery::CastRay/CastShape and JPH::BroadPhaseQuery::CollideShape. BodyID→Entity lookup via map in PhysicsSubsystem. Layer mask filtering via ObjectLayerFilter.
- [ ] 1.3 Add ClosestPoint query — ClosestPoint(point, max_dist, layer_mask) → { entity, point, distance }.

## 2. Character Controller

- [ ] 2.1 Create `runtime/engine/physics/components/character_controller.h` — CCharacterController: capsule_radius (0.3), capsule_height (1.8), max_slope_angle (50°), step_height (0.35), move_speed (6.0), jump_speed (8.0), gravity_scale (1.0), ground_layer_mask. Runtime state: is_grounded, ground_normal, velocity.
- [ ] 2.2 Create `runtime/engine/physics/components/character_controller.cpp` — Create(PhysicsWorld&, transform) → JPH::CharacterVirtual with capsule inner shape. Move(Vec3 input, float dt): applies input + gravity, calls ExtendedUpdate(). Ground detection via GetGroundState(). Jump(): set vertical velocity if grounded. GetPosition/SetPosition.
- [ ] 2.3 Create `runtime/engine/physics/character_movement.h/.cpp` — CharacterMovementSystem (TickGroup::Physics): for each CCharacterController, read input velocity, apply gravity (Z × gravity_scale × dt), call ExtendedUpdate(), write position to TransformComponent. Slope sliding for angle > max_slope_angle.
- [ ] 2.4 Add step-up smoothing — interpolate over 2-3 frames for visual quality (no teleport snap).
- [ ] 2.5 Add moving platform support — detect kinematic ground body, inherit platform velocity.

## 3. Physics Debug Draw

- [ ] 3.1 Create `runtime/engine/physics/physics_debug_draw.h/.cpp` — PhysicsDebugDraw (JPH::DebugRenderer): DrawLine, DrawTriangle. Collects into line buffer for existing debug line pass. CVar `physics.debug_draw` (0=off, 1=shapes, 2=+contacts, 3=all).
- [ ] 3.2 Color-coded visualization — green=static, blue=dynamic, yellow=kinematic, red=sleeping, magenta=trigger. Contact points as crosses, raycasts as lines (white=miss, red=hit).
- [ ] 3.3 Wire to `--physics-debug` CLI flag for headless screenshot validation.

## 4. Lua Physics API

- [ ] 4.1 Create `runtime/engine/scripting/api/lua_physics.h/.cpp` — Register `uz.physics` in LuaVM. Raycast: `uz.physics.raycast(origin, dir, max_dist, mask)` → hit table or nil. `uz.physics.raycast_all(...)` → array.
- [ ] 4.2 Add shape cast Lua API — `uz.physics.sphere_cast(...)`, `uz.physics.box_cast(...)`, `uz.physics.capsule_cast(...)`.
- [ ] 4.3 Add overlap Lua API — `uz.physics.overlap_sphere(center, radius, mask)` → entity array. `uz.physics.overlap_box(...)`.
- [ ] 4.4 Add rigid body Lua API — `uz.physics.set_velocity/get_velocity/apply_force/apply_impulse/set_kinematic`.
- [ ] 4.5 Add character controller Lua API — `uz.physics.move_character(entity, dir)`, `uz.physics.jump(entity)`, `uz.physics.is_grounded(entity)`.
- [ ] 4.6 Add collision/trigger callback Lua API — `uz.physics.on_collision(fn)`, `uz.physics.on_trigger(fn)`.

## 5. Testing

- [ ] 5.1 Unit tests for raycasts — cast against box/sphere/capsule, verify hit point/normal/distance.
- [ ] 5.2 Unit tests for shape casts — sphere cast through corridor, verify first hit.
- [ ] 5.3 Unit tests for overlaps — sphere overlap detecting multiple bodies.
- [ ] 5.4 Unit tests for character controller — grounded on flat, slope rejection, step-up.
- [ ] 5.5 Unit tests for Lua physics API — raycast from Lua, verify return table structure.
- [ ] 5.6 Integration test: character walks, steps stairs, slides steep slopes.

## 6. Demo

- [ ] 6.1 Extend `demos/physics_basic/` — character controller capsule, WASD movement, jump (Space), stairs + ramp.
- [ ] 6.2 Lua raycast demo — fire raycast from camera, draw debug line to hit point.

## 7. Documentation

- [ ] 7.1 Update `docs/engine/16-physics/` with queries, character controller, debug draw.
- [ ] 7.2 Add `uz.physics` to `docs/lua-api-reference.md`.
