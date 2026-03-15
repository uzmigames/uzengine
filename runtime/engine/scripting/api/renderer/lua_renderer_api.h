#pragma once

/// lua_renderer_api — Lua bindings for renderer controls and asset loading.
///
/// Registers three sub-tables into the existing `uz` global:
///
/// - `uz.renderer` — render path selection and debug-mode control.
/// - `uz.mesh`     — mesh loading from the VFS; returns opaque integer IDs.
/// - `uz.texture`  — texture loading from the VFS; returns opaque integer IDs.
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
/// `uz.renderer.*`, `uz.mesh.*`, or `uz.texture.*`.
///
/// # Thread Safety
/// NOT thread-safe. All functions must be called from the main/render thread.

struct lua_State;

namespace uz {

class SceneRenderer;

/// Registers `uz.renderer`, `uz.mesh`, and `uz.texture` sub-tables into the
/// existing `uz` global table.
///
/// Idempotent — calling again overwrites the previous sub-tables.
///
/// # Arguments
/// * `L` — Active Lua state. Must not be null.
void LuaRegisterRendererAPI(lua_State* L);

/// Stores a SceneRenderer pointer in the Lua registry for use by renderer
/// callbacks.
///
/// Must be called before any Lua script invokes `uz.renderer.*`, `uz.mesh.*`,
/// or `uz.texture.*` functions.  Passing `nullptr` disables renderer access.
///
/// # Arguments
/// * `L`        — Active Lua state. Must not be null.
/// * `renderer` — SceneRenderer instance to expose. May be null to revoke access.
void LuaSetSceneRenderer(lua_State* L, SceneRenderer* renderer);

} // namespace uz
