/// Lua bindings for the window API (resolution, title, fullscreen).

#include "lua_window_api.h"

#include "log.h"
#include "scene_renderer.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key — same as lua_renderer_api.cpp
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kRendererRegistryKey = "uz_scene_renderer";

[[nodiscard]] static SceneRenderer* GetRenderer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRendererRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<SceneRenderer*>(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.window.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.window.get_size() → width, height
///
/// Returns the current render target dimensions in pixels.
/// Returns 0, 0 if the SceneRenderer is not set.
static int WindowGetSize(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_pushinteger(L, 0);
        lua_pushinteger(L, 0);
        return 2;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetWidth()));
    lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetHeight()));
    return 2;
}

/// uz.window.get_render_path() → "deferred" | "forward"
static int WindowGetRenderPath(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_pushstring(L, "deferred");
        return 1;
    }
    const RenderPath path = renderer->GetRenderPath();
    lua_pushstring(L, path == RenderPath::Deferred ? "deferred" : "forward");
    return 1;
}

/// uz.window.get_debug_mode() → integer
static int WindowGetDebugMode(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_pushinteger(L, 8); // "off" sentinel
        return 1;
    }
    lua_pushinteger(L, static_cast<lua_Integer>(renderer->GetDebugMode()));
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterWindowAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterWindowAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L);

    lua_pushcfunction(L, WindowGetSize);
    lua_setfield(L, -2, "get_size");

    lua_pushcfunction(L, WindowGetRenderPath);
    lua_setfield(L, -2, "get_render_path");

    lua_pushcfunction(L, WindowGetDebugMode);
    lua_setfield(L, -2, "get_debug_mode");

    lua_setfield(L, -2, "window");
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.window API registered");
}

} // namespace uz
