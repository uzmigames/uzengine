# Proposal: phase1-timer-system

## Why

Lua scripts that need time-delayed or repeating actions must manually track countdown variables in `on_update` — this produces repetitive boilerplate and makes scripts harder for both humans and AI agents to read and generate correctly. A first-class timer API (`schedule_after`, `schedule_every`) eliminates this pattern entirely and is one of the most-used scripting conveniences in every game engine (Unity `Invoke`, Godot `create_timer`, Unreal `SetTimer`). It belongs in Phase 1 because it is a prerequisite for clean NPC behavior, pickups, triggers, and any time-based gameplay prototype.

## What Changes

- **`src/game/lua_vm.h/cpp`** — Add `LuaTimer` struct, `std::vector<LuaTimer> timers_`, `std::vector<int> next_frame_callbacks_`, `drain_timers(float dt)` method, `clear_timers()` on scene unload
- **`src/engine/scripting/lua_binding.h/cpp`** — Register `ctx:schedule_after`, `ctx:schedule_every`, `ctx:cancel_timer`, `ctx:schedule_next_frame` on the `GameContext` userdata metatable
- **`src/game/game.cpp`** — Call `lua_vm.drain_timers(dt)` in `uz_game_update` before per-node `on_update` dispatch
- **`docs/specs/timer-system.md`** — Full spec (already written in planning phase)
- **No IPC handler needed** — timers are a game-logic-only concern; no editor inspection required in Phase 1

Timer drain order per frame:
1. Next-frame callbacks fire first (via `schedule_next_frame`)
2. Timed callbacks fire in registration order
3. Per-node `on_update` runs after all timer callbacks

## Impact

- Affected specs: `docs/specs/timer-system.md` (new), `docs/specs/lua-scripting.md` (references ctx timer API)
- Affected modules: `game.dll` (`lua_vm`, `game.cpp`), Engine scripting layer (`lua_binding`)
- Breaking change: NO — purely additive; no existing Lua API changes
- User benefit: Eliminates manual countdown boilerplate in Lua scripts; AI can generate `schedule_after` patterns inline without understanding frame-delta accumulation; hot-reload correctly resets timers via `on_stop` / `on_start` without manual tracking
