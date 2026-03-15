# Lua Runtime API Specification

## Overview

Minimal Lua API surface needed to convert the PBR test demo from C++ to Lua script. Designed for expansion — start small, grow per-demo.

## Script Lifecycle

```lua
-- Every .lua script attached to an entity follows this contract:

function on_start(ctx)
    -- Called once when entity enters scene.
    -- ctx.entity = entity handle (lightuserdata)
    -- ctx.name = entity name (string)
    uz.log("Script started on: " .. ctx.name)
end

function on_update(ctx, dt)
    -- Called every frame.
    -- dt = delta time in seconds (number)
    if uz.input.is_key_just_pressed("F") then
        uz.renderer.set_render_path("forward")
    end
end

function on_stop(ctx)
    -- Called when entity is destroyed or scene unloads.
    uz.log("Script stopped on: " .. ctx.name)
end
```

## API Reference

### uz (global)

| Field | Type | Description |
|-------|------|-------------|
| `uz.delta_time` | number | Frame delta time (seconds) |
| `uz.total_time` | number | Total elapsed time (seconds) |
| `uz.frame_index` | integer | Current frame number |
| `uz.log(msg)` | function | Log info message |
| `uz.log_warn(msg)` | function | Log warning |
| `uz.log_error(msg)` | function | Log error |
| `uz.quit()` | function | Request engine exit |

### uz.scene

| Function | Returns | Description |
|----------|---------|-------------|
| `find(name)` | entity or nil | Find entity by name |
| `get_position(e)` | x, y, z | Get world position |
| `set_position(e, x, y, z)` | — | Set world position |
| `get_rotation(e)` | pitch, yaw, roll | Get rotation (degrees) |
| `set_rotation(e, pitch, yaw, roll)` | — | Set rotation (degrees) |
| `get_scale(e)` | x, y, z | Get scale |
| `set_scale(e, x, y, z)` | — | Set scale |

### uz.camera

| Function | Returns | Description |
|----------|---------|-------------|
| `get_position()` | x, y, z | Active camera position |
| `set_position(x, y, z)` | — | Move camera |
| `look_at(x, y, z)` | — | Orient camera to target |
| `get_fov()` | degrees | Current FOV |
| `set_fov(degrees)` | — | Set FOV |

### uz.input

| Function | Returns | Description |
|----------|---------|-------------|
| `is_key_down(key)` | bool | Key held this frame |
| `is_key_just_pressed(key)` | bool | Key pressed this frame (edge) |
| `get_mouse_delta()` | dx, dy | Mouse movement delta |
| `is_mouse_button_down(btn)` | bool | Mouse button held |

**Key names**: `"W"`, `"A"`, `"S"`, `"D"`, `"F"`, `"Space"`, `"Escape"`, `"Shift"`, `"Ctrl"`, `"Num0"`-`"Num9"`, `"F1"`-`"F12"`, `"Up"`, `"Down"`, `"Left"`, `"Right"`

**Button names**: `"Left"`, `"Right"`, `"Middle"`

### uz.renderer

| Function | Returns | Description |
|----------|---------|-------------|
| `set_render_path(path)` | — | `"deferred"` or `"forward"` |
| `get_render_path()` | string | Current render path |
| `set_debug_mode(n)` | — | 0=off, 1-7=GBuffer channels |
| `get_stats()` | table | `{fps, frame_time, draw_calls, triangles}` |

## PBR Demo Script Example

```lua
-- scripts/pbr_controller.lua
-- Attached to a "DemoController" entity in the scene

local camera_speed = 5.0
local mouse_sensitivity = 0.003
local yaw = 0
local pitch = 0

function on_start(ctx)
    uz.log("PBR Demo started — F=toggle render path, 0-7=debug modes")
    uz.camera.set_position(0, 2, 8)
    uz.camera.look_at(0, 0, 0)
end

function on_update(ctx, dt)
    -- Render path toggle
    if uz.input.is_key_just_pressed("F") then
        local current = uz.renderer.get_render_path()
        if current == "deferred" then
            uz.renderer.set_render_path("forward")
            uz.log("Switched to forward rendering")
        else
            uz.renderer.set_render_path("deferred")
            uz.log("Switched to deferred rendering")
        end
    end

    -- Debug mode toggle (0=off, 1-7=channels)
    for i = 0, 7 do
        if uz.input.is_key_just_pressed("Num" .. i) then
            uz.renderer.set_debug_mode(i)
        end
    end

    -- FPS camera
    if uz.input.is_mouse_button_down("Right") then
        local dx, dy = uz.input.get_mouse_delta()
        yaw = yaw - dx * mouse_sensitivity
        pitch = pitch - dy * mouse_sensitivity
        pitch = math.max(-1.4, math.min(1.4, pitch))
    end

    local cx, cy, cz = uz.camera.get_position()
    local forward_x = math.sin(yaw) * math.cos(pitch)
    local forward_y = math.sin(pitch)
    local forward_z = -math.cos(yaw) * math.cos(pitch)
    local right_x = math.cos(yaw)
    local right_z = math.sin(yaw)

    local speed = camera_speed * dt
    if uz.input.is_key_down("W") then
        cx = cx + forward_x * speed
        cy = cy + forward_y * speed
        cz = cz + forward_z * speed
    end
    if uz.input.is_key_down("S") then
        cx = cx - forward_x * speed
        cy = cy - forward_y * speed
        cz = cz - forward_z * speed
    end
    if uz.input.is_key_down("A") then
        cx = cx - right_x * speed
        cz = cz - right_z * speed
    end
    if uz.input.is_key_down("D") then
        cx = cx + right_x * speed
        cz = cz + right_z * speed
    end

    uz.camera.set_position(cx, cy, cz)
end

function on_stop(ctx)
    uz.log("PBR Demo stopped")
end
```

## Scene File Example

```json
{
  "name": "PBR Test Scene",
  "version": "1.0",
  "entities": [
    {
      "name": "DemoController",
      "components": {
        "script": { "path": "scripts/pbr_controller.lua" }
      }
    },
    {
      "name": "Camera",
      "components": {
        "transform": { "position": [0, 2, 8], "rotation": [0, 0, 0] },
        "camera": { "fov": 75, "near": 0.1, "far": 1000 }
      }
    },
    {
      "name": "Sun",
      "components": {
        "light": {
          "type": "directional",
          "direction": [0.5, 0.3, 0.8],
          "color": [3.0, 2.85, 2.7],
          "intensity": 1.0,
          "cast_shadows": true,
          "shadow_resolution": 2048,
          "shadow_cascades": 4
        }
      }
    },
    {
      "name": "Cube_0",
      "components": {
        "transform": { "position": [-4, 0, 0], "scale": [0.8, 0.8, 0.8] },
        "mesh": { "asset": "meshes/cube.uzmesh" },
        "material": {
          "albedo": "textures/Stonebricks_BaseColor.jpg",
          "normal": "textures/Stonebricks_Normal.jpg",
          "roughness": "textures/Stonebricks_Roughness.jpg",
          "metallic": "textures/Stonebricks_Metallic.jpg",
          "ao": "textures/Stonebricks_AO.jpg"
        }
      }
    },
    {
      "name": "PointLight_Red",
      "components": {
        "transform": { "position": [-3, 2, 2] },
        "light": { "type": "point", "color": [4, 0.2, 0.2], "radius": 8 }
      }
    }
  ]
}
```

Note: Full scene has 5 cubes (positions: -4,0,0 / -2,0,0 / 0,0,0 / 2,0,0 / 4,0,0), ground plane, 4 point lights, 1 directional sun. Abbreviated above for clarity.
