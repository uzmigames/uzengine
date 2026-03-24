#pragma once

/// lua_skylight_api — Lua bindings for SkyLightComponent.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// SkyLightComponent (intensity, color, source type, lower hemisphere, occlusion).
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # New Functions (added to uz.scene)
///
/// ## SkyLight (15.6)
/// - `uz.scene.create_sky_light(entity)` → void
/// - `uz.scene.set_sky_light_intensity(entity, intensity)` → void
/// - `uz.scene.set_sky_light_color(entity, r, g, b)` → void
/// - `uz.scene.set_sky_light_min_occlusion(entity, min_occlusion)` → void
/// - `uz.scene.set_sky_light_indirect_intensity(entity, v)` → void
/// - `uz.scene.set_sky_light_source_type(entity, type_str)` → void
/// - `uz.scene.set_sky_light_real_time_capture(entity, enabled)` → void
/// - `uz.scene.set_sky_light_cubemap_resolution(entity, res)` → void
/// - `uz.scene.set_sky_light_lower_hemisphere_black(entity, enabled)` → void
/// - `uz.scene.set_sky_light_lower_hemisphere_color(entity, r, g, b)` → void
/// - `uz.scene.set_sky_light_occlusion_max_distance(entity, dist)` → void
/// - `uz.scene.set_sky_light_occlusion_tint(entity, r, g, b)` → void
/// - `uz.scene.set_sky_light_occlusion_exponent(entity, exp)` → void
/// - `uz.scene.set_sky_light_enabled(entity, enabled)` → void

struct lua_State;

namespace uz {

/// Registers SkyLight functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterSkyLightAPI(lua_State* L);

} // namespace uz
