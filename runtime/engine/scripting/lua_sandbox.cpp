/// lua_sandbox — safe library set, text-only chunk loading, and per-script
/// read-only global environments for untrusted gameplay scripts.

#include "lua_sandbox.h"

#include "log.h"

extern "C" {
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>
}

#include <string_view>

UZ_DECLARE_LOG_CATEGORY(Scripting);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// First byte of every LuaJIT (and PUC Lua) bytecode dump (`"\x1bLJ"`).
static constexpr unsigned char kBytecodeSignatureByte = 0x1B;

/// Registry key of the weak-valued cache mapping engine tables to their
/// read-only proxies, so `uz.scene == uz.scene` holds inside scripts.
static constexpr const char* kProxyCacheKey = "uz.sandbox.proxy_cache";

/// Registry key of the metatable shared by every script environment.
static constexpr const char* kEnvMetatableKey = "uz.sandbox.env_metatable";

/// A standard library opener paired with the name it registers under.
struct SandboxLibrary {
    const char*   name;
    lua_CFunction open;
};

/// Libraries opened into the VM. `package`, `debug`, `io`, `os` and `ffi` are
/// deliberately absent. `jit` is opened only because `luaopen_jit` is what
/// turns the trace compiler on and loads its default parameters; without it
/// LuaJIT runs as a pure interpreter. Its global is removed right after.
static constexpr SandboxLibrary kSafeLibraries[] = {
    {"",               luaopen_base},   // also registers `coroutine`
    {LUA_TABLIBNAME,   luaopen_table},
    {LUA_STRLIBNAME,   luaopen_string},
    {LUA_MATHLIBNAME,  luaopen_math},
    {LUA_BITLIBNAME,   luaopen_bit},
    {LUA_JITLIBNAME,   luaopen_jit},
};

/// Globals removed after the libraries are open.
///
/// - Code loading: `load`, `loadfile`, `loadstring`, `dofile`, `require`, `module`.
/// - Environment escape: `getfenv`/`setfenv` reach and replace the environment
///   of any function on the call stack, including the host's.
/// - Metatable bypass: `rawset`/`rawget`/`rawequal` ignore the read-only
///   proxies' `__newindex` / `__index`.
/// - GC control: `collectgarbage`, `newproxy`.
/// - Libraries that must never be reachable even if a future change opens them.
static constexpr const char* kForbiddenGlobals[] = {
    "load", "loadfile", "loadstring", "dofile", "require", "module",
    "getfenv", "setfenv",
    "rawset", "rawget", "rawequal",
    "collectgarbage", "newproxy",
    LUA_LOADLIBNAME, LUA_DBLIBNAME, LUA_IOLIBNAME, LUA_OSLIBNAME,
    LUA_FFILIBNAME, LUA_JITLIBNAME,
};

/// Base-library values copied into the script whitelist as-is.
static constexpr const char* kWhitelistedBaseGlobals[] = {
    "assert", "error", "getmetatable", "setmetatable",
    "ipairs", "pairs", "next", "pcall", "xpcall", "select",
    "tonumber", "tostring", "type", "unpack", "print", "_VERSION",
};

/// Library tables exposed to scripts through read-only proxies.
static constexpr const char* kWhitelistedLibraries[] = {
    LUA_COLIBNAME, LUA_MATHLIBNAME, LUA_STRLIBNAME, LUA_TABLIBNAME, LUA_BITLIBNAME,
};

/// Global name of the engine API table.
static constexpr const char* kEngineApiGlobal = "uz";

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a relative stack index into an absolute one. Pseudo-indices
/// (registry, upvalues) are returned unchanged.
[[nodiscard]] static int AbsoluteIndex(lua_State* L, int idx) {
    return (idx < 0 && idx > LUA_REGISTRYINDEX) ? lua_gettop(L) + idx + 1 : idx;
}

/// `print(...)` replacement: formats every argument with the original
/// `tostring` (upvalue 1, honours `__tostring`), joins them with tabs like the
/// stock `print`, and writes the line to the engine log instead of stdout.
static int SandboxPrint(lua_State* L) {
    const int argc = lua_gettop(L);
    if (argc == 0) {
        UZ_LOG(Scripting, Info, "[Lua] ");
        return 0;
    }
    luaL_checkstack(L, 2 * argc + 1, "too many arguments to 'print'");
    for (int i = 1; i <= argc; ++i) {
        if (i > 1) lua_pushliteral(L, "\t");
        lua_pushvalue(L, lua_upvalueindex(1));
        lua_pushvalue(L, i);
        lua_call(L, 1, 1);
        if (lua_type(L, -1) != LUA_TSTRING) {
            return luaL_error(L, "'tostring' must return a string to 'print'");
        }
    }
    lua_concat(L, 2 * argc - 1);
    size_t length = 0;
    const char* text = lua_tolstring(L, -1, &length);
    UZ_LOG(Scripting, Info, "[Lua] {}", std::string_view(text, length));
    return 0;
}

/// `__newindex` of every read-only proxy: refuses the write with a message
/// naming the field.
static int SandboxReadOnlyNewIndex(lua_State* L) {
    if (lua_type(L, 2) == LUA_TSTRING) {
        return luaL_error(L, "attempt to modify read-only table (field '%s')",
                          lua_tostring(L, 2));
    }
    return luaL_error(L, "attempt to modify read-only table (key of type %s)",
                      luaL_typename(L, 2));
}

/// Fills the metatable at the top of the stack with the fields shared by all
/// read-only proxies: `__newindex` refuses writes and `__metatable` hides the
/// metatable (and with it the real table) from `getmetatable`.
static void SetReadOnlyProxyFields(lua_State* L) {
    lua_pushcfunction(L, SandboxReadOnlyNewIndex);
    lua_setfield(L, -2, "__newindex");
    LuaSandboxProtectMetatable(L);
}

static void PushDeepReadOnlyProxy(lua_State* L, int targetIdx, int cacheIdx);

/// `__index` of a deep proxy: reads `target[key]` (upvalue 1) and wraps any
/// table result in its own deep proxy, so nested engine tables such as
/// `uz.scene` are read-only as well. Upvalue 2 is the proxy cache.
static int SandboxDeepProxyIndex(lua_State* L) {
    lua_pushvalue(L, 2);
    lua_gettable(L, lua_upvalueindex(1));
    if (lua_istable(L, -1)) {
        PushDeepReadOnlyProxy(L, lua_gettop(L), lua_upvalueindex(2));
    }
    return 1;
}

/// Pushes the deep read-only proxy for the table at `targetIdx`, creating and
/// caching it on first use.
///
/// The proxy is an empty table whose metatable reads through
/// `SandboxDeepProxyIndex`, so values added to the target later (sub-APIs
/// registered after VM init, `uz.delta_time` updates) are always current.
static void PushDeepReadOnlyProxy(lua_State* L, int targetIdx, int cacheIdx) {
    targetIdx = AbsoluteIndex(L, targetIdx);
    cacheIdx  = AbsoluteIndex(L, cacheIdx);

    lua_pushvalue(L, targetIdx);
    lua_rawget(L, cacheIdx);                        // [cached?]
    if (lua_istable(L, -1)) return;
    lua_pop(L, 1);

    lua_newtable(L);                                // [proxy]
    lua_createtable(L, 0, 3);                       // [proxy, mt]
    lua_pushvalue(L, targetIdx);
    lua_pushvalue(L, cacheIdx);
    lua_pushcclosure(L, SandboxDeepProxyIndex, 2);
    lua_setfield(L, -2, "__index");
    SetReadOnlyProxyFields(L);
    lua_setmetatable(L, -2);                        // [proxy]

    lua_pushvalue(L, targetIdx);
    lua_pushvalue(L, -2);
    lua_rawset(L, cacheIdx);                        // cache[target] = proxy
}

/// Pushes a read-only proxy whose `__index` is the table at `targetIdx`
/// itself. A table `__index` keeps global lookups such as `math.sin` on the
/// JIT's fast path; it is only used for tables with no nested tables or
/// userdata, which would otherwise be handed out mutable.
static void PushShallowReadOnlyProxy(lua_State* L, int targetIdx) {
    targetIdx = AbsoluteIndex(L, targetIdx);
    lua_newtable(L);                                // [proxy]
    lua_createtable(L, 0, 3);                       // [proxy, mt]
    lua_pushvalue(L, targetIdx);
    lua_setfield(L, -2, "__index");
    SetReadOnlyProxyFields(L);
    lua_setmetatable(L, -2);                        // [proxy]
}

/// Returns true if any value stored in the table at `idx` is a table or
/// userdata, i.e. something a shallow proxy would expose by reference.
[[nodiscard]] static bool HasReferenceValues(lua_State* L, int idx) {
    idx = AbsoluteIndex(L, idx);
    lua_pushnil(L);
    while (lua_next(L, idx) != 0) {
        const int type = lua_type(L, -1);
        lua_pop(L, 1);
        if (type == LUA_TTABLE || type == LUA_TUSERDATA) {
            lua_pop(L, 1);                          // pop key, end traversal
            return true;
        }
    }
    return false;
}

/// Creates the weak-valued proxy cache, stores it in the registry and leaves
/// it on the stack. Weak values let unreferenced proxies be collected; a live
/// proxy stays cached, so table identity is stable while scripts can see it.
static void PushNewProxyCache(lua_State* L) {
    lua_newtable(L);                                // [cache]
    lua_createtable(L, 0, 1);                       // [cache, mt]
    lua_pushliteral(L, "v");
    lua_setfield(L, -2, "__mode");
    lua_setmetatable(L, -2);                        // [cache]
    lua_pushvalue(L, -1);
    lua_setfield(L, LUA_REGISTRYINDEX, kProxyCacheKey);
}

/// Copies the base-library globals into the whitelist at `whitelistIdx`.
[[nodiscard]] static bool AddBaseGlobals(lua_State* L, int whitelistIdx) {
    for (const char* name : kWhitelistedBaseGlobals) {
        lua_getglobal(L, name);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            UZ_LOG(Scripting, Error, "Lua sandbox: required global '{}' is missing", name);
            return false;
        }
        lua_setfield(L, whitelistIdx, name);
    }
    return true;
}

/// Adds read-only proxies of the standard library tables to the whitelist.
[[nodiscard]] static bool AddLibraryProxies(lua_State* L, int whitelistIdx, int cacheIdx) {
    for (const char* name : kWhitelistedLibraries) {
        lua_getglobal(L, name);                     // [lib]
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            UZ_LOG(Scripting, Error, "Lua sandbox: library '{}' is not open", name);
            return false;
        }
        if (HasReferenceValues(L, -1)) {
            PushDeepReadOnlyProxy(L, -1, cacheIdx); // [lib, proxy]
        } else {
            PushShallowReadOnlyProxy(L, -1);        // [lib, proxy]
        }
        lua_setfield(L, whitelistIdx, name);        // [lib]
        lua_pop(L, 1);
    }
    return true;
}

/// Adds the deep read-only proxy of the engine API table to the whitelist.
[[nodiscard]] static bool AddEngineApiProxy(lua_State* L, int whitelistIdx, int cacheIdx) {
    lua_getglobal(L, kEngineApiGlobal);             // [uz]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        UZ_LOG(Scripting, Error, "Lua sandbox: engine API global '{}' is not a table",
               kEngineApiGlobal);
        return false;
    }
    PushDeepReadOnlyProxy(L, -1, cacheIdx);         // [uz, proxy]
    lua_setfield(L, whitelistIdx, kEngineApiGlobal);
    lua_pop(L, 1);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void LuaSandboxOpenSafeLibs(lua_State* L) {
    for (const SandboxLibrary& lib : kSafeLibraries) {
        lua_pushcfunction(L, lib.open);
        lua_pushstring(L, lib.name);
        lua_call(L, 1, 0);
    }

    for (const char* name : kForbiddenGlobals) {
        lua_pushnil(L);
        lua_setglobal(L, name);
    }

    // luaopen_jit pre-registers loaders for jit.util (bytecode and trace
    // introspection) and jit.profile. Nothing can call them without
    // `require`, but drop the preload table so they cannot be resurrected.
    lua_pushnil(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "_PRELOAD");

    // string.dump turns any function into bytecode — the input format the
    // text-only loader exists to keep out.
    lua_getglobal(L, LUA_STRLIBNAME);
    lua_pushnil(L);
    lua_setfield(L, -2, "dump");
    lua_pop(L, 1);

    // print writes to stdout by default; route it to the engine log.
    lua_getglobal(L, "tostring");
    lua_pushcclosure(L, SandboxPrint, 1);
    lua_setglobal(L, "print");

    // getmetatable("").__index is the real string table; hide it so scripts
    // cannot mutate the string library behind their read-only proxy.
    lua_pushliteral(L, "");
    if (lua_getmetatable(L, -1)) {
        LuaSandboxProtectMetatable(L);
        lua_pop(L, 1);
    }
    lua_pop(L, 1);

    UZ_LOG(Scripting, Info,
           "Lua sandbox: opened {} libraries, removed {} globals and string.dump",
           sizeof(kSafeLibraries) / sizeof(kSafeLibraries[0]),
           sizeof(kForbiddenGlobals) / sizeof(kForbiddenGlobals[0]));
}

int LuaSandboxLoadTextChunk(lua_State* L, const char* data, size_t size,
                            const char* chunkName) {
    if (size > 0 && static_cast<unsigned char>(data[0]) == kBytecodeSignatureByte) {
        lua_pushfstring(L, "%s: precompiled bytecode chunks are not allowed, "
                           "scripts must be Lua source text", chunkName);
        return LUA_ERRSYNTAX;
    }
    return luaL_loadbufferx(L, data, size, chunkName, "t");
}

bool LuaSandboxBuildScriptGlobals(lua_State* L) {
    const int top = lua_gettop(L);

    PushNewProxyCache(L);
    const int cacheIdx = lua_gettop(L);
    lua_createtable(L, 0, 24);
    const int whitelistIdx = lua_gettop(L);

    if (!AddBaseGlobals(L, whitelistIdx) ||
        !AddLibraryProxies(L, whitelistIdx, cacheIdx) ||
        !AddEngineApiProxy(L, whitelistIdx, cacheIdx)) {
        lua_settop(L, top);
        return false;
    }

    // Shared environment metatable: reads fall through to the whitelist, and
    // the metatable itself is hidden so scripts cannot reach the whitelist.
    lua_createtable(L, 0, 2);
    lua_pushvalue(L, whitelistIdx);
    lua_setfield(L, -2, "__index");
    LuaSandboxProtectMetatable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, kEnvMetatableKey);

    lua_settop(L, top);
    UZ_LOG(Scripting, Info, "Lua sandbox: script global whitelist built");
    return true;
}

int LuaSandboxCreateScriptEnv(lua_State* L) {
    lua_getfield(L, LUA_REGISTRYINDEX, kEnvMetatableKey);  // [mt]
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        UZ_LOG(Scripting, Error,
               "Lua sandbox: script environment requested before the whitelist was built");
        return LUA_REFNIL;
    }

    lua_newtable(L);                                       // [mt, env]
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "_G");                             // env._G = env
    lua_pushvalue(L, -2);
    lua_setmetatable(L, -2);                               // [mt, env]

    const int ref = luaL_ref(L, LUA_REGISTRYINDEX);        // [mt]
    lua_pop(L, 1);
    return ref;
}

void LuaSandboxProtectMetatable(lua_State* L) {
    lua_pushboolean(L, 0);
    lua_setfield(L, -2, "__metatable");
}

} // namespace uz
