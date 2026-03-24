#pragma once

/// lua_fog_api — Lua bindings for HeightFogComponent.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// HeightFogComponent (density, color, height falloff, second layer,
/// directional inscattering, volumetric fog).
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
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

struct lua_State;

namespace uz {

/// Registers HeightFog functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterFogAPI(lua_State* L);

} // namespace uz
