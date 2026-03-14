#include "lua_vm.h"

#include "log.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

// ─────────────────────────────────────────────────────────────────────────────
// Log Category
// ─────────────────────────────────────────────────────────────────────────────

UZ_DEFINE_LOG_CATEGORY(Scripting, Info);

namespace uz {

lua_State* LuaVM::s_state = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// Sandbox — remove dangerous globals
// ─────────────────────────────────────────────────────────────────────────────

static void ApplySandbox(lua_State* L) {
    // Remove dangerous functions
    const char* forbidden[] = {
        "os", "io", "loadfile", "dofile", "load", "require",
        "collectgarbage", "newproxy", "rawset", "rawget", "rawequal",
    };
    for (const char* name : forbidden) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }
    UZ_LOG(Scripting, Info, "Lua sandbox applied - {} globals removed",
           sizeof(forbidden) / sizeof(forbidden[0]));
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool LuaVM::Init() {
    if (s_state) {
        UZ_LOG(Scripting, Warning, "LuaVM::Init() called but already initialized");
        return true;
    }

    s_state = luaL_newstate();
    if (!s_state) {
        UZ_LOG(Scripting, Error, "Failed to create Lua state");
        return false;
    }

    luaL_openlibs(s_state);
    ApplySandbox(s_state);

    UZ_LOG(Scripting, Info, "LuaVM initialized (LuaJIT 2.1)");
    return true;
}

void LuaVM::Shutdown() {
    if (s_state) {
        lua_close(s_state);
        s_state = nullptr;
        UZ_LOG(Scripting, Info, "LuaVM shutdown");
    }
}

lua_State* LuaVM::GetState() {
    return s_state;
}

bool LuaVM::IsInitialized() {
    return s_state != nullptr;
}

} // namespace uz
