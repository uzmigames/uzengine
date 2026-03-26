#pragma once

/// lua_atmosphere_api — Lua bindings for SkyAtmosphereComponent.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// SkyAtmosphereComponent (planet geometry, Rayleigh, Mie, ozone, art direction).
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # New Functions (added to uz.scene)
///
/// ## SkyAtmosphere (15.5)
/// - `uz.scene.create_sky_atmosphere(entity)` → void
/// - `uz.scene.set_sky_rayleigh_scale(entity, scale)` → void
/// - `uz.scene.set_sky_mie_scale(entity, scale)` → void
/// - `uz.scene.set_sky_multi_scattering(entity, factor)` → void
/// - `uz.scene.set_sky_bottom_radius(entity, km)` → void
/// - `uz.scene.set_sky_atmosphere_height(entity, km)` → void
/// - `uz.scene.set_sky_rayleigh_scattering(entity, r, g, b)` → void
/// - `uz.scene.set_sky_rayleigh_scale_height(entity, km)` → void
/// - `uz.scene.set_sky_mie_scattering(entity, value)` → void
/// - `uz.scene.set_sky_mie_absorption(entity, value)` → void
/// - `uz.scene.set_sky_mie_absorption_scale(entity, scale)` → void
/// - `uz.scene.set_sky_mie_anisotropy(entity, g)` → void
/// - `uz.scene.set_sky_mie_scale_height(entity, km)` → void
/// - `uz.scene.set_sky_ground_albedo(entity, r, g, b)` → void
/// - `uz.scene.set_sky_ozone_absorption(entity, r, g, b)` → void
/// - `uz.scene.set_sky_ozone_tip_altitude(entity, km)` → void
/// - `uz.scene.set_sky_ozone_tip_value(entity, v)` → void
/// - `uz.scene.set_sky_ozone_width(entity, km)` → void
/// - `uz.scene.set_sky_luminance_factor(entity, r, g, b)` → void
/// - `uz.scene.set_sky_height_fog_contribution(entity, v)` → void
///
/// ## Getters for all setters above
/// - `uz.scene.get_atmosphere_rayleigh_scale(entity)` → number | nil
/// - `uz.scene.get_atmosphere_rayleigh_scattering(entity)` → r, g, b | nil
/// - `uz.scene.get_atmosphere_rayleigh_scale_height(entity)` → number | nil
/// - `uz.scene.get_atmosphere_mie_scattering(entity)` → number | nil
/// - `uz.scene.get_atmosphere_mie_absorption(entity)` → number | nil
/// - `uz.scene.get_atmosphere_mie_absorption_scale(entity)` → number | nil
/// - `uz.scene.get_atmosphere_mie_anisotropy(entity)` → number | nil
/// - `uz.scene.get_atmosphere_mie_scale_height(entity)` → number | nil
/// - `uz.scene.get_atmosphere_bottom_radius(entity)` → number | nil
/// - `uz.scene.get_atmosphere_height(entity)` → number | nil
/// - `uz.scene.get_atmosphere_ground_albedo(entity)` → r, g, b | nil
/// - `uz.scene.get_atmosphere_ozone_absorption(entity)` → r, g, b | nil
/// - `uz.scene.get_atmosphere_ozone_tip_altitude(entity)` → number | nil
/// - `uz.scene.get_atmosphere_ozone_tip_value(entity)` → number | nil
/// - `uz.scene.get_atmosphere_ozone_width(entity)` → number | nil
/// - `uz.scene.get_atmosphere_multi_scattering(entity)` → number | nil
/// - `uz.scene.get_atmosphere_luminance_factor(entity)` → r, g, b | nil
/// - `uz.scene.get_atmosphere_height_fog_contribution(entity)` → number | nil
/// - `uz.scene.is_atmosphere_enabled(entity)` → bool | nil
/// - `uz.scene.get_atmosphere_mie_scattering_scale(entity)` → number | nil
///
/// ## New setters + getters
/// - `uz.scene.set_atmosphere_transform_mode(entity, mode)` → void
/// - `uz.scene.get_atmosphere_transform_mode(entity)` → string | nil
/// - `uz.scene.set_atmosphere_sun_illuminance(entity, r, g, b)` → void
/// - `uz.scene.get_atmosphere_sun_illuminance(entity)` → r, g, b | nil
/// - `uz.scene.set_atmosphere_sun_angular_radius(entity, radians)` → void
/// - `uz.scene.get_atmosphere_sun_angular_radius(entity)` → number | nil
/// - `uz.scene.set_atmosphere_sun_disk_intensity(entity, intensity)` → void
/// - `uz.scene.get_atmosphere_sun_disk_intensity(entity)` → number | nil
/// - `uz.scene.set_atmosphere_enabled(entity, enabled)` → void

struct lua_State;

namespace uz {

/// Registers SkyAtmosphere functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterAtmosphereAPI(lua_State* L);

} // namespace uz
