## 1. Prerequisites

- [x] 1.1 Confirm `phase0-shared-state-design` spec is complete — read `docs/specs/shared-state.md`
- [x] 1.2 Confirm `phase1-lua-scripting` is complete — `ctx` metatable and LuaVM exist
- [x] 1.3 Read `docs/specs/save-system.md` — understand `uz_game_save_state` hook and msgpack format

## 2. GameState C++ Implementation

- [x] 2.1 Create `src/game/game_state.h` — `GameState` class declaration with `GameStateValue` variant type
- [x] 2.2 Create `src/game/game_state.cpp` — implementation
- [x] 2.3 Implement `GameState::get<bool>(key) -> bool` — returns false if absent
- [x] 2.4 Implement `GameState::get<int32_t>(key) -> int32_t` — returns 0 if absent
- [x] 2.5 Implement `GameState::get<float>(key) -> float` — returns 0.0f if absent
- [x] 2.6 Implement `GameState::get<std::string>(key) -> std::string` — returns "" if absent
- [x] 2.7 Implement `GameState::get<Vec3>(key) -> Vec3` — returns Vec3::zero if absent
- [x] 2.8 Implement `GameState::set<T>(key, value)` — stores value; emits `GameStateChangedEvent` if value differs
- [x] 2.9 Implement `GameState::has(key) -> bool`
- [x] 2.10 Implement `GameState::remove(key)` — erases entry; emits `GameStateRemovedEvent`
- [x] 2.11 Implement `GameState::clear()` — erases all entries; does NOT emit per-key events
- [x] 2.12 Implement `GameState::keys() -> std::vector<std::string>` — returns sorted key list
- [x] 2.13 Implement `GameState::set_persistent(key, bool)` — marks key for save/load inclusion
- [x] 2.14 Add debug assert: `GameState` methods assert called on main thread

## 3. Change Event Integration

- [x] 3.1 Define `GameStateChangedEvent` struct in `src/game/game_state.h`: `key`, `GameStateValue old_value`, `GameStateValue new_value`
- [x] 3.2 Define `GameStateRemovedEvent` struct: `key`, `GameStateValue last_value`
- [x] 3.3 Integrate with `EventBus<GameStateChangedEvent>` — `GameState::set` posts via `EventBus::post()` at frame boundary
- [x] 3.4 Implement `GameState::on_change(key, fn) -> SubscriptionId` — EventBus subscription filtered by key
- [x] 3.5 Implement `GameState::unsubscribe(SubscriptionId)` — removes subscription
- [x] 3.6 Implement deferred dispatch: collect changed keys during `set()`, dispatch events at end of `uz_game_update`

## 4. Lua Proxy — ctx.shared

- [x] 4.1 Add `ctx.shared` table to `ctx` metatable setup in `lua_binding.cpp`
- [x] 4.2 Implement `__index` metamethod: `ctx.shared.health` → `game_state.get(key)` with type auto-detection
- [x] 4.3 Implement `__newindex` metamethod: `ctx.shared.health = 100` → `game_state.set(key, value)` with Lua type inference
- [x] 4.4 Implement `ctx.shared:on_change(key, fn) -> id` — C function binding to `GameState::on_change`
- [x] 4.5 Implement `ctx.shared:unsubscribe(id)` — C function binding
- [x] 4.6 Implement `ctx.shared:has(key) -> bool` — C function binding
- [x] 4.7 Implement `ctx.shared:remove(key)` — C function binding
- [x] 4.8 Implement `ctx.shared:keys() -> table` — C function binding, returns Lua array
- [x] 4.9 Implement `ctx.shared:set_persistent(key, bool)` — C function binding
- [x] 4.10 Lua type inference in `__newindex`: Lua integer → int32_t, Lua number (float) → float, bool → bool, string → string, Vec3 userdata → Vec3
- [x] 4.11 Log type mismatch warning: `[SharedState] key 'health': stored as float, requested as int — casting`

## 5. Serialization Integration

- [x] 5.1 Implement `GameState::serialize(msgpack::packer&)` — encode persistent keys only as msgpack map
- [x] 5.2 Implement `GameState::deserialize(msgpack::object)` — restore from save, merge with defaults
- [x] 5.3 Update `uz_game_save_state` — call `game_state.serialize()` into save block named `"shared_state"`
- [x] 5.4 Update `uz_game_load_state` — call `game_state.deserialize()` from `"shared_state"` block
- [x] 5.5 Ensure `deserialize` does NOT emit change events (bulk restore should not trigger game logic)
- [x] 5.6 Add schema version field: `{ "version": 1, "entries": {...} }` in msgpack block

## 6. IPC Handler Integration

- [x] 6.1 Create `src/runtime/ipc/handlers/state_handlers.cpp`
- [x] 6.2 Implement `scene.get_shared_state` handler — returns full key-value JSON dump
- [x] 6.3 Implement `scene.set_shared_state` handler — injects values at runtime (editor use)
- [x] 6.4 Implement `scene.shared_state_changed` event push — throttled to 10 Hz, pushes changed keys
- [x] 6.5 Register handlers under `#ifdef UZ_EDITOR` in `IpcRegistry::setup()`

## 7. Testing

- [x] 7.1 Unit test: `set<int32_t>("score", 100)` → `get<int32_t>("score") == 100`
- [x] 7.2 Unit test: `get<float>("missing")` returns 0.0f without error
- [x] 7.3 Unit test: `set` with same value does NOT emit `GameStateChangedEvent`
- [x] 7.4 Unit test: `remove("score")` → `has("score")` returns false
- [x] 7.5 Unit test: `clear()` empties store, does not emit per-key events
- [x] 7.6 Unit test: `serialize` → `deserialize` round-trip preserves all persistent key-value pairs
- [x] 7.7 Unit test: `deserialize` does not emit change events
- [x] 7.8 Integration test: Script A sets `ctx.shared.score = 42`; Script B reads `ctx.shared.score` → 42
- [x] 7.9 Integration test: `ctx.shared:on_change("health", fn)` callback fires when health changes
- [x] 7.10 Integration test: save game, load game — `ctx.shared.persistent_flag` preserved

## 8. Documentation

- [x] 8.1 Update `docs/specs/shared-state.md` — mark implementation complete, add code examples
- [x] 8.2 Update `docs/guides/lua-scripting-guide.md` — "Shared State" section with common patterns
- [x] 8.3 Update `docs/modules/game/README.md` — document `GameState` class
