# Phase 3: Physics System (Jolt)

## Why

Real-time physics simulation is essential for gameplay. Currently, UzEngine has no physics system, limiting what kinds of games can be built. Adding Jolt Physics (the same engine used in Horizon Forbidden West, Death Stranding, and others) enables:

- **Rigid body dynamics** — crates fall, objects slide, bodies respond to forces
- **Collision detection** — accurate hit detection for weapons, projectiles, interactions
- **Trigger volumes** — damage zones, pick-up areas, event triggers
- **Character controllers** — player movement with slope handling, jumping, falling
- **Lua physics API** — scripts can raycast, query overlaps, apply forces, receive collision events
- **Debug visualization** — physics wireframes and contact points visible in editor

Jolt Physics is MIT licensed, high-performance, and integrates cleanly via vcpkg. It solves the problem of how to add physics without heavy external dependencies.

**Current blocker**: No physics → no gameplay systems beyond static scenes.

## What Changes

### 1. **Jolt Physics Integration**
   - Add `Jolt` to `vcpkg.json`
   - Create `src/engine/physics/` module with:
     - `PhysicsWorld` singleton (manages Jolt's system)
     - `physics_world.h/cpp` — body/shape creation, fixed-step stepping
     - `physics_query.h/cpp` — raycasts, overlaps, shape casting
     - `physics_events.h/cpp` — collision/trigger event dispatch
     - `jolt_integration.h/cpp` — Jolt setup, custom allocators, configuration

### 2. **ECS Components**
   - `CRigidBody` — mass, restitution, friction, layer, kinematic flag, gravity scale, CCD
   - `CColliderBox`, `CColliderSphere`, `CColliderCapsule` — shape geometry + local offset
   - `CColliderMesh` — convex hull from mesh asset
   - `CColliderTerrain` — height-field collision from terrain asset
   - `CTrigger` — trigger volume (no collision response, only events)
   - `CCharacterController` — character control with slope handling, ground detection

### 3. **Scene Integration**
   - `.uzscene` syntax for colliders, rigid bodies, triggers, character controllers
   - Existing node transforms automatically sync with physics bodies
   - Properties: `collider`, `rigid_body_*`, `trigger`, `character_controller`
   - Example:
     ```ini
     [node id=5 type=Node3D name="Player" parent=1]
     collider = "capsule"
     collider_radius = 0.4
     collider_half_height = 0.9
     character_controller = true
     ```

### 4. **Collision Events**
   - `EventBus<CollisionEvent>` — dispatched every frame for active collisions
   - `EventBus<TriggerEvent>` — dispatched for trigger enter/exit
   - Events accessible from Lua via script callbacks: `on_collision_enter`, `on_collision_exit`, `on_trigger_enter`, `on_trigger_exit`

### 5. **Lua Physics API**
   - Query functions via `ctx.world`:
     - `ctx.world:raycast(origin, dir, max_dist, layer_mask)` → hit info
     - `ctx.world:linecast(from, to, layer_mask)` → hit info
     - `ctx.world:overlap_sphere(center, radius, layer_mask)` → `NodeRef[]`
     - `ctx.world:overlap_box(center, half_extents, layer_mask)` → `NodeRef[]`
   - Rigid body control:
     - `self:apply_force(Vec3)`, `self:apply_impulse(Vec3)`, `self:set_velocity(Vec3)`, `self:get_velocity()` → `Vec3`
   - Character controller:
     - `self:move(velocity, dt)`, `self:jump(impulse)`, `self:is_grounded()` → `bool`, `self:get_ground_normal()` → `Vec3`

### 6. **Collision Layers & Matrix**
   - 16-layer system (configurable in `.uzproj`)
   - Default layers: Default, Player, Enemy, Projectile, Trigger, Terrain, UI, PowerUp, etc.
   - Collision matrix in `.uzproj` defines which layers collide:
     ```toml
     [physics.layers]
     Player = 1
     Enemy = 2

     [physics.collision_matrix]
     Player = ["Default", "Enemy", "Terrain"]
     Enemy = ["Default", "Player", "Terrain"]
     ```

### 7. **Physics Debug Draw**
   - Jolt debug renderer output sent via IPC to VSCode
   - Shows:
     - Wireframe outlines of all bodies (white = dynamic, cyan = kinematic, gray = static)
     - Velocity vectors (red arrows)
     - Contact points (yellow dots)
     - Surface normals (blue arrows)
   - Editor-only; toggled via VSCode command or IPC

### 8. **Implementation Files**
   - New `src/engine/physics/` module
   - Updated `src/engine/ecs/components.h` with 6 new components
   - Updated `game.dll` with `PhysicsWorld` singleton and fixed-step loop
   - Updated Lua bindings in `src/engine/scripting/`
   - Updated SceneManager to create physics bodies from `.uzscene` syntax

## Impact

**Breaking changes**: None. Physics is opt-in per node.

**API additions**:
- 6 new ECS components
- `PhysicsWorld` in `game.dll`
- Lua `ctx.world` API and script callbacks
- `.uzproj` physics configuration section
- `.uzscene` collider/rigid-body/trigger syntax

**Dependencies**:
- Jolt Physics (MIT, vcpkg)
- No new external dependencies

**Performance**:
- Fixed 60 Hz physics step (independent of frame rate)
- Up to 4 sub-steps for stability
- Sleeping bodies automatically culled
- Supports thousands of bodies

**Documentation**:
- Complete physics spec in `docs/specs/physics-system.md`
- Lua API examples in spec
- `.uzscene` syntax documented
- Integration guide in spec

## Risk Mitigation

**Risk**: Jolt is large/complex library.
**Mitigation**: Start with basic rigid bodies + colliders; skip advanced features (ragdoll, soft bodies, constraints) for Phase 3+.

**Risk**: Physics debug draw impacts editor performance.
**Mitigation**: Debug draw is toggled off by default; only enabled when explicitly requested.

**Risk**: Collision matrix misconfiguration causes unexpected behavior.
**Mitigation**: Clear documentation, sensible defaults, validation warnings in editor.

## Success Criteria

1. ✅ Rigid bodies and colliders work in-game
2. ✅ Raycasts/overlaps return correct results
3. ✅ Collision events fire and are accessible from Lua
4. ✅ Character controller enables player movement with jumping
5. ✅ Physics debug draw renders correctly in VSCode
6. ✅ Comprehensive documentation and examples
7. ✅ 95%+ test coverage on core physics code
