#pragma once

/// lua_environment_api — Lua bindings for environment components.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// HeightFogComponent, SkyLightComponent, SkyAtmosphereComponent,
/// and VolumetricCloudsComponent.
///
/// # New Functions (added to uz.scene)
///
/// ## HeightFog (15.4)
/// - `uz.scene.create_fog(entity)` → void
/// - `uz.scene.set_fog_density(entity, density)` → void
/// - `uz.scene.set_fog_color(entity, r, g, b)` → void
/// - `uz.scene.set_fog_height_falloff(entity, falloff)` → void
/// - `uz.scene.set_fog_base_height(entity, height)` → void
/// - `uz.scene.set_fog_max_opacity(entity, opacity)` → void
/// - `uz.scene.set_fog_enabled(entity, enabled)` → void
/// - `uz.scene.set_fog_start_distance(entity, distance)` → void
/// - `uz.scene.set_fog_cutoff_distance(entity, distance)` → void
/// - `uz.scene.set_fog_second_density(entity, density)` → void
/// - `uz.scene.set_fog_second_height_falloff(entity, falloff)` → void
/// - `uz.scene.set_fog_second_base_height(entity, height)` → void
/// - `uz.scene.set_fog_directional_inscattering_color(entity, r, g, b)` → void
/// - `uz.scene.set_fog_directional_inscattering_exponent(entity, exponent)` → void
/// - `uz.scene.set_fog_directional_inscattering_start_distance(entity, distance)` → void
/// - `uz.scene.set_fog_volumetric(entity, enabled)` → void
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
/// ## VolumetricClouds
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

struct lua_State;

namespace uz {

/// Registers environment component functions into the existing `uz.scene` table.
///
/// Populates `uz.scene` with bindings for controlling HeightFogComponent
/// (density, color, height falloff, second layer, directional inscattering,
/// volumetric fog), SkyLightComponent (intensity, color, source type, lower
/// hemisphere, occlusion), SkyAtmosphereComponent (planet geometry, Rayleigh,
/// Mie, ozone, art direction), and VolumetricCloudsComponent (layer geometry,
/// density, wind, scattering, ray march parameters).
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterEnvironmentAPI(lua_State* L);

} // namespace uz
