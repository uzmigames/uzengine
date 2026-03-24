#pragma once

/// lua_cvar_api — Lua bindings for the CVar registry.
///
/// Registers one sub-table into the existing `uz` global:
///
/// - `uz.cvar` — read, write, reset, and enumerate console variables.
///
/// CVars use `CVarRegistry::Get()` singleton — no SceneRenderer dependency.
///
/// # Dependency Order
/// `LuaRegisterCvarAPI` must be called AFTER `RegisterUzTable()` has
/// created the `uz` global.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the main thread.

struct lua_State;

namespace uz {

/// Registers the `uz.cvar` sub-table into the existing `uz` global table.
///
/// Provides: get, set, reset, reset_all, info, list.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterCvarAPI(lua_State* L);

} // namespace uz
