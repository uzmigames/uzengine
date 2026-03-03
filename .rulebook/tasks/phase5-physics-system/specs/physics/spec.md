# Physics System Technical Specification

## ADDED Requirements

### Requirement 1: Jolt Physics Integration

The physics system SHALL use Jolt Physics (MIT license) as the underlying simulation engine.

#### Scenario 1.1: Engine Initialization
**Given** the engine is starting up
**When** the game.dll physics subsystem initializes
**Then** the system MUST:
- Create a single `PhysicsWorld` instance
- Load gravity vector from `.uzproj` [physics] section (default: [0, -9.81, 0])
- Load max body count (default: 10,000)
- Load max sub-steps (default: 4)
- Initialize Jolt's `PhysicsSystem` with those parameters
- Load collision layer configuration from `.uzproj`

#### Scenario 1.2: Engine Shutdown
**Given** the physics subsystem is initialized
**When** the engine shuts down
**Then** the system MUST:
- Call `PhysicsWorld::destroy()`
- Release all Jolt bodies and shapes
- Free all physics memory

### Requirement 2: ECS Components

The physics system SHALL provide six ECS components:

#### 2.1 CRigidBody
```
CRigidBody SHALL contain:
  - jolt_body_id: JPH::BodyID (opaque handle)
  - mass: float (kg; 0 = static)
  - restitution: float (0.0–1.0)
  - friction: float (0.0–1.0)
  - layer: uint8_t (0–15)
  - is_kinematic: bool
  - gravity_scale: float (default: 1.0)
  - continuous_ccd: bool (default: false)
```

#### 2.2 CColliderBox
```
CColliderBox SHALL contain:
  - half_extents: Vec3 (local-space dimensions)
  - offset: Vec3 (local offset from node origin)
```

#### 2.3 CColliderSphere
```
CColliderSphere SHALL contain:
  - radius: float
  - offset: Vec3
```

#### 2.4 CColliderCapsule
```
CColliderCapsule SHALL contain:
  - radius: float
  - half_height: float
  - offset: Vec3
```

#### 2.5 CColliderMesh
```
CColliderMesh SHALL contain:
  - mesh_id: StringId (reference to loaded mesh asset)
The system MUST auto-compute convex hull from mesh geometry.
```

#### 2.6 CColliderTerrain
```
CColliderTerrain SHALL contain:
  - terrain_id: StringId (reference to .uzterrain asset)
The system MUST use height-field collision from terrain data.
```

#### 2.7 CTrigger
```
CTrigger SHALL contain:
  - jolt_body_id: JPH::BodyID
Trigger bodies SHALL NOT respond to collision forces.
```

#### 2.8 CCharacterController
```
CCharacterController SHALL contain:
  - character: JPH::CharacterVirtual* (opaque pointer)
  - max_slope_angle: float (degrees; default: 45)
  - ground_margin: float (distance to ground; default: 0.05m)
  - is_grounded: bool (read-only)
```

### Requirement 3: Fixed-Step Physics Loop

The physics system SHALL step at a fixed 60 Hz rate.

#### Scenario 3.1: Frame Timing
**Given** the game is running at variable frame rate (e.g., 30 FPS)
**When** the game logic updates
**Then** the system MUST:
- Accumulate delta time into `PhysicsWorld.time_accumulator`
- While accumulator >= (1/60) seconds:
  - Step physics for (1/60) seconds
  - Subtract (1/60) from accumulator
- Repeat until accumulator < (1/60)

#### Scenario 3.2: Physics Before Scripts
**Given** a frame is executing
**When** physics step and script update both need to run
**Then** the system MUST:
- Step physics FIRST
- Update scripts AFTER
- This ensures scripts see the latest physics state

#### Scenario 3.3: Sub-Stepping
**Given** high-speed collisions (e.g., bullets, explosions)
**When** physics is configured with `max_substeps > 1`
**Then** Jolt MUST perform up to N sub-steps per frame for stability

### Requirement 4: Body Creation & Destruction

The physics system SHALL automatically create and destroy Jolt bodies when ECS components change.

#### Scenario 4.1: Body Creation
**Given** a node has `CRigidBody` and a collider component (CColliderBox, etc.)
**When** the components are added to the ECS
**Then** the system MUST:
- Determine the collider shape (box, sphere, capsule, mesh)
- Create a Jolt shape from that geometry
- Create a Jolt body with the shape and CRigidBody properties
- Store the body ID in CRigidBody.jolt_body_id

#### Scenario 4.2: Body Destruction
**Given** a node with an active physics body
**When** the CRigidBody component is removed
**Then** the system MUST:
- Retrieve the Jolt body ID
- Remove the body from the Jolt world
- Invalidate CRigidBody.jolt_body_id

#### Scenario 4.3: Static Bodies
**Given** CRigidBody.mass == 0
**When** the body is created
**Then** the system MUST create a static (immobile) body in Jolt

#### Scenario 4.4: Dynamic Bodies
**Given** CRigidBody.mass > 0
**When** the body is created
**Then** the system MUST:
- Create a dynamic body in Jolt
- Apply gravity and forces to the body
- Allow the body to be moved by physics

#### Scenario 4.5: Kinematic Bodies
**Given** CRigidBody.is_kinematic == true
**When** the body is created
**Then** the system MUST:
- Create a kinematic body
- Update position/rotation via Lua `set_velocity()` (not forces)
- Skip normal collision response (kinematic vs kinematic)

### Requirement 5: Transform Synchronization

The physics system SHALL keep ECS node transforms in sync with Jolt body positions.

#### Scenario 5.1: Physics to Transform
**Given** a Jolt body is at position (10, 5, 0) after stepping
**When** the physics step completes
**Then** the system MUST:
- Update the node's transform component (position/rotation)
- This happens via `SceneManager::set_property()` or equivalent

#### Scenario 5.2: Transform to Physics (Kinematic)
**Given** a kinematic body
**When** Lua script calls `self:set_velocity(Vec3(5, 0, 0))`
**Then** the system MUST:
- Update the Jolt body's velocity
- Jolt will move the body according to that velocity

### Requirement 6: Physics Queries

The physics system SHALL support raycasting and overlap queries via Jolt.

#### Scenario 6.1: Raycast
**Given** a ray origin, direction, and max distance
**When** `PhysicsQuery::raycast()` is called
**Then** the system MUST:
- Cast a ray through the Jolt world
- Return the closest hit (if any)
- Include: hit bool, position, normal, node handle, distance

#### Scenario 6.2: Linecast
**Given** two points and an optional layer mask
**When** `PhysicsQuery::linecast(from, to)` is called
**Then** the system MUST:
- Compute direction and distance from `to - from`
- Call raycast internally
- Return same result as raycast

#### Scenario 6.3: Overlap Sphere
**Given** a sphere center, radius, and optional layer mask
**When** `PhysicsQuery::overlap_sphere(center, radius)` is called
**Then** the system MUST:
- Query all bodies overlapping the sphere volume
- Return an array of node handles (may be empty)

#### Scenario 6.4: Overlap Box
**Given** an AABB center, half-extents, and optional layer mask
**When** `PhysicsQuery::overlap_box(center, half_extents)` is called
**Then** the system MUST:
- Query all bodies overlapping the box volume
- Return an array of node handles (may be empty)

#### Scenario 6.5: Layer Mask Filtering
**Given** a query with a layer mask (e.g., 0x5 = layers 0 and 2)
**When** the query executes
**Then** the system MUST:
- Only return hits from bodies on those layers
- Layers are bit-indexed: `layer_mask = (1 << layer_index)`

### Requirement 7: Collision Events

The physics system SHALL dispatch collision events via EventBus.

#### Scenario 7.1: Collision Enter
**Given** two bodies just made contact
**When** the physics step processes the contact
**Then** the system MUST:
- Create a `CollisionEvent` with:
  - node_a, node_b (handles to the two nodes)
  - contact_point (world-space Vec3)
  - contact_normal (unit Vec3 from A to B)
  - collision_impulse (float magnitude)
  - is_enter = true
- Dispatch via `EventBus<CollisionEvent>::publish(event)`

#### Scenario 7.2: Collision Persistent
**Given** two bodies that are in contact and remain in contact
**When** the next physics step executes
**Then** the system MUST:
- Dispatch another CollisionEvent with is_enter = false
- This allows persistent contact detection

#### Scenario 7.3: Collision Exit
**Given** two bodies that separate
**When** the physics step detects separation
**Then** the system MUST:
- Not dispatch a separate "exit" event
- (Only enter/persistent; exit is implicit)

#### Scenario 7.4: Trigger Events
**Given** a CTrigger body and another body
**When** the other body overlaps the trigger
**Then** the system MUST:
- Create `TriggerEvent` with:
  - trigger_node (the trigger)
  - other_node (the overlapping body)
  - entered = true
- Dispatch via `EventBus<TriggerEvent>::publish(event)`

#### Scenario 7.5: Trigger Exit
**Given** a body that was overlapping a trigger
**When** the body moves away
**Then** the system MUST:
- Create `TriggerEvent` with entered = false
- Dispatch immediately

### Requirement 8: Lua Physics API

The physics system SHALL expose query and control methods via Lua.

#### 8.1 Raycast Method
```lua
ctx.world:raycast(origin: Vec3, direction: Vec3, max_dist: float, [layer_mask: int]) -> table
```
- Returns: `{hit: bool, point: Vec3, normal: Vec3, node: NodeRef|nil, distance: float}`
- `direction` MUST be normalized
- `layer_mask` MAY be omitted (queries all layers)

#### 8.2 Linecast Method
```lua
ctx.world:linecast(from: Vec3, to: Vec3, [layer_mask: int]) -> table
```
- Same return type as raycast
- Internally computes direction and max_dist

#### 8.3 Overlap Sphere Method
```lua
ctx.world:overlap_sphere(center: Vec3, radius: float, [layer_mask: int]) -> NodeRef[]
```
- Returns an array of node references
- MAY be empty

#### 8.4 Overlap Box Method
```lua
ctx.world:overlap_box(center: Vec3, half_extents: Vec3, [layer_mask: int]) -> NodeRef[]
```
- Returns an array of node references
- MAY be empty

#### 8.5 Rigid Body Methods
```lua
self:apply_force(force: Vec3) -> nil
self:apply_impulse(impulse: Vec3) -> nil
self:set_velocity(velocity: Vec3) -> nil
self:get_velocity() -> Vec3
self:set_angular_velocity(angular_vel: Vec3) -> nil
self:get_angular_velocity() -> Vec3
```

#### 8.6 Character Controller Methods
```lua
self:move(movement: Vec3, dt: float) -> nil
self:jump(impulse: float) -> nil
self:is_grounded() -> bool
self:get_ground_normal() -> Vec3
```

#### 8.7 Collision Callbacks
Script MAY define:
```lua
function M:on_collision_enter(ctx, other, contact) end
function M:on_collision_exit(ctx, other) end
function M:on_trigger_enter(ctx, other) end
function M:on_trigger_exit(ctx, other) end
```
These MUST be called automatically when events fire.

### Requirement 9: Scene Integration

The physics system SHALL parse `.uzscene` syntax for physics configuration.

#### Scenario 9.1: Box Collider Syntax
```ini
[node id=5 type=Node3D name="Crate" parent=1]
collider = "box"
collider_half_extents = [0.5, 0.5, 0.5]
collider_offset = [0, 0, 0]
rigid_body_mass = 10.0
rigid_body_friction = 0.5
rigid_body_restitution = 0.1
rigid_body_layer = "Default"
```

#### Scenario 9.2: Sphere Collider Syntax
```ini
[node id=6 type=Node3D name="Sphere" parent=1]
collider = "sphere"
collider_radius = 1.0
collider_offset = [0, 1, 0]
rigid_body_mass = 5.0
```

#### Scenario 9.3: Capsule Collider Syntax
```ini
[node id=7 type=Node3D name="Capsule" parent=1]
collider = "capsule"
collider_radius = 0.5
collider_half_height = 1.0
collider_offset = [0, 1.5, 0]
rigid_body_mass = 8.0
```

#### Scenario 9.4: Trigger Syntax
```ini
[node id=12 type=Node3D name="DamageZone" parent=1]
collider = "box"
collider_half_extents = [5, 3, 5]
trigger = true
rigid_body_layer = "Trigger"
```

#### Scenario 9.5: Character Controller Syntax
```ini
[node id=3 type=Node3D name="Player" parent=1]
collider = "capsule"
collider_radius = 0.4
collider_half_height = 0.9
character_controller = true
character_max_slope = 45.0
character_ground_margin = 0.05
```

#### Scenario 9.6: Property Defaults
**Given** a node with missing collider properties
**When** the node is loaded
**Then** the system MUST use defaults:
- collider_offset: [0, 0, 0]
- rigid_body_friction: 0.5
- rigid_body_restitution: 0.5
- rigid_body_layer: "Default" (layer 0)
- gravity_scale: 1.0
- continuous_ccd: false
- character_max_slope: 45.0
- character_ground_margin: 0.05

### Requirement 10: Collision Layers & Matrix

The physics system SHALL support configurable collision layers and a collision matrix in `.uzproj`.

#### Scenario 10.1: Layer Definition
```toml
[physics.layers]
Default = 0
Player = 1
Enemy = 2
```
The system MUST support up to 16 layers (0–15).

#### Scenario 10.2: Collision Matrix
```toml
[physics.collision_matrix]
Player = ["Default", "Enemy", "Terrain"]
Enemy = ["Default", "Player", "Terrain"]
```
The system MUST:
- Resolve layer names to indices
- Build a Jolt GroupFilter from the matrix
- Apply the filter to all bodies at init time

#### Scenario 10.3: Undefined Layers
**Given** a node referencing a layer that doesn't exist in the matrix
**When** the scene loads
**Then** the system MUST:
- Log a warning (don't crash)
- Default to layer 0 (Default layer)

#### Scenario 10.4: Layer Persistence
**Given** the collision matrix is set at init time
**When** runtime layer changes occur
**Then** the system SHALL allow runtime gravity changes but collision matrix changes require engine restart

### Requirement 11: Physics Debug Draw

The physics system SHALL provide debug visualization in editor mode.

#### Scenario 11.1: Debug Renderer
**Given** the engine is in editor mode (--editor)
**When** physics debug draw is enabled
**Then** the system MUST:
- Extract wireframes from all Jolt bodies
- Extract contact points and normals
- Send via IPC to VSCode

#### Scenario 11.2: IPC Command
```
Command: physics.debug_draw
Args: {enabled: bool}
Returns: {}
```

#### Scenario 11.3: Debug Visualization
**Given** debug draw is enabled
**When** the VSCode overlay renders
**Then** it MUST show:
- White wireframes for dynamic bodies
- Cyan wireframes for kinematic bodies
- Gray wireframes for static bodies
- Red velocity vectors
- Yellow contact point markers
- Blue surface normal arrows

### Requirement 12: Error Handling

The physics system SHALL handle errors gracefully.

#### Scenario 12.1: Invalid Collider
**Given** a node references a mesh that doesn't exist
**When** the scene loads
**Then** the system MUST:
- Log an error
- Create a default sphere collider instead (don't crash)

#### Scenario 12.2: Invalid Layer
**Given** a node references a layer not in `.uzproj`
**When** the node is created
**Then** the system MUST:
- Log a warning
- Use layer 0 (Default) instead

#### Scenario 12.3: Lua Error in Callback
**Given** a script's `on_collision_enter` throws an exception
**When** the collision event fires
**Then** the system MUST:
- Catch the Lua error via pcall
- Log the error
- Continue (don't crash or disable physics)

### Requirement 13: Performance

The physics system SHALL support high body counts efficiently.

#### Scenario 13.1: Body Count
The system MUST support at least 1,000 dynamic bodies at 60 FPS on target hardware.

#### Scenario 13.2: Sleeping
The system MUST automatically put inactive bodies to sleep (no stepping).

#### Scenario 13.3: Spatial Partitioning
The system MUST use Jolt's built-in broadphase for efficient query performance.

---

## MODIFIED Requirements

### Requirement A: `.uzproj` Format
**MODIFIED** to include physics configuration:
```toml
[physics]
gravity = [0, -9.81, 0]
fixed_hz = 60
max_substeps = 4

[physics.layers]
Default = 0
Player = 1

[physics.collision_matrix]
Player = ["Default", "Terrain"]
```

### Requirement B: ECS System
**MODIFIED** to register 6 new physics components.

### Requirement C: game.dll Main Loop
**MODIFIED** to call `physics_step()` before script updates.

---

## REMOVED Requirements

None.

---

## Test Scenarios

### Test: Rigid Body Gravity
**Given** a sphere with mass 1.0 at position (0, 10, 0)
**When** the physics world steps for 1 second
**Then** the sphere MUST fall approximately 4.9 meters (under gravity = -9.81 m/s²)

### Test: Collision Detection
**Given** a sphere and a plane with collision enabled
**When** the sphere falls onto the plane
**Then** the `CollisionEvent` MUST fire with correct contact point and normal

### Test: Raycast
**Given** a static box at (0, 0, 0) and a ray from (-10, 0, 0) to (10, 0, 0)
**When** the ray is cast
**Then** it MUST hit the box and return distance ≈ 10.0

### Test: Lua Physics API
**Given** a Lua script with `ctx.world:raycast(...)`
**When** the script calls the method
**Then** it MUST return a valid result table (hit may be false, but structure is correct)

### Test: Character Controller
**Given** a character controller on flat ground
**When** `self:move(Vec3(5, 0, 0), 0.016)` is called for 60 frames
**Then** the character MUST move approximately 5 units in the X direction

---

## Success Criteria

1. All 13 core requirements implemented
2. All test scenarios pass
3. 95%+ code coverage on physics module
4. No memory leaks (ASAN clean)
5. Performance: 1000+ bodies @ 60 FPS
6. Comprehensive documentation
7. Lua physics API fully functional
8. Editor debug draw working
