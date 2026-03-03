## 1. Research — Read Related Specs

- [ ] 1.1 Read `docs/specs/lua-scripting.md` — understand `ctx` proxy structure and Lua binding patterns
- [ ] 1.2 Read `docs/specs/save-system.md` — understand msgpack slot format and `uz_game_save_state` hook
- [ ] 1.3 Read `docs/specs/event-bus.md` — understand `EventBus<T>` C++ and Lua string-dispatch layer
- [ ] 1.4 Read `docs/specs/scene-system.md` — understand scene-scoped vs. global lifetime
- [ ] 1.5 Identify use cases: player stats, quest flags, game mode booleans, AI aggro state

## 2. Design — GameState C++ Struct

- [ ] 2.1 Define `GameStateValue` variant: `std::variant<bool, int32_t, float, std::string, Vec3>`
- [ ] 2.2 Define `GameState` class: `std::unordered_map<std::string, GameStateValue>` backing store
- [ ] 2.3 Specify `get<T>(key) -> T` — returns default value if key absent; no exception
- [ ] 2.4 Specify `set<T>(key, value)` — stores value, fires change event if value differs
- [ ] 2.5 Specify `has(key) -> bool` — membership test without default construction
- [ ] 2.6 Specify `remove(key)` — deletes entry, fires remove event
- [ ] 2.7 Specify `clear()` — resets all entries (called on scene unload)
- [ ] 2.8 Specify `keys() -> vector<string>` — enumerate all keys (for serialization)
- [ ] 2.9 Specify ownership: single `GameState` instance lives on `LuaVM`, destroyed on `uz_game_stop`
- [ ] 2.10 Specify thread safety: main-thread-only access (assert in debug builds)

## 3. Design — Change Event Integration

- [ ] 3.1 Specify `GameStateChangedEvent` struct: `key`, `old_value`, `new_value`, `GameStateValue` typed
- [ ] 3.2 Specify integration with `EventBus<GameStateChangedEvent>` — `GameState::set` posts event
- [ ] 3.3 Specify `GameStateRemovedEvent` struct: `key`, `last_value`
- [ ] 3.4 Specify `on_change(key, fn)` convenience subscription — filters EventBus by key
- [ ] 3.5 Specify deferred event dispatch: events queued during `set()`, dispatched at end of `uz_game_update` frame boundary

## 4. Design — Lua Proxy (ctx.shared)

- [ ] 4.1 Specify `ctx.shared` metatable — `__index` calls `GameState::get`, `__newindex` calls `GameState::set`
- [ ] 4.2 Specify type inference from Lua values: Lua number (integer? → int32_t, else float), string → string, boolean → bool, Vec3 userdata → Vec3
- [ ] 4.3 Specify `ctx.shared:on_change(key, fn)` — registers Lua callback, returns subscription id
- [ ] 4.4 Specify `ctx.shared:unsubscribe(id)` — removes callback
- [ ] 4.5 Specify `ctx.shared:has(key) -> bool`
- [ ] 4.6 Specify `ctx.shared:remove(key)`
- [ ] 4.7 Specify `ctx.shared:keys() -> table` — returns array of all keys
- [ ] 4.8 Specify error reporting: type mismatch on get (e.g., float stored, int requested) → warning logged, value cast attempted

## 5. Design — Serialization Hooks

- [ ] 5.1 Specify `GameState::serialize(msgpack::packer&)` — encodes full key-value store as msgpack map
- [ ] 5.2 Specify `GameState::deserialize(msgpack::object)` — restores from save slot, merges with defaults
- [ ] 5.3 Specify integration point: `uz_game_save_state` calls `GameState::serialize`; `uz_game_load_state` calls `GameState::deserialize`
- [ ] 5.4 Specify `[persistent]` key annotation: only keys with `ctx.shared:set_persistent(key, true)` are included in save
- [ ] 5.5 Specify schema versioning: `GameState` save block includes `version` field for migration

## 6. Design — Editor IPC Integration

- [ ] 6.1 Specify `scene.get_shared_state` IPC handler — returns full key-value dump as JSON
- [ ] 6.2 Specify `scene.set_shared_state` IPC handler — allows editor to inject values at runtime
- [ ] 6.3 Specify `scene.shared_state_changed` IPC event — pushed to editor on every change (throttled to 10 Hz)
- [ ] 6.4 Specify Inspector panel integration: "Shared State" collapsible section showing all entries

## 7. Write Spec

- [ ] 7.1 Create `docs/specs/shared-state.md` — full spec with all sections from steps 2–6
- [ ] 7.2 Add Lua usage examples: health/score/flag patterns
- [ ] 7.3 Add serialization format diagram: msgpack structure for `GameState` save block
- [ ] 7.4 Update `docs/specs/save-system.md` — add `GameState` as a named save block
- [ ] 7.5 Update `docs/specs/event-bus.md` — add `GameStateChangedEvent` to built-in events table
- [ ] 7.6 Update `docs/README.md` — add `shared-state.md` to specs table

## 8. Review

- [ ] 8.1 Verify `ctx.shared` proxy is safe under Lua hot-reload (proxy re-bound on `on_start`)
- [ ] 8.2 Verify change events do not fire during `deserialize` (bulk load should not trigger game logic)
- [ ] 8.3 Confirm design is compatible with multi-scene streaming (shared state is global, not per-chunk)
- [ ] 8.4 Verify key namespace collision strategy (e.g., `"player.health"` vs `"health"`) is documented
