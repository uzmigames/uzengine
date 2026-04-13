<!-- doc-meta
title: Lua VFX Trail API
category: guides
theme: scripting
created: 2026-03-25
updated: 2026-03-25
status: active
author: docs-operator
-->

# Lua VFX Trail API

Complete reference for the Trail system in UzEngine's VFX Lua API. Trails render as ribbons connecting a sequence of world-space points, commonly used for weapon trails, motion paths, and projectile tails.

## Overview

The Trail system allows you to record motion paths in real-time and render them as textured ribbons. Trails manage their own point lifetime, culling old points automatically, and support width tapering, color gradients, and UV scrolling.

## Trail Creation

### create_trail(config)

Creates a new trail with specified configuration.

```lua
local trail = uz.vfx.create_trail({
    material_path = "content/materials/trail.uzmat",
    max_points = 128,
    width_start = 10.0,
    width_end = 5.0,
    point_lifetime = 0.5,
    min_point_distance = 2.0,
    sort_order = 100,
    is_active = true
})
```

**Parameters:**
- `config` (table): Configuration table with the following fields:
  - `material_path` (string): Path to `.uzmat` material file for rendering the ribbon
  - `max_points` (int): Maximum points the trail can store (typical: 64–256)
  - `width_start` (float): Ribbon width at trail start (centimeters)
  - `width_end` (float): Ribbon width at trail end (centimeters)
  - `point_lifetime` (float): How long each point persists (seconds)
  - `min_point_distance` (float): Minimum world distance to spawn a new point (cm; skips nearby points)
  - `sort_order` (int): Render order relative to other VFX (higher = later)
  - `is_active` (boolean): Start with recording enabled (default: true)
  - `ribbon_strands` (int): Number of parallel ribbon strands (default: 1). Values > 1 produce N parallel quad-strip ribbons offset perpendicular to the camera-facing plane, approximating a tube or wing shape. Each strand carries a unique `strandIndex` for per-strand shader modulation. Vertex count scales as `pointCount * 2 * ribbonStrands`.
  - `strand_spacing` (float): World-space distance between adjacent strands in metres (default: 0.1). Only meaningful when `ribbon_strands > 1`. A value of 0 collapses all strands into overlapping ribbons.

**Returns:**
- (number): Trail handle on success, or nil if material failed to load

**Notes:**
- `max_points` should be 2^N (64, 128, 256) for GPU buffer efficiency
- `min_point_distance` prevents point flooding when object is stationary
- Trail points flow from start (thick) to end (thin) as specified by width parameters

---

## Trail Lifecycle

### destroy_trail(handle)

Destroys a trail and frees its resources.

```lua
uz.vfx.destroy_trail(trail_handle)
```

**Parameters:**
- `handle` (number): Trail handle returned by `create_trail()`

**Behavior:**
- Unregisters the trail from the render system
- Frees GPU buffers and mesh data
- Handle becomes invalid after destruction

---

### set_trail_active(handle, active)

Enables or disables point recording.

```lua
uz.vfx.set_trail_active(trail_handle, true)  -- start recording
uz.vfx.set_trail_active(trail_handle, false) -- stop recording
```

**Parameters:**
- `handle` (number): Trail handle
- `active` (boolean): True to enable recording, false to disable

**Behavior:**
- Inactive trails continue rendering accumulated points but do not record new ones
- Useful for pausing motion capture or cleaning up without destroying the trail

---

### clear_trail(handle)

Removes all accumulated points from a trail.

```lua
uz.vfx.clear_trail(trail_handle)
```

**Parameters:**
- `handle` (number): Trail handle

**Behavior:**
- Instantly clears the point buffer; trail becomes invisible
- Does not change `is_active` state

---

## Trail Properties

### get_trail_length(handle)

Returns the current number of points in the trail.

```lua
local length = uz.vfx.get_trail_length(trail_handle)
print("Trail has", length, "points")
```

**Parameters:**
- `handle` (number): Trail handle

**Returns:**
- (number): Current point count (0 to `max_points`)

---

### get_trail_point_count(handle)

Alias for `get_trail_length()` — returns current point count.

```lua
local count = uz.vfx.get_trail_point_count(trail_handle)
```

**Parameters:**
- `handle` (number): Trail handle

**Returns:**
- (number): Current point count

---

### set_trail_width(handle, start, end)

Updates the ribbon width taper.

```lua
uz.vfx.set_trail_width(trail_handle, 15.0, 3.0)  -- thick to thin
```

**Parameters:**
- `handle` (number): Trail handle
- `start` (float): Ribbon width at the trail start (centimeters)
- `end` (float): Ribbon width at the trail end (centimeters)

**Behavior:**
- Width interpolates linearly from start to end across the ribbon
- Changes take effect immediately on newly rendered geometry

---

## Trail Styling

### set_trail_color(handle, start_rgba, end_rgba)

Sets color gradient from trail start to end.

```lua
-- Opaque white to transparent red
uz.vfx.set_trail_color(
    trail_handle,
    {r=1, g=1, b=1, a=1},    -- start color (white)
    {r=1, g=0, b=0, a=0}     -- end color (transparent red)
)
```

**Parameters:**
- `handle` (number): Trail handle
- `start_rgba` (table): Start color as `{r, g, b, a}` (values 0–1, linear color space)
- `end_rgba` (table): End color as `{r, g, b, a}` (values 0–1)

**Behavior:**
- Color is interpolated across the trail length
- Alpha is used for fadeout; set end alpha to 0 for natural trail decay
- Linear color space; convert from sRGB if using artist values

---

### set_trail_lifetime(handle, seconds)

Sets how long each point persists before being culled.

```lua
uz.vfx.set_trail_lifetime(trail_handle, 0.75)
```

**Parameters:**
- `handle` (number): Trail handle
- `seconds` (float): Point lifetime in seconds

**Behavior:**
- Older points are automatically removed from the trail buffer
- Affects trail length dynamically based on object speed (faster = longer trails)
- Changes take effect immediately for new points; old points keep their original lifetime

---

### set_trail_uv_scroll(handle, rate)

Configures UV scrolling along the trail direction.

```lua
uz.vfx.set_trail_uv_scroll(trail_handle, 0.5)  -- slow scroll
uz.vfx.set_trail_uv_scroll(trail_handle, 2.0)  -- fast scroll
```

**Parameters:**
- `handle` (number): Trail handle
- `rate` (float): UV scroll speed (units per second along trail)

**Behavior:**
- Positive values scroll in trail direction (newer → older)
- Useful for animated textures on the ribbon (flame, energy, etc.)
- Set to 0 to disable scrolling

---

## Example: Sword Trail

This example creates a glowing sword trail that updates per frame based on a weapon's position:

```lua
-- Initialization
local sword_trail = uz.vfx.create_trail({
    material_path = "content/materials/sword-trail.uzmat",
    max_points = 96,
    width_start = 8.0,
    width_end = 1.0,
    point_lifetime = 0.4,
    min_point_distance = 1.5,
    sort_order = 50,
    is_active = true
})

-- Set glowing blue color with fadeout
uz.vfx.set_trail_color(
    sword_trail,
    {r=0.2, g=0.6, b=1.0, a=1.0},   -- bright blue at start
    {r=0.0, g=0.0, b=0.5, a=0.0}    -- transparent dark blue at end
)

-- Enable subtle UV animation
uz.vfx.set_trail_uv_scroll(sword_trail, 0.3)

-- Main loop: record sword position
local sword_pos = {x=0, y=0, z=0}
for frame = 1, 600 do
    -- Get sword position from animation or player input
    sword_pos = get_sword_tip_position()

    -- Trail automatically records new points when distance > min_point_distance
    -- No explicit point-adding function needed

    -- Cleanup on weapon sheathe
    if is_weapon_sheathed() then
        uz.vfx.clear_trail(sword_trail)
        uz.vfx.set_trail_active(sword_trail, false)
    end

    -- Resume on weapon draw
    if is_weapon_drawn() then
        uz.vfx.set_trail_active(sword_trail, true)
    end

    uz.async.frame_wait()
end

-- Cleanup
uz.vfx.destroy_trail(sword_trail)
```

---

## Example: Projectile Tail

This example creates a particle-like tail for a projectile that fades out:

```lua
local projectile_tail = uz.vfx.create_trail({
    material_path = "content/materials/smoke-trail.uzmat",
    max_points = 64,
    width_start = 12.0,
    width_end = 4.0,
    point_lifetime = 0.6,
    min_point_distance = 3.0,
    sort_order = 25,
    is_active = true
})

-- Light gray smoke with full fadeout
uz.vfx.set_trail_color(
    projectile_tail,
    {r=0.8, g=0.8, b=0.8, a=0.4},   -- semi-transparent gray
    {r=0.5, g=0.5, b=0.5, a=0.0}    -- fully transparent
)

-- Disable UV scroll for smoke
uz.vfx.set_trail_uv_scroll(projectile_tail, 0.0)

-- Simulate projectile motion
local pos = {x=0, y=100, z=0}
local vel = {x=100, y=0, z=0}
local lifetime = 0.0
local max_lifetime = 10.0

while lifetime < max_lifetime do
    -- Update position
    vel.y = vel.y - 981 * 0.016  -- gravity (9.81 m/s² = 981 cm/s²)
    pos.x = pos.x + vel.x * 0.016
    pos.y = pos.y + vel.y * 0.016
    pos.z = pos.z + vel.z * 0.016

    -- Trail records point automatically based on min_point_distance

    -- Check for collision (pseudo-code)
    if is_colliding(pos) then
        break
    end

    lifetime = lifetime + 0.016
    uz.async.frame_wait()
end

-- Cleanup
uz.vfx.destroy_trail(projectile_tail)
```

---

## Example: Animated Color Trail

This example animates the trail color over time (useful for charged attacks):

```lua
local power_trail = uz.vfx.create_trail({
    material_path = "content/materials/energy-trail.uzmat",
    max_points = 128,
    width_start = 10.0,
    width_end = 2.0,
    point_lifetime = 0.5,
    min_point_distance = 2.0,
    sort_order = 60,
    is_active = true
})

-- Animate trail color based on power level
local charge = 0.0
for frame = 1, 300 do
    -- Increase charge
    charge = math.min(charge + 0.01, 1.0)

    -- Interpolate from blue (0% charge) to red (100% charge)
    local r = charge
    local g = 0.5 * (1 - charge)
    local b = 1 - charge
    local a = 0.5 + 0.5 * charge

    uz.vfx.set_trail_color(
        power_trail,
        {r=r, g=g, b=b, a=a},        -- start color (animated)
        {r=r*0.5, g=g*0.5, b=b*0.5, a=0.0}  -- end color (darker)
    )

    -- Width expands as power increases
    local width = 8.0 + 6.0 * charge
    uz.vfx.set_trail_width(power_trail, width, width * 0.3)

    uz.async.frame_wait()
end

uz.vfx.destroy_trail(power_trail)
```

---

## Key Takeaways

- **Trails are semi-autonomous**: Points are recorded automatically based on `min_point_distance`
- **No explicit point API**: Unlike some engines, UzEngine trails don't expose `add_point()` — pass position implicitly via object motion
- **Color is crucial**: Set `end_rgba.a = 0` for natural fadeout; avoid hard cutoff
- **Lifetime determines length**: Faster objects with longer lifetime = longer visual trails
- **Material drives visuals**: Trail geometry is simple; material controls appearance
- **Cleanup matters**: Always `destroy_trail()` when the trail is no longer needed

## References

- [Lua VFX API Reference](lua-vfx-api-reference.md)
- [VFX Distortion Pipeline](vfx-distortion-pipeline.md)
- [Lua Scripting Guide](../user/guides/lua-scripting.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-25 | Initial creation — complete trail API reference with sword, projectile, and animated examples |
