#include "lua_cvar_api.h"

#include "log.h"
#include "cvar.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <string>

// Scripting log category is defined in lua_vm.cpp — only declare here.
UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lua C functions — uz.cvar.*
// ─────────────────────────────────────────────────────────────────────────────

/// uz.cvar.get(name) → number | nil
///
/// Returns the current value of a CVar, or nil if not found.
static int CvarGet(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto* cvar = CVarRegistry::Get().Find(name);
    if (cvar) {
        lua_pushnumber(L, static_cast<double>(cvar->value));
    } else {
        lua_pushnil(L);
    }
    return 1;
}

/// uz.cvar.set(name, value)
///
/// Sets a CVar value. Clamped to the CVar's [min, max] range.
/// Logs a warning if the CVar does not exist.
static int CvarSet(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const float value = static_cast<float>(luaL_checknumber(L, 2));
    auto& registry = CVarRegistry::Get();
    if (registry.Find(name)) {
        registry.Set(name, value);
    } else {
        UZ_LOG(Scripting, Warning, "uz.cvar.set: unknown CVar '{}'", name);
    }
    return 0;
}

/// uz.cvar.reset(name)
///
/// Resets a CVar to its default value. Logs a warning if not found.
static int CvarReset(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    auto& registry = CVarRegistry::Get();
    if (registry.Find(name)) {
        registry.Reset(name);
    } else {
        UZ_LOG(Scripting, Warning, "uz.cvar.reset: unknown CVar '{}'", name);
    }
    return 0;
}

/// uz.cvar.reset_all()
///
/// Resets every registered CVar to its default value.
static int CvarResetAll(lua_State* L) {
    (void)L;
    CVarRegistry::Get().ResetAll();
    return 0;
}

/// uz.cvar.info(name) → table | nil
///
/// Returns a table with CVar metadata: { value, default, min, max, description }.
/// Returns nil if the CVar does not exist.
static int CvarInfo(lua_State* L) {
    const char* name = luaL_checkstring(L, 1);
    const auto* cvar = CVarRegistry::Get().Find(name);
    if (!cvar) {
        lua_pushnil(L);
        return 1;
    }

    lua_newtable(L);

    lua_pushstring(L, cvar->name.c_str());
    lua_setfield(L, -2, "name");

    lua_pushstring(L, cvar->description.c_str());
    lua_setfield(L, -2, "description");

    lua_pushnumber(L, static_cast<double>(cvar->value));
    lua_setfield(L, -2, "value");

    lua_pushnumber(L, static_cast<double>(cvar->defaultValue));
    lua_setfield(L, -2, "default");

    lua_pushnumber(L, static_cast<double>(cvar->minValue));
    lua_setfield(L, -2, "min");

    lua_pushnumber(L, static_cast<double>(cvar->maxValue));
    lua_setfield(L, -2, "max");

    return 1;
}

/// uz.cvar.list(prefix?) → table
///
/// Returns an array of CVar names. If prefix is given, only returns CVars
/// whose names start with that prefix (e.g., "r.Bloom" returns all bloom CVars).
/// If no prefix, returns all registered CVars.
static int CvarList(lua_State* L) {
    const char* prefix = nullptr;
    size_t prefixLen = 0;
    if (lua_gettop(L) >= 1 && lua_isstring(L, 1)) {
        prefix = lua_tolstring(L, 1, &prefixLen);
    }

    const auto& allCvars = CVarRegistry::Get().GetAll();
    lua_newtable(L);

    int idx = 1;
    for (const auto& [name, cvar] : allCvars) {
        if (prefix && prefixLen > 0) {
            if (name.compare(0, prefixLen, prefix, prefixLen) != 0) continue;
        }
        lua_pushstring(L, name.c_str());
        lua_rawseti(L, -2, idx++);
    }

    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void LuaRegisterCvarAPI(lua_State* L) {
    lua_getglobal(L, "uz");
    if (!lua_istable(L, -1)) {
        UZ_LOG(Scripting, Error,
            "LuaRegisterCvarAPI: 'uz' global is not a table — call RegisterUzTable first");
        lua_pop(L, 1);
        return;
    }

    // ── uz.cvar = { get, set, reset, reset_all, info, list } ─────────────
    lua_newtable(L);

    lua_pushcfunction(L, CvarGet);
    lua_setfield(L, -2, "get");

    lua_pushcfunction(L, CvarSet);
    lua_setfield(L, -2, "set");

    lua_pushcfunction(L, CvarReset);
    lua_setfield(L, -2, "reset");

    lua_pushcfunction(L, CvarResetAll);
    lua_setfield(L, -2, "reset_all");

    lua_pushcfunction(L, CvarInfo);
    lua_setfield(L, -2, "info");

    lua_pushcfunction(L, CvarList);
    lua_setfield(L, -2, "list");

    lua_setfield(L, -2, "cvar");

    lua_pop(L, 1);

    UZ_LOG(Scripting, Info, "uz.cvar API registered");
}

} // namespace uz
