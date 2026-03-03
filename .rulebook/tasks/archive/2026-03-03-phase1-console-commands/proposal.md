# Proposal: phase1-console-commands

## Why

UzEngine currently has no runtime command interface. Developers who need to inspect or manipulate engine state during gameplay must stop the process, attach a debugger, set breakpoints, and restart — a workflow that breaks flow and is impossible to hand off to AI tooling. Engine logging is reactive and cannot modify state; it only observes it after the fact.

A real-time debug console with a command registry solves this by giving developers a live text interface to the running engine. Commands like `print_scene`, `stats`, `set_timescale`, and `reload_scene` allow rapid iteration without restarting. This is the same workflow established by Quake, Unreal, and Unity's debug consoles, all of which are considered essential developer tooling.

The IPC exposure of the console (`console.execute`) is equally important for AI-assisted development. Claude can issue commands to inspect and modify game state programmatically, which is a core design goal of UzEngine's AI-native architecture. Without the console, the IPC server can only read engine state, not drive it.

## What Changes

- Add `ConsoleCommandRegistry` singleton in `src/engine/console/console_registry.c` — stores command name, description, handler function pointer, and completion callback; backed by a hash map in the permanent arena
- Implement `~`-key toggle in `src/engine/input/input.c` — opens and closes the overlay; restricted to Debug and Development builds via `#ifdef UZ_BUILD_DEBUG`
- Add console overlay render pass in `src/engine/renderer/console_pass.c` — semi-transparent background quad, scrollable output log (last 256 lines), input line at bottom; rendered as a DX12 overlay after the main scene pass
- Implement Tab autocomplete in `src/engine/console/console_input.c` — queries `ConsoleCommandRegistry` for prefix matches, cycles through candidates; also completes asset paths for commands that take a file argument
- Implement Up/Down arrow command history in `src/engine/console/console_input.c` — circular buffer of 64 entries; persisted to `%APPDATA%/UzEngine/console_history.txt` on shutdown
- Register built-in commands in `src/engine/console/console_builtins.c`: `help`, `quit`, `reload_scene`, `set_cvar`, `list_cvars`, `stats`, `spawn`, `kill`, `set_prop`, `reload_shaders`, `reload_scripts`
- Add `ctx.console` Lua API in `src/engine/scripting/lua_console.c` — `ctx.console:register("name", fn)`, `ctx.console:execute("cmd")`, `ctx.console:print("text")`, `ctx.console:clear()`
- Expose console via IPC in `src/engine/ipc/ipc_handlers.c` — add `console.execute`, `console.get_commands`, `console.get_history` handlers so the VSCode extension and Claude can drive the console remotely
- Add CVar system in `src/engine/console/cvar.c` — `CVar` struct with name, value (string), description, flags (`CVAR_CHEAT`, `CVAR_READONLY`); `set_cvar` and `list_cvars` built-in commands use this registry

## Impact

- Affected specs: `docs/specs/console-commands.md`
- Affected code: `src/engine/console/`, `src/engine/input/input.c`, `src/engine/renderer/console_pass.c`, `src/engine/scripting/lua_console.c`, `src/engine/ipc/ipc_handlers.c`
- Breaking change: NO
- User benefit: Developers can inspect and modify live game state without stopping the engine; Claude can drive the engine via IPC console commands for AI-assisted debugging and scene authoring
