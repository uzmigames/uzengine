# Scripting — Lua API Reference

> **Status**: Active (Phase 1 Complete — Core APIs; Phase 2+ for extended bindings)
> **Source**: `engine/scripting/lua_*_api.cpp`, `lua_vm.cpp` (registration)
> **Depends on**: LuaVM (§15.2), subsystems (§9: Input, Camera, Scene/ECS)
> **Implements**: Core APIs (uz.log, uz.scene, uz.camera, uz.input, uz.delta_time, uz.total_time)

## Purpose

The Lua API is the complete set of functions and classes available to scripts. This reference covers the main bindings. See individual subsystem docs for detailed behavior.

## Core Types

### Vec3 (Vector)

3-component floating-point vector.

```lua
local v = uz.Vec3.new(1, 2, 3)
local v2 = uz.Vec3(4, 5, 6)  -- Shorthand

-- Properties
print(v.x, v.y, v.z)

-- Operators
local sum = v + v2
local scaled = v * 2.0
local dot = uz.Vec3.dot(v, v2)
local cross = uz.Vec3.cross(v, v2)

-- Methods
local len = v:length()
local normalized = v:normalize()
local lerped = uz.Vec3.lerp(v, v2, 0.5)
```

### Quat (Quaternion)

Rotation representation.

```lua
local q = uz.Quat.new(0, 0, 0, 1)  -- (x, y, z, w)
local q2 = uz.Quat.from_axis_angle({1, 0, 0}, math.rad(90))

-- Methods
local rotated_vec = q:rotate_vector(uz.Vec3(1, 0, 0))
local slerped = uz.Quat.slerp(q, q2, 0.5)
local conjugate = q:conjugate()
```

### Transform

Position + rotation + scale.

```lua
local t = uz.Transform.new()
t.position = uz.Vec3(100, 50, 200)
t.rotation = uz.Quat.from_axis_angle({0, 1, 0}, math.rad(45))
t.scale = uz.Vec3(1, 1, 1)

-- Inverse transform
local t_inv = t:inverse()

-- Composition
local t3 = uz.Transform.compose(t, t2)
```

## World and Entities

### World

```lua
-- Access current world
local world = ctx.world

-- Spawn entity
local entity = world:spawn_entity("Player")
local entity = world:spawn_entity_from_class("Character", {
    position = uz.Vec3(0, 0, 0),
    rotation = uz.Quat.identity(),
})

-- Query entities
local by_name = world:find_entity_by_name("NPC_001")
local all_entities = world:get_all_entities()

-- Destroy entity
world:destroy_entity(entity)

-- World state
local entity_count = world:get_entity_count()
```

### Entity

```lua
-- Properties
local name = entity:get_name()
entity:set_name("NewName")

-- Transform
local pos = entity:get_position()
entity:set_position(uz.Vec3(100, 50, 200))

local rot = entity:get_rotation()
entity:set_rotation(uz.Quat.from_axis_angle({0, 1, 0}, math.rad(45)))

local scale = entity:get_scale()
entity:set_scale(uz.Vec3(2, 2, 2))

-- Hierarchy
entity:set_parent(parent_entity)
local parent = entity:get_parent()
local children = entity:get_children()

-- Components
local mesh = entity:get_component("StaticMeshComponent")
if mesh then
    mesh:set_material("material_path")
end

-- Metadata
entity:set_tag("Enemy")
local has_tag = entity:has_tag("Enemy")

-- Active state
entity:set_active(true)
local is_active = entity:is_active()
```

## Input

### Input System

```lua
-- Keyboard
if ctx.input:is_key_down("w") then
    -- W is held
end

if ctx.input:is_key_pressed("space") then
    -- Space just pressed
end

if ctx.input:is_key_released("escape") then
    -- Escape just released
end

-- Mouse
local mouse_pos = ctx.input:get_mouse_position()  -- {x, y}
local mouse_delta = ctx.input:get_mouse_delta()   -- {x, y}
local scroll = ctx.input:get_mouse_scroll()       -- float

-- Gamepad
local stick_x = ctx.input:get_gamepad_axis("left_stick_x")
local stick_y = ctx.input:get_gamepad_axis("left_stick_y")

-- Event subscriptions
ctx.input:on_key_press("e", function()
    print("E key pressed")
end)

ctx.input:on_mouse_click("left", function(pos)
    print("Left click at", pos)
end)
```

## Physics

### Physics System

```lua
-- Query
local hit = ctx.physics:ray_cast(from_pos, to_pos, {
    channel = "Pawn",
    ignore_entity = ctx.entity,
})

if hit:is_hit() then
    print("Hit entity:", hit:get_entity():get_name())
    print("Distance:", hit:get_distance())
    print("Impact point:", hit:get_impact_point())
    print("Impact normal:", hit:get_impact_normal())
end

-- Overlap
local overlapping = ctx.physics:overlap_sphere(center, radius, {
    channel = "Trigger",
})
for _, entity in ipairs(overlapping) do
    print("Overlapping:", entity:get_name())
end

-- Set gravity
ctx.physics:set_gravity(uz.Vec3(0, -9.81, 0))

-- Debug visualization
ctx.physics:debug_draw_line(from, to, uz.Vec3(1, 0, 0))  -- Red line
```

### Physics Component

```lua
local physics = entity:get_component("RigidBodyComponent")

-- Properties
physics:set_mass(100.0)
physics:set_velocity(uz.Vec3(10, 0, 0))
physics:add_velocity(uz.Vec3(0, 50, 0))  -- Impulse

-- Collision
physics:set_collision_enabled(true)
physics:set_collision_channel("Pawn")
physics:set_collision_response("Trigger", "Ignore")  -- Don't collide with triggers

-- Queries
local velocity = physics:get_velocity()
local kinetic_energy = physics:get_kinetic_energy()

-- Events
physics:on_collision_begin(function(other_entity)
    print("Collided with:", other_entity:get_name())
end)

physics:on_collision_end(function(other_entity)
    print("Stopped colliding with:", other_entity:get_name())
end)
```

## Audio

### Audio System

```lua
-- Play sounds
ctx.audio:play_sound_2d("content/audio/jump.wav", {
    volume = 1.0,
    pitch = 1.0,
})

ctx.audio:play_sound_3d("content/audio/explosion.wav", location, {
    volume = 1.0,
    inner_radius = 500,
    outer_radius = 2000,
    falloff = 1.0,
})

-- Stop all sounds
ctx.audio:stop_all_sounds()

-- Volume control
ctx.audio:set_master_volume(0.8)
ctx.audio:set_bus_volume("Ambient", 0.5)

-- Query
local voice_count = ctx.audio:get_active_voice_count()
```

## Camera

### Camera System

```lua
-- Active camera
local camera = ctx.camera:get_active_camera()

-- Properties
camera:set_fov(90.0)
camera:set_near_plane(0.1)
camera:set_far_plane(10000.0)

-- Shake
ctx.camera:play_shake({
    duration = 0.5,
    blend_in = 0.1,
    blend_out = 0.2,
    location_y = {amplitude = 1.0, frequency = 8.0},
    rotation_yaw = {amplitude = 2.0, frequency = 8.0},
})
```

## UI

### UI System (Phase 3+)

```lua
-- Create text
ctx.ui:create_text({
    text = "Hello World",
    position = {x = 100, y = 100},
    color = {r = 1, g = 1, b = 1},
    size = 32,
})

-- Create button
ctx.ui:create_button({
    text = "Click Me",
    position = {x = 200, y = 200},
    on_click = function()
        print("Button clicked!")
    end,
})

-- Create panel
ctx.ui:create_panel({
    position = {x = 0, y = 0},
    size = {width = 800, height = 600},
    background_color = {r = 0.1, g = 0.1, b = 0.1},
})
```

## Scene and Levels

### World State

```lua
-- Current level/scene
local scene_name = ctx.world:get_scene_name()

-- Load new scene (resets world)
ctx.world:load_scene("content/scenes/level_2.uzscene")

-- Pause/resume game
ctx.world:pause()
ctx.world:resume()
local paused = ctx.world:is_paused()

-- Time management
local delta_time = ctx.world:get_delta_time()
local total_time = ctx.world:get_total_time()
ctx.world:set_timescale(0.5)  -- Slow motion
```

## Logging

### Logging API

```lua
-- Log with category and level
ctx.log("Player took damage", "Gameplay", "Info")
ctx.log_warning("Enemy AI failed to find path")
ctx.log_error("Script error: undefined variable")
ctx.log_debug("Position update: x=" .. pos.x)
```

## Events

### Event System

```lua
-- Subscribe to custom events
ctx.world:subscribe("PlayerDied", function(player_entity)
    print("Player died!")
    local camera = ctx.camera:get_active_camera()
    camera:play_shake({duration = 1.0})
end)

-- Fire custom events
ctx.world:fire_event("PlayerDied", ctx.entity)

-- Built-in events
ctx.world:on("EntityCreated", function(entity)
    print("Entity created:", entity:get_name())
end)

ctx.world:on("EntityDestroyed", function(entity)
    print("Entity destroyed:", entity:get_name())
end)
```

## Timers and Scheduling

### Async/Delayed Execution (Phase 3.5)

```lua
-- Call function after delay
ctx.world:schedule_after(2.0, function()
    print("Called after 2 seconds")
end)

-- Call function repeatedly
local timer_id = ctx.world:schedule_every(0.5, function()
    print("Called every 0.5 seconds")
end)

-- Cancel timer
ctx.world:cancel_timer(timer_id)

-- Coroutines (Lua native)
function async_example(ctx)
    local coro = coroutine.create(function()
        coroutine.yield()  -- Wait 1 frame
        print("Frame 2")
        coroutine.yield()
        print("Frame 3")
    end)

    coroutine.resume(coro)  -- Run 1 step per frame
end
```

## Complete Example Script

```lua
-- scripts/player_controller.lua

local move_speed = 500
local jump_force = 1000
local grounded = false
local current_health = 100

function on_start(ctx)
    ctx.log("Player controller started", "Gameplay")

    -- Subscribe to input
    ctx.input:on_key_press("space", function()
        if grounded then jump(ctx) end
    end)

    -- Subscribe to damage event
    ctx.world:subscribe("TakeDamage", function(amount)
        current_health = current_health - amount
        ctx.log("Player took " .. amount .. " damage, health=" .. current_health)
        if current_health <= 0 then
            on_death(ctx)
        end
    end)
end

function on_update(ctx, dt)
    local entity = ctx.entity
    local input = ctx.input

    -- Movement input
    local move_dir = uz.Vec3.new(0, 0, 0)
    if input:is_key_down("w") then move_dir = move_dir + uz.Vec3(0, 0, 1) end
    if input:is_key_down("a") then move_dir = move_dir + uz.Vec3(-1, 0, 0) end
    if input:is_key_down("s") then move_dir = move_dir + uz.Vec3(0, 0, -1) end
    if input:is_key_down("d") then move_dir = move_dir + uz.Vec3(1, 0, 0) end

    -- Apply movement
    if move_dir:length() > 0 then
        move_dir = move_dir:normalize()
        local pos = entity:get_position()
        pos = pos + move_dir * move_speed * dt
        entity:set_position(pos)
    end

    -- Check if grounded
    local pos = entity:get_position()
    local ground_check = ctx.physics:ray_cast(
        pos,
        pos + uz.Vec3(0, -100, 0)
    )
    grounded = ground_check:is_hit()

    -- Camera shake on land
    if ground_check:is_hit() and not was_grounded then
        ctx.camera:play_shake({duration = 0.2})
    end
    was_grounded = grounded
end

function on_stop(ctx)
    ctx.log("Player controller stopped", "Gameplay")
end

function jump(ctx)
    local entity = ctx.entity
    local physics = entity:get_component("RigidBodyComponent")
    if physics then
        local vel = physics:get_velocity()
        physics:set_velocity(uz.Vec3(vel.x, jump_force, vel.z))
    end
end

function on_death(ctx)
    ctx.log("Player died!", "Gameplay")
    ctx.world:fire_event("PlayerDied", ctx.entity)
    ctx.world:schedule_after(2.0, function()
        ctx.world:load_scene("content/scenes/main_menu.uzscene")
    end)
end
```

## Engine Comparison

| API Category | UE5 (Blueprint) | Godot GDScript | Unity C# | **UzEngine Lua** |
|-------------|-----------------|-----------------|----------|-----------------|
| Vectors | FVector | Vector3 | Vector3 | uz.Vec3 |
| Entities | AActor | Node | GameObject | uz.Entity |
| Physics | FHitResult | RayQueryParameters | RaycastHit | uz.Physics |
| Input | APlayerController | Input | Input | ctx.input |
| Audio | UAudioComponent | AudioStreamPlayer | AudioSource | ctx.audio |
| Logging | UE_LOG | print() | Debug.Log | ctx.log |

## Rules

1. **ALWAYS check return values** — ray_cast, overlap_sphere may return nil/empty
2. **NEVER store entity references in globals** — entities may be destroyed
3. **ALWAYS clean up event subscriptions** — manually unsubscribe in on_stop
4. **NEVER call blocking operations** — schedules/await are non-blocking
5. **ALWAYS handle nil gracefully** — missing components return nil
