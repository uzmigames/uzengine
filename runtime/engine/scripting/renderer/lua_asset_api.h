#pragma once

/// lua_asset_api — Lua bindings for mesh and texture asset loading.
///
/// Registers two sub-tables into the existing `uz` global:
///
/// - `uz.mesh`    — mesh loading from the VFS and procedural generation.
/// - `uz.texture` — texture loading from the VFS.
///
/// The `SceneRenderer` pointer is retrieved from the Lua registry (stored
/// via `LuaSetSceneRenderer` from `lua_renderer_api`).  Passing nullptr
/// disables asset access — all functions return nil gracefully.
///
/// # Dependency Order
/// `LuaRegisterAssetAPI` must be called AFTER `RegisterUzTable()` has
/// created the `uz` global and AFTER `LuaSetSceneRenderer` has been called.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the main/render thread.

struct lua_State;

namespace uz {

/// Registers `uz.mesh` and `uz.texture` sub-tables into the existing `uz`
/// global table.
///
/// `uz.mesh` provides: load, generate_sphere, generate_plane.
/// `uz.texture` provides: load.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterAssetAPI(lua_State* L);

} // namespace uz
