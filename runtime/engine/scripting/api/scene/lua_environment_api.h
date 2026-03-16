#pragma once

/// lua_environment_api — Lua bindings for environment components.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// HeightFogComponent, SkyLightComponent, and SkyAtmosphereComponent.
///
/// # New Functions (added to uz.scene)
///
/// ## HeightFog (15.4)
/// - `uz.scene.set_fog_density(entity, density)` → void
/// - `uz.scene.set_fog_color(entity, r, g, b)` → void
/// - `uz.scene.set_fog_height_falloff(entity, falloff)` → void
/// - `uz.scene.set_fog_base_height(entity, height)` → void
/// - `uz.scene.set_fog_max_opacity(entity, opacity)` → void
/// - `uz.scene.set_fog_enabled(entity, enabled)` → void
///
/// ## SkyLight (15.6)
/// - `uz.scene.create_sky_light(entity)` → void
/// - `uz.scene.set_sky_light_intensity(entity, intensity)` → void
/// - `uz.scene.set_sky_light_color(entity, r, g, b)` → void
/// - `uz.scene.set_sky_light_min_occlusion(entity, min_occlusion)` → void
///
/// ## SkyAtmosphere artistic multipliers (15.5)
/// - `uz.scene.set_sky_rayleigh_scale(entity, scale)` → void
/// - `uz.scene.set_sky_mie_scale(entity, scale)` → void
/// - `uz.scene.set_sky_multi_scattering(entity, factor)` → void

struct lua_State;

namespace uz {

/// Registers environment component functions into the existing `uz.scene` table.
///
/// Populates `uz.scene` with bindings for controlling HeightFogComponent
/// (density, color, height falloff, max opacity), SkyLightComponent
/// (intensity, color, min occlusion), and SkyAtmosphereComponent artistic
/// multipliers (Rayleigh scale, Mie scale, multi-scattering factor).
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterEnvironmentAPI(lua_State* L);

} // namespace uz
