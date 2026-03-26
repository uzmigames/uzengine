#pragma once

/// Lua bindings for extended per-light-type properties (directional 15.5, point 15.6, spot 15.7).
///
/// Registered as a sub-set of the light API. Called by `LuaRegisterLightAPI()`.

struct lua_State;

namespace uz {

/// Registers extended light property functions into `uz.scene`.
///
/// Adds directional light cascade/shadow controls, point light falloff details,
/// and spot light shadow bias. Must be called AFTER `uz.scene` table exists.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterLightExtendedAPI(lua_State* L);

} // namespace uz
