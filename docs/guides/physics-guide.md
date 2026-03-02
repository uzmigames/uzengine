# Physics Guide — UzEngine

Add realistic physics to your game with Jolt Physics. Create dynamic objects, detect collisions, cast rays, and build gameplay interactions.

> **Status**: Physics system comes in Phase 3. Lua scripting (Phase 2-3) is the primary interface.

---

## Overview

### What Jolt Provides

Jolt Physics 5.1 is a modern, high-performance physics engine integrated into UzEngine:

| Feature | Purpose |
|---------|---------|
| **Rigid body dynamics** | Objects fall, collide, bounce |
| **Collision detection** | Detect when objects touch |
| **Raycasting** | Cast rays from editor or scripts |
| **Character controller** | Player movement (separate from rigid bodies) |
| **Constraints** | Joints, hinges, motors |
| **Trigger volumes** | Non-physical collision zones |
| **Collision layers** | Control what collides with what |

### Architecture

```
Game.dll
├── Physics Manager (manager, allocator)
├── Jolt Physics (bodies, shapes, world)
├── ECS Components (RigidBody, Collider, CharacterController)
└── Lua Bindings (ctx.physics API)
```

Physics updates are driven by the game loop:
1. **Lua on_update** — movement commands
2. **Physics step** — Jolt::StepSimulation()
3. **ECS sync** — physics results → component transforms
4. **Rendering** — draw updated positions

---

## Adding Physics to Objects

### Scene Definition (Phase 1+)

In `.uzscene` files, add physics components to nodes:

```ini
[uzscene version=1 name="Level"]

[node id=1 type=Node3D name="GroundPlane" parent=0]
position = [0, -1, 0]
scale = [10, 0.1, 10]

; Add a physics collider (static, non-moving)
[component type=CColliderBox parent=1]
size = [10, 0.2, 10]
is_trigger = false

; Add a rigid body (dynamic, affected by physics)
[node id=2 type=Node3D name="Ball" parent=0]
position = [0, 5, 0]

[component type=CRigidBody parent=2]
shape = "sphere"
radius = 0.5
mass = 1.0
restitution = 0.8          ; Bounciness
friction = 0.4

[component type=CCollider parent=2]
shape = "sphere"
radius = 0.5
```

### Rigid Body Modes

When adding a `CRigidBody` component, specify its motion type:

#### Static (Non-Moving)
```ini
[component type=CRigidBody parent=2]
motion_type = "static"     ; Never moves (ground, walls)
shape = "box"
size = [5, 0.5, 5]
```
- Used for: Terrain, walls, platforms
- Collides with: Everything
- Performance: Very fast (no simulation)

#### Dynamic (Simulated)
```ini
[component type=CRigidBody parent=2]
motion_type = "dynamic"    ; Falls, bounces, responds to forces
mass = 1.0
shape = "box"
size = [1, 1, 1]
```
- Used for: Objects, props, enemies
- Collides with: Everything
- Performance: Medium (physics step required)

#### Kinematic (Script-Controlled)
```ini
[component type=CRigidBody parent=2]
motion_type = "kinematic"  ; Moves via script, no gravity
shape = "capsule"
radius = 0.4
height = 1.8
```
- Used for: Animated objects, elevators, moving platforms
- Collides with: Everything
- Performance: Fast (script moves it; Jolt calculates collisions)

### Collider Shapes

Jolt supports several built-in shapes:

**Box (AABB):**
```ini
[component type=CCollider parent=2]
shape = "box"
size = [2, 2, 2]           ; Half-extents (width, height, depth)
```

**Sphere:**
```ini
[component type=CCollider parent=2]
shape = "sphere"
radius = 1.0
```

**Capsule (cylinder with rounded ends):**
```ini
[component type=CCollider parent=2]
shape = "capsule"
radius = 0.4
height = 1.8               ; Height of cylinder, excluding caps
```

**Cylinder:**
```ini
[component type=CCollider parent=2]
shape = "cylinder"
radius = 0.5
height = 2.0
```

**Convex mesh (custom shape):**
```ini
[component type=CCollider parent=2]
shape = "mesh"
mesh = "res://assets/collider_shape.obj"
```

**Mesh (complex terrain):**
```ini
[component type=CCollider parent=2]
shape = "heightfield"
mesh = "res://assets/terrain.obj"
```

---

## Static vs Dynamic vs Kinematic

### Static Bodies

**Use for:** Ground, walls, terrain, fixed obstacles

```ini
[component type=CRigidBody parent=groundnode]
motion_type = "static"
shape = "box"
size = [50, 0.5, 50]
```

**Characteristics:**
- Transforms fixed; never move automatically
- Don't affect performance when stationary
- Great for level geometry

### Dynamic Bodies

**Use for:** Objects affected by gravity and forces

```lua
-- Lua example: Drop a box
function M:on_start(ctx)
    self.box = ctx.scene:add_node({
        type = "Node3D",
        name = "Box"
    })
    ctx.scene:add_component(self.box, "CRigidBody", {
        motion_type = "dynamic",
        shape = "box",
        size = [1, 1, 1],
        mass = 2.0
    })
end
```

**Characteristics:**
- Falls under gravity
- Responds to collisions
- Can be pushed by other bodies
- Slower than static (physics step required)

### Kinematic Bodies

**Use for:** Moving platforms, elevators, scripted objects

```lua
-- Lua example: Moving platform
function M:on_update(ctx, dt)
    -- Script moves the platform; Jolt calculates collisions
    self.platform.position = self.platform.position + vec3(0, 0, 2 * dt)
end
```

**Characteristics:**
- Script controls position/rotation
- Collides with everything
- Doesn't respond to forces
- Good for interactive objects

---

## Collision Layers

Control which objects collide with which by assigning them to **collision layers**.

### Configuration (project.uzproj)

```toml
[physics]
layers = ["DEFAULT", "PLAYER", "ENEMY", "TRIGGER", "TERRAIN"]

# Collision matrix: layer X can collide with layer Y?
collision_matrix = {
    DEFAULT = ["DEFAULT", "PLAYER", "ENEMY", "TRIGGER"],
    PLAYER = ["DEFAULT", "ENEMY", "TERRAIN", "TRIGGER"],
    ENEMY = ["DEFAULT", "PLAYER", "TERRAIN"],
    TRIGGER = ["PLAYER", "ENEMY"],  # Triggers collide with actors
    TERRAIN = ["PLAYER", "ENEMY"]
}
```

### Assigning Objects to Layers

In `.uzscene`:
```ini
[component type=CRigidBody parent=2]
motion_type = "dynamic"
layer = "PLAYER"           ; This object is in PLAYER layer
mask = ["DEFAULT", "TERRAIN"]  ; Collides with these layers
```

Or in Lua:
```lua
ctx.physics:set_layer(node_ref, "ENEMY")
ctx.physics:set_mask(node_ref, {"DEFAULT", "TERRAIN"})
```

---

## Collision Callbacks

### Detect Collisions in Lua

**Listen to collision events:**
```lua
function M:on_start(ctx)
    ctx.events:subscribe("collision_enter", function(data)
        ctx.log("Collision: " .. data.a.name .. " hit " .. data.b.name)

        -- Example: damage on collision
        if data.b.name == "Player" then
            player:take_damage(data.contact.impact_velocity:length())
        end
    end)

    ctx.events:subscribe("collision_exit", function(data)
        ctx.log("Separation: " .. data.a.name .. " and " .. data.b.name)
    end)
end
```

**Collision event data:**
```lua
{
    a = node_ref,           -- One colliding object
    b = node_ref,           -- Other colliding object
    contact = {
        position = vec3(...),       -- Contact point
        normal = vec3(...),         -- Contact normal
        impact_velocity = vec3(...) -- Relative velocity
    }
}
```

---

## Trigger Volumes

Triggers are non-physical collision zones. Use them to detect when objects enter regions.

### Define a Trigger in Scene

```ini
[node id=5 type=Node3D name="DamageZone" parent=0]
position = [0, 0, 0]

[component type=CTrigger parent=5]
shape = "box"
size = [5, 5, 5]
```

### Respond to Triggers in Lua

```lua
function M:on_start(ctx)
    self.trigger = ctx.scene:find_node("DamageZone")

    ctx.events:subscribe("trigger_enter", function(data)
        if data.trigger == self.trigger then
            ctx.log(data.other.name .. " entered damage zone!")
            data.other:take_damage(10)  -- Assume the node has this method
        end
    end)

    ctx.events:subscribe("trigger_exit", function(data)
        if data.trigger == self.trigger then
            ctx.log(data.other.name .. " left damage zone")
        end
    end)
end
```

---

## Character Controller

A **character controller** is a special kinematic body designed for player movement. It handles climbing, falling, and ground detection automatically.

### Setup (Phase 3)

```ini
[node id=10 type=Node3D name="Player" parent=0]

[component type=CCharacterController parent=10]
radius = 0.4
height = 1.8
mass = 80.0
```

### Usage in Lua

```lua
local M = {}

function M:on_update(ctx, dt)
    -- Read input
    local move_x = ctx.input:axis("move_horizontal")
    local move_y = ctx.input:axis("move_vertical")

    -- Move horizontally (character controller handles ground collision)
    ctx.physics:move_character(self.node_ref, vec3(move_x, 0, move_y), dt)

    -- Jump (if grounded)
    if ctx.input:pressed("jump") and ctx.physics:is_grounded(self.node_ref) then
        ctx.physics:jump(self.node_ref, 5.0)  -- Jump velocity
    end

    -- Check if on ground
    if ctx.physics:is_grounded(self.node_ref) then
        ctx.log("Player on ground")
    end
end

return M
```

### Character Controller API

| Function | Args | Returns | Purpose |
|----------|------|---------|---------|
| `move_character` | `node, direction, dt` | — | Move character in direction |
| `jump` | `node, velocity` | — | Apply upward jump force |
| `is_grounded` | `node` | bool | Is character touching ground? |
| `get_velocity` | `node` | vec3 | Current velocity |

---

## Raycasting

Cast rays from the editor or scripts to detect what's in front of an object.

### Raycast from Lua

```lua
function M:on_update(ctx, dt)
    -- Cast ray from this node forward
    local origin = self.node_ref.position
    local direction = vec3(0, 0, -1)  -- Straight ahead
    local max_distance = 100.0

    local hit = ctx.physics:ray_cast({
        origin = origin,
        direction = direction,
        max_distance = max_distance
    })

    if hit then
        ctx.log("Hit: " .. hit.node.name .. " at distance " .. hit.distance)
        -- hit.position = contact point (vec3)
        -- hit.normal = surface normal (vec3)
        -- hit.distance = distance from origin (float)
    else
        ctx.log("No hit")
    end
end
```

### Overlap Queries

**Check if a sphere overlaps objects:**
```lua
local overlaps = ctx.physics:overlap_sphere({
    center = self.node_ref.position,
    radius = 5.0,
    layer_mask = {"ENEMY"}
})

-- overlaps = {node1, node2, node3, ...}
for i, enemy in ipairs(overlaps) do
    ctx.log("Overlapping: " .. enemy.name)
end
```

**Check if a box overlaps objects:**
```lua
local overlaps = ctx.physics:overlap_box({
    center = self.node_ref.position,
    size = [5, 5, 5],
    rotation = self.node_ref.rotation,
    layer_mask = {"DEFAULT"}
})
```

---

## Physics Queries from Editor

### Debug Visualization

Toggle physics wireframe in the editor:

```
uz_call("physics.debug_draw", {"enabled": true})
```

See all rigid bodies, colliders, and triggers rendered as wireframes.

### Test Raycasts

Test raycasts from the editor viewport:

```
uz_call("physics.ray_cast", {
    "origin": [0, 5, 0],
    "direction": [0, -1, 0],
    "max_dist": 100
})
```

Returns the first object hit.

### Modify Gravity

Override gravity for testing:

```
uz_call("physics.set_gravity", {"x": 0, "y": -20, "z": 0})
```

Useful for testing low-gravity or high-gravity scenarios without modifying the scene.

---

## Physics Performance Tips

### Use Appropriate Body Types

**Bad:**
```ini
[component type=CRigidBody parent=terrain]
motion_type = "dynamic"    ; Ground as dynamic? Slow!
```

**Good:**
```ini
[component type=CRigidBody parent=terrain]
motion_type = "static"     ; Ground never moves
```

### Use Simpler Shapes

**Slow:** Mesh colliders (per-triangle checks)
**Fast:** Spheres, boxes, capsules (analytical checks)

Prefer primitive shapes when possible.

### Reduce Collision Layers

**Bad:** Every object checks every other object
**Good:** Use collision layers so irrelevant objects skip checks

```toml
# Only check ENEMY vs PLAYER, skip everything else
collision_matrix = {
    PLAYER = ["TERRAIN", "ENEMY"],
    ENEMY = ["TERRAIN", "PLAYER"],
    TERRAIN = ["PLAYER", "ENEMY"]
}
```

### Batch Raycast Operations

**Bad:**
```lua
function M:on_update(ctx, dt)
    for i = 1, 100 do
        ctx.physics:ray_cast(...)  -- 100 raycasts per frame!
    end
end
```

**Good:**
```lua
function M:on_start(ctx)
    ctx.schedule_every(0.1, function()
        for i = 1, 100 do
            ctx.physics:ray_cast(...)  -- Every 100ms, not every frame
        end
    end)
end
```

---

## Common Patterns

### Bullet Raycast (Hitscan Weapon)

```lua
local M = {}

function M:fire(ctx)
    local origin = self.weapon_pos
    local direction = self.aim_direction
    local damage = 25

    local hit = ctx.physics:ray_cast({
        origin = origin,
        direction = direction,
        max_distance = 1000
    })

    if hit then
        -- Spawn impact effect
        ctx.vfx:spawn("res://vfx/bullet_impact.uzfx", {
            position = hit.position,
            rotation = quat.look_at(hit.normal)
        })

        -- Damage target
        hit.node:take_damage(damage)

        ctx.log("Hit " .. hit.node.name .. " for " .. damage .. " damage!")
    else
        ctx.log("Miss!")
    end
end

return M
```

### Explosion Damage (Overlap)

```lua
local M = {}

function M:explode(ctx, position, radius, damage)
    -- Find all entities in radius
    local hit_nodes = ctx.physics:overlap_sphere({
        center = position,
        radius = radius,
        layer_mask = {"DEFAULT", "ENEMY"}
    })

    for i, node in ipairs(hit_nodes) do
        -- Calculate distance-based damage falloff
        local distance = (node.position - position):length()
        local falloff = 1.0 - (distance / radius)
        local final_damage = damage * falloff

        node:take_damage(final_damage)

        -- Push away from explosion
        local push_dir = (node.position - position):normalize()
        ctx.physics:apply_force(node, push_dir * 50)
    end

    ctx.log("Explosion dealt damage to " .. #hit_nodes .. " objects")
end

return M
```

### Door Trigger with Collision

```lua
local M = {}

function M:on_start(ctx)
    self.is_open = false
    self.door_body = self.node_ref  -- Door has a rigid body

    ctx.events:subscribe("trigger_enter", function(data)
        if data.other.name == "Player" and not self.is_open then
            self:open(ctx)
        end
    end)
end

function M:open(ctx)
    self.is_open = true

    -- Animate door rotation
    ctx.schedule_over(1.0, function(progress)
        local rotation = quat.from_euler(0, math.pi/2 * progress, 0)
        self.node_ref.rotation = rotation
    end)
end

return M
```

### Knockback Effect

```lua
function apply_knockback(ctx, node, direction, force)
    local body = ctx.physics:get_rigidbody(node)
    if body then
        ctx.physics:apply_impulse(node, direction:normalize() * force)
    end
end

-- Usage: explosion pushes nearby objects
local direction = (victim.position - explosion_pos):normalize()
apply_knockback(ctx, victim, direction, 50)
```

---

## Debugging Physics

### Enable Debug Visualization

```
uz_call("physics.debug_draw", {"enabled": true})
```

See:
- Static bodies (gray)
- Dynamic bodies (blue)
- Kinematic bodies (green)
- Triggers (yellow wireframe)
- Contacts (red points)

### Log Physics Events

```lua
function M:on_start(ctx)
    ctx.events:subscribe("collision_enter", function(data)
        ctx.log("COLLISION: " .. data.a.name .. " <-> " .. data.b.name)
        ctx.log("  Position: " .. tostring(data.contact.position))
        ctx.log("  Velocity: " .. tostring(data.contact.impact_velocity))
    end)
end
```

### Inspect Rigidbody Properties

```
uz_call("physics.inspect_body", {"node": "Player"})
→ {
    motion_type: "dynamic",
    mass: 80,
    velocity: [0, -9.81, 0],
    angular_velocity: [0, 0, 0],
    ...
}
```

---

## Related Documentation

- [Lua Scripting Guide](lua-scripting-guide.md) — Script physics interactions
- [Scene System Spec](../specs/scene-system.md) (Phase 1) — Define .uzscene
- [Engine Vision](../specs/engine-vision.md) — Jolt integration details

---

## FAQ

**Q: What's the difference between rigid bodies and colliders?**
A: A rigid body is the physics object that moves and responds to forces. A collider is its shape. You need both for physics to work.

**Q: Can I have multiple colliders on one body?**
A: Yes! Add multiple `CCollider` components to the same node. Jolt combines them into a compound shape.

**Q: How do I make an object ignore gravity?**
A: Set gravity scale to 0: `gravity_scale = 0.0` in the `CRigidBody` component.

**Q: Why does my character fall through the ground?**
A: The ground is probably not static. Make sure the ground has `motion_type = "static"` and its collider shape matches its visual mesh.

**Q: Can I use mesh colliders for complex shapes?**
A: Yes, but they're slower. Prefer convex shapes. For terrain, use heightfield colliders.

**Q: How do I rotate a rigid body?**
A: Use `apply_torque()` or `apply_angular_impulse()` in Lua. Or set `node.rotation` for kinematic bodies.
