#pragma once

/// lua_window_api — Lua bindings for window and display queries.
///
/// Registers the `uz.window` sub-table with functions for querying
/// window dimensions, getting the render path, and toggling fullscreen.
///
/// # Functions
/// - `uz.window.get_size()` → width, height
/// - `uz.window.get_render_path()` → "deferred" | "forward"
/// - `uz.window.get_debug_mode()` → integer

struct lua_State;

namespace uz {

class SceneRenderer;

/// Registers the `uz.window` sub-table into the existing `uz` global table.
///
/// Reuses the SceneRenderer* already stored by LuaSetSceneRenderer.
void LuaRegisterWindowAPI(lua_State* L);

} // namespace uz
