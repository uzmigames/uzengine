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

### Material Properties

Access and modify material properties on entities with StaticMeshComponent:

```lua
-- Get all material properties (returns table with 20 fields)
local mat = uz.scene.get_material(entity)
if mat then
    -- Base color
    print(mat.base_color_r, mat.base_color_g, mat.base_color_b)
    -- PBR parameters
    print(mat.roughness_multiplier)
    print(mat.metallic_multiplier)
    -- Normal and detail
    print(mat.normal_intensity)
    print(mat.ao_intensity)
    -- Transparency
    print(mat.opacity)
    print(mat.alpha_clip_threshold)
    print(mat.alpha_test)
    print(mat.two_sided)
    -- Emission
    print(mat.emissive_color_r, mat.emissive_color_g, mat.emissive_color_b)
    print(mat.emissive_intensity)
    -- Texture coordinates
    print(mat.uv_scale_x, mat.uv_scale_y)
    print(mat.uv_offset_x, mat.uv_offset_y)
end
```

### Material API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.scene.get_material(entity)` | entity | table or nil | Get all material fields: base_color_r/g/b, roughness_multiplier, metallic_multiplier, normal_intensity, ao_intensity, opacity, alpha_test, alpha_clip_threshold, two_sided, emissive_color_r/g/b, emissive_intensity, uv_scale_x/y, uv_offset_x/y |

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
-- Create and configure height-based volumetric fog (exponential)
uz.scene.create_fog(entity)                                        -- Add HeightFogComponent
uz.scene.set_fog_density(entity, 0.01)                             -- Primary fog density
uz.scene.set_fog_color(entity, 0.8, 0.85, 0.95)                   -- Fog color (linear RGB)
uz.scene.set_fog_height_falloff(entity, 0.2)                       -- Exponential height falloff
uz.scene.set_fog_base_height(entity, 100.0)                        -- Fog base height (cm)
uz.scene.set_fog_max_opacity(entity, 1.0)                          -- Maximum opacity (0-1)
uz.scene.set_fog_enabled(entity, true)                             -- Toggle fog on/off
uz.scene.set_fog_start_distance(entity, 100.0)                     -- Minimum distance before fog begins
uz.scene.set_fog_cutoff_distance(entity, 100000.0)                 -- Maximum fog distance
uz.scene.set_fog_second_density(entity, 0.005)                     -- Second fog layer density (0 = disabled)
uz.scene.set_fog_second_height_falloff(entity, 0.15)               -- Second layer height falloff
uz.scene.set_fog_second_base_height(entity, 500.0)                 -- Second layer base height (cm)
uz.scene.set_fog_directional_inscattering_color(entity, 1, 0.8, 0.6)  -- Sun-facing fog glow color
uz.scene.set_fog_directional_inscattering_exponent(entity, 8.0)    -- Inscattering cone tightness
uz.scene.set_fog_directional_inscattering_start_distance(entity, 1000.0)  -- Inscattering start dist (cm)
uz.scene.set_fog_volumetric(entity, false)                         -- Enable volumetric fog (Phase 3+)
```

#### Sky Light (IBL Ambient)

```lua
-- Add sky light component for ambient lighting
uz.scene.create_sky_light(entity)

-- Configure sky light intensity and color
uz.scene.set_sky_light_intensity(entity, 1.0)                      -- Intensity multiplier
uz.scene.set_sky_light_color(entity, 1.0, 1.0, 1.0)                -- Tint color (linear RGB)
uz.scene.set_sky_light_min_occlusion(entity, 0.1)                  -- Minimum ambient occlusion (floor)
uz.scene.set_sky_light_indirect_intensity(entity, 2.0)             -- Diffuse IBL multiplier
uz.scene.set_sky_light_source_type(entity, "captured")             -- "captured" or "cubemap"
uz.scene.set_sky_light_real_time_capture(entity, true)             -- Recapture every frame
uz.scene.set_sky_light_cubemap_resolution(entity, 128)             -- Capture res per face (power of 2)
uz.scene.set_sky_light_lower_hemisphere_black(entity, false)       -- Solid color below horizon
uz.scene.set_sky_light_lower_hemisphere_color(entity, 0.1, 0.1, 0.15)  -- Lower hemisphere color
uz.scene.set_sky_light_occlusion_max_distance(entity, 1000.0)      -- AO influence distance (cm)
uz.scene.set_sky_light_occlusion_tint(entity, 0.3, 0.3, 0.3)       -- Occluded area tint
uz.scene.set_sky_light_occlusion_exponent(entity, 2.0)             -- AO falloff exponent
uz.scene.set_sky_light_enabled(entity, true)                       -- Enable/disable sky light
```

#### Sky Atmosphere (Physically-Based)

```lua
-- Create and configure physically-based sky atmosphere
uz.scene.create_sky_atmosphere(entity)                             -- Add SkyAtmosphereComponent (idempotent)

-- Core atmospheric parameters (Bruneton 2017 method)
uz.scene.set_sky_bottom_radius(entity, 6360.0)                     -- Planet radius (km, default 6360)
uz.scene.set_sky_atmosphere_height(entity, 60.0)                   -- Atmosphere thickness (km, default 60)

-- Rayleigh scattering (molecular, causes blue sky)
uz.scene.set_sky_rayleigh_scattering(entity, 5.8e-6, 13.5e-6, 33.1e-6)  -- Coefficients km^-1
uz.scene.set_sky_rayleigh_scale_height(entity, 8.0)                -- Scale height (km, default 8)

-- Mie scattering (aerosol, causes haze)
uz.scene.set_sky_mie_scattering(entity, 10.0e-6)                   -- Coefficient km^-1
uz.scene.set_sky_mie_absorption(entity, 4.4e-6)                    -- Absorption km^-1
uz.scene.set_sky_mie_absorption_scale(entity, 1.0)                 -- Absorption multiplier
uz.scene.set_sky_mie_anisotropy(entity, 0.8)                       -- HG phase function [-1,1]
uz.scene.set_sky_mie_scale_height(entity, 1.2)                     -- Scale height (km, default 1.2)

-- Ozone absorption (upper atmosphere)
uz.scene.set_sky_ozone_absorption(entity, 1.36e-6, 0.0, 0.0)       -- Absorption km^-1 (only R)
uz.scene.set_sky_ozone_tip_altitude(entity, 25.0)                  -- Peak altitude (km, default 25)
uz.scene.set_sky_ozone_tip_value(entity, 0.08)                     -- Peak density
uz.scene.set_sky_ozone_width(entity, 15.0)                         -- Half-width (km, default 15)

-- Ground and sky
uz.scene.set_sky_ground_albedo(entity, 0.1, 0.1, 0.1)              -- Ground reflectance
uz.scene.set_sky_luminance_factor(entity, 1.0, 1.0, 1.0)           -- Per-channel sky luminance multiplier

-- Integration
uz.scene.set_sky_height_fog_contribution(entity, 0.5)              -- Atmosphere contribution to height fog
```

#### Light Extra Parameters

```lua
-- Volumetric light and indirect lighting
uz.scene.set_light_volumetric_scattering(entity, 1.0)   -- Volumetric scattering intensity
uz.scene.get_light_volumetric_scattering(entity)        -- Get volumetric scattering intensity
uz.scene.set_light_indirect_intensity(entity, 1.0)      -- Indirect lighting multiplier
uz.scene.get_light_indirect_intensity(entity)           -- Get indirect intensity
uz.scene.set_light_shadow_bias(entity, 0.001)           -- Depth bias for shadow filtering
uz.scene.get_light_shadow_bias(entity)                  -- Get shadow bias
uz.scene.set_light_shadow_slope_bias(entity, 0.002)     -- Slope-based bias
uz.scene.get_light_shadow_slope_bias(entity)            -- Get slope bias

-- Cast shadows now works on StaticMeshComponent too
uz.scene.set_cast_shadows(entity, true)                 -- Works on lights and static meshes
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
| `uz.scene.set_light_volumetric_scattering(entity, intensity)` | entity, intensity: float | — | Volumetric light scattering |
| `uz.scene.get_light_volumetric_scattering(entity)` | entity | float | Get volumetric scattering intensity |
| `uz.scene.set_light_indirect_intensity(entity, intensity)` | entity, intensity: float | — | Indirect lighting multiplier |
| `uz.scene.get_light_indirect_intensity(entity)` | entity | float | Get indirect intensity |
| `uz.scene.set_light_shadow_bias(entity, bias)` | entity, bias: float | — | Depth bias for shadow filtering |
| `uz.scene.get_light_shadow_bias(entity)` | entity | float | Get shadow bias |
| `uz.scene.set_light_shadow_slope_bias(entity, bias)` | entity, bias: float | — | Slope-based depth bias |
| `uz.scene.get_light_shadow_slope_bias(entity)` | entity | float | Get slope bias |
| **Light Cascades (Directional Only)** | | | |
| `uz.scene.set_light_shadow_cascades(entity, count)` | entity, count: int (1-4) | — | Number of cascade splits |
| `uz.scene.get_light_shadow_cascades(entity)` | entity | int or nil | Get cascade count |
| `uz.scene.set_light_cascade_distribution_exponent(entity, v)` | entity, v: float | — | Cascade distribution curve |
| `uz.scene.get_light_cascade_distribution_exponent(entity)` | entity | float or nil | Get cascade distribution |
| `uz.scene.set_light_cascade_transition_fraction(entity, v)` | entity, v: float (0-1) | — | Cascade blend fraction |
| `uz.scene.get_light_cascade_transition_fraction(entity)` | entity | float or nil | Get cascade transition |
| `uz.scene.set_light_dynamic_shadow_distance(entity, cm)` | entity, cm: float | — | Dynamic shadow distance |
| `uz.scene.get_light_dynamic_shadow_distance(entity)` | entity | float or nil | Get dynamic shadow distance |
| `uz.scene.set_light_far_shadow_cascade_count(entity, count)` | entity, count: int | — | Far cascade count |
| `uz.scene.get_light_far_shadow_cascade_count(entity)` | entity | int or nil | Get far cascade count |
| `uz.scene.set_light_far_shadow_distance(entity, cm)` | entity, cm: float | — | Far shadow distance |
| `uz.scene.get_light_far_shadow_distance(entity)` | entity | float or nil | Get far shadow distance |
| `uz.scene.set_light_shadow_distance_fadeout_fraction(entity, v)` | entity, v: float (0-1) | — | Shadow fade-out distance |
| `uz.scene.get_light_shadow_distance_fadeout_fraction(entity)` | entity | float or nil | Get fadeout fraction |
| **Point Light Shadows** | | | |
| `uz.scene.set_light_point_shadow_bias(entity, bias)` | entity, bias: float | — | Point light shadow bias |
| `uz.scene.get_light_point_shadow_bias(entity)` | entity | float or nil | Get point shadow bias |
| **Spot Light Shadows** | | | |
| `uz.scene.set_light_spot_shadow_bias(entity, bias)` | entity, bias: float | — | Spot light shadow bias |
| `uz.scene.get_light_spot_shadow_bias(entity)` | entity | float or nil | Get spot shadow bias |
| **Contact Shadows** | | | |
| `uz.scene.set_light_contact_shadow_length(entity, cm)` | entity, cm: float | — | Contact shadow trace length |
| `uz.scene.get_light_contact_shadow_length(entity)` | entity | float or nil | Get contact shadow length |
| **Area Lights** | | | |
| `uz.scene.set_light_source_length(entity, cm)` | entity, cm: float | — | Rectangular light length |
| `uz.scene.get_light_source_length(entity)` | entity | float or nil | Get source length |
| `uz.scene.set_light_source_angle(entity, radians)` | entity, radians: float | — | Rectangular light angle |
| `uz.scene.get_light_source_angle(entity)` | entity | float or nil | Get source angle |
| `uz.scene.set_light_source_soft_angle(entity, radians)` | entity, radians: float | — | Source soft edge angle |
| `uz.scene.get_light_source_soft_angle(entity)` | entity | float or nil | Get source soft angle |
| `uz.scene.set_light_soft_source_radius(entity, cm)` | entity, cm: float | — | Soft source radius |
| `uz.scene.get_light_soft_source_radius(entity)` | entity | float or nil | Get soft source radius |
| **Spot Light Specific** | | | |
| `uz.scene.set_spot_angles(entity, inner, outer)` | entity, inner, outer: float (degrees) | — | Spot light cone angles |
| `uz.scene.get_spot_angles(entity)` | entity | (inner, outer) or nil | Get spot cone angles |
| **Atmosphere Integration** | | | |
| `uz.scene.set_light_atmosphere_sun_index(entity, idx)` | entity, idx: int (0-3) | — | Atmosphere sun index |
| `uz.scene.get_light_atmosphere_sun_index(entity)` | entity | int or nil | Get atmosphere sun index |
| `uz.scene.set_light_atmosphere_sun_disk_color_scale(entity, v)` | entity, v: float | — | Sun disk color scale |
| `uz.scene.get_light_atmosphere_sun_disk_color_scale(entity)` | entity | float or nil | Get sun disk color scale |
| `uz.scene.set_light_use_inverse_squared_falloff(entity, bool)` | entity, bool: bool | — | Use inverse squared falloff |
| `uz.scene.get_light_use_inverse_squared_falloff(entity)` | entity | bool or nil | Check inverse squared falloff |
| `uz.scene.set_light_falloff_exponent(entity, v)` | entity, v: float | — | Light falloff exponent |
| `uz.scene.get_light_falloff_exponent(entity)` | entity | float or nil | Get falloff exponent |
| **Other Light Properties** | | | |
| `uz.scene.add_directional_light(entity)` | entity | — | Add directional light component |
| `uz.scene.add_point_light(entity)` | entity | — | Add point light component |
| `uz.scene.add_spot_light(entity)` | entity | — | Add spot light component |
| `uz.scene.remove_light(entity)` | entity | — | Remove light component |
| `uz.scene.get_light_intensity(entity)` | entity | float or nil | Get light intensity |
| `uz.scene.get_light_color(entity)` | entity | (r, g, b) or nil | Get light color (linear RGB) |
| `uz.scene.get_light_range(entity)` | entity | float or nil | Get light range (cm) |
| `uz.scene.get_light_direction(entity)` | entity | (x, y, z) or nil | Get light direction (normalized) |
| **Height Fog** | | | |
| `uz.scene.create_fog(entity)` | entity | — | Add HeightFogComponent (idempotent) |
| `uz.scene.set_fog_density(entity, density)` | entity, density: float | — | Primary fog density |
| `uz.scene.set_fog_color(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Fog color (linear RGB) |
| `uz.scene.set_fog_height_falloff(entity, falloff)` | entity, falloff: float | — | Exponential height falloff curve |
| `uz.scene.set_fog_base_height(entity, height)` | entity, height: float (cm) | — | Fog base altitude |
| `uz.scene.set_fog_max_opacity(entity, opacity)` | entity, opacity: float (0-1) | — | Maximum fog opacity |
| `uz.scene.set_fog_enabled(entity, enabled)` | entity, enabled: bool | — | Toggle fog on/off |
| `uz.scene.set_fog_start_distance(entity, distance)` | entity, distance: float (cm) | — | Minimum distance before fog begins |
| `uz.scene.set_fog_cutoff_distance(entity, distance)` | entity, distance: float (cm) | — | Maximum fog distance |
| `uz.scene.set_fog_second_density(entity, density)` | entity, density: float | — | Second fog layer density (0 = disabled) |
| `uz.scene.set_fog_second_height_falloff(entity, falloff)` | entity, falloff: float | — | Second layer height falloff |
| `uz.scene.set_fog_second_base_height(entity, height)` | entity, height: float (cm) | — | Second layer base height |
| `uz.scene.set_fog_directional_inscattering_color(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Sun-facing fog glow color |
| `uz.scene.set_fog_directional_inscattering_exponent(entity, exponent)` | entity, exponent: float | — | Inscattering cone tightness |
| `uz.scene.set_fog_directional_inscattering_start_distance(entity, distance)` | entity, distance: float (cm) | — | Inscattering start distance |
| `uz.scene.set_fog_volumetric(entity, enabled)` | entity, enabled: bool | — | Enable volumetric fog (Phase 3+) |
| `uz.scene.get_fog_density(entity)` | entity | float or nil | Get primary fog density |
| `uz.scene.get_fog_color(entity)` | entity | (r, g, b) or nil | Get fog color (linear RGB) |
| `uz.scene.get_fog_height_falloff(entity)` | entity | float or nil | Get height falloff curve |
| `uz.scene.get_fog_base_height(entity)` | entity | float or nil | Get fog base altitude (cm) |
| `uz.scene.get_fog_max_opacity(entity)` | entity | float or nil | Get maximum fog opacity (0-1) |
| `uz.scene.get_fog_start_distance(entity)` | entity | float or nil | Get fog start distance (cm) |
| `uz.scene.get_fog_cutoff_distance(entity)` | entity | float or nil | Get fog cutoff distance (cm) |
| `uz.scene.get_fog_second_density(entity)` | entity | float or nil | Get second layer density |
| `uz.scene.get_fog_second_height_falloff(entity)` | entity | float or nil | Get second layer height falloff |
| `uz.scene.get_fog_second_base_height(entity)` | entity | float or nil | Get second layer base height (cm) |
| `uz.scene.get_fog_directional_inscattering_color(entity)` | entity | (r, g, b) or nil | Get inscattering color |
| `uz.scene.get_fog_directional_inscattering_exponent(entity)` | entity | float or nil | Get inscattering cone tightness |
| `uz.scene.get_fog_directional_inscattering_start_distance(entity)` | entity | float or nil | Get inscattering start distance |
| **Sky Light (IBL)** | | | |
| `uz.scene.create_sky_light(entity)` | entity | — | Add SkyLightComponent |
| `uz.scene.set_sky_light_intensity(entity, intensity)` | entity, intensity: float | — | Intensity multiplier |
| `uz.scene.set_sky_light_color(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Tint color (linear RGB) |
| `uz.scene.set_sky_light_min_occlusion(entity, min_occlusion)` | entity, min_occlusion: float (0-1) | — | Minimum ambient occlusion floor |
| `uz.scene.set_sky_light_indirect_intensity(entity, v)` | entity, v: float | — | Diffuse IBL multiplier |
| `uz.scene.set_sky_light_source_type(entity, type_str)` | entity, type_str: "captured"/"cubemap" | — | Cubemap source type |
| `uz.scene.set_sky_light_real_time_capture(entity, bool)` | entity, bool: bool | — | Recapture every frame |
| `uz.scene.set_sky_light_cubemap_resolution(entity, res)` | entity, res: uint (power of 2) | — | Capture resolution per face |
| `uz.scene.set_sky_light_lower_hemisphere_black(entity, bool)` | entity, bool: bool | — | Solid color below horizon |
| `uz.scene.set_sky_light_lower_hemisphere_color(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Lower hemisphere color |
| `uz.scene.set_sky_light_occlusion_max_distance(entity, dist)` | entity, dist: float (cm) | — | AO influence distance |
| `uz.scene.set_sky_light_occlusion_tint(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Occluded area tint |
| `uz.scene.set_sky_light_occlusion_exponent(entity, exp)` | entity, exp: float | — | AO falloff exponent |
| `uz.scene.set_sky_light_enabled(entity, bool)` | entity, bool: bool | — | Enable/disable sky light |
| `uz.scene.get_sky_light_intensity(entity)` | entity | float or nil | Get intensity multiplier |
| `uz.scene.get_sky_light_color(entity)` | entity | (r, g, b) or nil | Get tint color (linear RGB) |
| `uz.scene.get_sky_light_min_occlusion(entity)` | entity | float or nil | Get minimum ambient occlusion floor |
| `uz.scene.get_sky_light_indirect_intensity(entity)` | entity | float or nil | Get diffuse IBL multiplier |
| `uz.scene.get_sky_light_source_type(entity)` | entity | "captured" or "cubemap" or nil | Get cubemap source type |
| `uz.scene.get_sky_light_cubemap_resolution(entity)` | entity | uint or nil | Get capture resolution per face |
| `uz.scene.get_sky_light_lower_hemisphere_color(entity)` | entity | (r, g, b) or nil | Get lower hemisphere color |
| `uz.scene.get_sky_light_occlusion_max_distance(entity)` | entity | float or nil | Get AO influence distance (cm) |
| `uz.scene.get_sky_light_occlusion_tint(entity)` | entity | (r, g, b) or nil | Get occluded area tint |
| `uz.scene.get_sky_light_occlusion_exponent(entity)` | entity | float or nil | Get AO falloff exponent |
| **Sky Atmosphere** | | | |
| `uz.scene.create_sky_atmosphere(entity)` | entity | — | Add SkyAtmosphereComponent (idempotent) |
| `uz.scene.set_sky_bottom_radius(entity, km)` | entity, km: float | — | Planet radius (default 6360 km) |
| `uz.scene.set_sky_atmosphere_height(entity, km)` | entity, km: float | — | Atmosphere thickness (default 60 km) |
| `uz.scene.set_sky_rayleigh_scattering(entity, r, g, b)` | entity, r, g, b: float | — | Rayleigh coefficients km^-1 |
| `uz.scene.set_sky_rayleigh_scale_height(entity, km)` | entity, km: float | — | Rayleigh scale height (default 8 km) |
| `uz.scene.set_sky_mie_scattering(entity, value)` | entity, value: float | — | Mie coefficient km^-1 |
| `uz.scene.set_sky_mie_absorption(entity, value)` | entity, value: float | — | Mie absorption km^-1 |
| `uz.scene.set_sky_mie_absorption_scale(entity, scale)` | entity, scale: float | — | Mie absorption multiplier |
| `uz.scene.set_sky_mie_anisotropy(entity, g)` | entity, g: float [-1,1] | — | HG phase function |
| `uz.scene.set_sky_mie_scale_height(entity, km)` | entity, km: float | — | Mie scale height (default 1.2 km) |
| `uz.scene.set_sky_ground_albedo(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Ground reflectance |
| `uz.scene.set_sky_ozone_absorption(entity, r, g, b)` | entity, r, g, b: float | — | Ozone absorption km^-1 |
| `uz.scene.set_sky_ozone_tip_altitude(entity, km)` | entity, km: float | — | Ozone peak altitude (default 25 km) |
| `uz.scene.set_sky_ozone_tip_value(entity, v)` | entity, v: float | — | Ozone peak density |
| `uz.scene.set_sky_ozone_width(entity, km)` | entity, km: float | — | Ozone half-width (default 15 km) |
| `uz.scene.set_sky_luminance_factor(entity, r, g, b)` | entity, r, g, b: float (0-1) | — | Per-channel sky luminance multiplier |
| `uz.scene.set_sky_height_fog_contribution(entity, v)` | entity, v: float | — | Atmosphere contribution to height fog |
| `uz.scene.set_atmosphere_transform_mode(entity, mode)` | entity, mode: string | — | Sky atmosphere transform mode |
| `uz.scene.get_atmosphere_transform_mode(entity)` | entity | string or nil | Get sky atmosphere transform mode |
| `uz.scene.set_atmosphere_sun_illuminance(entity, lux)` | entity, lux: float | — | Sun illuminance in lux |
| `uz.scene.get_atmosphere_sun_illuminance(entity)` | entity | float or nil | Get sun illuminance in lux |
| `uz.scene.set_atmosphere_sun_angular_radius(entity, radians)` | entity, radians: float | — | Sun disk angular radius |
| `uz.scene.get_atmosphere_sun_angular_radius(entity)` | entity | float or nil | Get sun disk angular radius |
| `uz.scene.set_atmosphere_sun_disk_intensity(entity, intensity)` | entity, intensity: float | — | Sun disk intensity scale |
| `uz.scene.get_atmosphere_sun_disk_intensity(entity)` | entity | float or nil | Get sun disk intensity scale |
| `uz.scene.set_atmosphere_enabled(entity, enabled)` | entity, enabled: bool | — | Enable/disable atmosphere |
| `uz.scene.is_atmosphere_enabled(entity)` | entity | bool or nil | Check if atmosphere is enabled |
| `uz.scene.get_atmosphere_bottom_radius(entity)` | entity | float or nil | Get planet radius (km) |
| `uz.scene.get_atmosphere_height(entity)` | entity | float or nil | Get atmosphere thickness (km) |
| `uz.scene.get_atmosphere_rayleigh_scattering(entity)` | entity | (r, g, b) or nil | Get Rayleigh coefficients km^-1 |
| `uz.scene.get_atmosphere_rayleigh_scale_height(entity)` | entity | float or nil | Get Rayleigh scale height (km) |
| `uz.scene.get_atmosphere_mie_scattering(entity)` | entity | float or nil | Get Mie coefficient km^-1 |
| `uz.scene.get_atmosphere_mie_absorption(entity)` | entity | float or nil | Get Mie absorption km^-1 |
| `uz.scene.get_atmosphere_mie_absorption_scale(entity)` | entity | float or nil | Get Mie absorption multiplier |
| `uz.scene.get_atmosphere_mie_anisotropy(entity)` | entity | float or nil | Get HG phase function |
| `uz.scene.get_atmosphere_mie_scale_height(entity)` | entity | float or nil | Get Mie scale height (km) |
| `uz.scene.get_atmosphere_ground_albedo(entity)` | entity | (r, g, b) or nil | Get ground reflectance |
| `uz.scene.get_atmosphere_ozone_absorption(entity)` | entity | (r, g, b) or nil | Get ozone absorption km^-1 |
| `uz.scene.get_atmosphere_ozone_tip_altitude(entity)` | entity | float or nil | Get ozone peak altitude (km) |
| `uz.scene.get_atmosphere_ozone_tip_value(entity)` | entity | float or nil | Get ozone peak density |
| `uz.scene.get_atmosphere_ozone_width(entity)` | entity | float or nil | Get ozone half-width (km) |
| `uz.scene.get_atmosphere_luminance_factor(entity)` | entity | (r, g, b) or nil | Get per-channel sky luminance multiplier |
| `uz.scene.get_atmosphere_height_fog_contribution(entity)` | entity | float or nil | Get atmosphere contribution to height fog |

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

## Volumetric Clouds

### Cloud System

Volumetric clouds provide realistic cloud rendering with physically-based scattering and wind-driven animation.

```lua
-- Create volumetric clouds
uz.scene.create_clouds(entity)

-- Configure cloud layer
uz.scene.set_clouds_layer_bottom_altitude(entity, 5.0)   -- Base altitude (km, default 5)
uz.scene.set_clouds_layer_height(entity, 10.0)           -- Layer thickness (km, default 10)
uz.scene.set_clouds_coverage(entity, 0.5)                -- Global coverage [0,1]
uz.scene.set_clouds_density(entity, 1.0)                 -- Density multiplier
uz.scene.set_clouds_type(entity, 0.5)                    -- Cloud type: 0=stratus, 1=cumulus

-- Wind and animation
uz.scene.set_clouds_wind_velocity(entity, 0.1, 0.0, 0.0)    -- Wind in km/s (X, Y, Z)
uz.scene.set_clouds_detail_wind_scale(entity, 2.0)          -- Detail noise wind multiplier

-- Light interaction
uz.scene.set_clouds_extinction(entity, 0.2)                  -- Extinction coefficient km^-1
uz.scene.set_clouds_scattering_albedo(entity, 0.9)           -- Scattering ratio [0,1]
uz.scene.set_clouds_ambient_multiplier(entity, 0.5)          -- Ambient fill multiplier

-- Phase function (scattering directionality)
uz.scene.set_clouds_phase_g(entity, 0.4)                     -- Primary HG phase eccentricity
uz.scene.set_clouds_phase_g2(entity, -0.2)                   -- Secondary phase eccentricity
uz.scene.set_clouds_phase_blend(entity, 0.8)                 -- Blend between phase lobes [0,1]

-- Ray marching
uz.scene.set_clouds_tracing_max_distance(entity, 50.0)       -- Max ray march distance (km)
uz.scene.set_clouds_primary_steps(entity, 64)                -- Primary ray march steps
uz.scene.set_clouds_light_steps(entity, 6)                   -- Light march steps

-- Toggle
uz.scene.set_clouds_enabled(entity, true)                    -- Enable/disable clouds
```

### Volumetric Clouds API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.scene.create_clouds(entity)` | entity | — | Add VolumetricCloudsComponent (idempotent) |
| `uz.scene.set_clouds_layer_bottom_altitude(entity, km)` | entity, km: float | — | Cloud base altitude (default 5 km) |
| `uz.scene.set_clouds_layer_height(entity, km)` | entity, km: float | — | Cloud layer thickness (default 10 km) |
| `uz.scene.set_clouds_coverage(entity, v)` | entity, v: float [0,1] | — | Global coverage |
| `uz.scene.set_clouds_density(entity, v)` | entity, v: float | — | Density multiplier |
| `uz.scene.set_clouds_type(entity, v)` | entity, v: float [0,1] | — | Cloud type (0=stratus, 1=cumulus) |
| `uz.scene.set_clouds_wind_velocity(entity, x, y, z)` | entity, x, y, z: float | — | Wind in km/s |
| `uz.scene.set_clouds_detail_wind_scale(entity, v)` | entity, v: float | — | Detail noise wind multiplier |
| `uz.scene.set_clouds_extinction(entity, v)` | entity, v: float | — | Extinction coefficient km^-1 |
| `uz.scene.set_clouds_scattering_albedo(entity, v)` | entity, v: float [0,1] | — | Scattering ratio |
| `uz.scene.set_clouds_phase_g(entity, v)` | entity, v: float | — | Primary HG phase eccentricity |
| `uz.scene.set_clouds_phase_g2(entity, v)` | entity, v: float | — | Secondary phase eccentricity |
| `uz.scene.set_clouds_phase_blend(entity, v)` | entity, v: float [0,1] | — | Blend between phase lobes |
| `uz.scene.set_clouds_ambient_multiplier(entity, v)` | entity, v: float | — | Ambient fill multiplier |
| `uz.scene.set_clouds_tracing_max_distance(entity, km)` | entity, km: float | — | Max ray march distance |
| `uz.scene.set_clouds_primary_steps(entity, n)` | entity, n: integer | — | Primary ray march steps |
| `uz.scene.set_clouds_light_steps(entity, n)` | entity, n: integer | — | Light march steps |
| `uz.scene.set_clouds_enabled(entity, bool)` | entity, bool: bool | — | Enable/disable clouds |
| `uz.scene.get_clouds_layer_bottom_altitude(entity)` | entity | float or nil | Get cloud base altitude (km) |
| `uz.scene.get_clouds_layer_height(entity)` | entity | float or nil | Get cloud layer thickness (km) |
| `uz.scene.get_clouds_coverage(entity)` | entity | float or nil | Get global coverage (0-1) |
| `uz.scene.get_clouds_density(entity)` | entity | float or nil | Get density multiplier |
| `uz.scene.get_clouds_type(entity)` | entity | float or nil | Get cloud type (0=stratus, 1=cumulus) |
| `uz.scene.get_clouds_wind_velocity(entity)` | entity | (x, y, z) or nil | Get wind in km/s |
| `uz.scene.get_clouds_detail_wind_scale(entity)` | entity | float or nil | Get detail noise wind multiplier |
| `uz.scene.get_clouds_extinction(entity)` | entity | float or nil | Get extinction coefficient km^-1 |
| `uz.scene.get_clouds_scattering_albedo(entity)` | entity | float or nil | Get scattering ratio (0-1) |
| `uz.scene.get_clouds_phase_g(entity)` | entity | float or nil | Get primary HG phase eccentricity |
| `uz.scene.get_clouds_phase_g2(entity)` | entity | float or nil | Get secondary phase eccentricity |
| `uz.scene.get_clouds_phase_blend(entity)` | entity | float or nil | Get blend between phase lobes (0-1) |
| `uz.scene.get_clouds_ambient_multiplier(entity)` | entity | float or nil | Get ambient fill multiplier |
| `uz.scene.get_clouds_tracing_max_distance(entity)` | entity | float or nil | Get max ray march distance (km) |
| `uz.scene.get_clouds_primary_steps(entity)` | entity | integer or nil | Get primary ray march steps |
| `uz.scene.get_clouds_light_steps(entity)` | entity | integer or nil | Get light march steps |
| `uz.scene.is_clouds_enabled(entity)` | entity | bool or nil | Check if clouds are enabled |

## Mesh Generation

### Procedural Mesh API

```lua
-- Generate a UV sphere
local mesh_id = uz.mesh.generate_sphere(32, 16)  -- sectors=32, stacks=16

-- Generate a flat plane
local mesh_id = uz.mesh.generate_plane(1.0)      -- uv_scale=1.0

-- Use mesh in a StaticMeshComponent
if mesh_id then
    uz.scene.set_mesh(entity, mesh_id)
end
```

### Mesh Generation API Reference

| Function | Parameters | Return | Notes |
|----------|-----------|--------|-------|
| `uz.mesh.generate_sphere(sectors, stacks)` | sectors: integer, stacks: integer | mesh_id or nil | Generate UV sphere (default 32x16) |
| `uz.mesh.generate_plane(uv_scale)` | uv_scale: float | mesh_id or nil | Generate flat quad (default uv_scale=1.0) |

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

-- Read the active camera's pose (all return nil when no camera is active).
-- get_forward follows the renderer's rule: the look-at target when one is
-- set (scene `look_at` or uz.camera.look_at), else rotation * Forward.
local px, py, pz = uz.camera.get_position()
local fx, fy, fz = uz.camera.get_forward()

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
| `uz.camera.get_position()` | — | x, y, z: float \| nil | Active camera world position |
| `uz.camera.get_forward()` | — | x, y, z: float \| nil | Unit forward direction (Z-up world): look-at target when set, else rotation × Forward. Lets a controller start from the scene camera instead of teleporting |

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
