#pragma once

/// lua_light_api — Lua bindings for light creation and manipulation.
///
/// Extends the `uz.scene` sub-table with functions for creating, querying,
/// and modifying directional, point, and spot lights on entities.
///
/// # New Functions (added to uz.scene)
/// - `uz.scene.add_directional_light(entity, r, g, b, intensity)` → void
/// - `uz.scene.add_point_light(entity, r, g, b, intensity, range)` → void
/// - `uz.scene.add_spot_light(entity, r, g, b, intensity, range, inner_deg, outer_deg)` → void
/// - `uz.scene.set_light_color(entity, r, g, b)` → void
/// - `uz.scene.set_light_intensity(entity, intensity)` → void
/// - `uz.scene.get_light_color(entity)` → r, g, b | nil
/// - `uz.scene.get_light_intensity(entity)` → number | nil
/// - `uz.scene.set_light_range(entity, range)` → void (point/spot only)
/// - `uz.scene.set_light_volumetric_scattering(entity, intensity)` → void
/// - `uz.scene.get_light_volumetric_scattering(entity)` → number | nil
/// - `uz.scene.set_light_indirect_intensity(entity, intensity)` → void
/// - `uz.scene.get_light_indirect_intensity(entity)` → number | nil
/// - `uz.scene.set_light_shadow_bias(entity, bias)` → void
/// - `uz.scene.get_light_shadow_bias(entity)` → number | nil
/// - `uz.scene.set_light_shadow_slope_bias(entity, bias)` → void
/// - `uz.scene.get_light_shadow_slope_bias(entity)` → number | nil
/// - `uz.scene.set_cast_shadows(entity, enabled)` → void (lights + static meshes)
///
/// ## DirectionalLight extended (15.5)
/// - `uz.scene.set_light_atmosphere_sun_index(entity, index)` → void
/// - `uz.scene.get_light_atmosphere_sun_index(entity)` → int | nil
/// - `uz.scene.set_light_source_angle(entity, degrees)` → void
/// - `uz.scene.get_light_source_angle(entity)` → number | nil
/// - `uz.scene.set_light_source_soft_angle(entity, degrees)` → void
/// - `uz.scene.get_light_source_soft_angle(entity)` → number | nil
/// - `uz.scene.set_light_shadow_cascades(entity, count)` → void
/// - `uz.scene.get_light_shadow_cascades(entity)` → int | nil
/// - `uz.scene.set_light_cascade_distribution_exponent(entity, exp)` → void
/// - `uz.scene.get_light_cascade_distribution_exponent(entity)` → number | nil
/// - `uz.scene.set_light_cascade_transition_fraction(entity, fraction)` → void
/// - `uz.scene.get_light_cascade_transition_fraction(entity)` → number | nil
/// - `uz.scene.set_light_shadow_distance_fadeout_fraction(entity, fraction)` → void
/// - `uz.scene.get_light_shadow_distance_fadeout_fraction(entity)` → number | nil
/// - `uz.scene.set_light_dynamic_shadow_distance(entity, cm)` → void
/// - `uz.scene.get_light_dynamic_shadow_distance(entity)` → number | nil
/// - `uz.scene.set_light_contact_shadow_length(entity, length)` → void
/// - `uz.scene.get_light_contact_shadow_length(entity)` → number | nil
/// - `uz.scene.set_light_far_shadow_cascade_count(entity, count)` → void
/// - `uz.scene.get_light_far_shadow_cascade_count(entity)` → int | nil
/// - `uz.scene.set_light_far_shadow_distance(entity, cm)` → void
/// - `uz.scene.get_light_far_shadow_distance(entity)` → number | nil
/// - `uz.scene.set_light_atmosphere_sun_disk_color_scale(entity, r, g, b)` → void
/// - `uz.scene.get_light_atmosphere_sun_disk_color_scale(entity)` → r, g, b | nil
///
/// ## PointLight extended (15.6)
/// - `uz.scene.get_light_use_inverse_squared_falloff(entity)` → bool | nil
/// - `uz.scene.get_light_falloff_exponent(entity)` → number | nil
/// - `uz.scene.set_light_soft_source_radius(entity, radius)` → void
/// - `uz.scene.get_light_soft_source_radius(entity)` → number | nil
/// - `uz.scene.set_light_source_length(entity, length)` → void
/// - `uz.scene.get_light_source_length(entity)` → number | nil
/// - `uz.scene.set_light_point_shadow_bias(entity, bias)` → void
/// - `uz.scene.get_light_point_shadow_bias(entity)` → number | nil
/// - `uz.scene.get_light_source_radius(entity)` → number | nil
///
/// ## SpotLight extended (15.7)
/// - `uz.scene.set_light_spot_shadow_bias(entity, bias)` → void
/// - `uz.scene.get_light_spot_shadow_bias(entity)` → number | nil

struct lua_State;

namespace uz {

/// Registers light manipulation functions into the existing `uz.scene` table.
///
/// Populates `uz.scene` with bindings for creating directional, point, and spot
/// lights, and for querying/modifying light color, intensity, and range.
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterLightAPI(lua_State* L);

} // namespace uz
