#include "lua_time_api.h"

#include "log.h"
#include "time_manager.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Registry key
// ─────────────────────────────────────────────────────────────────────────────

static constexpr const char* kTimeManagerKey = "uz_time_manager";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helper
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] static TimeManager* GetTimeManager(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kTimeManagerKey);
    void* p = lua_touserdata(L, -1);
    lua_pop(L, 1);
    return static_cast<TimeManager*>(p);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.time.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.time.get_time_scale() → number
static int TimeGetTimeScale(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { lua_pushnumber(L, 1.0); return 1; }
    lua_pushnumber(L, static_cast<double>(tm->GetTimeScale()));
    return 1;
}

/// uz.time.set_time_scale(scale) → void
///
/// Sets the time scale multiplier. Clamped to [0.0, 10.0] by TimeManager.
static int TimeSetTimeScale(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { UZ_LOG(Scripting, Warning, "uz.time.set_time_scale: TimeManager not set"); return 0; }
    const float scale = static_cast<float>(luaL_checknumber(L, 1));
    tm->SetTimeScale(scale);
    return 0;
}

/// uz.time.get_frame_count() → integer
static int TimeGetFrameCount(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { lua_pushinteger(L, 0); return 1; }
    lua_pushinteger(L, static_cast<lua_Integer>(tm->GetFrameCount()));
    return 1;
}

/// uz.time.get_fixed_dt() → number
static int TimeGetFixedDt(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { lua_pushnumber(L, 1.0 / 60.0); return 1; }
    lua_pushnumber(L, static_cast<double>(tm->GetFixedDeltaTime()));
    return 1;
}

/// uz.time.set_fixed_dt(dt) → void
static int TimeSetFixedDt(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { UZ_LOG(Scripting, Warning, "uz.time.set_fixed_dt: TimeManager not set"); return 0; }
    const float dt = static_cast<float>(luaL_checknumber(L, 1));
    tm->SetFixedDeltaTime(dt);
    return 0;
}

/// uz.time.get_raw_dt() → number (unscaled wall-clock delta)
static int TimeGetRawDt(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { lua_pushnumber(L, 0.0); return 1; }
    lua_pushnumber(L, static_cast<double>(tm->GetRawDeltaTime()));
    return 1;
}

/// uz.time.pause() — sets time_scale to 0
static int TimePause(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { return 0; }
    tm->SetTimeScale(0.0f);
    UZ_LOG(Scripting, Info, "[Lua] uz.time.pause()");
    return 0;
}

/// uz.time.unpause() — sets time_scale to 1
static int TimeUnpause(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { return 0; }
    tm->SetTimeScale(1.0f);
    UZ_LOG(Scripting, Info, "[Lua] uz.time.unpause()");
    return 0;
}

/// uz.time.is_paused() → bool
static int TimeIsPaused(lua_State* L) {
    TimeManager* tm = GetTimeManager(L);
    if (!tm) { lua_pushboolean(L, 0); return 1; }
    lua_pushboolean(L, tm->GetTimeScale() == 0.0f ? 1 : 0);
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterTimeAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error, "LuaRegisterTimeAPI: 'uz' global is not a table");
        lua_pop(L, 1);
        return;
    }

    lua_newtable(L);

    lua_pushcfunction(L, TimeGetTimeScale);
    lua_setfield(L, -2, "get_time_scale");

    lua_pushcfunction(L, TimeSetTimeScale);
    lua_setfield(L, -2, "set_time_scale");

    lua_pushcfunction(L, TimeGetFrameCount);
    lua_setfield(L, -2, "get_frame_count");

    lua_pushcfunction(L, TimeGetFixedDt);
    lua_setfield(L, -2, "get_fixed_dt");

    lua_pushcfunction(L, TimeSetFixedDt);
    lua_setfield(L, -2, "set_fixed_dt");

    lua_pushcfunction(L, TimeGetRawDt);
    lua_setfield(L, -2, "get_raw_dt");

    lua_pushcfunction(L, TimePause);
    lua_setfield(L, -2, "pause");

    lua_pushcfunction(L, TimeUnpause);
    lua_setfield(L, -2, "unpause");

    lua_pushcfunction(L, TimeIsPaused);
    lua_setfield(L, -2, "is_paused");

    lua_setfield(L, -2, "time");
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.time API registered");
}

void LuaSetTimeManager(lua_State* L, TimeManager* timeManager) {
    lua_pushlightuserdata(L, static_cast<void*>(timeManager));
    lua_setfield(L, LUA_REGISTRYINDEX, kTimeManagerKey);
    UZ_LOG(Scripting, Debug, "LuaSetTimeManager: tm={}", static_cast<void*>(timeManager));
}

} // namespace uz
