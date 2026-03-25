# Scripting — Rendering APIs Reference

> **Status**: Active (Advanced rendering component bindings)
> **Source**: `engine/scripting/scene/lua_decal_api.cpp`, `lua_reflection_probe_api.cpp`, `renderer/lua_postprocess_api.cpp`
> **Depends on**: LuaVM (§15.2), Renderer (§1.3), PostProcessVolumeManager
> **Implements**: Decals, Reflection Probes, Post-Process Volumes

## Purpose

This reference covers advanced Lua bindings for rendering components that require spatial awareness and fine-tuned material properties. These APIs enable runtime control over decals, reflection probes, and post-process effects.

## Decals

### Decal System

Decals project material properties (albedo, normal, roughness, metallic) onto surfaces using DBuffer pre-pass blending. Decals are useful for damage, graffiti, weathering, and visual effects.

```lua
-- Create a decal component on an entity
uz.scene.create_decal(entity)

-- Configure decal size and orientation
uz.scene.set_decal_extents(entity, 100, 100, 50)  -- Width, height, depth (cm)

-- Set decal textures
uz.scene.set_decal_albedo_texture(entity, texture_id)     -- Base color texture
uz.scene.set_decal_normal_texture(entity, texture_id)     -- Surface normal (DXT5, BC5)
uz.scene.set_decal_material_texture(entity, texture_id)   -- Roughness, metallic (packed)

-- Configure decal blending
uz.scene.set_decal_opacity(entity, 0.8)           -- Opacity multiplier (0-1)
uz.scene.set_decal_normal_blend(entity, 1.0)      -- Normal strength (0-1)
uz.scene.set_decal_roughness(entity, 0.5)         -- Roughness override
uz.scene.set_decal_metallic(entity, 0.0)          -- Metallic override

-- Priority and fade
uz.scene.set_decal_priority(entity, 0)            -- Lower=rendered first; for overlaps
uz.scene.set_decal_fade_distance(entity, 1000.0)  -- Distance-based fade (cm)
uz.scene.set_decal_angle_fade_start(entity, 0.5)  -- Angle fade threshold (0-1)

-- Enable/disable
uz.scene.set_decal_enabled(entity, true)          -- Toggle decal rendering
```

### Decal API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.scene.create_decal(entity)` | entity | — | Add DecalComponent (idempotent) |
| `uz.scene.set_decal_extents(entity, x, y, z)` | entity, x, y, z: float | — | Decal projection volume size (cm) |
| `uz.scene.get_decal_extents(entity)` | entity | (x, y, z) or nil | Get decal projection volume size |
| `uz.scene.set_decal_albedo_texture(entity, id)` | entity, id: texture handle | — | Set albedo/base color texture |
| `uz.scene.get_decal_albedo_texture(entity)` | entity | texture id or nil | Get albedo texture |
| `uz.scene.set_decal_normal_texture(entity, id)` | entity, id: texture handle | — | Set normal map texture |
| `uz.scene.get_decal_normal_texture(entity)` | entity | texture id or nil | Get normal texture |
| `uz.scene.set_decal_material_texture(entity, id)` | entity, id: texture handle | — | Set roughness/metallic texture |
| `uz.scene.get_decal_material_texture(entity)` | entity | texture id or nil | Get material texture |
| `uz.scene.set_decal_opacity(entity, v)` | entity, v: float (0-1) | — | Decal opacity multiplier |
| `uz.scene.get_decal_opacity(entity)` | entity | float or nil | Get opacity multiplier |
| `uz.scene.set_decal_normal_blend(entity, v)` | entity, v: float (0-1) | — | Normal contribution strength |
| `uz.scene.get_decal_normal_blend(entity)` | entity | float or nil | Get normal blend factor |
| `uz.scene.set_decal_roughness(entity, v)` | entity, v: float (0-1) | — | Roughness material value |
| `uz.scene.get_decal_roughness(entity)` | entity | float or nil | Get roughness value |
| `uz.scene.set_decal_metallic(entity, v)` | entity, v: float (0-1) | — | Metallic material value |
| `uz.scene.get_decal_metallic(entity)` | entity | float or nil | Get metallic value |
| `uz.scene.set_decal_priority(entity, v)` | entity, v: int | — | Rendering priority (lower = first) |
| `uz.scene.get_decal_priority(entity)` | entity | int or nil | Get priority value |
| `uz.scene.set_decal_fade_distance(entity, v)` | entity, v: float (cm) | — | Distance-based fade range |
| `uz.scene.get_decal_fade_distance(entity)` | entity | float or nil | Get fade distance (cm) |
| `uz.scene.set_decal_angle_fade_start(entity, v)` | entity, v: float (0-1) | — | Angle fade threshold |
| `uz.scene.get_decal_angle_fade_start(entity)` | entity | float or nil | Get angle fade threshold |
| `uz.scene.set_decal_enabled(entity, bool)` | entity, bool: bool | — | Enable/disable decal |
| `uz.scene.is_decal_enabled(entity)` | entity | bool or nil | Check if decal is enabled |
| `uz.scene.remove_decal(entity)` | entity | — | Remove DecalComponent |

## Reflection Probes

### Reflection Probe System

Reflection probes capture local environment cubemaps for accurate specular reflections on polished surfaces. Probes are baked once, then used by materials within the influence radius.

```lua
-- Create a reflection probe at entity position
uz.scene.create_reflection_probe(entity)

-- Configure influence volume
uz.scene.set_probe_influence_radius(entity, 500.0)   -- Sphere radius (cm)
uz.scene.set_probe_blend_distance(entity, 100.0)     -- Blend distance to neighbors (cm)

-- Capture settings
uz.scene.set_probe_resolution(entity, 128)           -- Cubemap resolution per face (power of 2)
uz.scene.set_probe_priority(entity, 0)               -- Priority (higher wins)
uz.scene.set_probe_needs_capture(entity, true)       -- Trigger immediate capture

-- Enable/disable
uz.scene.set_probe_enabled(entity, true)             -- Toggle probe influence
```

### Reflection Probe API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.scene.create_reflection_probe(entity)` | entity | — | Add ReflectionProbeComponent (idempotent) |
| `uz.scene.set_probe_influence_radius(entity, v)` | entity, v: float (cm) | — | Influence sphere radius |
| `uz.scene.get_probe_influence_radius(entity)` | entity | float or nil | Get influence radius (cm) |
| `uz.scene.set_probe_blend_distance(entity, v)` | entity, v: float (cm) | — | Blend distance to neighbors |
| `uz.scene.get_probe_blend_distance(entity)` | entity | float or nil | Get blend distance (cm) |
| `uz.scene.set_probe_resolution(entity, v)` | entity, v: uint | — | Cubemap resolution per face |
| `uz.scene.get_probe_resolution(entity)` | entity | uint or nil | Get resolution per face |
| `uz.scene.set_probe_priority(entity, v)` | entity, v: int | — | Probe priority (higher = preferred) |
| `uz.scene.get_probe_priority(entity)` | entity | int or nil | Get priority value |
| `uz.scene.set_probe_enabled(entity, bool)` | entity, bool: bool | — | Enable/disable probe |
| `uz.scene.is_probe_enabled(entity)` | entity | bool or nil | Check if probe is enabled |
| `uz.scene.set_probe_needs_capture(entity, bool)` | entity, bool: bool | — | Flag for immediate capture |
| `uz.scene.get_probe_needs_capture(entity)` | entity | bool or nil | Check if capture is pending |
| `uz.scene.remove_reflection_probe(entity)` | entity | — | Remove ReflectionProbeComponent |

## Post-Process Volumes

### Post-Process System

Post-process volumes blend multiple settings (bloom, exposure, depth-of-field, color grading) based on camera position. Volumes can be overlapping with priority-based blending.

```lua
-- Create a post-process volume
local vol_id = uz.postprocess.add_volume()

-- Configure volume
uz.postprocess.set_volume_enabled(vol_id, true)           -- Enable blending
uz.postprocess.set_volume_priority(vol_id, 0)             -- Priority (higher = stronger)
uz.postprocess.set_volume_blend_weight(vol_id, 1.0)       -- Blend weight (0-1)
uz.postprocess.set_volume_blend_radius(vol_id, 1000.0)    -- Blend distance (cm)

-- Set post-process settings
uz.postprocess.set_bloom_intensity(vol_id, 1.0)
uz.postprocess.set_bloom_threshold(vol_id, 1.0)
uz.postprocess.set_exposure_min(vol_id, -10.0)
uz.postprocess.set_exposure_max(vol_id, 10.0)
uz.postprocess.set_exposure_bias(vol_id, 0.0)
uz.postprocess.set_white_temp(vol_id, 6500.0)             -- Kelvin temperature
uz.postprocess.set_vignette_intensity(vol_id, 0.0)
uz.postprocess.set_ao_intensity(vol_id, 1.0)
uz.postprocess.set_ssr_intensity(vol_id, 1.0)

-- Query blended result at camera position
local blended = uz.postprocess.compute_blended(x, y, z)
uz.log("Blended bloom: " .. blended.bloom_intensity)

-- Clean up
uz.postprocess.remove_volume(vol_id)
```

### Post-Process API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.postprocess.add_volume()` | — | volume_id: int | Create new post-process volume |
| `uz.postprocess.remove_volume(vol_id)` | vol_id: int | — | Remove post-process volume |
| `uz.postprocess.clear()` | — | — | Clear all volumes |
| `uz.postprocess.get_volume_count()` | — | int | Get number of active volumes |
| **Volume Control** | | | |
| `uz.postprocess.set_volume_enabled(vol_id, bool)` | vol_id: int, bool: bool | — | Enable/disable volume blending |
| `uz.postprocess.is_volume_enabled(vol_id)` | vol_id: int | bool | Check if volume is enabled |
| `uz.postprocess.set_volume_priority(vol_id, v)` | vol_id: int, v: float | — | Set priority (higher = stronger) |
| `uz.postprocess.get_volume_priority(vol_id)` | vol_id: int | float | Get priority value |
| `uz.postprocess.set_volume_blend_weight(vol_id, v)` | vol_id: int, v: float (0-1) | — | Set blend weight |
| `uz.postprocess.get_volume_blend_weight(vol_id)` | vol_id: int | float | Get blend weight |
| `uz.postprocess.set_volume_blend_radius(vol_id, v)` | vol_id: int, v: float (cm) | — | Set blend distance |
| `uz.postprocess.get_volume_blend_radius(vol_id)` | vol_id: int | float | Get blend distance |
| **Bloom** | | | |
| `uz.postprocess.set_bloom_intensity(vol_id, v)` | vol_id: int, v: float | — | Bloom brightness |
| `uz.postprocess.get_bloom_intensity(vol_id)` | vol_id: int | float | Get bloom intensity |
| `uz.postprocess.set_bloom_threshold(vol_id, v)` | vol_id: int, v: float | — | Bloom luminance threshold |
| `uz.postprocess.get_bloom_threshold(vol_id)` | vol_id: int | float | Get bloom threshold |
| **Exposure & Tonemap** | | | |
| `uz.postprocess.set_exposure_min(vol_id, v)` | vol_id: int, v: float (EV) | — | Auto-exposure minimum (EV) |
| `uz.postprocess.get_exposure_min(vol_id)` | vol_id: int | float | Get exposure min (EV) |
| `uz.postprocess.set_exposure_max(vol_id, v)` | vol_id: int, v: float (EV) | — | Auto-exposure maximum (EV) |
| `uz.postprocess.get_exposure_max(vol_id)` | vol_id: int | float | Get exposure max (EV) |
| `uz.postprocess.set_exposure_bias(vol_id, v)` | vol_id: int, v: float (EV) | — | Manual exposure bias |
| `uz.postprocess.get_exposure_bias(vol_id)` | vol_id: int | float | Get exposure bias (EV) |
| `uz.postprocess.set_exposure_speed_up(vol_id, v)` | vol_id: int, v: float (EV/s) | — | Adaptation speed (darker) |
| `uz.postprocess.get_exposure_speed_up(vol_id)` | vol_id: int | float | Get speed up |
| `uz.postprocess.set_exposure_speed_down(vol_id, v)` | vol_id: int, v: float (EV/s) | — | Adaptation speed (brighter) |
| `uz.postprocess.get_exposure_speed_down(vol_id)` | vol_id: int | float | Get speed down |
| `uz.postprocess.set_white_temp(vol_id, v)` | vol_id: int, v: float (K) | — | White balance temperature (Kelvin) |
| `uz.postprocess.get_white_temp(vol_id)` | vol_id: int | float | Get white temp (Kelvin) |
| `uz.postprocess.set_white_tint(vol_id, v)` | vol_id: int, v: float | — | White balance tint |
| `uz.postprocess.get_white_tint(vol_id)` | vol_id: int | float | Get white tint |
| **Depth of Field** | | | |
| `uz.postprocess.set_dof_focal_distance(vol_id, v)` | vol_id: int, v: float (cm) | — | DoF focal distance |
| `uz.postprocess.get_dof_focal_distance(vol_id)` | vol_id: int | float | Get focal distance |
| `uz.postprocess.set_dof_focal_region(vol_id, v)` | vol_id: int, v: float (cm) | — | DoF in-focus region |
| `uz.postprocess.get_dof_focal_region(vol_id)` | vol_id: int | float | Get focal region |
| `uz.postprocess.set_dof_near_transition(vol_id, v)` | vol_id: int, v: float (cm) | — | DoF near transition |
| `uz.postprocess.get_dof_near_transition(vol_id)` | vol_id: int | float | Get near transition |
| `uz.postprocess.set_dof_far_transition(vol_id, v)` | vol_id: int, v: float (cm) | — | DoF far transition |
| `uz.postprocess.get_dof_far_transition(vol_id)` | vol_id: int | float | Get far transition |
| `uz.postprocess.set_dof_fstop(vol_id, v)` | vol_id: int, v: float | — | DoF f-stop (higher = sharper) |
| `uz.postprocess.get_dof_fstop(vol_id)` | vol_id: int | float | Get f-stop |
| **Motion Blur** | | | |
| `uz.postprocess.set_motion_blur_amount(vol_id, v)` | vol_id: int, v: float | — | Motion blur strength |
| `uz.postprocess.get_motion_blur_amount(vol_id)` | vol_id: int | float | Get motion blur amount |
| `uz.postprocess.set_motion_blur_max(vol_id, v)` | vol_id: int, v: float (pixels) | — | Max motion blur pixels |
| `uz.postprocess.get_motion_blur_max(vol_id)` | vol_id: int | float | Get motion blur max |
| `uz.postprocess.set_motion_blur_target_fps(vol_id, v)` | vol_id: int, v: float | — | Motion blur target FPS |
| `uz.postprocess.get_motion_blur_target_fps(vol_id)` | vol_id: int | float | Get target FPS |
| **Film & Grading** | | | |
| `uz.postprocess.set_film_slope(vol_id, v)` | vol_id: int, v: float | — | Film curve slope |
| `uz.postprocess.get_film_slope(vol_id)` | vol_id: int | float | Get film slope |
| `uz.postprocess.set_film_toe(vol_id, v)` | vol_id: int, v: float | — | Film toe |
| `uz.postprocess.get_film_toe(vol_id)` | vol_id: int | float | Get film toe |
| `uz.postprocess.set_film_shoulder(vol_id, v)` | vol_id: int, v: float | — | Film shoulder |
| `uz.postprocess.get_film_shoulder(vol_id)` | vol_id: int | float | Get film shoulder |
| `uz.postprocess.set_film_black_clip(vol_id, v)` | vol_id: int, v: float | — | Film black point |
| `uz.postprocess.get_film_black_clip(vol_id)` | vol_id: int | float | Get film black clip |
| `uz.postprocess.set_film_white_clip(vol_id, v)` | vol_id: int, v: float | — | Film white point |
| `uz.postprocess.get_film_white_clip(vol_id)` | vol_id: int | float | Get film white clip |
| **Ambient Occlusion** | | | |
| `uz.postprocess.set_ao_intensity(vol_id, v)` | vol_id: int, v: float | — | AO strength (0-1) |
| `uz.postprocess.get_ao_intensity(vol_id)` | vol_id: int | float | Get AO intensity |
| `uz.postprocess.set_ao_radius(vol_id, v)` | vol_id: int, v: float (cm) | — | AO sampling radius |
| `uz.postprocess.get_ao_radius(vol_id)` | vol_id: int | float | Get AO radius |
| `uz.postprocess.set_ao_power(vol_id, v)` | vol_id: int, v: float | — | AO contrast power |
| `uz.postprocess.get_ao_power(vol_id)` | vol_id: int | float | Get AO power |
| **Screen-Space Reflections** | | | |
| `uz.postprocess.set_ssr_intensity(vol_id, v)` | vol_id: int, v: float | — | SSR strength (0-1) |
| `uz.postprocess.get_ssr_intensity(vol_id)` | vol_id: int | float | Get SSR intensity |
| `uz.postprocess.set_ssr_quality(vol_id, v)` | vol_id: int, v: float | — | SSR quality (0-1) |
| `uz.postprocess.get_ssr_quality(vol_id)` | vol_id: int | float | Get SSR quality |
| **Vignette** | | | |
| `uz.postprocess.set_vignette_intensity(vol_id, v)` | vol_id: int, v: float (0-1) | — | Vignette strength |
| `uz.postprocess.get_vignette_intensity(vol_id)` | vol_id: int | float | Get vignette intensity |
| **Override Management** | | | |
| `uz.postprocess.set_all_overrides(vol_id)` | vol_id: int | — | Enable all settings for blending |
| `uz.postprocess.clear_all_overrides(vol_id)` | vol_id: int | — | Disable all settings |
| `uz.postprocess.set_override(vol_id, name)` | vol_id: int, name: string | — | Enable specific setting for blending |
| `uz.postprocess.clear_override(vol_id, name)` | vol_id: int, name: string | — | Disable specific setting |
| **Query** | | | |
| `uz.postprocess.compute_blended(x, y, z)` | x, y, z: float (world position) | table or nil | Get blended settings at position |

## Examples

### Decal Damage

```lua
-- Create bullet hole decal at impact point
function on_bullet_hit(entity, impact_pos, impact_normal)
    local decal = uz.Entity()  -- Create a new entity for the decal
    uz.scene.set_position(decal, impact_pos.x, impact_pos.y, impact_pos.z)
    uz.scene.create_decal(decal)
    uz.scene.set_decal_extents(decal, 20, 20, 10)
    uz.scene.set_decal_albedo_texture(decal, bullet_hole_texture)
    uz.scene.set_decal_normal_texture(decal, bullet_hole_normal)
end
```

### Reflection Probe Placement

```lua
-- Place reflection probes in a polished interior
function setup_probes()
    local probes = {
        { pos = uz.Vec3(0, 0, 200), radius = 400 },
        { pos = uz.Vec3(400, 0, 200), radius = 400 },
        { pos = uz.Vec3(800, 0, 200), radius = 400 },
    }

    for i, p in ipairs(probes) do
        local probe_entity = uz.Entity()
        uz.scene.set_position(probe_entity, p.pos.x, p.pos.y, p.pos.z)
        uz.scene.create_reflection_probe(probe_entity)
        uz.scene.set_probe_influence_radius(probe_entity, p.radius)
        uz.scene.set_probe_resolution(probe_entity, 256)
        uz.scene.set_probe_priority(probe_entity, i)
    end
end
```

### Dynamic Post-Process Blending

```lua
-- Create underwater post-process volume
function enter_water_zone()
    local vol = uz.postprocess.add_volume()
    uz.postprocess.set_volume_enabled(vol, true)
    uz.postprocess.set_volume_blend_radius(vol, 500)  -- Smooth transition
    uz.postprocess.set_bloom_intensity(vol, 0.5)      -- Reduced bloom
    uz.postprocess.set_vignette_intensity(vol, 0.3)   -- Underwater vignette
    uz.postprocess.set_white_temp(vol, 4500)          -- Cooler color (underwater)
    return vol
end
```

## Engine Comparison

| Feature | UE5 | Godot | **UzEngine Lua** |
|---------|-----|-------|---|
| **Decals** | UMaterialDecalComponent | DecalVolume node | uz.scene.create_decal() |
| **Reflection Probes** | UReflectionCaptureComponent | ReflectionProbe3D node | uz.scene.create_reflection_probe() |
| **Post-Process** | APostProcessVolume | WorldEnvironment (deprecated) | uz.postprocess.add_volume() |
| **Accessibility** | C++/Blueprint only | GDScript only | Lua scripting + Phase 3 C++ |

