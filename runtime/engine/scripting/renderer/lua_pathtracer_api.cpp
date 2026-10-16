#include "lua_pathtracer_api.h"

#include "log.h"
#include "cvar.h"
#include "scene_renderer.h"

/// Lua bindings for path tracer controls and progressive accumulation state.

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// Scripting log category is defined in lua_vm.cpp.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// Registry key constants

/// Shared with LuaSetSceneRenderer and the renderer API.
static constexpr const char* kRendererRegistryKey = "uz_scene_renderer";

/// Returns null when no SceneRenderer has been injected.
[[nodiscard]] static SceneRenderer* GetRenderer(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kRendererRegistryKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<SceneRenderer*>(p);
}

// Lua C functions - uz.pathtracer.*

/// uz.pathtracer.enable(enabled) - takes effect on the next rendered frame.
static int PathTracerEnable(lua_State* L) {
    const bool enabled = lua_toboolean(L, 1) != 0;
    CVarRegistry::Get().Set("r.PathTracer.Enable", enabled ? 1.f : 0.f);
    return 0;
}

/// uz.pathtracer.is_enabled() -> boolean
static int PathTracerIsEnabled(lua_State* L) {
    lua_pushboolean(L, CVarRegistry::Get().GetBool("r.PathTracer.Enable") ? 1 : 0);
    return 1;
}

/// uz.pathtracer.set_max_bounces(count) - clamped by the CVar to [1,16].
static int PathTracerSetMaxBounces(lua_State* L) {
    const int count = static_cast<int>(luaL_checkinteger(L, 1));
    CVarRegistry::Get().Set("r.PathTracer.MaxBounces", static_cast<float>(count));
    return 0;
}

/// uz.pathtracer.set_samples_per_frame(count) - clamped by the CVar to [1,16].
static int PathTracerSetSamplesPerFrame(lua_State* L) {
    const int count = static_cast<int>(luaL_checkinteger(L, 1));
    CVarRegistry::Get().Set("r.PathTracer.SamplesPerFrame", static_cast<float>(count));
    return 0;
}

/// uz.pathtracer.set_env_intensity(intensity) - clamped by the CVar to [0,10].
static int PathTracerSetEnvIntensity(lua_State* L) {
    const float intensity = static_cast<float>(luaL_checknumber(L, 1));
    CVarRegistry::Get().Set("r.PathTracer.EnvIntensity", intensity);
    return 0;
}

/// uz.pathtracer.reset_accum() - invokes the exec CVar; reset occurs next frame.
static int PathTracerResetAccum(lua_State* L) {
    (void)L;
    CVarRegistry::Get().Set("r.PathTracer.ResetAccum", 1.f);
    return 0;
}

/// uz.pathtracer.samples_accumulated() -> integer
/// Returns the renderer's history counter, not samples_per_frame times frames.
/// Returns zero when no renderer is injected or before accumulation begins.
static int PathTracerSamplesAccumulated(lua_State* L) {
    SceneRenderer* renderer = GetRenderer(L);
    lua_pushinteger(L, renderer
        ? static_cast<lua_Integer>(renderer->GetPathTracerSamplesAccumulated()) : 0);
    return 1;
}

void LuaRegisterPathTracerAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterPathTracerAPI: 'uz' global is not a table - call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L);

    lua_pushcfunction(L, PathTracerEnable);
    lua_setfield(L, -2, "enable");

    lua_pushcfunction(L, PathTracerIsEnabled);
    lua_setfield(L, -2, "is_enabled");

    lua_pushcfunction(L, PathTracerSetMaxBounces);
    lua_setfield(L, -2, "set_max_bounces");

    lua_pushcfunction(L, PathTracerSetSamplesPerFrame);
    lua_setfield(L, -2, "set_samples_per_frame");

    lua_pushcfunction(L, PathTracerSetEnvIntensity);
    lua_setfield(L, -2, "set_env_intensity");

    lua_pushcfunction(L, PathTracerResetAccum);
    lua_setfield(L, -2, "reset_accum");

    lua_pushcfunction(L, PathTracerSamplesAccumulated);
    lua_setfield(L, -2, "samples_accumulated");

    lua_setfield(L, -2, "pathtracer");
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.pathtracer API registered");
}

} // namespace uz
