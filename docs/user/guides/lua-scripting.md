# Lua Scripting Guide

> **Status**: Phase 1 Complete — LuaJIT 2.1 core scripting system is functional and tested.
> **Location**: `docs/engine/15-scripting/` for full technical reference

## What is Lua Scripting?

Lua is an embedded scripting language that lets you write game logic without recompiling the engine. Scripts are hot-reloadable — modify a `.lua` file and the changes are instantly reflected in the editor.

**UzEngine uses LuaJIT 2.1** — a just-in-time compiled Lua dialect that's nearly as fast as C++ for most tasks.

## Quick Start

### Create Your First Script

1. Create a file: `sample/content/scripts/hello.lua`

```lua
function on_start(entity)
    uz.log("Hello from Lua!")
end

function on_update(entity, dt)
    uz.log("Frame " .. uz.total_time)
end

function on_stop(entity)
    uz.log("Script stopped")
end
```

2. Attach the script to an entity in a `.uzscene` file:

```json
{
    "id": "my_entity",
    "components": {
        "SceneComponent": { "position": [0, 0, 0] },
        "ScriptComponent": { "script": "scripts/hello.lua" }
    }
}
```

3. Run the engine — you'll see the log messages.

4. Edit the script and save — it reloads instantly without losing your viewport camera or world state.

### Script Lifecycle

Every script has three functions:

```lua
function on_start(entity)
    -- Called once when the entity is created or the script loads
    -- Initialize state, set up listeners
end

function on_update(entity, dt)
    -- Called every frame
    -- dt = time since last frame (float, seconds)
    -- Handle input, move objects, etc.
end

function on_stop(entity)
    -- Called when the entity is destroyed or script unloads
    -- Clean up resources
end
```

## Common Tasks

### Moving a Camera

Use `uz.camera` to position and orient the view:

```lua
function on_update(entity, dt)
    local speed = 10.0 * dt

    -- Move camera
    local cam_x, cam_y, cam_z = 0, -5, 2
    if uz.input.is_key_down("W") then
        cam_z = cam_z + speed
    end
    if uz.input.is_key_down("S") then
        cam_z = cam_z - speed
    end

    -- Set camera position and look direction
    uz.camera.set_position(cam_x, cam_y, cam_z)
    uz.camera.look_at(cam_x, cam_y + 5, cam_z)
end
```

### Reading Input

Use `uz.input` to query keyboard and mouse state:

```lua
function on_update(entity, dt)
    -- Keyboard: state and state-change queries
    if uz.input.is_key_down("W") then
        -- W is held
    end

    if uz.input.is_key_just_pressed("Space") then
        -- Space was just pressed (this frame only)
    end

    -- Mouse: delta and scroll
    local dx, dy = uz.input.get_mouse_delta()
    local sx, sy = uz.input.get_scroll_delta()

    -- Mouse buttons
    if uz.input.is_mouse_button_down("Left") then
        -- Left click is held
    end
end
```

### Finding and Rotating Entities

Use `uz.scene` to query and manipulate scene objects:

```lua
function on_start(entity)
    -- Get all entities
    local all = uz.scene.get_entities()

    for i = 1, #all do
        local e = all[i]
        local name = uz.scene.get_name(e)
        uz.log("Entity: " .. name)
    end
end

function on_update(entity, dt)
    -- Rotate an entity
    local pitch, yaw, roll = uz.scene.get_rotation(entity)
    roll = roll + 30.0 * dt  -- 30 degrees per second
    uz.scene.set_rotation(entity, pitch, yaw, roll)
end
```

### Changing Render Settings

Use `uz.renderer` to control the render pipeline and debug visualization:

```lua
function on_update(entity, dt)
    -- Toggle render path with F key
    if uz.input.is_key_just_pressed("F") then
        uz.renderer.set_render_path("forward")
        uz.log("Switched to forward rendering")
    end

    -- Cycle debug visualization with Tab
    if uz.input.is_key_just_pressed("Tab") then
        uz.renderer.set_debug_view("Albedo")  -- Show base color
    end
end
```

### Adjusting Console Variables (CVars)

Use `uz.cvar` to query and modify render settings at runtime:

```lua
function on_update(entity, dt)
    -- Get current values
    local bloom_enabled = uz.cvar.get("r.Bloom")
    local exposure = uz.cvar.get("r.Exposure")
    uz.log("Current exposure: " .. exposure)

    -- Set values
    if uz.input.is_key_just_pressed("B") then
        uz.cvar.set("r.Bloom", bloom_enabled == 1 and 0 or 1)
    end

    if uz.input.is_key_just_pressed("E") then
        uz.cvar.set("r.Exposure", uz.cvar.get("r.Exposure") + 0.1)
    end

    -- Reset to defaults
    if uz.input.is_key_just_pressed("R") then
        uz.cvar.reset("r.Bloom")
        uz.cvar.reset_all()  -- reset all CVars
    end

    -- Query CVar metadata
    local info = uz.cvar.info("r.Bloom.Intensity")
    if info then
        uz.log("CVar: " .. info.name .. " = " .. info.value .. " (default: " .. info.default .. ")")
    end

    -- List all CVars by prefix
    local bloom_vars = uz.cvar.list("r.Bloom")
    for i = 1, #bloom_vars do
        uz.log("Found: " .. bloom_vars[i])
    end
end
```

### Quitting the Application

```lua
function on_update(entity, dt)
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
    end
end
```

## Coordinate System

UzEngine uses **Z-up** coordinates:

- **X** = forward direction
- **Y** = right direction
- **Z** = up direction

When writing camera controllers or entity movement, keep this in mind:

```lua
-- Z-up FPS camera
function on_update(entity, dt)
    local speed = 5.0 * dt

    -- Move on horizontal plane (XY)
    if uz.input.is_key_down("W") then
        cam_x = cam_x + speed  -- Move forward
    end
    if uz.input.is_key_down("D") then
        cam_y = cam_y + speed  -- Move right
    end

    -- Move vertically (Z)
    if uz.input.is_key_down("E") then
        cam_z = cam_z + speed  -- Move up
    end

    uz.camera.set_position(cam_x, cam_y, cam_z)
end
```

## Timing

Access frame timing with globals:

```lua
function on_update(entity, dt)
    -- dt = time since last frame (seconds)
    -- uz.total_time = time since engine start (seconds)

    uz.log("FPS: " .. math.floor(1.0 / dt))

    -- Quit after 30 seconds
    if uz.total_time > 30.0 then
        uz.quit()
    end
end
```

## Best Practices

### 1. Keep Scripts Stateless

Scripts are **hot-reloaded**, meaning local variables are NOT preserved across edits. Design scripts that reinitialize state in `on_start()`:

```lua
-- ✅ GOOD: State initialized in on_start
local my_value = 0

function on_start(entity)
    my_value = 0  -- Reinitialize after hot-reload
end

function on_update(entity, dt)
    my_value = my_value + 1
end
```

```lua
-- ❌ BAD: Relies on state persisting across hot-reload
local my_value = 0

function on_update(entity, dt)
    my_value = my_value + 1  -- Lost on hot-reload!
end
```

### 2. Use Euler Angles

Rotations are in **Euler angles** (pitch, yaw, roll), not quaternions. All in **degrees**, not radians:

```lua
-- ✅ GOOD: Degrees
uz.scene.set_rotation(entity, 0, 90, 0)  -- 90° yaw

-- ❌ BAD: Radians won't work as expected
uz.scene.set_rotation(entity, 0, math.pi / 2, 0)
```

### 3. Check Input Every Frame

Input state is only valid during `on_update()`. If you need to respond to input, query it there:

```lua
-- ✅ GOOD: Check input in on_update
function on_update(entity, dt)
    if uz.input.is_key_just_pressed("Space") then
        uz.log("Space pressed")
    end
end

-- ❌ BAD: on_start only runs once
function on_start(entity)
    if uz.input.is_key_just_pressed("Space") then
        -- This won't work as expected
    end
end
```

### 4. Use Logging for Debugging

```lua
uz.log("Position: " .. cam_x .. ", " .. cam_y .. ", " .. cam_z)
uz.log("Rotation yaw: " .. yaw)
uz.log("Input delta: " .. dx .. ", " .. dy)
```

### 5. Handle Nil Values

Scene queries may return nil if entities don't exist:

```lua
-- ✅ GOOD: Check for nil
function on_update(entity, dt)
    local all = uz.scene.get_entities()
    for i = 1, #all do
        local e = all[i]
        local name = uz.scene.get_name(e)
        if name then
            uz.log("Entity: " .. name)
        end
    end
end

-- ❌ BAD: Assuming all queries succeed
function on_update(entity, dt)
    local name = uz.scene.get_name(entity)
    uz.log("Name: " .. name)  -- Crashes if name is nil
end
```

## API Reference

For complete API documentation, see:

- **Global Functions**: `uz.log()`, `uz.quit()`, `uz.delta_time`, `uz.total_time`
- **Camera**: `uz.camera.set_position()`, `uz.camera.look_at()`
- **Input**: `uz.input.is_key_down()`, `uz.input.is_key_just_pressed()`, `uz.input.get_mouse_delta()`, etc.
- **Scene**: `uz.scene.get_entities()`, `uz.scene.get_name()`, `uz.scene.get_rotation()`, `uz.scene.set_rotation()`
- **Scene I/O**: `uz.scene.load_scene()`, `uz.scene.save_scene()`
- **Materials**: `uz.scene.set_material()`, `uz.scene.get_material()` — set/query material properties (albedo, metallic, roughness, shading models, etc.)
- **Renderer**: `uz.renderer.set_render_path()`, `uz.renderer.set_debug_view()`
- **CVars**: `uz.cvar.get()`, `uz.cvar.set()`, `uz.cvar.reset()`, `uz.cvar.reset_all()`, `uz.cvar.info()`, `uz.cvar.list()` — query and modify console variables at runtime

See [docs/engine/15-scripting/06-lua-api.md](../../engine/15-scripting/06-lua-api.md) for the full reference with examples.

## Example: FPS Camera Controller

Here's a complete FPS camera script that you can use as a starting point:

```lua
-- scripts/fps_camera.lua
-- First-person camera with WASD movement and mouse look

local camera_speed = 5.0
local mouse_sensitivity = 0.15
local yaw = 90.0
local pitch = -15.0
local cam_x, cam_y, cam_z = 0, -8, 3

function on_start(entity)
    uz.log("FPS Camera started")
    uz.log("WASD: move | QE: up/down | Right-click+drag: look | Escape: quit")
end

function on_update(entity, dt)
    -- Quit
    if uz.input.is_key_just_pressed("Escape") then
        uz.quit()
        return
    end

    -- Mouse look (when right button held)
    if uz.input.is_mouse_button_down("Right") then
        local dx, dy = uz.input.get_mouse_delta()
        yaw = yaw + dx * mouse_sensitivity
        pitch = pitch - dy * mouse_sensitivity
        if pitch > 89 then pitch = 89 end
        if pitch < -89 then pitch = -89 end
    end

    -- Calculate movement direction
    local rad = math.pi / 180.0
    local cy = math.cos(yaw * rad)
    local sy = math.sin(yaw * rad)
    local cp = math.cos(pitch * rad)
    local sp = math.sin(pitch * rad)

    local fwd_x = cy * cp
    local fwd_y = sy * cp
    local fwd_z = sp

    local right_x = -sy
    local right_y = cy

    local speed = camera_speed * dt

    -- Movement (WASD)
    if uz.input.is_key_down("W") then
        cam_x = cam_x + fwd_x * speed
        cam_y = cam_y + fwd_y * speed
        cam_z = cam_z + fwd_z * speed
    end
    if uz.input.is_key_down("S") then
        cam_x = cam_x - fwd_x * speed
        cam_y = cam_y - fwd_y * speed
        cam_z = cam_z - fwd_z * speed
    end
    if uz.input.is_key_down("A") then
        cam_x = cam_x - right_x * speed
        cam_y = cam_y - right_y * speed
    end
    if uz.input.is_key_down("D") then
        cam_x = cam_x + right_x * speed
        cam_y = cam_y + right_y * speed
    end

    -- Vertical (QE)
    if uz.input.is_key_down("E") then
        cam_z = cam_z + speed
    end
    if uz.input.is_key_down("Q") then
        cam_z = cam_z - speed
    end

    -- Apply camera
    uz.camera.set_position(cam_x, cam_y, cam_z)

    local look_dist = 10.0
    uz.camera.look_at(
        cam_x + fwd_x * look_dist,
        cam_y + fwd_y * look_dist,
        cam_z + fwd_z * look_dist
    )
end

function on_stop(entity)
    uz.log("FPS Camera stopped")
end
```

## Troubleshooting

### Script Won't Load

- Check the file path in `ScriptComponent` — it's relative to the project's `content/` directory
- Example: Script at `sample/content/scripts/hello.lua` should be specified as `scripts/hello.lua`

### Changes Don't Take Effect

- Save the file — hot-reload is triggered by file modifications
- Check the console for Lua errors
- Local variables don't persist across hot-reload — use `on_start()` to reinitialize

### Input Not Working

- Input state is only valid in `on_update()` — don't call input functions from `on_start()`
- Key names are lowercase: `"w"`, not `"W"` or `"KEY_W"`

### Camera Not Responding

- Remember the Z-up coordinate system — X is forward, Y is right, Z is up
- Always call `uz.camera.set_position()` after calculating movement
- Use `uz.camera.look_at()` to point the camera at a target

## Next Steps

- Read the full [API reference](../../engine/15-scripting/06-lua-api.md)
- Explore the [sample scripts](https://github.com/uzbuild/uz-engine/tree/main/sample/content/scripts/) in the repository
- Check out [engine/15-scripting/](../../engine/15-scripting/) for technical internals
