# Proposal: phase0-shared-state-design

## Why

Lua scripts on different nodes need a way to share global game state (player health, score, flags, inventory counts) without coupling node scripts to each other via direct node references. Without a shared state store, developers write `ctx.scene:find_node("GameManager"):get("health")` — fragile, order-dependent, and impossible to serialize cleanly.

A centralized `GameState` store — with typed get/set, change events, and save/load hooks — solves inter-script communication cleanly. This must be designed before `phase1-shared-state` implements it, because the design must be compatible with `phase3-save-system` serialization and `phase3-event-bus` change events.

## What Changes

- **No code changes** — this is a design-only task.
- **`docs/specs/shared-state.md`** — new spec defining:
  - `GameState` C++ struct — flat key-value store, typed entries (bool/int/float/string/vec3)
  - `ctx.shared` Lua proxy — `ctx.shared.health`, `ctx.shared:on_change("health", fn)`
  - Change events — integrated with EventBus (`GameStateChangedEvent<T>`)
  - Serialization hooks — compatible with `save-system.md` msgpack slot format
  - Thread safety — main-thread-only access, no locks needed

## Impact

- Affected specs: `docs/specs/shared-state.md` (new)
- Downstream tasks: `phase1-shared-state`, `phase3-save-system` (serialization), `phase3-event-bus` (change events)
- Breaking change: NO — design only
- User benefit: clean inter-script communication without node coupling; automatic save/load of all shared state
