#pragma once

/// lua_scene_api — Lua bindings for scene entity/transform queries.
///
/// Registers the `uz.scene` sub-table in the Lua global `uz` with functions
/// for finding entities by name and reading/writing their transforms. The World
/// pointer is injected at runtime and stored in the Lua registry so all
/// callbacks can access it without a global variable.
///
/// # Entity Encoding
/// Entities are passed to/from Lua as lightuserdata. The 64-bit entity handle
/// (`id` + `generation`, each uint32_t) is packed into a single `void*`:
///
/// ```
/// void* = (uint64_t)((uint64_t)id << 32 | generation)
/// ```
///
/// This avoids heap allocation for every entity reference while preserving
/// the full generational handle for stale-entity detection.
///
/// # Error Handling
/// All Lua-callable functions validate their arguments and return nil/false on
/// error rather than raising Lua errors or crashing. Missing components are
/// logged at Warning level and return nil.
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.

struct lua_State;

namespace uz {

class World;
class IVFS;

/// Registers the `uz.scene` sub-table into the existing `uz` global table.
///
/// Must be called after `RegisterUzTable()` has created the `uz` table and
/// while the Lua VM is being initialized. Safe to call again after a restart
/// (idempotent — overwrites the previous sub-table).
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
void LuaRegisterSceneAPI(lua_State* L);

/// Stores a World pointer in the Lua registry for use by scene callbacks.
///
/// Must be called before any Lua script invokes `uz.scene.*` functions.
/// Passing `nullptr` disables scene access (all scene functions return nil).
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
/// * `world` - World instance to expose. May be null to revoke access.
void LuaSetWorld(lua_State* L, World* world);

/// Stores a VFS pointer in the Lua registry for use by scene load callbacks.
///
/// Must be called before any Lua script invokes `uz.scene.load_scene`.
/// Passing `nullptr` disables VFS access (`load_scene` returns false).
///
/// # Arguments
/// * `L`   - Active Lua state. Must not be null.
/// * `vfs` - IVFS instance to expose. May be null to revoke access.
void LuaSetVFS(lua_State* L, IVFS* vfs);

} // namespace uz
