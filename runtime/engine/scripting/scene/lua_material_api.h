#pragma once

/// lua_material_api — Lua bindings for mesh and material assignment on entities.
///
/// Registers `uz.scene.set_mesh`, `uz.scene.get_mesh`, `uz.scene.set_material`,
/// and `uz.scene.get_material` into the existing `uz.scene` sub-table. Must be
/// called AFTER `LuaRegisterSceneAPI` has created and registered `uz.scene`.
///
/// # Ownership
/// Materials created via `uz.scene.set_material` are owned by the Lua material
/// storage inside this module. They live until the Lua VM shuts down or the
/// module state is reset (e.g. script reload).
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.

struct lua_State;

namespace uz {

/// Extends the existing `uz.scene` sub-table with mesh and material functions:
/// `set_mesh`, `get_mesh`, `set_material`, `get_material`.
///
/// Must be called after `LuaRegisterSceneAPI` has created `uz.scene` and after
/// `LuaSetWorld` + the SceneRenderer registry key have been populated.
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
void LuaRegisterMaterialAPI(lua_State* L);

} // namespace uz
