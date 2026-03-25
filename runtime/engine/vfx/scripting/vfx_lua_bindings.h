#pragma once

/// vfx_lua_bindings — Lua bindings for the VFX system.
///
/// Registers the `uz.vfx` sub-table in the Lua global `uz` with functions
/// for spawning, controlling, and querying particle emitters from script.
///
/// The VFXSystem pointer is stored in the Lua registry under the key
/// `"uz_vfx_system"` and retrieved by each C closure. This mirrors the
/// pattern used by `lua_scene_api` (`"uz_world"`) and avoids a global.
///
/// # Lua Surface
/// ```lua
/// -- Spawn an emitter at a world-space position. Returns entity lightuserdata.
/// local e = uz.vfx:spawn("effects/explosion.uzvfx", {1, 2, 3})
///
/// -- Drive a named parameter (float or Vec3).
/// uz.vfx:set_parameter(e, "intensity", 2.5)
/// uz.vfx:set_parameter(e, "windDir",   {1, 0, 0})
///
/// -- Trigger a one-shot burst event.
/// uz.vfx:emit_event(e, "burst")
///
/// -- Control emission state.
/// uz.vfx:start(e)
/// uz.vfx:stop(e)
/// uz.vfx:pause(e, true)
///
/// -- Query live particle count (CPU-side approximation).
/// local count = uz.vfx:get_alive_count(e)
///
/// -- LOD control: set per-emitter distance thresholds.
/// uz.vfx:set_lod_distances(e, 30, 80, 150)
/// uz.vfx:set_lod_enabled(e, true)
/// local lod = uz.vfx:get_lod_level(e)  -- 0=Full, 1=Reduced, 2=Minimal, 3=Culled
///
/// -- Budget queries: global particle budget statistics.
/// local stats = uz.vfx:get_budget_stats()  -- {total, used, available, utilization_pct}
/// local alive = uz.vfx:get_total_alive()
/// local count = uz.vfx:get_emitter_count()
///
/// -- Composite effects: spawn/stop multi-emitter effects from .uzvfx_composite files.
/// local handles = uz.vfx:spawn_composite("effects/explosion.uzvfx_composite", {0, 0, 0})
/// uz.vfx:stop_composite(handles)
/// ```
///
/// # Thread Safety
/// Not thread-safe. All functions must be called from the main thread.
///
/// # Lifetime
/// `RegisterVFXLuaBindings` must be called after `RegisterUzTable()` and
/// before any script calls `uz.vfx.*`. The `VFXSystem*` must remain valid
/// for the entire lifetime of the Lua state.

struct lua_State;

namespace uz {

class VFXSystem;

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

/// Registers the `uz.vfx` sub-table and stores `vfxSystem` in the registry.
///
/// Creates the `uz.vfx` table with all binding functions and inserts it into
/// the existing `uz` global. Also stores `vfxSystem` as a lightuserdata under
/// the registry key `"uz_vfx_system"` so that each binding function can
/// retrieve it without a global.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null. The `uz` global table must
///   already exist (call `RegisterUzTable()` first).
/// - `vfxSystem`: Pointer to the engine's VFXSystem. Must not be null and must
///   remain valid for the entire lifetime of `L`.
///
/// # Side Effects
/// - Logs `VFX/Info` on success.
/// - Logs `VFX/Error` and returns early if the `uz` global is not a table.
///
/// # Example
/// ```cpp
/// LuaVM::Init();
/// RegisterVFXLuaBindings(LuaVM::GetState(), &myVfxSystem);
/// ```
void RegisterVFXLuaBindings(lua_State* L, VFXSystem* vfxSystem);

/// Clears the `"uz_vfx_system"` registry entry.
///
/// Call this before destroying the VFXSystem while the Lua state is still
/// alive, so that any stale script calls to `uz.vfx.*` receive a clean
/// error rather than accessing freed memory.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null.
///
/// # Side Effects
/// - Logs `VFX/Info` on success.
/// - No-op if the key is not present.
void UnregisterVFXLuaBindings(lua_State* L);

} // namespace uz
