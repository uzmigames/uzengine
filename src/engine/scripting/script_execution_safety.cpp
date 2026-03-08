#include "engine/scripting/script_execution_safety.h"
#include "engine/core/diag.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <cstdlib>
#include <cstring>

namespace uz {

// ---------------------------------------------------------------------------
// LuaAllocator
// ---------------------------------------------------------------------------

void* LuaAllocator::alloc(void* ud, void* ptr, size_t osize, size_t nsize) {
    auto* a = static_cast<LuaAllocator*>(ud);

    if (nsize == 0) {
        // Free
        a->used.fetch_sub(osize, std::memory_order_relaxed);
        free(ptr);
        return nullptr;
    }

    if (ptr == nullptr) {
        // New allocation (osize is the Lua type tag, not previous size)
        size_t current = a->used.load(std::memory_order_relaxed);
        if (current + nsize > a->limit) {
            return nullptr;  // Triggers LUA_ERRMEM
        }
        void* result = malloc(nsize);
        if (result) {
            a->used.fetch_add(nsize, std::memory_order_relaxed);
        }
        return result;
    }

    // Realloc
    if (nsize > osize) {
        size_t delta = nsize - osize;
        size_t current = a->used.load(std::memory_order_relaxed);
        if (current + delta > a->limit) {
            return nullptr;  // Triggers LUA_ERRMEM
        }
    }

    void* result = realloc(ptr, nsize);
    if (result) {
        // Adjust tracked memory: subtract old, add new
        if (nsize >= osize) {
            a->used.fetch_add(nsize - osize, std::memory_order_relaxed);
        } else {
            a->used.fetch_sub(osize - nsize, std::memory_order_relaxed);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Instruction hook
// ---------------------------------------------------------------------------

void lua_instruction_hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "script exceeded instruction budget");
}

void lua_reset_instruction_hook(lua_State* L, int budget) {
    lua_sethook(L, lua_instruction_hook, LUA_MASKCOUNT, budget);
}

// ---------------------------------------------------------------------------
// Immediate abort hook (used by watchdog)
// ---------------------------------------------------------------------------

void lua_immediate_abort_hook(lua_State* L, lua_Debug* /*ar*/) {
    luaL_error(L, "script watchdog: execution exceeded wall-clock timeout");
}

// ---------------------------------------------------------------------------
// Sandbox escape detection
// ---------------------------------------------------------------------------

// Registry key for the ScriptingConfig pointer
static const char* const kGlobalGuardConfigKey = "uz_global_guard_config";

static int sandbox_newindex(lua_State* L) {
    // Stack: table, key, value
    const char* key = lua_tostring(L, 2);
    if (!key) {
        // Non-string key — block silently
        return 0;
    }

    // Retrieve config from registry
    lua_getfield(L, LUA_REGISTRYINDEX, kGlobalGuardConfigKey);
    auto* config = static_cast<const ScriptingConfig*>(lua_touserdata(L, -1));
    lua_pop(L, 1);

    if (config) {
        // Check whitelist
        for (const auto& allowed : config->allowed_globals) {
            if (allowed == key) {
                // Whitelisted — perform the raw assignment
                lua_rawset(L, 1);
                return 0;
            }
        }

        if (config->sandbox_globals == "warn") {
            // Warn mode — allow assignment but log
            DIAG_FMT("[Lua] WARNING: Script set global '%s' (warn mode)", key);
            lua_rawset(L, 1);
            return 0;
        }
    }

    // Block mode (default) — reject and log
    DIAG_FMT("[Lua] BLOCKED: Script attempted to set global '%s'", key);
    return 0;
}

void lua_install_global_guard(lua_State* L, const ScriptingConfig* config) {
    // Store config pointer in registry
    lua_pushlightuserdata(L, const_cast<ScriptingConfig*>(config));
    lua_setfield(L, LUA_REGISTRYINDEX, kGlobalGuardConfigKey);

    // Create metatable for _G: { __newindex = sandbox_newindex }
    lua_pushvalue(L, LUA_GLOBALSINDEX);  // push _G
    lua_newtable(L);                      // metatable
    lua_pushcfunction(L, sandbox_newindex);
    lua_setfield(L, -2, "__newindex");
    lua_setmetatable(L, -2);              // setmetatable(_G, mt)
    lua_pop(L, 1);                        // pop _G
}

} // namespace uz
