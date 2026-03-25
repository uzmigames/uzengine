#pragma once

/// decal_lua_bindings -- Lua bindings for the decal system.
///
/// Registers decal functions on the `uz.vfx` sub-table for spawning,
/// fading, destroying, and querying decals from Lua scripts.
///
/// The DecalManager pointer is stored in the Lua registry under
/// `"uz_decal_manager"`.
///
/// # Lua Surface
/// ```lua
/// -- Spawn a decal at a hit point with surface normal.
/// local d = uz.vfx:spawn_decal("materials/bullet_hole.uzmat", {0,0,0}, {0,0,1})
///
/// -- Fade out over 2 seconds, then destroy.
/// uz.vfx:fade_out_decal(d, 2.0)
/// uz.vfx:destroy_decal(d)
///
/// -- Change blend mode and normal intensity.
/// uz.vfx:set_decal_blend_mode(d, "additive")
/// uz.vfx:set_decal_normal_intensity(d, 0.5)
///
/// -- Query decal system stats.
/// local stats = uz.vfx:get_decal_stats()
/// print(stats.active_count, stats.pool_available, stats.atlas_occupancy)
/// ```
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.
///
/// # Lifetime
/// `RegisterDecalLuaBindings` must be called AFTER `RegisterVFXLuaBindings`
/// (which creates the `uz.vfx` table) and before any script calls the decal
/// functions. The `DecalManager*` must remain valid for the entire lifetime
/// of the Lua state.

struct lua_State;

namespace uz {

class DecalManager;

/// Registers decal functions on the existing `uz.vfx` table.
///
/// Adds `spawn_decal`, `fade_out_decal`, `destroy_decal`, `set_decal_blend_mode`,
/// `set_decal_normal_intensity`, and `get_decal_stats` to the `uz.vfx` table.
/// Also stores `decalManager` as a lightuserdata in the Lua registry under
/// `"uz_decal_manager"`.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null. The `uz.vfx` table must already
///   exist (call `RegisterVFXLuaBindings` first).
/// - `decalManager`: Pointer to the engine's DecalManager. Must not be null and
///   must remain valid for the entire lifetime of `L`.
///
/// # Side Effects
/// - Logs `VFX/Info` on success.
/// - Logs `VFX/Error` and returns early if `uz.vfx` is not found.
void RegisterDecalLuaBindings(lua_State* L, DecalManager* decalManager);

/// Clears the `"uz_decal_manager"` registry entry.
///
/// Call this before destroying the DecalManager while the Lua state is still
/// alive, so that any stale script calls receive a clean error rather than
/// accessing freed memory.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null.
///
/// # Side Effects
/// - Logs `VFX/Info` on success.
/// - No-op if the key is not present.
void UnregisterDecalLuaBindings(lua_State* L);

} // namespace uz
