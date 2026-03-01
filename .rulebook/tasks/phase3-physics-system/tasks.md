# Phase 3: Physics System — Implementation Checklist

## 1. Jolt Physics Integration

- [ ] 1.1 Add `Jolt` to `vcpkg.json` and vcpkg manifest
- [ ] 1.2 Create `src/engine/physics/jolt_integration.h/cpp` (Jolt init, custom allocators)
- [ ] 1.3 Create `src/engine/physics/physics_world.h/cpp` (PhysicsWorld singleton, body management)
- [ ] 1.4 Add CMake target `uz_physics` linking to Jolt
- [ ] 1.5 Verify Jolt compiles and links in all build configurations

## 2. ECS Components

- [ ] 2.1 Add `CRigidBody` to `src/engine/ecs/components.h` with all fields
- [ ] 2.2 Add `CColliderBox`, `CColliderSphere`, `CColliderCapsule` components
- [ ] 2.3 Add `CColliderMesh` component (convex hull from mesh)
- [ ] 2.4 Add `CColliderTerrain` component (height-field from terrain)
- [ ] 2.5 Add `CTrigger` component (trigger volumes)
- [ ] 2.6 Add `CCharacterController` component (player controller wrapper)
- [ ] 2.7 Register all components with ECS registry

## 3. Physics World Lifecycle

- [ ] 3.1 Implement `PhysicsWorld::create()` (init Jolt system, read `.uzproj` config)
- [ ] 3.2 Implement `PhysicsWorld::on_component_added()` (convert ECS components to Jolt bodies)
- [ ] 3.3 Implement `PhysicsWorld::on_component_removed()` (remove bodies from Jolt)
- [ ] 3.4 Implement fixed-step loop: `physics_step(dt)` with accumulator pattern
- [ ] 3.5 Integrate physics step into `game.dll` main loop (before script updates)

## 4. Physics Queries

- [ ] 4.1 Implement `PhysicsQuery::raycast()` (return hit info table)
- [ ] 4.2 Implement `PhysicsQuery::linecast()` (wrapper around raycast)
- [ ] 4.3 Implement `PhysicsQuery::overlap_sphere()` (return NodeRef array)
- [ ] 4.4 Implement `PhysicsQuery::overlap_box()` (return NodeRef array)
- [ ] 4.5 Add layer-mask filtering to all query functions

## 5. Collision Events

- [ ] 5.1 Create `CollisionEvent` struct in ECS headers
- [ ] 5.2 Create `TriggerEvent` struct in ECS headers
- [ ] 5.3 Implement event dispatch from Jolt contact manifold to `EventBus<CollisionEvent>`
- [ ] 5.4 Implement event dispatch for trigger overlaps to `EventBus<TriggerEvent>`
- [ ] 5.5 Wire EventBus dispatch into physics step

## 6. Lua Bindings

- [ ] 6.1 Create `src/engine/scripting/lua_physics_binding.h/cpp`
- [ ] 6.2 Implement `ctx.world` global with raycast/linecast/overlap methods
- [ ] 6.3 Implement rigid body methods: `self:apply_force()`, `self:apply_impulse()`, `self:set_velocity()`, `self:get_velocity()`
- [ ] 6.4 Implement character controller methods: `self:move()`, `self:jump()`, `self:is_grounded()`, `self:get_ground_normal()`
- [ ] 6.5 Register collision callback dispatchers (on_collision_enter, on_collision_exit, on_trigger_enter, on_trigger_exit)
- [ ] 6.6 Implement layer-mask argument conversion (Lua integer to bitmask)

## 7. Scene System Integration

- [ ] 7.1 Update `SceneManager` to parse `.uzscene` collider syntax
- [ ] 7.2 Parse `collider` property (box/sphere/capsule/mesh/terrain)
- [ ] 7.3 Parse `collider_*` properties (radius, half_extents, offset, etc.)
- [ ] 7.4 Parse `rigid_body_*` properties (mass, friction, restitution, layer, kinematic, gravity_scale, continuous_ccd)
- [ ] 7.5 Parse `trigger` property (boolean)
- [ ] 7.6 Parse `character_controller` property (boolean)
- [ ] 7.7 Parse `character_max_slope` and `character_ground_margin`
- [ ] 7.8 Create physics components when node is instantiated

## 8. Collision Layers & Matrix

- [ ] 8.1 Read `[physics.layers]` section from `.uzproj`
- [ ] 8.2 Read `[physics.collision_matrix]` section from `.uzproj`
- [ ] 8.3 Build Jolt `GroupFilter` from collision matrix
- [ ] 8.4 Apply filter to PhysicsWorld on initialization
- [ ] 8.5 Validate layer references (warn on undefined layers)
- [ ] 8.6 Provide sensible defaults if layers not specified

## 9. Physics Debug Draw

- [ ] 9.1 Create `src/engine/physics/debug_draw.h/cpp` wrapping Jolt's debug renderer
- [ ] 9.2 Implement conversion of Jolt primitives (lines, wireframes, contact points) to JSON
- [ ] 9.3 Implement IPC handler `physics.debug_draw` to toggle debug drawing
- [ ] 9.4 Send debug draw data each frame via IPC (editor mode only)
- [ ] 9.5 Create VSCode command to toggle physics debug visualization
- [ ] 9.6 Render debug geometry in VSCode WebView overlay

## 10. Testing

- [ ] 10.1 Write unit tests for raycast/linecast queries (doctest)
- [ ] 10.2 Write unit tests for overlap queries (doctest)
- [ ] 10.3 Write unit tests for rigid body creation and destruction
- [ ] 10.4 Write unit tests for collision event dispatch
- [ ] 10.5 Write unit tests for character controller movement
- [ ] 10.6 Write integration tests: spawn bodies, simulate, verify transforms updated
- [ ] 10.7 Write integration tests: collision events fire correctly
- [ ] 10.8 Achieve 95%+ code coverage on physics module

## 11. Documentation

- [ ] 11.1 Complete `docs/specs/physics-system.md` (already done, verify completeness)
- [ ] 11.2 Add physics-system to `docs/README.md` specs table (already done, verify)
- [ ] 11.3 Add physics-system to Rulebook tasks table in `docs/README.md` (already done, verify)
- [ ] 11.4 Add example Lua scripts to `docs/examples/` (player controller, projectile, trigger)
- [ ] 11.5 Create architecture decision record: why Jolt over Bullet/PhysX

## 12. Code Quality

- [ ] 12.1 Run clang-format on all new files
- [ ] 12.2 Run clang-tidy; fix all warnings
- [ ] 12.3 Run ASAN on test suite
- [ ] 12.4 Verify no memory leaks (ASAN)
- [ ] 12.5 Type-check (no compiler warnings)
- [ ] 12.6 Documentation: all public APIs documented with comments

## 13. Integration

- [ ] 13.1 Test physics in a small demo scene (sphere falling onto plane)
- [ ] 13.2 Test Lua raycast from a script
- [ ] 13.3 Test Lua collision callback firing on impact
- [ ] 13.4 Test character controller movement with input
- [ ] 13.5 Test physics debug draw in VSCode editor
- [ ] 13.6 Test layer collisions with custom matrix
- [ ] 13.7 Test trigger enter/exit events

<!-- Test Coverage: 95%+ on physics module -->
