#pragma once

/// lua_pass_inspector_api — Lua bindings for the render pass inspector.
///
/// Registers one sub-table into the existing `uz` global:
///
/// - `uz.pass_inspector` — control and inspect render pass execution,
///   target visibility, stats, validation, timing, and grid layout.
///
/// The `SceneRenderer` pointer is retrieved from the Lua registry (stored
/// via `LuaSetSceneRenderer` from `lua_renderer_api`).  Passing nullptr
/// disables pass inspector access — all functions return gracefully.
///
/// # Dependency Order
/// `LuaRegisterPassInspectorAPI` must be called AFTER `RegisterUzTable()` has
/// created the `uz` global and AFTER `LuaSetSceneRenderer` has been called.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the main/render thread.

struct lua_State;

namespace uz {

/// Registers the `uz.pass_inspector` sub-table into the existing `uz` global
/// table.
///
/// Provides: vis, toggle, is_pass_enabled, dump, stats, validate,
///           list_targets, grid, compare, get_timing.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterPassInspectorAPI(lua_State* L);

} // namespace uz
