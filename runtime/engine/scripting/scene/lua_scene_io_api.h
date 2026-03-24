#pragma once

/// lua_scene_io_api — Lua bindings for scene load/save operations.
///
/// Registers `uz.scene.load_scene` and `uz.scene.save_scene` into the existing
/// `uz.scene` sub-table. Must be called AFTER `LuaRegisterSceneAPI` has created
/// and registered `uz.scene`.
///
/// # VFS Dependency
/// Both functions require an IVFS pointer stored in the Lua registry via
/// `LuaSetVFS`. If the VFS is not set, operations return false.
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.

struct lua_State;

namespace uz {

class IVFS;

/// Extends the existing `uz.scene` sub-table with I/O functions:
/// `load_scene`, `save_scene`.
///
/// Must be called after `LuaRegisterSceneAPI` has created `uz.scene` and after
/// `LuaSetWorld` + `LuaSetVFS` have been populated.
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
void LuaRegisterSceneIoAPI(lua_State* L);

/// Stores a VFS pointer in the Lua registry for use by scene load/save callbacks.
///
/// Must be called before any Lua script invokes `uz.scene.load_scene` or
/// `uz.scene.save_scene`. Passing `nullptr` disables VFS access (operations
/// return false).
///
/// # Arguments
/// * `L`   - Active Lua state. Must not be null.
/// * `vfs` - IVFS instance to expose. May be null to revoke access.
void LuaSetVFS(lua_State* L, IVFS* vfs);

} // namespace uz
