<!-- doc-meta
  title: Lua VFX Decal API
  category: guides
  status: current
-->

# Lua VFX Decal API

Decal functions are registered on the `uz.vfx` table alongside particle and trail
bindings. They require `RegisterDecalLuaBindings(L, &decalManager)` to be called
after `RegisterVFXLuaBindings`.

## Functions

### spawn_decal

```lua
local handle = uz.vfx:spawn_decal(material_path, position, normal, lifetime)
```

Spawns a projected decal at the given world position.

| Parameter | Type | Description |
|-----------|------|-------------|
| `material_path` | string | Path to the decal material asset (`.uzmat`) |
| `position` | table | World-space position `{x, y, z}` |
| `normal` | table | Surface normal direction `{x, y, z}` |
| `lifetime` | number | Duration in seconds before automatic removal |

**Returns:** Integer handle on success, `nil` if the decal manager is not registered
or the pool is exhausted.

### fade_out_decal

```lua
uz.vfx:fade_out_decal(handle, duration)
```

Begins an alpha fade-out over the specified duration. The decal is automatically
destroyed when the fade completes.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | integer | Handle returned by `spawn_decal` |
| `duration` | number | Fade duration in seconds |

### destroy_decal

```lua
uz.vfx:destroy_decal(handle)
```

Immediately removes the decal. No fade animation.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | integer | Handle returned by `spawn_decal` |

### set_decal_blend_mode

```lua
uz.vfx:set_decal_blend_mode(handle, mode)
```

Sets the blending mode for the decal.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | integer | Handle returned by `spawn_decal` |
| `mode` | string | One of: `"opaque"`, `"additive"`, `"multiply"`, `"screen"` |

Raises a Lua error if `mode` is not one of the four valid strings.

### set_decal_normal_intensity

```lua
uz.vfx:set_decal_normal_intensity(handle, value)
```

Controls the strength of the decal's normal map blending with the surface.

| Parameter | Type | Description |
|-----------|------|-------------|
| `handle` | integer | Handle returned by `spawn_decal` |
| `value` | number | Normal intensity (0.0 = no effect, 1.0 = full strength) |

### get_decal_stats

```lua
local stats = uz.vfx:get_decal_stats()
```

Returns a table with global decal system statistics.

| Field | Type | Description |
|-------|------|-------------|
| `active_count` | integer | Number of currently active decals |
| `pool_available` | integer | Remaining slots in the decal pool |
| `atlas_occupancy` | number | Fraction of the decal atlas in use (0.0 to 1.0) |

**Returns:** Table on success, `nil` if the decal manager is not registered.

## Usage Example

```lua
function on_start()
    -- Spawn a scorch mark on the ground.
    local ground_pos    = {0, 0.01, 0}
    local ground_normal = {0, 1, 0}
    scorch = uz.vfx:spawn_decal("materials/decals/scorch.uzmat",
                                 ground_pos, ground_normal, 30.0)

    if scorch then
        -- Use multiply blending for a burn mark appearance.
        uz.vfx:set_decal_blend_mode(scorch, "multiply")
        uz.vfx:set_decal_normal_intensity(scorch, 0.6)
    end

    -- Check pool status.
    local stats = uz.vfx:get_decal_stats()
    if stats then
        uz.log(string.format("Decals: %d active, %d available",
            stats.active_count, stats.pool_available))
    end
end

function on_update(dt)
    elapsed = (elapsed or 0) + dt

    -- Fade out the scorch after 20 seconds.
    if scorch and elapsed >= 20.0 and not fading then
        uz.vfx:fade_out_decal(scorch, 5.0)
        fading = true
        uz.log("Scorch decal fading out")
    end
end

function on_stop()
    -- Clean up if the decal is still alive.
    if scorch then
        uz.vfx:destroy_decal(scorch)
        scorch = nil
    end
end
```

## Blend Mode Reference

| Mode | Effect |
|------|--------|
| `"opaque"` | Fully replaces the surface color |
| `"additive"` | Adds decal color to the surface (brightens) |
| `"multiply"` | Multiplies with surface color (darkens -- good for scorch/dirt) |
| `"screen"` | Inverse multiply (lightens -- good for wet surfaces) |

## Thread Safety

All decal functions must be called from the main thread. The decal manager is
not thread-safe.

## Lifetime

Decal handles are valid until `destroy_decal` is called or the decal expires
naturally (lifetime reached). Using a stale handle logs a warning and is a no-op.
