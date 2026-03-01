# Proposal: phase1-lua-scripting

## Why

UzEngine currently supports only C++ game logic via `game.dll`. While C++ provides full performance, its ~2.5s hot-reload cycle is too slow for rapid iteration on NPC behavior, triggers, pickups, and gameplay prototyping. Adding LuaJIT as a per-node scripting layer gives a ~60ms hot-reload path while keeping C++ as the primary heavy-lifting layer below it.

Lua is the industry standard for embeddable game scripting (WoW, Roblox, LÖVE2D, Corona), is well-understood by AI agents, and its plain-text syntax is ideal for AI-assisted development. LuaJIT's JIT compiler and FFI provide near-C++ performance for common game logic patterns.

## What Changes

- **`src/engine/scripting/`** — new subsystem in `uz_engine.lib`:
  - `lua_binding.h/cpp` — Vec3, Quat, NodeRef, Scene, InputState metatables
  - `script_runner.h/cpp` — per-node on_start/on_update/on_stop dispatch via lua_pcall
  - `lua_sandbox.h/cpp` — strips io.*, os.*, require, load from VM
- **`game.dll`** links LuaJIT 2.1 static lib (`libluajit.lib` via vcpkg)
- **`ecs/components.h`** — `CScriptRef` gains `ScriptKind kind` field (CPP or LUA)
- **`.uzscene` format** — `script = "res://game/foo.lua"` works alongside `.cpp` paths
- **`src/runtime/ipc/handlers/lua_handlers.cpp`** — `lua.eval`, `lua.reload_script`, `lua.get_errors`, `lua.get_globals` (editor-only)
- **Hot-reload** — FileWatcher dispatches `.lua` changes to `LuaScriptReloader` (~60ms path)
- **VSCode diagnostics** — Lua errors annotated on file+line (reuses shader-diagnostics.ts pattern)

## Impact

- Affected specs: `docs/specs/lua-scripting.md` (new), `docs/specs/scene-system.md` (script attachment)
- Affected modules: Engine (`scripting/`), Runtime (`lua_handlers`), Editor (diagnostics)
- Breaking change: NO — C++ scripts unchanged; `.lua` is opt-in per node
- User benefit: ~60ms hot-reload for NPC/gameplay logic; AI can iterate on Lua scripts in seconds without C++ recompile
