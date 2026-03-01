## 1. Implementation

### 1a. C++ EventBus Core

- [ ] 1.1 Create `src/engine/event_bus.h` — define `SubId` (uint32_t), `EventBus<T>` template class with static `subscribers_`, `next_id_`, `dispatching_` members
- [ ] 1.2 Implement `EventBus<T>::emit(const T&)` — iterate subscribers, call handlers; guard re-entrancy with `UZ_ASSERT(!dispatching_)`
- [ ] 1.3 Implement `EventBus<T>::subscribe(std::function<void(const T&)>)` → `SubId`
- [ ] 1.4 Implement `EventBus<T>::unsubscribe(SubId)` — erase from subscribers vector; no-op on unknown id
- [ ] 1.5 Implement `AutoSub<T>` RAII wrapper — stores `SubId`, calls `EventBus<T>::unsubscribe` in destructor; move-only, non-copyable
- [ ] 1.6 Define predefined engine event structs in `event_bus.h`: `SceneLoaded{StringId scene_id}`, `SceneUnloaded{StringId scene_id}`, `NodeAdded{NodeHandle node, NodeHandle parent}`, `NodeRemoved{NodeHandle node}`
- [ ] 1.7 Create `src/engine/event_bus.cpp` — explicit template instantiations for all predefined event types to avoid ODR issues
- [ ] 1.8 Emit `SceneLoaded` from `SceneManager::on_scene_ready()` (after all nodes initialized)
- [ ] 1.9 Emit `SceneUnloaded` from `SceneManager::begin_unload()` (before teardown)
- [ ] 1.10 Emit `NodeAdded` from `SceneManager::apply_add_node_command()`
- [ ] 1.11 Emit `NodeRemoved` from `SceneManager::apply_remove_node_command()`

### 1b. Lua EventBus Bridge

- [ ] 1.12 Add `lua_subscribers_` (`std::unordered_map<std::string, std::vector<LuaSubEntry>>`) to `LuaVM` in `src/game/lua_vm.h`
- [ ] 1.13 Add `LuaSubEntry` struct: `uint32_t sub_id`, `int callback_ref` (LUA_REGISTRYINDEX)
- [ ] 1.14 Implement `LuaVM::lua_emit(const std::string& event, lua_State* payload_pusher)` — iterate subscribers for event, call each via `lua_pcall`; log errors and remove failing handler
- [ ] 1.15 Implement `LuaVM::lua_subscribe(const std::string& event, int callback_ref)` → `uint32_t sub_id`
- [ ] 1.16 Implement `LuaVM::lua_unsubscribe(uint32_t sub_id)` — scan all event entries, remove matching sub_id, unref callback
- [ ] 1.17 Implement `LuaVM::clear_all_subscriptions()` — unref all callback_refs, clear map; called from `uz_game_stop`
- [ ] 1.18 Register `ctx.events` userdata in `lua_binding.cpp` — metatable with `emit`, `subscribe`, `unsubscribe` methods
- [ ] 1.19 Bridge predefined engine C++ events to Lua: subscribe `EventBus<SceneLoaded>` etc. in `lua_binding.cpp` to forward events to `LuaVM::lua_emit`

### 1c. IPC Handler

- [ ] 1.20 Add `events.list_subscribers` handler to `src/runtime/ipc/handlers/scene_handlers.cpp` (inside `#ifdef UZ_EDITOR`) — returns `[{event_name, sub_id, type: "lua"|"cpp"}]`

## 2. Testing

- [ ] 2.1 Unit test: `EventBus<T>::subscribe` + `emit` — verify handler called once per emit with correct event data
- [ ] 2.2 Unit test: Multiple subscribers — emit one event, verify all handlers are called in subscription order
- [ ] 2.3 Unit test: `unsubscribe` — verify handler is not called after unsubscription
- [ ] 2.4 Unit test: `unsubscribe` with unknown SubId — verify no-op, no crash
- [ ] 2.5 Unit test: `AutoSub<T>` destructor — verify subscription is removed when AutoSub goes out of scope
- [ ] 2.6 Unit test: Re-entrant `emit` — verify debug assert fires when a handler calls `emit<SameType>`
- [ ] 2.7 Unit test: `SceneLoaded` emitted after all nodes initialized — subscribe before scene load, verify callback receives correct `scene_id`
- [ ] 2.8 Unit test: `NodeAdded` / `NodeRemoved` — enqueue SceneCommand, verify events fire after command is applied
- [ ] 2.9 Lua unit test: `ctx.events:subscribe("PlayerDied", cb)` + `ctx.events:emit("PlayerDied", {x=1})` — verify `cb` called with correct data table
- [ ] 2.10 Lua unit test: `ctx.events:unsubscribe(sub_id)` — verify cb not called after unsubscription
- [ ] 2.11 Lua unit test: Lua handler throws error — verify error logged, handler removed, other handlers still fire
- [ ] 2.12 Lua unit test: `clear_all_subscriptions()` — verify no callbacks fire after clear, no LUA_REGISTRYINDEX ref leak
- [ ] 2.13 Integration test: C++ system emits `PlayerDied` (custom event); two Lua scripts subscribed — verify both handlers fire with correct payload in same frame
- [ ] 2.14 Integration test: Lua script hot-reload — `on_stop` unsubscribes, `on_start` re-subscribes; verify no double-subscription after reload
- [ ] 2.15 Integration test: Scene change clears all Lua subscriptions — subscribe in one scene, load new scene, verify old handlers do not fire

## 3. Documentation

- [x] 3.1 `docs/specs/event-bus.md` — full spec (written in planning phase)
- [ ] 3.2 `docs/specs/lua-scripting.md` — add `ctx.events` section to Engine API table: `ctx.events:emit`, `ctx.events:subscribe`, `ctx.events:unsubscribe`
- [ ] 3.3 `docs/specs/scene-system.md` — add predefined engine events table (`SceneLoaded`, `SceneUnloaded`, `NodeAdded`, `NodeRemoved`) to SceneManager section
- [ ] 3.4 `docs/README.md` — `phase3-event-bus` row in Rulebook Tasks table (done in planning phase)
- [ ] 3.5 Code comments in `event_bus.h` — document thread-safety invariant (Game Logic thread only) and re-entrancy restriction
- [ ] 3.6 Code comments in `lua_vm.h` — document auto-cleanup of Lua subscriptions on node/scene teardown
