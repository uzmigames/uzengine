#pragma once

/// lua_entity_api — Lua bindings for entity lifecycle management.
///
/// Extends the `uz.scene` sub-table with entity creation, destruction,
/// validation, and naming. Requires LuaSetWorld() to have been called
/// before any script invokes these functions.
///
/// # New Functions (added to uz.scene)
/// - `uz.scene.create_entity(name?)` → entity lightuserdata
/// - `uz.scene.destroy_entity(entity)` → void
/// - `uz.scene.is_valid(entity)` → bool
/// - `uz.scene.get_name(entity)` → string | nil
/// - `uz.scene.set_name(entity, name)` → void
/// - `uz.scene.entity_count()` → integer
///
/// # Entity Encoding
/// Same encoding as lua_scene_api: entities are packed into lightuserdata
/// as `(uint64_t)((uint64_t)id << 32 | generation)`.
///
/// # Error Handling
/// All functions validate arguments and return nil/false on error rather
/// than raising Lua errors. Missing World pointer is logged at Warning level.
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.

struct lua_State;

namespace uz {

/// Registers entity lifecycle functions into the existing `uz.scene` table.
///
/// Must be called AFTER `LuaRegisterSceneAPI()` has created the `uz.scene` table.
/// Adds `create_entity`, `destroy_entity`, `is_valid`, `get_name`, `set_name`,
/// and `entity_count` to the existing table without replacing it.
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
void LuaRegisterEntityAPI(lua_State* L);

} // namespace uz
