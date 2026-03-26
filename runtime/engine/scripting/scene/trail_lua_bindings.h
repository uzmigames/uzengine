#pragma once

/// trail_lua_bindings — Lua bindings for the trail VFX system.
///
/// Registers trail functions on the `uz.vfx` sub-table for creating,
/// destroying, configuring, and querying ribbon trails from Lua scripts.
///
/// The TrailSystem pointer is stored in the Lua registry under
/// `"uz_trail_system"`.
///
/// # Lua Surface
/// ```lua
/// -- Create a trail. Returns an opaque integer handle.
/// local h = uz.vfx:create_trail({ width_start = 0.3, point_lifetime = 1.0 })
///
/// -- Modify trail properties at runtime.
/// uz.vfx:set_trail_width(h, 0.4, 0.02)
/// uz.vfx:set_trail_color(h, {1, 0.5, 0, 1}, {1, 0.1, 0, 0})
/// uz.vfx:set_trail_lifetime(h, 0.8)
/// uz.vfx:set_trail_uv_scroll(h, 2.5)
/// uz.vfx:set_trail_active(h, false)
///
/// -- Query trail state.
/// local len = uz.vfx:get_trail_length(h)
/// local pts = uz.vfx:get_trail_point_count(h)
///
/// -- Reset and remove.
/// uz.vfx:clear_trail(h)
/// uz.vfx:destroy_trail(h)
/// ```
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.
///
/// # Lifetime
/// `RegisterTrailLuaBindings` must be called AFTER `RegisterVFXLuaBindings`
/// (which creates the `uz.vfx` table). The `TrailSystem*` must remain valid
/// for the entire lifetime of the Lua state.

struct lua_State;

namespace uz {

class TrailSystem;

/// Registers trail functions on the existing `uz.vfx` table.
///
/// Retrieves the `uz.vfx` table from the `uz` global and adds trail-specific
/// functions to it. Also stores `trailSystem` as a lightuserdata under the
/// registry key `"uz_trail_system"`.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null. The `uz.vfx` table must
///   already exist (call `RegisterVFXLuaBindings` first).
/// - `trailSystem`: Pointer to the engine's TrailSystem. Must not be null
///   and must remain valid for the entire lifetime of `L`.
///
/// # Side Effects
/// - Logs `VFX/Info` on success.
/// - Logs `VFX/Error` and returns early if `uz` or `uz.vfx` is not a table.
void RegisterTrailLuaBindings(lua_State* L, TrailSystem* trailSystem);

/// Clears the `"uz_trail_system"` registry entry.
///
/// Call this before destroying the TrailSystem while the Lua state is still
/// alive, so that any stale script calls receive a clean error rather than
/// accessing freed memory.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null.
///
/// # Side Effects
/// - Logs `VFX/Info` on success.
/// - No-op if the key is not present.
void UnregisterTrailLuaBindings(lua_State* L);

} // namespace uz
