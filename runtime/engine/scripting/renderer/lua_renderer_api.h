#pragma once

/// lua_renderer_api — Lua bindings for renderer controls.
///
/// Registers two sub-tables into the existing `uz` global:
///
/// - `uz.renderer` — render path selection, debug-mode control, screenshot,
///   lens flare, shadow cache, screen percentage, upscaler, and resolution.
/// - `uz.debug`    — debug view mode aliases for script authoring convenience.
///
/// Asset loading (uz.mesh, uz.texture), CVar access (uz.cvar), and pass
/// inspection (uz.pass_inspector) are registered by their dedicated modules:
/// `lua_asset_api`, `lua_cvar_api`, and `lua_pass_inspector_api`.
///
/// The `SceneRenderer` pointer is injected at runtime and stored in the Lua
/// registry so all callbacks can access it without a global variable.  Passing
/// `nullptr` via `LuaSetSceneRenderer` disables renderer access (all functions
/// return nil gracefully).
///
/// # Dependency Order
/// `LuaRegisterRendererAPI` must be called AFTER `RegisterUzTable()` has
/// created the `uz` global and AFTER `SceneRenderer::Init()` has succeeded.
/// `LuaSetSceneRenderer` must be called before any Lua script invokes
/// `uz.renderer.*` or `uz.debug.*`.
///
/// # Thread Safety
/// NOT thread-safe. All functions must be called from the main/render thread.

struct lua_State;

namespace uz {

class SceneRenderer;

/// Registers `uz.renderer` and `uz.debug` sub-tables into the existing `uz`
/// global table.
///
/// Idempotent — calling again overwrites the previous sub-tables.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterRendererAPI(lua_State* L);

/// Stores a SceneRenderer pointer in the Lua registry for use by renderer
/// callbacks.
///
/// Must be called before any Lua script invokes `uz.renderer.*` or
/// `uz.debug.*` functions.  Passing `nullptr` disables renderer access.
/// Also used by `lua_asset_api` and `lua_pass_inspector_api` which read
/// the same registry key `"uz_scene_renderer"`.
///
/// # Arguments
/// * `L`        — Active Lua state. Must not be null.
/// * `renderer` — SceneRenderer instance to expose. May be null to revoke access.
void LuaSetSceneRenderer(lua_State* L, SceneRenderer* renderer);

} // namespace uz
