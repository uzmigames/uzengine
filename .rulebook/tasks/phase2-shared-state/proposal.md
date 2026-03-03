# Proposal: phase1-shared-state

## Why

Lua scripts on separate nodes have no clean way to share global game state. The current workaround — `ctx.scene:find_node("GameManager")` — is fragile, order-dependent, and breaks during scene streaming. A centralized `GameState` store with typed get/set, change event subscriptions, and automatic save/load integration provides the canonical inter-script communication primitive for UzEngine games.

## What Changes

- **`src/game/game_state.h/cpp`** — `GameState` class: typed key-value store, `get<T>`/`set<T>`/`has`/`remove`, `GameStateChangedEvent` emission
- **`src/engine/scripting/lua_binding.cpp`** — extend `ctx` metatable with `ctx.shared` proxy (metatable `__index`/`__newindex`)
- **`src/runtime/ipc/handlers/state_handlers.cpp`** — `scene.get_shared_state`, `scene.set_shared_state`, `scene.shared_state_changed` IPC handlers
- **`uz_game_save_state` / `uz_game_load_state`** — serialize/restore `GameState` as msgpack block
- **Tests** — get/set correctness, change events, Lua proxy, serialization round-trip

## Impact

- Affected specs: `docs/specs/shared-state.md` (implementation of design spec)
- Dependencies: `phase0-shared-state-design` (spec complete), `phase1-lua-scripting` (LuaVM/ctx), `phase3-event-bus` (change events)
- Breaking change: NO — additive; no existing API changed
- User benefit: clean inter-script communication; automatic save/load of all persistent game flags
