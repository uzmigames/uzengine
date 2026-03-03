## 1. Research — Read Related Specs

- [x] 1.1 Read `docs/specs/lua-scripting.md` — understand `ctx` proxy structure and Lua binding patterns
- [x] 1.2 Read `docs/specs/save-system.md` — understand msgpack slot format and `uz_game_save_state` hook
- [x] 1.3 Read `docs/specs/event-bus.md` — understand `EventBus<T>` C++ and Lua string-dispatch layer
- [x] 1.4 Read `docs/specs/scene-system.md` — understand scene-scoped vs. global lifetime
- [x] 1.5 Identify use cases: player stats, quest flags, game mode booleans, AI aggro state

## 2. Design — GameState C++ Struct

- [x] 2.1 Define `GameStateValue` variant: `std::variant<bool, int32_t, float, std::string, Vec3>`
- [x] 2.2 Define `GameState` class: `std::unordered_map<std::string, GameStateValue>` backing store
- [x] 2.3 Specify `get<T>(key) -> T` — returns default value if key absent; no exception
- [x] 2.4 Specify `set<T>(key, value)` — stores value, fires change event if value differs
- [x] 2.5 Specify `has(key) -> bool` — membership test without default construction
- [x] 2.6 Specify `remove(key)` — deletes entry, fires remove event
- [x] 2.7 Specify `clear()` — resets all entries (called on scene unload)
- [x] 2.8 Specify `keys() -> vector<string>` — enumerate all keys (for serialization)
- [x] 2.9 Specify ownership: single `GameState` instance lives on `LuaVM`, destroyed on `uz_game_stop`
- [x] 2.10 Specify thread safety: main-thread-only access (assert in debug builds)

## 3. Design — Change Event Integration

- [x] 3.1 Specify `GameStateChangedEvent` struct: `key`, `old_value`, `new_value`, `GameStateValue` typed
- [x] 3.2 Specify integration with `EventBus<GameStateChangedEvent>` — `GameState::set` posts event
- [x] 3.3 Specify `GameStateRemovedEvent` struct: `key`, `last_value`
- [x] 3.4 Specify `on_change(key, fn)` convenience subscription — filters EventBus by key
- [x] 3.5 Specify deferred event dispatch: events queued during `set()`, dispatched at end of `uz_game_update` frame boundary

## 4. Design — Lua Proxy (ctx.shared)

- [x] 4.1 Specify `ctx.shared` metatable — `__index` calls `GameState::get`, `__newindex` calls `GameState::set`
- [x] 4.2 Specify type inference from Lua values: Lua number (integer? → int32_t, else float), string → string, boolean → bool, Vec3 userdata → Vec3
- [x] 4.3 Specify `ctx.shared:on_change(key, fn)` — registers Lua callback, returns subscription id
- [x] 4.4 Specify `ctx.shared:unsubscribe(id)` — removes callback
- [x] 4.5 Specify `ctx.shared:has(key) -> bool`
- [x] 4.6 Specify `ctx.shared:remove(key)`
- [x] 4.7 Specify `ctx.shared:keys() -> table` — returns array of all keys
- [x] 4.8 Specify error reporting: type mismatch on get (e.g., float stored, int requested) → warning logged, value cast attempted

## 5. Design — Serialization Hooks

- [x] 5.1 Specify `GameState::serialize(msgpack::packer&)` — encodes full key-value store as msgpack map
- [x] 5.2 Specify `GameState::deserialize(msgpack::object)` — restores from save slot, merges with defaults
- [x] 5.3 Specify integration point: `uz_game_save_state` calls `GameState::serialize`; `uz_game_load_state` calls `GameState::deserialize`
- [x] 5.4 Specify `[persistent]` key annotation: only keys with `ctx.shared:set_persistent(key, true)` are included in save
- [x] 5.5 Specify schema versioning: `GameState` save block includes `version` field for migration

## 6. Design — Editor IPC Integration

- [x] 6.1 Specify `scene.get_shared_state` IPC handler — returns full key-value dump as JSON
- [x] 6.2 Specify `scene.set_shared_state` IPC handler — allows editor to inject values at runtime
- [x] 6.3 Specify `scene.shared_state_changed` IPC event — pushed to editor on every change (throttled to 10 Hz)
- [x] 6.4 Specify Inspector panel integration: "Shared State" collapsible section showing all entries

## 7. Write Spec

- [x] 7.1 Create `docs/specs/shared-state.md` — full spec with all sections from steps 2–6
- [x] 7.2 Add Lua usage examples: health/score/flag patterns
- [x] 7.3 Add serialization format diagram: msgpack structure for `GameState` save block
- [x] 7.4 Update `docs/specs/save-system.md` — add `GameState` as a named save block
- [x] 7.5 Update `docs/specs/event-bus.md` — add `GameStateChangedEvent` to built-in events table
- [x] 7.6 Update `docs/README.md` — add `shared-state.md` to specs table

## 8. Review

- [x] 8.1 Verify `ctx.shared` proxy is safe under Lua hot-reload (proxy re-bound on `on_start`)
- [x] 8.2 Verify change events do not fire during `deserialize` (bulk load should not trigger game logic)
- [x] 8.3 Confirm design is compatible with multi-scene streaming (shared state is global, not per-chunk)
- [x] 8.4 Verify key namespace collision strategy (e.g., `"player.health"` vs `"health"`) is documented
