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

## Global Functions

### Engine Lifecycle

```lua
-- Quit the application
uz.quit()

-- Logging
uz.log("Message to output")

-- Frame timing
local dt = uz.delta_time        -- Time since last frame (float, seconds)
local t = uz.total_time         -- Total elapsed time since launch (float, seconds)

-- Example: Quit after 5 seconds
function on_update(entity, dt)
    if uz.total_time > 5.0 then
        uz.quit()
    end
end
```

### Global API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.quit()` | — | — | Exit the application |
| `uz.log(msg)` | msg: string | — | Log a message to the console |
| `uz.delta_time` | — | float | Elapsed time since last frame (seconds) |
| `uz.total_time` | — | float | Total elapsed time since engine start (seconds) |

## Scene and Entities

### Scene Queries

```lua
-- Get all entities in the scene
local all_entities = uz.scene.get_entities()
for i = 1, #all_entities do
    local entity = all_entities[i]
    uz.log("Entity: " .. uz.scene.get_name(entity))
end

-- Query by name
local rotation = uz.scene.get_rotation(entity)  -- (pitch, yaw, roll)
uz.scene.set_rotation(entity, pitch, yaw, roll)

-- Get entity name
local name = uz.scene.get_name(entity)
```

### Scene API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.scene.get_entities()` | — | table of entities | Returns all entities currently in the scene |
| `uz.scene.get_name(entity)` | entity | string | Get entity's display name |
| `uz.scene.get_rotation(entity)` | entity | (pitch, yaw, roll) | Get rotation in Euler angles (degrees) |
| `uz.scene.set_rotation(entity, pitch, yaw, roll)` | entity, pitch, yaw, roll | — | Set rotation in Euler angles (degrees) |

### Entity Operations

The scene API focuses on direct entity queries. For component-level access, use the entity directly in your scripts:

```lua
-- Example: Finding and operating on entities
function on_start(entity)
    local all = uz.scene.get_entities()
    for i = 1, #all do
        local e = all[i]
        local name = uz.scene.get_name(e)
        if name and name:find("Cube_") then
            -- Found a cube, modify it
            uz.scene.set_rotation(e, 0, 0, 0)
        end
    end
end
```

### Environment and Lighting API (v0.2.26+)

Environment API provides control over light properties, atmospheric effects, and sky lighting — essential for dynamic scene lighting adjustments.

#### Light Temperature & Scaling

```lua
-- Set light color temperature (blackbody Kelvin value)
uz.scene.set_light_temperature(entity, 6500)  -- D65 daylight white
uz.scene.set_light_temperature(entity, 3000)  -- Warm tungsten light

-- Get temperature setting
local kelvin, is_enabled = uz.scene.get_light_temperature(entity)

-- Light contribution multipliers
uz.scene.set_light_specular_scale(entity, 1.5)  -- Brighten highlights
uz.scene.set_light_diffuse_scale(entity, 0.8)   -- Reduce diffuse contribution
```

#### Point/Spot Light Falloff

```lua
-- Control light attenuation curve
uz.scene.set_light_falloff(entity, true, 2.0)   -- Inverse square (physically-based)
uz.scene.set_light_falloff(entity, false, 8.0)  -- Custom exponent falloff

-- Area light approximation (sphere radius)
uz.scene.set_light_source_radius(entity, 0.5)  -- 0.5m sphere light
```

#### Atmosphere Link

```lua
-- Link directional light to sky atmosphere as the sun
uz.scene.set_atmosphere_sun_light(entity, true)  -- This light drives the sky
```

#### Height Fog

```lua
-- Configure height-based volumetric fog
uz.scene.set_fog_density(entity, 0.01)          -- Fog density
uz.scene.set_fog_color(entity, 0.8, 0.85, 0.95) -- Fog color (linear RGB)
uz.scene.set_fog_height_falloff(entity, 0.2)    -- Exponential height falloff
uz.scene.set_fog_base_height(entity, 100.0)     -- Fog base height in meters
uz.scene.set_fog_max_opacity(entity, 1.0)       -- Maximum opacity (0-1)
uz.scene.set_fog_enabled(entity, true)          -- Toggle fog on/off
```

#### Sky Light (IBL Ambient)

```lua
-- Add sky light component for ambient lighting
uz.scene.create_sky_light(entity)

-- Configure sky light intensity and color
uz.scene.set_sky_light_intensity(entity, 1.0)         -- Intensity multiplier
uz.scene.set_sky_light_color(entity, 1.0, 1.0, 1.0)   -- Tint color (linear RGB)
uz.scene.set_sky_light_min_occlusion(entity, 0.1)     -- Minimum ambient occlusion (floor)
```

#### Sky Atmosphere Artistic Parameters

```lua
-- Artistic control over atmosphere scattering
uz.scene.set_sky_rayleigh_scale(entity, 1.0)      -- Rayleigh (molecular) scattering
uz.scene.set_sky_mie_scale(entity, 1.0)           -- Mie (aerosol) scattering
uz.scene.set_sky_multi_scattering(entity, 1.0)    -- Multiple scattering approximation
```

#### Environment API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.scene.set_light_temperature(entity, kelvin, use_temperature)` | entity, kelvin: float, use_temperature: bool (opt, default true) | — | Set blackbody color temperature |
| `uz.scene.get_light_temperature(entity)` | entity | (kelvin: float, use_temperature: bool) | Get temperature setting |
| `uz.scene.set_light_specular_scale(entity, scale)` | entity, scale: float (0-1) | — | Per-light specular highlight multiplier |
| `uz.scene.set_light_diffuse_scale(entity, scale)` | entity, scale: float (0-1) | — | Per-light diffuse multiplier |
| `uz.scene.set_light_falloff(entity, use_inverse_squared, exponent)` | entity, use_inverse_squared: bool, exponent: float | — | Set light attenuation curve |
| `uz.scene.set_light_source_radius(entity, radius)` | entity, radius: float (meters) | — | Area light sphere radius |
| `uz.scene.set_atmosphere_sun_light(entity, enabled)` | entity, enabled: bool | — | Link directional light to sky as the sun |
| `uz.scene.set_fog_density(entity, density)` | entity, density: float | — | Set height fog density |
| `uz.scene.set_fog_color(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Set fog color (linear RGB) |
| `uz.scene.set_fog_height_falloff(entity, falloff)` | entity, falloff: float | — | Exponential height falloff curve |
| `uz.scene.set_fog_base_height(entity, height)` | entity, height: float (meters) | — | Fog base altitude |
| `uz.scene.set_fog_max_opacity(entity, opacity)` | entity, opacity: float (0-1) | — | Maximum fog opacity |
| `uz.scene.set_fog_enabled(entity, enabled)` | entity, enabled: bool | — | Toggle fog on/off |
| `uz.scene.create_sky_light(entity)` | entity | — | Add SkyLightComponent |
| `uz.scene.set_sky_light_intensity(entity, intensity)` | entity, intensity: float | — | Set IBL ambient intensity |
| `uz.scene.set_sky_light_color(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Set sky light tint (linear RGB) |
| `uz.scene.set_sky_light_min_occlusion(entity, min_occlusion)` | entity, min_occlusion: float (0-1) | — | Minimum ambient occlusion floor |
| `uz.scene.set_sky_rayleigh_scale(entity, scale)` | entity, scale: float | — | Rayleigh scattering artistic multiplier |
| `uz.scene.set_sky_mie_scale(entity, scale)` | entity, scale: float | — | Mie scattering artistic multiplier |
| `uz.scene.set_sky_multi_scattering(entity, factor)` | entity, factor: float | — | Multiple scattering approximation |

## Input

### Input System

```lua
-- Keyboard — Key state queries
if uz.input.is_key_down("w") then
    -- W is currently held down
end

if uz.input.is_key_just_pressed("space") then
    -- Space was just pressed (this frame only)
end

-- Mouse input
local dx, dy = uz.input.get_mouse_delta()  -- Movement since last frame
local sx, sy = uz.input.get_scroll_delta() -- Scroll delta (x, y)

-- Mouse button queries
if uz.input.is_mouse_button_down("Right") then
    -- Right mouse button is held
end

-- Gamepad (planned Phase 2+)
-- local stick_x = uz.input.get_gamepad_axis("left_stick_x")
```

### Input API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.input.is_key_down(key)` | key: string | bool | True while key is held |
| `uz.input.is_key_just_pressed(key)` | key: string | bool | True only on the frame key is pressed |
| `uz.input.get_mouse_delta()` | — | (dx, dy) | Mouse movement in pixels |
| `uz.input.get_scroll_delta()` | — | (sx, sy) | Scroll wheel movement |
| `uz.input.is_mouse_button_down(button)` | button: "Left"/"Right"/"Middle" | bool | True while button is held |

### Key Names

Use lowercase key names: `"w"`, `"escape"`, `"shift"`, `"ctrl"`, etc. Special keys: `"Space"`, `"Tab"`, `"Return"`, `"Delete"`, `"Backspace"`, etc.

## Physics (Planned Phase 3+)

Physics raycast and overlap queries are planned for Phase 3. For now, use the available APIs (input, scene, renderer, camera).

## Audio (Planned Phase 3+)

Audio playback and mixing are planned for Phase 3. The XAudio2 backend is implemented in C++, but Lua bindings are not yet available.

## Camera

### Camera System

```lua
-- Set camera position (world space)
uz.camera.set_position(x, y, z)

-- Make camera look at a target point
uz.camera.look_at(target_x, target_y, target_z)

-- Example: FPS camera controller
function on_update(entity, dt)
    -- Get mouse look delta
    local dx, dy = uz.input.get_mouse_delta()

    -- Update camera position based on input
    local speed = 10.0 * dt
    if uz.input.is_key_down("w") then
        cam_z = cam_z + speed
    end

    -- Apply position
    uz.camera.set_position(cam_x, cam_y, cam_z)

    -- Point camera at a look-ahead position
    uz.camera.look_at(cam_x, cam_y + 5, cam_z)
end
```

### Camera API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.camera.set_position(x, y, z)` | x, y, z: float | — | Set camera world position |
| `uz.camera.look_at(x, y, z)` | x, y, z: float | — | Make camera look at a point in world space |

### Coordinate System

Z-up convention: X=forward, Y=right, Z=up. This matches the demo camera controller.

## Renderer

### Renderer System

```lua
-- Set render path (deferred or forward)
uz.renderer.set_render_path("deferred")  -- or "forward"

-- Set debug visualization mode
uz.renderer.set_debug_mode(0)  -- 0=off, 1-8=various debug channels

-- Set debug view mode by name
uz.renderer.set_debug_view("Albedo")

-- Example: Toggle render path with keyboard
if uz.input.is_key_just_pressed("F") then
    if current_render_path == "deferred" then
        uz.renderer.set_render_path("forward")
    else
        uz.renderer.set_render_path("deferred")
    end
end
```

### Renderer API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.renderer.set_render_path(path)` | path: "deferred"/"forward" | — | Switch rendering pipeline |
| `uz.renderer.set_debug_mode(mode)` | mode: 0-8 (int) | — | 0=off, 1-8=debug channels |
| `uz.renderer.set_debug_view(name)` | name: string | — | Debug view mode name (see below) |

### Debug View Modes

Available debug views for rendering diagnostics:
- `"Lit"` — Normal PBR rendering (default)
- `"Albedo"` — Base color without lighting
- `"Normal"` — Surface normals (RGB)
- `"Metallic"` — Metallic parameter (grayscale)
- `"Roughness"` — Roughness parameter (grayscale)
- `"AO"` — Ambient occlusion (grayscale)
- `"Depth"` — Linearized depth (black=near, white=far)
- `"WorldPos"` — World coordinates (false-color)
- `"Velocity"` — Motion vectors
- `"Wireframe"` — Geometry edges
- `"Overdraw"` — Overdraw visualization

## UI (Planned Phase 3+)

UI system with HTML/CSS rendering is planned for Phase 3. Lua bindings for UI creation will be available then.

## Events (Planned Phase 3+)

Event bus and custom event subscriptions are planned for Phase 3.

## Timers and Scheduling (Planned Phase 3+)

Async scheduling (`schedule_after`, `schedule_every`) is planned for Phase 3. Use Lua coroutines for now if you need frame-by-frame control.

## Complete Example Scripts

### FPS Camera Controller

```lua
-- scripts/camera_controller.lua
-- Simple FPS camera with WASD movement and mouse look
-- Z-up coordinate system: X=forward, Y=right, Z=up

local camera_speed = 5.0
local mouse_sensitivity = 0.15
local yaw = 90.0
local pitch = -15.0
local cam_x, cam_y, cam_z = 0, -8, 3

function on_start(entity)
    uz.log("Camera controller started")
end

function on_update(entity, dt)
    -- Quit on Escape
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

    -- Calculate movement axes
    local rad = math.pi / 180.0
    local cy = math.cos(yaw * rad)
    local sy = math.sin(yaw * rad)
    local cp = math.cos(pitch * rad)
    local sp = math.sin(pitch * rad)

    -- Forward direction in Z-up (X=forward, Y=right, Z=up)
    local fwd_x = cy * cp
    local fwd_y = sy * cp
    local fwd_z = sp

    -- Right direction
    local right_x = -sy
    local right_y = cy

    local speed = camera_speed * dt

    -- WASD movement
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

    -- Vertical (E/Q)
    if uz.input.is_key_down("E") then
        cam_z = cam_z + speed
    end
    if uz.input.is_key_down("Q") then
        cam_z = cam_z - speed
    end

    -- Apply camera position and look direction
    uz.camera.set_position(cam_x, cam_y, cam_z)

    local look_dist = 10.0
    uz.camera.look_at(
        cam_x + fwd_x * look_dist,
        cam_y + fwd_y * look_dist,
        cam_z + fwd_z * look_dist
    )
end

function on_stop(entity)
    uz.log("Camera controller stopped")
end
```

### Entity Finder and Rotator

```lua
-- scripts/cube_rotator.lua
-- Finds all entities named "Cube_*" and rotates them

local cubes = {}
local angles = {}
local base_speed = 30.0  -- degrees per second

function on_start(entity)
    -- Collect all cube entities
    local all = uz.scene.get_entities()
    for i = 1, #all do
        local e = all[i]
        local name = uz.scene.get_name(e)
        if name and name:sub(1, 5) == "Cube_" then
            cubes[#cubes + 1] = e
            angles[#cubes] = 0.0
        end
    end
    uz.log("CubeRotator: found " .. #cubes .. " cubes")
end

function on_update(entity, dt)
    for i = 1, #cubes do
        local e = cubes[i]
        -- Each cube rotates at different speed
        local speed = base_speed * (0.5 + i * 0.3)
        angles[i] = angles[i] + speed * dt
        if angles[i] > 360 then angles[i] = angles[i] - 360 end

        -- Rotate around Z axis (Z-up): set_rotation(pitch, yaw, roll)
        uz.scene.set_rotation(e, 0, 0, angles[i])
    end
end

function on_stop(entity)
    uz.log("CubeRotator stopped")
end
```

## Engine Comparison

| Feature | UE5 Scripting | Godot GDScript | Unity C# | **UzEngine Lua (Phase 1)** |
|---------|---------------|-----------------|----------|---------------------------|
| **Primary Language** | C++ | GDScript | C# | C++ |
| **Secondary Language** | Blueprint (visual) | C# (GDExtension) | None | Lua (text) |
| **Input Queries** | APlayerController | Input singleton | Input class | uz.input.* |
| **Camera Control** | Pawn/CharacterBP | Camera3D node | Camera component | uz.camera.* |
| **Scene Queries** | FindActorByName() | get_tree().get_nodes_in_group() | GameObject.Find() | uz.scene.* |
| **Entity Rotation** | FRotator / Quat | Vector3 (Euler) | Quaternion | Euler (pitch, yaw, roll) |
| **Renderer Control** | PostProcessVolume | WorldEnvironment | Graphics settings | uz.renderer.* |
| **Logging** | UE_LOG macro | print() | Debug.Log | uz.log() |
| **Frame Timing** | DeltaTime (float) | get_process_delta_time() | Time.deltaTime | uz.delta_time |
| **Hot-Reload** | C++ live coding | GDScript instant | C# domain reload | Lua instant (no state persistence) |

## Rules

1. **Coordinate System**: Z-up convention — X=forward, Y=right, Z=up (matches all engine systems)
2. **State Preservation**: Script state (local variables) is NOT preserved across hot-reload — design scripts to be stateless or reinitialize in `on_start()`
3. **Entity Queries**: Use `uz.scene.get_entities()` to find entities by name; no LINQ-style queries yet
4. **Input Lifecycle**: Input state is captured once per frame; call input functions in `on_update()`
5. **No Blocking Calls**: All Lua callbacks are synchronous; no async/await yet (Phase 3+)
6. **Nil Handling**: Always check return values; missing entities or components may be nil
7. **Rotation Format**: Euler angles (pitch, yaw, roll) in degrees; not radians
