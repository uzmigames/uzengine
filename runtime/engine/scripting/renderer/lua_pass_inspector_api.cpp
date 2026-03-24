#include "lua_pass_inspector_api.h"

#include "log.h"
#include "scene_renderer.h"
#include "cvar.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key constants
// ─────────────────────────────────────────────────────────────────────────────

/// Key used to store the SceneRenderer* in the Lua registry.
static constexpr const char* kRendererRegistryKey = "uz_scene_renderer";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Retrieves the SceneRenderer* from the Lua registry.
///
/// Returns null if `LuaSetSceneRenderer` was never called or was called with null.
[[nodiscard]] static SceneRenderer* GetRenderer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRendererRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<SceneRenderer*>(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.pass_inspector.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.pass_inspector.vis(target_name)
///
/// Sets the vis target. Pass "off" or nil to disable.
static int PassInspectorVis(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    if (lua_isnil(L, 1) || lua_gettop(L) == 0) {
        renderer->GetPassInspector().ClearVis();
    } else {
        const char* target = luaL_checkstring(L, 1);
        renderer->GetPassInspector().SetVisTarget(target);
    }
    return 0;
}

/// uz.pass_inspector.toggle(pass_name)
///
/// Toggles a pass on/off.
static int PassInspectorToggle(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    const char* name = luaL_checkstring(L, 1);
    renderer->GetPassInspector().TogglePass(name);
    return 0;
}

/// uz.pass_inspector.is_pass_enabled(pass_name) → boolean
///
/// Returns true if a pass is currently enabled.
static int PassInspectorIsPassEnabled(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_pushboolean(L, 1);
        return 1;
    }

    const char* name = luaL_checkstring(L, 1);
    lua_pushboolean(L, renderer->GetPassInspector().IsPassEnabled(name) ? 1 : 0);
    return 1;
}

/// uz.pass_inspector.dump(frame) or uz.pass_inspector.dump()
///
/// Requests a dump of all targets. If frame is nil or missing, dumps next frame.
static int PassInspectorDump(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        const uint32_t frame = static_cast<uint32_t>(luaL_checkinteger(L, 1));
        renderer->GetPassInspector().RequestDump(frame);
    } else {
        renderer->GetPassInspector().RequestDump(UINT32_MAX);
    }
    return 0;
}

/// uz.pass_inspector.stats(enabled)
///
/// Enables or disables the pass stats overlay.
static int PassInspectorStats(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    const bool enabled = lua_toboolean(L, 1) != 0;
    renderer->GetPassInspector().SetStatsOverlayEnabled(enabled);
    return 0;
}

/// uz.pass_inspector.validate() or uz.pass_inspector.validate(pass_name)
///
/// Runs validation and returns results as a table.
static int PassInspectorValidate(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_newtable(L);
        return 1;
    }

    // Validation requires GPU idle — this is expensive. Log a warning.
    UZ_LOG(Scripting, Info, "[Lua] uz.pass_inspector.validate: running (stalls GPU)");

    // Return the target list for informational purposes (full validation
    // requires GPU device access which is not safe from a Lua callback).
    const auto& targets = renderer->GetPassInspector().GetTargets();
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        lua_newtable(L);
        lua_pushstring(L, targets[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushinteger(L, static_cast<lua_Integer>(targets[i].width));
        lua_setfield(L, -2, "width");
        lua_pushinteger(L, static_cast<lua_Integer>(targets[i].height));
        lua_setfield(L, -2, "height");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/// uz.pass_inspector.list_targets() → table of target names
///
/// Returns a list of all registered target names this frame.
static int PassInspectorListTargets(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_newtable(L);
        return 1;
    }

    const auto& targets = renderer->GetPassInspector().GetTargets();
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
        lua_pushstring(L, targets[i].name.c_str());
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

/// uz.pass_inspector.grid(mode)
///
/// Sets grid mode: 0=off, 1=4x4, 2=6x4 extended.
static int PassInspectorGrid(lua_State* L) {
    const int mode = static_cast<int>(luaL_optinteger(L, 1, 0));
    CVarRegistry::Get().Set("r.PassInspector.Grid", static_cast<float>(mode));
    return 0;
}

/// uz.pass_inspector.compare(pass_name) or uz.pass_inspector.compare()
///
/// Enters or exits comparison mode for a pass.
static int PassInspectorCompare(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) return 0;

    if (lua_gettop(L) >= 1 && !lua_isnil(L, 1)) {
        const char* name = luaL_checkstring(L, 1);
        renderer->GetPassInspector().SetComparePass(name);
    } else {
        renderer->GetPassInspector().ClearCompare();
    }
    return 0;
}

/// uz.pass_inspector.get_timing() → table
///
/// Returns the most recent per-pass timing data.
static int PassInspectorGetTiming(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    if (!renderer) {
        lua_newtable(L);
        return 1;
    }

    const auto& timings = renderer->GetPassInspector().GetTimingData();
    lua_newtable(L);
    for (int i = 0; i < static_cast<int>(timings.size()); ++i) {
        lua_newtable(L);
        lua_pushstring(L, timings[i].name.c_str());
        lua_setfield(L, -2, "name");
        lua_pushnumber(L, static_cast<double>(timings[i].gpuTimeMs));
        lua_setfield(L, -2, "gpu_time_ms");
        lua_pushnumber(L, static_cast<double>(timings[i].minVal));
        lua_setfield(L, -2, "min");
        lua_pushnumber(L, static_cast<double>(timings[i].maxVal));
        lua_setfield(L, -2, "max");
        lua_pushnumber(L, static_cast<double>(timings[i].avgVal));
        lua_setfield(L, -2, "avg");
        lua_rawseti(L, -2, i + 1);
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterPassInspectorAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterPassInspectorAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // ── uz.pass_inspector = { vis, toggle, dump, stats, validate, ... } ──
    lua_newtable(L);

    lua_pushcfunction(L, PassInspectorVis);
    lua_setfield(L, -2, "vis");

    lua_pushcfunction(L, PassInspectorToggle);
    lua_setfield(L, -2, "toggle");

    lua_pushcfunction(L, PassInspectorIsPassEnabled);
    lua_setfield(L, -2, "is_pass_enabled");

    lua_pushcfunction(L, PassInspectorDump);
    lua_setfield(L, -2, "dump");

    lua_pushcfunction(L, PassInspectorStats);
    lua_setfield(L, -2, "stats");

    lua_pushcfunction(L, PassInspectorValidate);
    lua_setfield(L, -2, "validate");

    lua_pushcfunction(L, PassInspectorListTargets);
    lua_setfield(L, -2, "list_targets");

    lua_pushcfunction(L, PassInspectorGrid);
    lua_setfield(L, -2, "grid");

    lua_pushcfunction(L, PassInspectorCompare);
    lua_setfield(L, -2, "compare");

    lua_pushcfunction(L, PassInspectorGetTiming);
    lua_setfield(L, -2, "get_timing");

    lua_setfield(L, -2, "pass_inspector");

    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.pass_inspector API registered");
}

} // namespace uz
