#pragma once

/// lua_postprocess_api — Lua bindings for PostProcessVolume management.
///
/// Registers the `uz.postprocess` sub-table into the existing `uz` global.
///
/// Exposes the full PostProcessVolumeManager API to Lua scripts:
/// - Volume creation, removal, and enumeration.
/// - Per-volume property get/set (enabled, priority, blend weight, blend radius).
/// - Per-volume PostProcessSettings get/set for all 28 fields. Each setter
///   automatically sets the corresponding override flag so the value participates
///   in blending without requiring a separate `set_override` call.
/// - Override flag management (set_all_overrides, clear_all_overrides,
///   set_override, clear_override by string name).
/// - Blending query: `uz.postprocess.compute_blended(x, y, z)` returns a flat
///   table with all 28 blended fields for a given camera position.
///
/// The PostProcessVolumeManager pointer is injected at runtime via
/// `LuaSetPostProcessManager` and stored in the Lua registry under the key
/// `"uz_postprocess_manager"`. Passing `nullptr` gracefully disables access
/// (all functions return nil or no-op with a warning).
///
/// # Dependency Order
/// `LuaRegisterPostProcessAPI` must be called AFTER `RegisterUzTable()` has
/// created the `uz` global.
/// `LuaSetPostProcessManager` must be called before any Lua script invokes
/// `uz.postprocess.*`.
///
/// # Thread Safety
/// NOT thread-safe. All functions must be called from the game thread.

struct lua_State;

namespace uz {

class PostProcessVolumeManager;

/// Registers the `uz.postprocess` sub-table into the existing `uz` global.
///
/// Idempotent — calling again overwrites the previous sub-table.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterPostProcessAPI(lua_State* L);

/// Stores a PostProcessVolumeManager pointer in the Lua registry for use
/// by postprocess callbacks.
///
/// Must be called before any Lua script invokes `uz.postprocess.*`.
/// Passing `nullptr` disables postprocess access.
///
/// # Arguments
/// * `L`   — Active Lua state. Must not be null.
/// * `mgr` — PostProcessVolumeManager instance to expose. May be null to revoke.
void LuaSetPostProcessManager(lua_State* L, PostProcessVolumeManager* mgr);

} // namespace uz
