# Lua Scripting Engineer — Persistent Memory

## Project: UzEngine
LuaJIT 2.1 embedded inside game.dll. Per-node .lua scripts. ~60ms hot-reload.

## Key Paths
- `docs/specs/lua-scripting.md` — complete spec (sandbox rules, API, lifecycle)
- `src/engine/scripting/` — binding helpers only (no LuaJIT linked here)
- `src/game/lua_vm.h/cpp` — VM lifecycle, module registry
- `src/game/lua_script_reloader.h/cpp` — FileWatcher hot-reload

## Architecture Rules
- LuaJIT linked ONLY in game.dll — never uz_engine.lib or UzEngine.exe
- Always use lua_pcall for script dispatch — never lua_call
- lua_sandbox_apply() called immediately after luaL_openlibs()
- Error in script → node disabled, log warning, engine continues (no crash)

## Sandbox: Stripped Globals
io, os, require, dofile, loadfile, load, package, debug (kept in UZ_DEBUG)

## Script Module Pattern
- Shared prototype M per file
- Per-node self via setmetatable({}, {__index=M})
- Lifecycle: M:on_start(ctx), M:on_update(ctx, dt), M:on_stop(ctx)

## LuaJIT Version
- Target: LuaJIT 2.1 via vcpkg
