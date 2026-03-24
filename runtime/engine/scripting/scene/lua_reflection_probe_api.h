#pragma once

/// lua_reflection_probe_api — Lua bindings for ReflectionProbeComponent.
///
/// Extends the `uz.scene` sub-table with functions for controlling
/// ReflectionProbeComponent (influence radius, blend distance, resolution,
/// priority, enabled, needs capture).
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # New Functions (added to uz.scene)
///
/// ## ReflectionProbe
/// - `uz.scene.create_reflection_probe(entity)` → void
/// - `uz.scene.set_probe_influence_radius(entity, v)` → void
/// - `uz.scene.get_probe_influence_radius(entity)` → v | nil
/// - `uz.scene.set_probe_blend_distance(entity, v)` → void
/// - `uz.scene.get_probe_blend_distance(entity)` → v | nil
/// - `uz.scene.set_probe_resolution(entity, v)` → void
/// - `uz.scene.get_probe_resolution(entity)` → v | nil
/// - `uz.scene.set_probe_priority(entity, v)` → void
/// - `uz.scene.get_probe_priority(entity)` → v | nil
/// - `uz.scene.set_probe_enabled(entity, bool)` → void
/// - `uz.scene.is_probe_enabled(entity)` → bool | nil
/// - `uz.scene.set_probe_needs_capture(entity, bool)` → void
/// - `uz.scene.get_probe_needs_capture(entity)` → bool | nil
/// - `uz.scene.remove_reflection_probe(entity)` → void

struct lua_State;

namespace uz {

/// Registers ReflectionProbeComponent functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterReflectionProbeAPI(lua_State* L);

} // namespace uz
