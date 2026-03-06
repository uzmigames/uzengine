# Proposal: phase1-script-execution-safety

## Why

`while true do end` written in any Lua `on_update` function freezes render.exe entirely. The game logic thread stalls, the IPC server becomes unresponsive, the editor viewport freezes, and the user must force-kill the process. This is a guaranteed Day 1 issue — every new Lua user makes this mistake.

LuaJIT compiles hot loops to native code, bypassing standard Lua instruction hooks. Both a CPU instruction budget AND a watchdog thread are required. Without this, Lua scripting is unsafe for production and developer use.

## What Changes

- **Instruction hook**: `lua_sethook()` with `LUA_MASKCOUNT` and 5M instruction budget per `on_update` call
- **Watchdog thread**: Background thread terminates scripts exceeding 100ms wall-clock time via `lua_sethook` with immediate error
- **Memory allocator cap**: Custom `lua_newstate` allocator rejects allocations beyond 32 MB per VM, preventing OOM crashes
- **Error recovery**: `lua_pcall` wraps all script calls; errored scripts are marked and skipped until hot-reloaded
- **VSCode diagnostics**: Timeout/crash errors sent via IPC to Problems panel with file+line
- **Sandbox escape detection**: `__newindex` metamethod on `_G` logs and blocks global modifications
- **Configuration**: `.uzproj` `[scripting]` section for instruction budget, timeout, memory limit

## Impact

- Affected specs: `docs/specs/scripting/script-execution-safety.md`
- Affected code: `src/engine/scripting/lua_vm.cpp`, `script_runner.cpp`, game.dll LuaVM lifecycle
- Breaking change: NO (adds safety constraints, existing working scripts unaffected)
- User benefit: Engine cannot be frozen by user scripts; clean error messages in VSCode; memory-safe Lua VM
