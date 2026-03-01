# Phase 1 — Debug Console Commands

## Context

Without a debug console, developers must rely on:
- Engine logging (slow, reactive)
- Breakpoints in debugger (stops the world)
- Hot-reload and manual re-testing (tedious)

A **real-time command interface** is essential for:
- **Live inspection**: `print_scene`, `stats` → check game state without pausing
- **Quick experiments**: `set_timescale 0.5` → slow-motion testing, `spawn <prefab>` → test spawning logic
- **Scene manipulation**: `kill <node>`, `set_prop <node> <prop> <value>` → edit state mid-gameplay
- **Hot-reload feedback**: `reload_shaders`, `reload_scripts` → instant feedback on changes
- **AI-assisted debugging**: Claude can execute commands via IPC to inspect and modify game state

## Goal

Implement a **debug console** with:
- **~-key toggle**: Open/close console overlay in-game (debug builds only)
- **Command registry**: `ConsoleCommandRegistry` singleton in `game.dll`
- **Lua registration**: Game code registers commands via `ctx.console:register()`
- **C++ registration**: Optional C++ API for built-in commands
- **Built-in commands**: `help`, `quit`, `reload_scene`, `reload_shaders`, `set_timescale`, `stats`, `spawn`, `kill`, `set_prop`, etc.
- **Auto-complete**: Tab key completes command names and prefab paths
- **Command history**: Up/Down arrows navigate previous commands
- **IPC exposure**: `console.execute`, `console.get_commands`, `console.get_history` for editor access
- **Rich output**: Commands return formatted strings (plain text or JSON)

## Why Phase 1?

Console is a **development prerequisite** for all phases:
- Phase 1 developers need to inspect/manipulate scenes during development
- Phase 2+ rendering work requires quick testing (toggle debug visualizations)
- Game developers using UzEngine will want console commands for their own projects
- AI-assisted debugging (Claude running commands via IPC) requires console early

## Dependencies

- **Phase 0** (IPC/MCP server): ✅ Already has basic primitives
- **Phase 1** (Scene system, Lua scripting): 🔄 In progress
  - Scene must exist for `print_scene`, `kill`, `set_prop` commands
  - Lua must be initialized for `ctx.console` API
- **External libs**: None (just C++ STL)

## Spec

[docs/specs/console-commands.md](../../../docs/specs/console-commands.md)

