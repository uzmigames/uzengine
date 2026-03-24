#pragma once

/// lua_clouds_api — Lua bindings for VolumetricCloudsComponent.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// VolumetricCloudsComponent (layer geometry, density, wind, scattering,
/// ray march parameters).
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # New Functions (added to uz.scene)
///
/// ## VolumetricClouds — Setters
/// - `uz.scene.create_clouds(entity)` → void
/// - `uz.scene.set_clouds_layer_bottom_altitude(entity, km)` → void
/// - `uz.scene.set_clouds_layer_height(entity, km)` → void
/// - `uz.scene.set_clouds_coverage(entity, v)` → void
/// - `uz.scene.set_clouds_density(entity, v)` → void
/// - `uz.scene.set_clouds_type(entity, v)` → void
/// - `uz.scene.set_clouds_wind_velocity(entity, x, y, z)` → void
/// - `uz.scene.set_clouds_detail_wind_scale(entity, v)` → void
/// - `uz.scene.set_clouds_extinction(entity, v)` → void
/// - `uz.scene.set_clouds_scattering_albedo(entity, v)` → void
/// - `uz.scene.set_clouds_phase_g(entity, v)` → void
/// - `uz.scene.set_clouds_phase_g2(entity, v)` → void
/// - `uz.scene.set_clouds_phase_blend(entity, v)` → void
/// - `uz.scene.set_clouds_ambient_multiplier(entity, v)` → void
/// - `uz.scene.set_clouds_tracing_max_distance(entity, km)` → void
/// - `uz.scene.set_clouds_primary_steps(entity, n)` → void
/// - `uz.scene.set_clouds_light_steps(entity, n)` → void
/// - `uz.scene.set_clouds_enabled(entity, enabled)` → void
///
/// ## VolumetricClouds — Getters
/// - `uz.scene.get_clouds_layer_bottom_altitude(entity)` → number | nil
/// - `uz.scene.get_clouds_layer_height(entity)` → number | nil
/// - `uz.scene.get_clouds_coverage(entity)` → number | nil
/// - `uz.scene.get_clouds_density(entity)` → number | nil
/// - `uz.scene.get_clouds_type(entity)` → number | nil
/// - `uz.scene.get_clouds_wind_velocity(entity)` → x, y, z | nil
/// - `uz.scene.get_clouds_detail_wind_scale(entity)` → number | nil
/// - `uz.scene.get_clouds_extinction(entity)` → number | nil
/// - `uz.scene.get_clouds_scattering_albedo(entity)` → number | nil
/// - `uz.scene.get_clouds_phase_g(entity)` → number | nil
/// - `uz.scene.get_clouds_phase_g2(entity)` → number | nil
/// - `uz.scene.get_clouds_phase_blend(entity)` → number | nil
/// - `uz.scene.get_clouds_ambient_multiplier(entity)` → number | nil
/// - `uz.scene.get_clouds_tracing_max_distance(entity)` → number | nil
/// - `uz.scene.get_clouds_primary_steps(entity)` → int | nil
/// - `uz.scene.get_clouds_light_steps(entity)` → int | nil
/// - `uz.scene.is_clouds_enabled(entity)` → bool

struct lua_State;

namespace uz {

/// Registers VolumetricClouds functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterCloudsAPI(lua_State* L);

} // namespace uz
