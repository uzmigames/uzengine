# Proposal: phase1-lua-dap-server

## Why

When a Lua script crashes, developers see only a single error line in the VSCode output panel. Setting breakpoints, stepping through NPC logic, or inspecting local variables requires quitting the game and adding print statements. This makes debugging complex gameplay bugs extremely slow.

Implementing a Debug Adapter Protocol server — tunneled through the existing IPC WebSocket — enables VSCode's built-in debugger UI to attach to the running game: set breakpoints in `.lua` files, step over/into/out, inspect local variables and upvalues, and view the full call stack. No separate debug port or process required.

## What Changes

- **`src/runtime/ipc/handlers/dap_handlers.cpp`** — IPC handler for `debug.dap` messages
- **`src/engine/scripting/lua_debug_hooks.h/cpp`** — LuaJIT debug hook integration (`lua_sethook`) for line/call/return events
- **`src/engine/scripting/lua_debug_session.h/cpp`** — session state: breakpoints, pause state, step mode
- **`vscode-extension/src/lua-debug-adapter.ts`** — `UzEngineLuaDebugAdapter` implementing VS Code `DebugAdapter` interface
- **Tests** — breakpoint hit, step commands, variable inspection, attach/detach lifecycle

## Impact

- Affected specs: `docs/specs/debug-infrastructure.md` (DAP bridge section)
- Dependencies: `phase0-debug-infrastructure-design` (DAP-over-IPC spec), `phase1-lua-scripting`
- Breaking change: NO — editor-only feature; game builds unaffected (`#ifdef UZ_EDITOR`)
- User benefit: full VSCode debugger UX for Lua scripts; dramatically faster gameplay bug resolution
