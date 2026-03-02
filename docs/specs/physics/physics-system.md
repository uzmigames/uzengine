# Physics System Spec

> **Status**: Planned (Phase 3)
> **Library**: Jolt Physics (MIT license)
> **Owner**: `game.dll` (C++ physics engine interface)
> **Lua API**: Available Phase 3+ via `ctx.world` queries and rigid body methods

---

## Overview

Jolt Physics provides real-time rigid body simulation, collision detection, and character control for UzEngine. The physics engine runs entirely inside `game.dll` (not in `UzEngine.exe`) to maintain the clean C ABI boundary.

**Key principles**:
- Single `PhysicsWorld` singleton in `game.dll` updated at fixed 60 Hz (independent of frame rate)
- Colliders are ECS components; rigid bodies update their transform components each physics frame
- Collision events dispatch via `EventBus` and are accessible from Lua scripts
- All editor physics debug draw is sent via IPC to VSCode overlay (editor mode only)
- Physics layers (16 max) and collision matrix are configured in `.uzproj`

---

## Architecture

### Physics World Lifecycle

```cpp
// game.dll — physics subsystem

struct PhysicsWorld {
    JPH::PhysicsSystem*     jph;              // Jolt's main system
    float                   time_accumulator; // For fixed-step integration
    Arena*                  arena;
};

// Initialization (on game start)
PhysicsWorld world = PhysicsWorld::create(config.gravity, config.max_bodies);

// Every frame (in on_update)
void physics_step(PhysicsWorld& world, float dt) {
    world.time_accumulator += dt;

    while (world.time_accumulator >= PHYSICS_DT) {
        // Fixed 60 Hz physics step
        world.jph->Update(PHYSICS_DT, ...);

        // Dispatch collision events to EventBus
        process_collision_manifolds(world);

        world.time_accumulator -= PHYSICS_DT;
    }
}
```

**Timing**:
- `PHYSICS_DT = 1.0 / 60.0` (16.67 ms)
- Game runs at variable frame rate (tied to VSCode canvas refresh)
- Physics accumulator pattern handles frame rate variance
- Up to 4 sub-steps allowed (configurable in `.uzproj`)

### Integration with ECS

Rigid bodies are represented by two ECS components:

```cpp
// Component 1: Rigid body properties
struct CRigidBody {
    JPH::BodyID     jolt_body_id;      // Handle into Jolt's body set
    float           mass;              // kg (0 = static)
    float           restitution;       // 0.0–1.0 (bounciness)
    float           friction;          // 0.0–1.0
    uint8_t         layer;             // Collision layer (0–15)
    bool            is_kinematic;      // Kinematics move, don't collide with dynamics
    float           gravity_scale;     // Multiplier on world gravity
    bool            continuous_ccd;    // Continuous collision detection for fast movers
};

// Component 2: Collider shape (one per node, combined with CRigidBody)
// Only ONE collider per node; complex shapes use CColliderMesh
struct CColliderBox {
    Vec3 half_extents;  // local-space
    Vec3 offset;        // local offset from node
};

struct CColliderSphere {
    float radius;
    Vec3  offset;
};

struct CColliderCapsule {
    float radius;
    float half_height;
    Vec3  offset;
};

struct CColliderMesh {
    StringId mesh_id;   // Loaded mesh asset; hull is auto-computed
};

struct CColliderTerrain {
    StringId terrain_id; // Terrain asset for height-field collisions
};

// Component 3: Triggers (no collision response, only overlap events)
struct CTrigger {
    JPH::BodyID jolt_body_id;
    // Fires on enter/exit; physics layer still applies
};

// Component 4: Character controller (special case; see below)
struct CCharacterController {
    JPH::CharacterVirtual* character;
    float                  max_slope_angle; // degrees
    float                  ground_margin;   // distance to ground
    bool                   is_grounded;
};
```

**Creation flow**:
1. Scene loads a node with `collider = "box"` and `rigid_body_mass = 80.0`
2. SceneManager creates the CColliderBox and CRigidBody components
3. PhysicsWorld::on_component_added() converts them to Jolt bodies and shapes
4. Each frame: CRigidBody.jolt_body_id is updated → node transform via `set_property` command

---

## Collision Layers & Matrix

### Layer Configuration (`.uzproj`)

```toml
[physics]
gravity         = [0, -9.81, 0]
fixed_hz        = 60
max_substeps    = 4

# Define up to 16 layers (optional names; default: Layer0–Layer15)
[physics.layers]
Default     = 0
Player      = 1
Enemy       = 2
Projectile  = 3
Trigger     = 4
Terrain     = 5
UI          = 6
PowerUp     = 7

# Collision matrix: which layers collide with which (symmetric)
[physics.collision_matrix]
# Format: LayerName = [list of layers it collides with]
Default     = ["Default", "Player", "Enemy", "Projectile", "Terrain"]
Player      = ["Default", "Enemy", "Projectile", "Terrain", "PowerUp"]
Enemy       = ["Default", "Player", "Projectile", "Terrain"]
Projectile  = ["Default", "Player", "Enemy", "Terrain"]
Trigger     = ["Player", "Enemy", "Projectile"]
Terrain     = ["Default", "Player", "Enemy", "Projectile"]
PowerUp     = ["Player"]
```

**Trigger layer** (`Trigger` = 4):
- Nodes with `CTrigger` component belong to this layer
- Triggers do NOT participate in normal collision response
- Only fires enter/exit events (handled separately from collision events)

### At Runtime

PhysicsWorld loads the collision matrix during initialization:

```cpp
void PhysicsWorld::init_collision_matrix(const ProjectConfig& cfg) {
    JPH::GroupFilter filter = /* build from cfg.collision_matrix */;
    world.jph->GetPhysicsSystem()->SetGroupFilter(filter);
}
```

Nodes query their layer when adding rigid bodies:

```ini
[node id=5 type=Node3D name="Player" parent=1]
collider = "capsule"
collider_radius = 0.4
collider_half_height = 0.9
rigid_body_mass = 80.0
rigid_body_layer = "Player"        ; resolved to layer index at load time
```

---

## ECS Component Details

### `CRigidBody` — Full Reference

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| `jolt_body_id` | `JPH::BodyID` | N/A | Managed internally; invalid = not yet created |
| `mass` | `float` | 1.0 | kg; 0 = static (immobile) |
| `restitution` | `float` | 0.5 | Bounciness; 0 = no bounce, 1 = perfect |
| `friction` | `float` | 0.5 | 0 = frictionless, 1 = high friction |
| `layer` | `uint8_t` | 0 | Collision layer (0–15) |
| `is_kinematic` | `bool` | false | If true, body moves via `set_velocity`, not forces |
| `gravity_scale` | `float` | 1.0 | Multiplier on world gravity; use <1 for floaty objects |
| `continuous_ccd` | `bool` | false | Enable for fast-moving objects (bullets, etc.) |

**Static vs Dynamic vs Kinematic**:
- **Static** (`mass = 0`): Immobile; used for terrain, walls, static geometry
- **Dynamic** (`mass > 0`): Affected by forces, gravity, collisions
- **Kinematic** (`is_kinematic = true`): Moved via script, doesn't collide with other kinematics

### Collider Shape Components

**Box**:
```cpp
struct CColliderBox { Vec3 half_extents; Vec3 offset; };
// Usage: half_extents = [10, 0.5, 5] → box is 20×1×10 units
```

**Sphere**:
```cpp
struct CColliderSphere { float radius; Vec3 offset; };
```

**Capsule**:
```cpp
struct CColliderCapsule { float radius; float half_height; Vec3 offset; };
// Total height = 2 * (radius + half_height)
```

**Mesh** (Convex Hull):
```cpp
struct CColliderMesh { StringId mesh_id; };
// Jolt auto-computes convex hull from mesh geometry
// Useful for irregular shapes (barrels, crates, etc.)
```

**Terrain** (Height Field):
```cpp
struct CColliderTerrain { StringId terrain_id; };
// Special case: height-field collision from .uzterrain asset
```

### `CTrigger` — Overlap Volumes

```cpp
struct CTrigger {
    JPH::BodyID jolt_body_id;
    // Nodes with this component fire TriggerEvent on enter/exit
    // No collision response; purely event-based
};
```

**Usage in `.uzscene`**:
```ini
[node id=12 type=Node3D name="DamageZone" parent=1]
collider = "box"
collider_half_extents = [5, 3, 5]
trigger = true                      ; no rigid_body_mass (not a body)
```

Triggers always fire enter/exit events regardless of layer configuration. Use physics layers to prevent unwanted overlaps.

### `CCharacterController` — Special Case

```cpp
struct CCharacterController {
    JPH::CharacterVirtual* character;
    float                  max_slope_angle;  // degrees (default: 45)
    float                  ground_margin;    // distance to ground (default: 0.05m)
    bool                   is_grounded;      // read-only; updated by physics step
};
```

**Purpose**: Player/NPC movement with automatic slope handling, step climbing, and ground detection.

**Usage in `.uzscene`**:
```ini
[node id=5 type=Node3D name="Player" parent=1]
collider = "capsule"
collider_radius = 0.4
collider_half_height = 0.9
character_controller = true          ; enables CCharacterController
character_max_slope = 45.0
character_ground_margin = 0.05
```

---

## Collision Events

### `CollisionEvent` — ECS Structure

Fired via `EventBus<CollisionEvent>` when two rigid bodies collide.

```cpp
struct CollisionEvent {
    NodeHandle node_a;              // First body
    NodeHandle node_b;              // Second body
    Vec3       contact_point;       // World-space collision point
    Vec3       contact_normal;      // Normal from A → B
    float      collision_impulse;   // Magnitude of impulse applied
    bool       is_enter;            // true = new collision, false = persistent
};
```

**Dispatch**:
- Fired once per unique contact pair per physics frame
- Events are buffered and dispatched at the start of the Game Logic thread
- Lua scripts receive these via collision callbacks (see Lua API section)

### `TriggerEvent` — ECS Structure

Fired via `EventBus<TriggerEvent>` when any shape enters/exits a trigger volume.

```cpp
struct TriggerEvent {
    NodeHandle trigger_node;        // The trigger volume
    NodeHandle other_node;          // The overlapping body
    bool       entered;             // true = enter, false = exit
};
```

**Dispatch**:
- Fired every physics frame while overlap persists (not just on enter)
- Lua can filter with `if event.entered then ... end`

---

## Lua Physics API

All physics queries and rigid body control is exposed via `ctx.world` (injected by `game.dll`).

### Query Functions (Raycasts & Overlaps)

#### `ctx.world:raycast(origin, direction, max_dist [, layer_mask])` → result table

Cast a ray and return the first hit.

```lua
local hit = ctx.world:raycast(
    self.position,                  -- origin: Vec3
    Vec3(0, -1, 0):normalized(),    -- direction: Vec3 (should be normalized)
    100.0                           -- max_dist: float
)

if hit.hit then
    ctx:log("Hit " .. hit.node.name .. " at distance " .. hit.distance)
    -- hit.point, hit.normal, hit.node available
end
```

**Return table fields**:

| Field | Type | Description |
|-------|------|-------------|
| `hit` | `bool` | `true` if anything was hit |
| `point` | `Vec3` | World-space hit position (zero if no hit) |
| `normal` | `Vec3` | Surface normal (zero if no hit) |
| `node` | `NodeRef\|nil` | The node that was hit (nil if no hit) |
| `distance` | `float` | Distance from origin to hit point (0 if no hit) |

**Optional `layer_mask`**:
- Integer bitmask of layers to include: `0x5 = (1 << 0) | (1 << 2) = layers 0 and 2`
- Omit to query all layers
- Example: `ctx.world:raycast(origin, dir, 50.0, 0x3)` — only layers 0 and 1

#### `ctx.world:linecast(from, to [, layer_mask])` → result table

Convenience: cast a ray from `from` to `to`. Returns same result table as `raycast`.

```lua
local hit = ctx.world:linecast(self.position, target.position)
if not hit.hit then
    -- Clear line of sight to target
    self:fire_weapon(ctx, target)
end
```

#### `ctx.world:overlap_sphere(center, radius [, layer_mask])` → `NodeRef[]`

Return all nodes whose colliders overlap a sphere.

```lua
local victims = ctx.world:overlap_sphere(
    explosion.position,
    5.0             -- radius in units
)

for _, node in ipairs(victims) do
    if node:has_tag("damageable") then
        node:send_event("damage", {amount=50})
    end
end
```

**Notes**:
- Returns a Lua array (may be empty `{}`)
- Order is unspecified (Jolt's internal order)
- The querying node itself may be included — filter by `node.id ~= self.id` if needed
- Returns only nodes with colliders (ignores trigger-only nodes)

#### `ctx.world:overlap_box(center, half_extents [, layer_mask])` → `NodeRef[]`

Return all nodes whose colliders overlap an axis-aligned box.

```lua
local items = ctx.world:overlap_box(
    self.position,
    Vec3(1.0, 0.5, 1.0)   -- half_extents: box is 2×1×2 units
)
for _, item in ipairs(items) do
    if item:has_tag("pickup") then
        collect_item(ctx, item)
    end
end
```

### Rigid Body Control

Called on the node's Lua script (methods available via `self`).

#### `self:apply_force(force: Vec3) → nil`

Apply a force (N) to the rigid body's center of mass.

```lua
function M:on_start(ctx)
    -- Apply upward force (e.g., jump)
    self:apply_force(Vec3(0, 500, 0))
end
```

#### `self:apply_impulse(impulse: Vec3) → nil`

Apply an instantaneous impulse (N·s) — like `apply_force` but in a single frame.

```lua
function M:jump(ctx, power)
    self:apply_impulse(Vec3(0, power, 0))
end
```

#### `self:set_velocity(velocity: Vec3) → nil`

Set the linear velocity directly (m/s). Useful for kinematic bodies.

```lua
function M:on_update(ctx, dt)
    local input_dir = ctx.input:axis("Horizontal")
    self:set_velocity(Vec3(input_dir * 5.0, self:get_velocity().y, 0))
end
```

#### `self:get_velocity() → Vec3`

Return the current linear velocity (m/s).

```lua
local vel = self:get_velocity()
ctx:log(string.format("Speed: %.1f m/s", vel:length()))
```

#### `self:set_angular_velocity(velocity: Vec3) → nil`

Set the rotational velocity (rad/s).

#### `self:get_angular_velocity() → Vec3`

Return the current angular velocity.

### Character Controller Methods

Available only on nodes with `CCharacterController`.

#### `self:move(movement: Vec3, dt: float) → nil`

Move the character by the given displacement, handling slope climbing and ground detection.

```lua
function M:on_update(ctx, dt)
    local input_x = ctx.input:axis("Horizontal")
    local input_z = ctx.input:axis("Vertical")

    local move = Vec3(input_x, 0, input_z) * self.speed * dt
    self:move(move, dt)
end
```

#### `self:jump(impulse: float) → nil`

Jump with the given upward impulse, only if grounded.

```lua
if ctx.input:pressed("Jump") and self:is_grounded() then
    self:jump(5.0)
end
```

#### `self:is_grounded() → bool`

Return `true` if the character is in contact with the ground (or within `ground_margin`).

```lua
if self:is_grounded() then
    self.fall_velocity = 0.0
else
    self.fall_velocity = self.fall_velocity - 9.81 * dt
    self:apply_force(Vec3(0, self.fall_velocity, 0))
end
```

#### `self:get_ground_normal() → Vec3`

Return the surface normal of the ground beneath the character. Useful for slope-relative movement.

```lua
local ground_normal = self:get_ground_normal()
local slope_angle = math.acos(ground_normal.y)  -- radians
ctx:log("Slope angle: " .. math.deg(slope_angle) .. " degrees")
```

### Script Collision Callbacks

Script methods called automatically by the engine on collision events.

#### `M:on_collision_enter(ctx, other, contact)`

Called when a new collision begins with `other` node. `contact` is a contact info table.

```lua
function M:on_collision_enter(ctx, other, contact)
    ctx:log("Collided with " .. other.name .. " at " .. contact.point:str())

    if other:has_tag("enemy") then
        self:take_damage(ctx, 10)
    end
end
```

**`contact` table fields**:

| Field | Type | Description |
|-------|------|-------------|
| `point` | `Vec3` | Collision point (world-space) |
| `normal` | `Vec3` | Normal from self → other |
| `impulse` | `float` | Magnitude of impulse applied |
| `relative_velocity` | `Vec3` | Relative velocity at contact point |

#### `M:on_collision_exit(ctx, other)`

Called when a collision with `other` ends.

```lua
function M:on_collision_exit(ctx, other)
    if other.name == "PowerUp" then
        self.is_boosted = false
    end
end
```

#### `M:on_trigger_enter(ctx, other)`

Called when `other` enters a trigger volume that this node owns.

```lua
function M:on_trigger_enter(ctx, other)
    if other:has_tag("player") then
        ctx:log("Player entered zone!")
        self:activate_trap(ctx)
    end
end
```

#### `M:on_trigger_exit(ctx, other)`

Called when `other` exits a trigger volume.

```lua
function M:on_trigger_exit(ctx, other)
    if other:has_tag("player") then
        self:deactivate_trap(ctx)
    end
end
```

---

## `.uzscene` Syntax

### Rigid Bodies

```ini
; Dynamic rigid body (moves under gravity and forces)
[node id=5 type=Node3D name="Crate" parent=1]
position             = [0, 5, 0]
collider             = "box"
collider_half_extents = [0.5, 0.5, 0.5]
rigid_body_mass      = 10.0           ; kg
rigid_body_friction  = 0.5
rigid_body_restitution = 0.1
rigid_body_layer     = "Default"
; gravity_scale = 1.0 (default)
; continuous_ccd = false (default)

; Static rigid body (immobile)
[node id=6 type=Node3D name="Floor" parent=1]
scale                = [50, 1, 50]
collider             = "box"
collider_half_extents = [1, 1, 1]
rigid_body_mass      = 0              ; 0 = static

; Kinematic body (moved via script, doesn't respond to forces)
[node id=7 type=Node3D name="Platform" parent=1]
collider             = "box"
collider_half_extents = [5, 0.5, 2]
rigid_body_mass      = 1.0
rigid_body_kinematic = true           ; moved by script, not forces

; Capsule collider
[node id=8 type=Node3D name="Cylinder" parent=1]
collider             = "capsule"
collider_radius      = 0.5
collider_half_height = 1.0
collider_offset      = [0, 1.0, 0]    ; local offset
rigid_body_mass      = 5.0
```

### Triggers

```ini
; Trigger volume (no collision response, only enter/exit events)
[node id=12 type=Node3D name="DamageZone" parent=1]
position       = [0, 0, 0]
scale          = [10, 3, 10]
collider       = "box"
collider_half_extents = [1, 1, 1]
trigger        = true                 ; no rigid_body_mass
rigid_body_layer = "Trigger"
```

### Character Controller

```ini
; Player character with built-in controller
[node id=3 type=Node3D name="Player" parent=1]
position              = [0, 1, 0]
collider              = "capsule"
collider_radius       = 0.4
collider_half_height  = 0.9
character_controller  = true
character_max_slope   = 45.0          ; degrees
character_ground_margin = 0.05        ; meters
```

### Custom Collider Offset

```ini
; Collider can be offset from node origin (useful for non-centered geometry)
[node id=15 type=Node3D name="Enemy" parent=1]
position = [0, 0, 0]
collider = "sphere"
collider_radius = 1.0
collider_offset = [0, 1.5, 0]         ; 1.5 units above node origin
rigid_body_mass = 20.0
```

---

## Physics Debug Draw

### In Editor Mode (`--editor`)

Jolt Physics has a built-in debug renderer. When enabled, the engine sends debug primitives (lines, wireframes, contact points) via IPC to VSCode.

```cpp
// game.dll — per-frame debug rendering
if (UZ_EDITOR) {
    JPH::DebugRenderer debug_renderer;
    world.jph->DrawBodies(debug_renderer);  // wireframe for all bodies
    world.jph->DrawConstraints(debug_renderer);

    // Send debug_renderer's primitives via IPC to VSCode
    ipc_send("physics.debug_draw", {
        lines: [...],          // wireframe geometry
        contacts: [...],       // contact point markers
        velocities: [...]      // velocity arrows
    });
}
```

### IPC Command

| Command | Args | Returns | Purpose |
|---------|------|---------|---------|
| `physics.debug_draw` | `bool` | `{}` | Enable/disable debug rendering |
| `physics.get_bodies` | — | `[{name, shape, layer, ...}]` | List all bodies in scene (editor only) |
| `physics.set_gravity` | `{gravity: Vec3}` | `{}` | Change world gravity at runtime |

### VSCode Overlay

Debug draw appears as:
- **Wireframe wireframes** for all bodies (white for dynamic, cyan for kinematic, gray for static)
- **Red vectors** showing velocity
- **Yellow points** at contact locations
- **Blue arrows** showing surface normals

Toggle via VSCode command: `UzEngine: Toggle Physics Debug`

---

## Phase Implementation Details

### Phase 3 — Core Physics (this phase)

**Deliverables**:
1. Jolt Physics integration (vcpkg)
2. ECS components: `CRigidBody`, `CCollider*`, `CTrigger`, `CCharacterController`
3. `PhysicsWorld` singleton in `game.dll` with fixed-step loop
4. Collision event dispatch via `EventBus`
5. Lua physics API: queries + rigid body control + callbacks
6. Physics debug draw via IPC
7. `.uzproj` physics configuration (gravity, layers, matrix)
8. `.uzscene` syntax for all collider types

**Out of scope**:
- Soft bodies (Phase 4)
- Constraints, hinges, springs (Phase 4)
- Ragdoll (separate Phase 3 task: `phase3-ragdoll`)
- Vehicle physics (Phase 5+)

---

## Performance Considerations

### Fixed Timestep

Physics runs at 60 Hz regardless of frame rate:
- 60 FPS: 1 physics step per frame
- 30 FPS: 2 physics steps per frame
- 120 FPS: 0.5 physics steps per frame (interpolated)

Accumulator pattern ensures stable physics.

### Body Count

Jolt handles thousands of bodies efficiently. Practical limits:
- **Simple shapes** (boxes, spheres): 5,000+ bodies @ 60 FPS
- **Convex meshes**: 1,000–2,000 bodies @ 60 FPS
- **Terrain**: One-per-level (height-field is highly optimized)

### Sleeping

Jolt automatically puts inactive bodies to sleep. Sleeping bodies are not stepped, reducing CPU load.

---

## Integration with Other Systems

### Animation System

When skeletal meshes are animated (Phase 3), physics can respond:
- **Physics-driven animation**: Physics results are read back into bone transforms
- **Animation-driven physics**: Bones push physics bodies (e.g., ragdoll)

Separate `phase3-ragdoll` task covers this.

### Audio System

Collision impacts can trigger sound events:

```lua
function M:on_collision_enter(ctx, other, contact)
    local impact_speed = contact.relative_velocity:length()
    if impact_speed > 5.0 then
        -- Trigger impact sound
        other:send_event("impact_sound", {intensity = impact_speed})
    end
end
```

### Input System (Phase 3)

Player input drives character controller:

```lua
function M:on_update(ctx, dt)
    local move = Vec3(
        ctx.input:axis("Horizontal"),
        0,
        ctx.input:axis("Vertical")
    ) * self.speed
    self:move(move, dt)
end
```

---

## Common Patterns

### Player Movement

```lua
local M = {}

function M:on_start(ctx)
    self.speed = 5.0
    self.jump_power = 10.0
end

function M:on_update(ctx, dt)
    -- Read input
    local input_x = ctx.input:axis("Horizontal")
    local input_z = ctx.input:axis("Vertical")

    -- Calculate movement
    local move = Vec3(input_x, 0, input_z):normalized() * self.speed * dt
    self:move(move, dt)

    -- Jump
    if ctx.input:pressed("Jump") and self:is_grounded() then
        self:jump(self.jump_power)
    end

    -- Fall damage
    local vel = self:get_velocity()
    if vel.y < -20.0 and self:is_grounded() then
        self:send_event("take_damage", {amount = 50})
    end
end

return M
```

### Projectile

```lua
local M = {}

function M:on_start(ctx)
    self.lifetime = 5.0
    self.age = 0.0
end

function M:on_update(ctx, dt)
    self.age = self.age + dt
    if self.age > self.lifetime then
        ctx.scene:remove_node(self)
    end
end

function M:on_collision_enter(ctx, other, contact)
    if other:has_tag("damageable") then
        other:send_event("take_damage", {amount = 25})
    end
    ctx.scene:remove_node(self)
end

return M
```

### Pickup Trigger

```lua
local M = {}

function M:on_trigger_enter(ctx, other)
    if other:has_tag("player") then
        other:send_event("pickup_power_up", {type = self.power_up_type})
        ctx.scene:remove_node(self)
    end
end

return M
```

### Line-of-Sight Query

```lua
local M = {}

function M:can_see(ctx, target)
    local hit = ctx.world:linecast(self.position, target.position)
    if hit.hit and hit.node.id ~= target.id then
        return false  -- Something blocking
    end
    return true       -- Line of sight clear
end

function M:on_update(ctx, dt)
    if self:can_see(ctx, self.target) then
        self:attack(ctx, self.target)
    end
end

return M
```

---

## C++ Source Structure

```
src/engine/physics/
├── physics_world.h/cpp        — PhysicsWorld singleton, body creation/deletion
├── physics_query.h/cpp        — Raycast, overlap, shape casting
├── physics_events.h/cpp       — CollisionEvent, TriggerEvent dispatch
├── jolt_integration.h/cpp     — Jolt library setup, custom allocators
├── character_controller.h/cpp — CharacterVirtual wrapper
└── debug_draw.h/cpp           — Jolt debug renderer → IPC

src/engine/ecs/components.h    — CRigidBody, CCollider*, CTrigger, CCharacterController

src/engine/scripting/          — Lua bindings
├── lua_physics_binding.h/cpp  — ctx.world, rigid body methods, callbacks

game/
└── physics_callbacks.cpp       — Dispatch on_collision_* / on_trigger_* Lua methods
```

---

## Related Documentation

- [specs/scene-system.md](scene-system.md) — Node types and `.uzscene` format
- [specs/lua-scripting.md](lua-scripting.md) — Script lifecycle, `ctx` API, hot-reload
- [specs/event-bus.md](event-bus.md) — `EventBus<T>` pattern for collision events
- [specs/ragdoll.md](ragdoll.md) — Physics-driven skeletal animation (Phase 3)
- [engine/design-decisions.md](../engine/design-decisions.md) — Why Jolt over Bullet/PhysX

---

## Physics Materials

Physics materials are reusable assets (`.uzphysmat`) that define surface interaction properties:

```ini
[PhysicsMaterial]
name = ice
friction = 0.05
restitution = 0.8
friction_combine = minimum     ; minimum | average | maximum | multiply
restitution_combine = average
```

Materials are referenced in scene nodes:
```ini
[CColliderBox]
size = 1 1 1
material = res://physics/ice.uzphysmat
```

The `PhysicsMaterialRegistry` loads and caches materials; if a path is missing, a default material (friction=0.5, restitution=0.3) is used and a warning is logged.

## Continuous Collision Detection (CCD)

CCD is configurable per body to prevent fast-moving objects from tunneling:

```ini
[CRigidBody]
motion_quality = linear_cast   ; discrete | linear_cast
```

Internally maps to Jolt's `EMotionQuality::LinearCast`. Enable for bullets, thrown objects, and fast characters. Disable for static/slow bodies to save performance.

## Constraint Types

In addition to `CCharacterController`, the physics system supports:

| Constraint | Jolt Equivalent | Use Case |
|-----------|----------------|----------|
| `CFixedJoint` | FixedConstraint | Welded objects |
| `CHingeJoint` | HingeConstraint | Doors, wheels |
| `CSliderJoint` | SliderConstraint | Pistons, drawers |
| `CConeJoint` | ConeConstraint | Ragdoll limbs |
| `CBreakableJoint` | any + `max_impulse` | Destructible attachments |

Breakable joints fire `JointBrokenEvent{entity_a, entity_b}` on the EventBus when the impulse threshold is exceeded.

## Physics LOD

Bodies beyond `physics_lod_distance` (default: 50m, configurable in `.uzproj`) are switched to a simplified convex hull representation and their tick rate is halved. Bodies beyond `physics_sleep_distance` (default: 200m) are put to sleep unless a stimulus (explosion, trigger) wakes them.

## Determinism Contract

For multiplayer or replay use cases, physics determinism requires:
1. Fixed timestep (default 1/60s, not variable)
2. Jolt built with `JPH_DETERMINISTIC_SORT=1`
3. No use of `std::unordered_map` in simulation-affecting code paths
4. All random values use a seeded `DeterministicRNG` instance
5. Platform-specific: x64 only; ARM requires separate validation

Enable via `.uzproj`:
```toml
[physics]
deterministic = true
fixed_timestep = 0.016666
```
