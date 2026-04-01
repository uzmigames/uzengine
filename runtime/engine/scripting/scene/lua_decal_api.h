#pragma once

/// lua_decal_api — Lua bindings for DecalComponent (material-based).
///
/// DecalComponent references a MaterialInterface* for visual properties.
/// The Lua API exposes component-level fields (priority, fade, enabled)
/// and delegates material properties (opacity) through the material.
///
/// # Functions (in uz.scene)
/// - `uz.scene.create_decal(entity)` → void
/// - `uz.scene.remove_decal(entity)` → void
/// - `uz.scene.set_decal_opacity(entity, v)` → void
/// - `uz.scene.get_decal_opacity(entity)` → v | nil
/// - `uz.scene.set_decal_priority(entity, v)` → void
/// - `uz.scene.get_decal_priority(entity)` → v | nil
/// - `uz.scene.set_decal_fade_distance(entity, v)` → void
/// - `uz.scene.get_decal_fade_distance(entity)` → v | nil
/// - `uz.scene.set_decal_enabled(entity, bool)` → void
/// - `uz.scene.is_decal_enabled(entity)` → bool | nil

struct lua_State;

namespace uz {

/// Registers DecalComponent functions into the existing `uz.scene` table.
void LuaRegisterDecalAPI(lua_State* L);

} // namespace uz
