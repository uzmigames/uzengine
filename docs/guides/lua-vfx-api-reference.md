<!-- doc-meta
title: Lua VFX API Reference
category: guides
theme: scripting
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# Lua VFX API Reference

Comprehensive reference for the VFX Lua API. The `uz.vfx` namespace provides particle emitter spawning, parameter control, LOD management, and budget tracking for GPU-driven visual effects.

## Overview

The VFX system in UzEngine is fully scriptable via Lua. You can spawn emitters, control their behavior at runtime, monitor performance, and manage complex composite effects with ease.

## Particle Emitter Functions

### spawn(graph_path, position)

Spawns a particle emitter from a `.uzvfx_graph` asset.

```lua
local handle = uz.vfx.spawn("content/vfx/smoke.uzvfx_graph", {x=0, y=10, z=0})
if handle then
    print("Emitter spawned, handle:", handle)
else
    print("Failed to spawn emitter")
end
```

**Parameters:**
- `graph_path` (string): Path to the `.uzvfx_graph` file relative to the engine root
- `position` (table): Spawn position as `{x, y, z}` in world coordinates (centimeters)

**Returns:**
- (number | nil): Emitter handle on success, nil if spawn failed (invalid path or graph parsing error)

**Matching Logic:**
- Matches graph asset by graphPath string
- If multiple emitters match, tiebreaker is closest position to spawn point

---

### stop(handle)

Stops particle emission from an active emitter.

```lua
uz.vfx.stop(emitter_handle)
```

**Parameters:**
- `handle` (number): Emitter handle returned by `spawn()`

**Behavior:**
- Stops the emission loop; existing particles continue to age and die naturally
- Emitter can be resumed with `start()`

---

### start(handle)

Resumes emission on a stopped emitter.

```lua
uz.vfx.start(emitter_handle)
```

**Parameters:**
- `handle` (number): Emitter handle returned by `spawn()`

**Behavior:**
- Reactivates emission loop after `stop()` was called
- No-op if emitter is already emitting

---

### pause(handle, paused)

Pauses or unpauses an emitter without stopping emission.

```lua
uz.vfx.pause(emitter_handle, true)  -- pause
uz.vfx.pause(emitter_handle, false) -- unpause
```

**Parameters:**
- `handle` (number): Emitter handle
- `paused` (boolean): True to pause, false to unpause

**Behavior:**
- Paused emitters do not emit new particles but existing particles continue aging
- Differs from `stop()` — particles are not killed on pause

---

### get_alive_count(handle)

Returns the current number of alive particles in an emitter.

```lua
local count = uz.vfx.get_alive_count(emitter_handle)
print("Particles alive:", count)
```

**Parameters:**
- `handle` (number): Emitter handle

**Returns:**
- (number): CPU-side particle count (updated after GPU readback)

---

### set_parameter(handle, name, value)

Sets a float or Vec3 parameter on an emitter.

```lua
uz.vfx.set_parameter(emitter_handle, "spawn_rate", 50.0)
uz.vfx.set_parameter(emitter_handle, "spawn_velocity", {x=10, y=20, z=5})
```

**Parameters:**
- `handle` (number): Emitter handle
- `name` (string): Parameter name (must exist in the graph's parameters)
- `value` (number | table): Float value or Vec3 table `{x, y, z}`

**Behavior:**
- Updates are applied to the GPU CB on next frame
- Vec3 parameter names conventionally end with `_velocity` or similar

---

### set_parameter_vec4(handle, name, vec4)

Sets a Vec4 parameter on an emitter.

```lua
uz.vfx.set_parameter_vec4(emitter_handle, "color_tint", {x=1, y=0.5, z=0, w=1})
```

**Parameters:**
- `handle` (number): Emitter handle
- `name` (string): Parameter name
- `vec4` (table): Vec4 as `{x, y, z, w}`

**Behavior:**
- Applied to GPU CB on next frame

---

### get_parameter(handle, name)

Reads a parameter from an emitter.

```lua
local rate = uz.vfx.get_parameter(emitter_handle, "spawn_rate")
local vel = uz.vfx.get_parameter(emitter_handle, "spawn_velocity")

if vel then
    print(string.format("Velocity: x=%.2f, y=%.2f, z=%.2f", vel.x, vel.y, vel.z))
end
```

**Parameters:**
- `handle` (number): Emitter handle
- `name` (string): Parameter name

**Returns:**
- (number | table | nil): Float, Vec3 table `{x, y, z}`, Vec4 table `{x, y, z, w}`, or nil if parameter doesn't exist

---

### emit_event(handle, name [, count [, velocity_scale]])

Fires a burst event on an emitter (e.g., explosion, impact).

```lua
-- Simple event
uz.vfx.emit_event(emitter_handle, "impact")

-- Burst with count
uz.vfx.emit_event(emitter_handle, "explosion", 100)

-- Burst with velocity scaling
uz.vfx.emit_event(emitter_handle, "shockwave", 50, 2.0)
```

**Parameters:**
- `handle` (number): Emitter handle
- `name` (string): Event name (must exist in the graph)
- `count` (number, optional): Number of particles to emit. Default: 1
- `velocity_scale` (number, optional): Multiplier for event velocity. Default: 1.0

**Behavior:**
- Emits `count` particles immediately with event-specific velocity
- Velocity is scaled by `velocity_scale` before application

---

### get_emitter_field(handle, field_name)

Reads an emitter field value.

```lua
local rate = uz.vfx.get_emitter_field(emitter_handle, "emission_rate")
local is_active = uz.vfx.get_emitter_field(emitter_handle, "is_emitting")
```

**Parameters:**
- `handle` (number): Emitter handle
- `field_name` (string): Field name from list below

**Returns:**
- Field value, or nil if field doesn't exist

---

### set_emitter_field(handle, field_name, value)

Writes an emitter field value.

```lua
uz.vfx.set_emitter_field(emitter_handle, "emission_rate", 100.0)
uz.vfx.set_emitter_field(emitter_handle, "is_emitting", false)
uz.vfx.set_emitter_field(emitter_handle, "gravity", {x=0, y=0, z=-980})
```

**Parameters:**
- `handle` (number): Emitter handle
- `field_name` (string): Field name from list below
- `value` (number | table | boolean | string): New field value

**Supported Fields:**

| Field | Type | Description |
|-------|------|-------------|
| `emission_rate` | float | Particles per second |
| `lifetime` | float | Base particle lifetime (seconds) |
| `lifetime_variation` | float | Random lifetime ±variance (seconds) |
| `spawn_velocity` | Vec3 `{x,y,z}` | Initial velocity vector (cm/s) |
| `velocity_variation` | float | Random velocity ±variance (cm/s) |
| `spread_angle` | float | Spread cone angle (degrees) |
| `gravity` | Vec3 `{x,y,z}` | Gravity acceleration (cm/s²) |
| `damping` | float | Velocity damping per second (0–1) |
| `priority` | int | Sort priority (higher = rendered last) |
| `start_delay` | float | Delay before emission starts (seconds) |
| `max_particles` | int | Maximum particles (read-only) |
| `is_emitting` | boolean | Enable/disable emission |
| `graph_path` | string | Graph asset path (read-only) |
| `soft_particle_fade_range` | float | Fade range for soft particles (cm) |

---

## LOD Functions

### set_lod_distances(handle, d0, d1, d2)

Configures Level-of-Detail distance thresholds for an emitter.

```lua
-- Full quality up to 200cm, reduced to 500cm, minimal to 1000cm
uz.vfx.set_lod_distances(emitter_handle, 200.0, 500.0, 1000.0)
```

**Parameters:**
- `handle` (number): Emitter handle
- `d0` (float): Distance threshold 0→1 (Full→Reduced), in centimeters
- `d1` (float): Distance threshold 1→2 (Reduced→Minimal), in centimeters
- `d2` (float): Distance threshold 2→3 (Minimal→Culled), in centimeters

**Behavior:**
- LOD evaluation is camera-distance based
- Particle spawn rate and simulation detail scale down at each LOD level

---

### get_lod_level(handle)

Returns the current LOD level for an emitter.

```lua
local lod = uz.vfx.get_lod_level(emitter_handle)
-- lod: 0=Full, 1=Reduced, 2=Minimal, 3=Culled
```

**Parameters:**
- `handle` (number): Emitter handle

**Returns:**
- (number): LOD level
  - `0`: Full quality
  - `1`: Reduced quality
  - `2`: Minimal quality
  - `3`: Culled (not rendered)

---

### set_lod_enabled(handle, enabled)

Enables or disables LOD evaluation for an emitter.

```lua
uz.vfx.set_lod_enabled(emitter_handle, true)  -- LOD active
uz.vfx.set_lod_enabled(emitter_handle, false) -- always full quality
```

**Parameters:**
- `handle` (number): Emitter handle
- `enabled` (boolean): True to enable LOD, false to always use full quality

**Behavior:**
- Disabled emitters always render at full quality regardless of distance

---

## Budget Functions

### get_budget_stats()

Returns global VFX system statistics.

```lua
local stats = uz.vfx.get_budget_stats()
print(string.format("Budget: %d/%d (%.1f%%)", stats.used, stats.total, stats.utilization_pct))
```

**Returns:**
- (table): Statistics table
  - `total` (number): Total particle budget
  - `used` (number): Current particles allocated
  - `available` (number): Remaining particles
  - `utilization_pct` (number): Percentage used (0–100)

---

### get_total_alive()

Returns the total number of alive particles across all emitters.

```lua
local total = uz.vfx.get_total_alive()
print("Total particles:", total)
```

**Parameters:**
- None

**Returns:**
- (number): Sum of all particles from all active emitters

---

### get_emitter_count()

Returns the number of registered emitter instances.

```lua
local count = uz.vfx.get_emitter_count()
print("Active emitters:", count)
```

**Parameters:**
- None

**Returns:**
- (number): Count of emitters (including paused/stopped)

---

## Composite Functions

### spawn_composite(path, position)

Spawns a composite effect (group of linked emitters).

```lua
local handles = uz.vfx.spawn_composite("content/vfx/explosion.uzvfx_composite", {x=100, y=0, z=0})
for i, handle in ipairs(handles) do
    print("Spawned emitter", i, "handle:", handle)
end
```

**Parameters:**
- `path` (string): Path to `.uzvfx_composite` JSON file
- `position` (table): Spawn position `{x, y, z}`

**Returns:**
- (table): Array of emitter handles on success, or empty table if composite failed to load

**Composite Format:**
The `.uzvfx_composite` file is a JSON array defining linked emitters:

```json
[
  {
    "graph_path": "content/vfx/smoke.uzvfx_graph",
    "position_offset": [0, 0, 0],
    "lifetime": 3.0
  },
  {
    "graph_path": "content/vfx/sparks.uzvfx_graph",
    "position_offset": [50, 0, 0],
    "lifetime": 2.0
  }
]
```

---

### stop_composite(handles_table)

Stops all emitters in a composite effect.

```lua
local handles = uz.vfx.spawn_composite(...)
-- ... later ...
uz.vfx.stop_composite(handles)
```

**Parameters:**
- `handles_table` (table): Array of handles returned by `spawn_composite()`

**Behavior:**
- Calls `stop()` on each handle in the table

---

## Example: Complete Emitter Lifecycle

```lua
-- Spawn emitter
local emitter = uz.vfx.spawn("content/vfx/fire.uzvfx_graph", {x=0, y=100, z=0})
if not emitter then
    print("Failed to spawn fire emitter")
    return
end

-- Configure
uz.vfx.set_emitter_field(emitter, "emission_rate", 75.0)
uz.vfx.set_parameter(emitter, "color_tint", {x=1, y=0.8, z=0.2})
uz.vfx.set_lod_distances(emitter, 300.0, 700.0, 1200.0)

-- Event loop
for frame = 1, 300 do
    local count = uz.vfx.get_alive_count(emitter)
    local lod = uz.vfx.get_lod_level(emitter)

    if frame == 60 then
        -- Emit burst
        uz.vfx.emit_event(emitter, "flare", 50, 1.5)
    end

    if frame == 180 then
        -- Start fadeout
        uz.vfx.stop(emitter)
    end

    uz.async.frame_wait()
end

-- Cleanup happens automatically when emitter is destroyed
```

---

## Key Takeaways

- **Emitters are handles**: All operations use opaque numeric handles returned by `spawn()`
- **Parameters vs Fields**: Use `set_parameter()` for graph-defined uniforms, `set_emitter_field()` for engine properties
- **Vec3 and Vec4 tables**: Always use `{x, y, z}` format for vectors
- **LOD is automatic**: Set distances once, camera distance drives LOD evaluation
- **Composites are convenience**: Use them for multi-emitter effects with position offsets
- **Readback is delayed**: `get_alive_count()` reflects previous frame's GPU state

## References

- [VFX Distortion Pipeline](vfx-distortion-pipeline.md)
- [VFX Flipbook/SubUV Guide](vfx-flipbook-subuv-guide.md)
- [VFX Vector Field Guide](vfx-vector-field-guide.md)
- [VFX Sub-Emitter System](vfx-sub-emitter-system.md)
- [Lua Scripting](../user/guides/lua-scripting.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation — complete particle emitter, LOD, budget, and composite function reference |
