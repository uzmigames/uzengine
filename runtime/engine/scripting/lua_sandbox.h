#pragma once

/// lua_sandbox — security boundary between untrusted gameplay scripts and the
/// host process.
///
/// Script paths come from scene files, so a project opened in the editor can
/// run arbitrary Lua in Play Mode. Everything in this file exists so that such
/// a script can only compute and call the `uz.*` engine API: it can never load
/// native code, read or write files, run bytecode, reach the registry, or
/// tamper with state that other scripts share.
///
/// # Layers
/// 1. `LuaSandboxOpenSafeLibs` — opens only base, table, string, math, bit
///    (plus the JIT compiler, whose `jit` table is removed again) and strips
///    every code-loading and environment-escaping function.
/// 2. `LuaSandboxLoadTextChunk` — compiles source text only; precompiled
///    LuaJIT bytecode is rejected because LuaJIT does not verify it.
/// 3. `LuaSandboxBuildScriptGlobals` + `LuaSandboxCreateScriptEnv` — every
///    script runs in its own environment whose reads fall through to a
///    read-only whitelist; the shared library tables and the `uz` API are
///    exposed through read-only proxies.
///
/// # Thread Safety
/// NOT thread-safe. Must be called from the thread that owns the `lua_State`.

#include <cstddef>

struct lua_State;

namespace uz {

/// Opens the safe standard libraries and removes every unsafe global.
///
/// Opens `base` (which also provides `coroutine`), `table`, `string`, `math`
/// and `bit` with the LuaJIT calling convention (`lua_pushcfunction` +
/// `lua_call`). `package`, `debug`, `io`, `os` and `ffi` are never opened.
/// The `jit` library is opened only because its opener is what enables the
/// trace compiler and sets its default parameters; the `jit` global is
/// removed immediately afterwards, so scripts cannot reach it.
///
/// Afterwards it removes `load`, `loadfile`, `loadstring`, `dofile`,
/// `require`, `module`, `getfenv`, `setfenv`, `collectgarbage`, `newproxy`,
/// `rawset`, `rawget`, `rawequal` and `string.dump`, replaces `print` with a
/// version that writes to `UZ_LOG(Scripting, Info, ...)` instead of stdout,
/// and locks the string metatable so the real `string` table cannot be
/// reached through `getmetatable("")`.
///
/// # Parameters
/// - `L`: A freshly created Lua state with no libraries opened. Must not be null.
void LuaSandboxOpenSafeLibs(lua_State* L);

/// Compiles `size` bytes of Lua source text and pushes the resulting chunk.
///
/// Rejects any buffer whose first byte is `0x1B` (the LuaJIT bytecode
/// signature) with an explicit error, and loads with mode `"t"` so the
/// LuaJIT loader itself also refuses binary chunks after a BOM or `#!` line.
///
/// # Parameters
/// - `L`:         Active Lua state. Must not be null.
/// - `data`:      Source bytes. May be null only when `size` is 0.
/// - `size`:      Number of bytes in `data`.
/// - `chunkName`: Name used in error messages (e.g. `"scripts/foo.lua"`).
///
/// # Returns
/// `0` with the compiled function on the stack, or a non-zero Lua status
/// (`LUA_ERRSYNTAX` / `LUA_ERRMEM`) with an error message string on the stack.
[[nodiscard]] int LuaSandboxLoadTextChunk(lua_State* L, const char* data, size_t size,
                                          const char* chunkName);

/// Builds the read-only global whitelist that every script environment reads
/// through, and stores it in the registry.
///
/// Must be called after `LuaSandboxOpenSafeLibs` and after the `uz` global
/// table exists. Sub-APIs registered into `uz` later (VFX, trails, decals,
/// camera manager) remain visible because the `uz` proxy reads the live table.
///
/// Whitelisted names: `assert`, `error`, `getmetatable`, `setmetatable`,
/// `ipairs`, `pairs`, `next`, `pcall`, `xpcall`, `select`, `tonumber`,
/// `tostring`, `type`, `unpack`, `print`, `_VERSION`, plus read-only proxies
/// of `coroutine`, `math`, `string`, `table`, `bit` and a deep read-only proxy
/// of `uz`.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null.
///
/// # Returns
/// `true` on success, `false` if a required global is missing (logged).
bool LuaSandboxBuildScriptGlobals(lua_State* L);

/// Creates a fresh, isolated environment table for one script and returns its
/// registry reference.
///
/// The environment starts empty except for `_G`, which refers to the
/// environment itself. Reads of undefined names fall through to the shared
/// read-only whitelist; writes always land in this environment. The
/// environment's metatable is protected, so `getmetatable` / `setmetatable`
/// cannot detach or reach the whitelist.
///
/// Stack effect: none.
///
/// # Parameters
/// - `L`: Active Lua state. Must not be null.
///
/// # Returns
/// A registry reference, or `LUA_REFNIL` if `LuaSandboxBuildScriptGlobals`
/// has not run on this state (logged).
[[nodiscard]] int LuaSandboxCreateScriptEnv(lua_State* L);

/// Prevents scripts from reading or replacing the metatable at the top of the
/// stack by setting its `__metatable` field.
///
/// `getmetatable` then returns `false` and `setmetatable` raises an error.
/// C code using `lua_getmetatable` / `luaL_checkudata` is unaffected.
///
/// Stack effect: none (the metatable stays on the stack).
///
/// # Parameters
/// - `L`: Active Lua state with a metatable at index -1. Must not be null.
void LuaSandboxProtectMetatable(lua_State* L);

} // namespace uz
