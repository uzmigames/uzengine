#pragma once

/// lua_decal_api — Lua bindings for DecalComponent.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// DecalComponent (extents, textures, opacity, normal blend, roughness,
/// metallic, priority, fade distance, angle fade, enabled).
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # New Functions (added to uz.scene)
///
/// ## Decal
/// - `uz.scene.create_decal(entity)` → void
/// - `uz.scene.set_decal_extents(entity, x, y, z)` → void
/// - `uz.scene.get_decal_extents(entity)` → x, y, z | nil
/// - `uz.scene.set_decal_albedo_texture(entity, id)` → void
/// - `uz.scene.get_decal_albedo_texture(entity)` → id | nil
/// - `uz.scene.set_decal_normal_texture(entity, id)` → void
/// - `uz.scene.get_decal_normal_texture(entity)` → id | nil
/// - `uz.scene.set_decal_material_texture(entity, id)` → void
/// - `uz.scene.get_decal_material_texture(entity)` → id | nil
/// - `uz.scene.set_decal_opacity(entity, v)` → void
/// - `uz.scene.get_decal_opacity(entity)` → v | nil
/// - `uz.scene.set_decal_normal_blend(entity, v)` → void
/// - `uz.scene.get_decal_normal_blend(entity)` → v | nil
/// - `uz.scene.set_decal_roughness(entity, v)` → void
/// - `uz.scene.get_decal_roughness(entity)` → v | nil
/// - `uz.scene.set_decal_metallic(entity, v)` → void
/// - `uz.scene.get_decal_metallic(entity)` → v | nil
/// - `uz.scene.set_decal_priority(entity, v)` → void
/// - `uz.scene.get_decal_priority(entity)` → v | nil
/// - `uz.scene.set_decal_fade_distance(entity, v)` → void
/// - `uz.scene.get_decal_fade_distance(entity)` → v | nil
/// - `uz.scene.set_decal_angle_fade_start(entity, v)` → void
/// - `uz.scene.get_decal_angle_fade_start(entity)` → v | nil
/// - `uz.scene.set_decal_enabled(entity, bool)` → void
/// - `uz.scene.is_decal_enabled(entity)` → bool | nil
/// - `uz.scene.remove_decal(entity)` → void

struct lua_State;

namespace uz {

/// Registers DecalComponent functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterDecalAPI(lua_State* L);

} // namespace uz
