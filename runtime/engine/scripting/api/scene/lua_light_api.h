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
