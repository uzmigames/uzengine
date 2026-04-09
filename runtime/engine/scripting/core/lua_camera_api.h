#pragma once

/// lua_camera_api — Lua bindings for the active camera.
///
/// Registers the `uz.camera` sub-table in the Lua global `uz` with functions
/// for querying and controlling the scene's active camera. The active camera
/// is identified each call by iterating `CameraComponent` with `isActive == true`.
///
/// If no camera is active, all getter functions return nil and setter functions
/// are no-ops with a Warning log. This matches the engine's own camera selection
/// behaviour (first active camera found wins).
///
/// # Dependency
/// The World pointer must be set via `LuaSetWorld()` (from lua_scene_api.h) before
/// any `uz.camera.*` function is called. Both APIs share the same registry key.
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.

struct lua_State;

namespace uz {

/// Registers the `uz.camera` sub-table into the existing `uz` global table.
///
/// Must be called after `RegisterUzTable()` and while the Lua VM is being
/// initialized. Safe to call again after a VM restart (overwrites previous table).
///
/// # Arguments
/// * `L` - Active Lua state. Must not be null.
void LuaRegisterCameraAPI(lua_State* L);

class CameraManager;

/// Stores the CameraManager pointer in the Lua registry for mgr_* functions.
void LuaSetCameraManager(lua_State* L, CameraManager* mgr);

/// Registers uz.camera.mgr_create/mgr_switch/mgr_list/mgr_get_active/mgr_destroy.
/// Must be called after LuaRegisterCameraAPI and LuaSetCameraManager.
void LuaRegisterCameraManagerAPI(lua_State* L);

} // namespace uz
