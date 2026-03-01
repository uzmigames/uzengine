# Spec: EventBus / Signals

> **Status**: Planned (Phase 3)
> **Owner**: `uz_engine.lib` (C++ typed `EventBus<T>`) + `game.dll` (Lua string-dispatch bridge)
> **Thread safety**: Game Logic thread only; all emit/subscribe calls must be on this thread

---

## Overview

The EventBus provides decoupled communication between game systems — a publisher emits an event without knowing who is listening; subscribers handle it without knowing who published.

In C++ this is implemented as a typed templated bus (`EventBus<T>`) with zero-overhead dispatch. In Lua it uses string-keyed dispatch (acceptable overhead for scripting). The two layers interoperate: a C++ system can emit `PlayerDied{}` and a Lua script can subscribe to `"PlayerDied"`.

This is equivalent to:
- **Godot Signals** — but C++ typed (not string dispatch at the C++ level)
- **Unity Events / C# delegates** — but without reflection
- **Unreal `DECLARE_DYNAMIC_MULTICAST_DELEGATE`** — but simpler (no UObject required)

---

## C++ API

### Including the EventBus

```cpp
#include "engine/event_bus.h"
```

`EventBus` is a global singleton per event type, stored in the Engine layer (`uz_engine.lib`). Accessing it is zero-cost (compile-time specialization, no virtual dispatch).

### Defining an Event

Events are plain C++ structs — no base class, no macro, no registration:

```cpp
// src/engine/events.h — predefined engine events
struct SceneLoaded   { StringId scene_id; };
struct SceneUnloaded { StringId scene_id; };
struct NodeAdded     { NodeHandle node; NodeHandle parent; };
struct NodeRemoved   { NodeHandle node; };

// game.dll — custom game events (defined anywhere)
struct PlayerDied   { NodeHandle player; Vec3 death_position; };
struct EnemySpawned { NodeHandle enemy; uint32_t wave; };
struct ItemPickedUp { NodeHandle item; NodeHandle player; StringId item_type; };
```

### `EventBus<T>::emit(args...)`

Fire an event synchronously. All subscribers for `T` are called in subscription order before `emit` returns.

```cpp
// Emit from any system on the Game Logic thread
EventBus<PlayerDied>::emit(PlayerDied{ .player = player_handle, .death_position = pos });
```

- Synchronous: all handlers run before `emit` returns
- Handlers run on the same thread as the caller (Game Logic thread)
- Handlers must not call `emit<SameType>` recursively (asserted in debug builds)

### `EventBus<T>::subscribe(handler)` → `SubId`

Register a callback to receive events of type `T`. Returns a `SubId` that must be used to unsubscribe.

```cpp
// In a system or component:
SubId id = EventBus<PlayerDied>::subscribe([this](const PlayerDied& e) {
    respawn_player(e.player, e.death_position);
});
```

- `handler`: `std::function<void(const T&)>` or compatible callable
- Returns: `SubId` — an opaque 32-bit integer, unique per EventBus specialization
- Subscribing from a destructor that is called during an event dispatch is not allowed (asserted)

### `EventBus<T>::unsubscribe(SubId)`

Remove a previously registered handler. Safe to call with an already-removed `SubId` (no-op).

```cpp
EventBus<PlayerDied>::unsubscribe(id);
```

### RAII Helper: `AutoSub<T>`

Automatically unsubscribes when it goes out of scope. Preferred for system-lifetime subscriptions:

```cpp
class RespawnSystem {
    AutoSub<PlayerDied> sub_;
public:
    RespawnSystem() {
        sub_ = EventBus<PlayerDied>::subscribe([this](const PlayerDied& e) {
            handle_player_death(e);
        });
    }
    // ~RespawnSystem() automatically calls unsubscribe via AutoSub destructor
};
```

---

## Predefined Engine Events

These events are emitted by the engine core (`uz_engine.lib`) and can be subscribed to from `game.dll`:

| Event Struct | Emitted By | When |
|---|---|---|
| `SceneLoaded` | `SceneManager` | After all nodes are initialized, before first `on_update` |
| `SceneUnloaded` | `SceneManager` | Before scene teardown begins |
| `NodeAdded` | `SceneManager` | After `SceneCommand::ADD_NODE` is applied |
| `NodeRemoved` | `SceneManager` | Before node is destroyed |

```cpp
// Example: game.dll subscribes to SceneLoaded to run post-load setup
sub_ = EventBus<SceneLoaded>::subscribe([this](const SceneLoaded& e) {
    ctx_->log_info("Scene loaded: " + e.scene_id.str());
    initialize_game_state();
});
```

Custom game events (defined in `game.dll`) are not visible to `uz_engine.lib`. The engine does not need to know about game-specific events.

---

## Lua API

Lua uses string-keyed dispatch. The `ctx.events` object is injected into the VM by `lua_binding.cpp`:

### `ctx.events:emit("EventName", data)`

Emit a named event with an arbitrary Lua table as payload.

```lua
-- Emit a custom game event from a Lua script
ctx.events:emit("PlayerDied", {
    node_id  = self.id,
    position = self.position,
})
```

- `"EventName"`: string event name (case-sensitive)
- `data`: any Lua value — typically a table; passed by value (shallow copy) to all handlers
- Runs all registered Lua handlers synchronously before returning

### `ctx.events:subscribe("EventName", callback)` → `sub_id`

Register a Lua function to receive the named event. Returns a `sub_id` integer for later unsubscription.

```lua
function M:on_start(ctx)
    self.death_sub = ctx.events:subscribe("PlayerDied", function(data)
        ctx:log("Player died at " .. tostring(data.position))
        self:on_player_died(ctx, data)
    end)
end
```

- `callback`: Lua function, called with the `data` table as its only argument
- Returns: `sub_id` (integer)

### `ctx.events:unsubscribe(sub_id)`

Remove a previously registered Lua handler.

```lua
function M:on_stop(ctx)
    ctx.events:unsubscribe(self.death_sub)
    self.death_sub = nil
end
```

Safe to call with an already-removed `sub_id` (no-op).

---

## C++ ↔ Lua Interop

C++ events can be bridged to Lua via a bridge subscription registered in `lua_binding.cpp`:

```cpp
// In lua_binding.cpp — bridge engine events to Lua string dispatch
EventBus<SceneLoaded>::subscribe([L](const SceneLoaded& e) {
    lua_event_dispatch(L, "SceneLoaded", [&](lua_State* L) {
        lua_newtable(L);
        lua_pushstring(L, e.scene_id.str());
        lua_setfield(L, -2, "scene_id");
    });
});
```

All four predefined engine events (`SceneLoaded`, `SceneUnloaded`, `NodeAdded`, `NodeRemoved`) are bridged automatically. Custom C++ events from `game.dll` can be bridged manually using the same pattern.

Lua scripts cannot emit C++ typed events directly — they can only emit string-keyed events into the Lua dispatch layer. If a Lua event needs to cross into C++, `game.dll` must bridge it explicitly.

---

## Implementation Details

### C++ EventBus Internals

```cpp
// src/engine/event_bus.h
template<typename T>
class EventBus {
    struct Entry {
        SubId                   id;
        std::function<void(const T&)> handler;
    };

    static std::vector<Entry> subscribers_;
    static uint32_t           next_id_;
    static bool               dispatching_;  // re-entrancy guard

public:
    static void emit(const T& event) {
        UZ_ASSERT(!dispatching_, "Recursive emit<T> not allowed");
        dispatching_ = true;
        for (auto& e : subscribers_) {
            e.handler(event);
        }
        dispatching_ = false;
    }

    static SubId subscribe(std::function<void(const T&)> handler) {
        SubId id = next_id_++;
        subscribers_.push_back({ id, std::move(handler) });
        return id;
    }

    static void unsubscribe(SubId id) {
        auto it = std::find_if(subscribers_.begin(), subscribers_.end(),
                               [id](const Entry& e) { return e.id == id; });
        if (it != subscribers_.end()) subscribers_.erase(it);
    }
};
```

### Lua EventBus Internals

The Lua dispatch table is a `std::unordered_map<std::string, std::vector<LuaSubEntry>>` stored in `LuaVM`. Each `LuaSubEntry` holds a `lua_ref` (LUA_REGISTRYINDEX) to the callback function.

```cpp
struct LuaSubEntry {
    uint32_t sub_id;
    int      callback_ref;  // LUA_REGISTRYINDEX
};

class LuaVM {
    std::unordered_map<std::string, std::vector<LuaSubEntry>> lua_subscribers_;
    uint32_t next_lua_sub_id_ = 1;
};
```

Dispatching iterates the vector for the event name and calls each callback via `lua_pcall`. Errors are logged and the failing handler is removed (same policy as timer errors).

---

## Thread Safety

All EventBus operations must occur on the **Game Logic thread**:

- `emit<T>` — Game Logic thread only
- `subscribe<T>` — Game Logic thread only (typically in system constructors or `on_start`)
- `unsubscribe(SubId)` — Game Logic thread only (typically in system destructors or `on_stop`)

Emitting from the Render thread or Main thread is not allowed and will trigger an assertion in debug builds. If a render or physics callback needs to signal game logic, use the `SceneCommand` queue (thread-safe MPSC queue from non-game threads to game logic).

---

## Lifetime and Auto-Cleanup

### Lua Script Unload / Hot-Reload

When a Lua script unloads (hot-reload or scene change), all `sub_id`s registered by that script's instances must be unsubscribed. Scripts are responsible for calling `ctx.events:unsubscribe(sub_id)` in `M:on_stop(ctx)`.

The LuaVM tracks which `sub_id`s were registered per node instance and force-unsubscribes them on node teardown as a safety net — but explicit `on_stop` cleanup is the correct pattern.

### Scene Change

All Lua subscriptions are cleared on scene unload (`LuaVM::clear_all_subscriptions()`). C++ subscriptions via `AutoSub<T>` are cleared when their owner systems are destroyed.

---

## Error Handling

| Error | Behavior |
|-------|----------|
| Lua handler throws | Logged with file+line; handler removed from subscriber list |
| C++ handler throws | `std::terminate` (C++ exception policy; handlers must not throw) |
| `emit` during `emit` (re-entrancy) | Debug assert; undefined behavior in release |
| `unsubscribe` with unknown id | No-op |
| `subscribe` during active dispatch | Deferred to after current dispatch completes |

C++ handlers must be `noexcept`. The C++ EventBus does not wrap handlers in try/catch.

---

## IPC Handler

One IPC command is provided for editor/AI inspection:

| Command | Args | Returns | Purpose |
|---------|------|---------|---------|
| `events.list_subscribers` | `{event: string}` | `[{sub_id, type: "lua"|"cpp"}]` | Inspect who is subscribed (editor only) |

Available only in `--editor` mode (`UZ_EDITOR=1`). Stripped in shipping.

---

## Comparison with Other Engines

| Feature | Godot Signals | Unity Events | UzEngine EventBus |
|---------|---------------|--------------|-------------------|
| C++ dispatch | String-based | Reflection-based | Typed template, zero overhead |
| Lua/Script dispatch | Built-in signals | — | String-based (acceptable for scripting) |
| Cross-system comm | Via `emit_signal` | Via UnityEvent.Invoke | `EventBus<T>::emit` |
| Lifetime management | Automatic on free | Manual or `AddListener`/`RemoveListener` | `AutoSub<T>` RAII or manual |
| Thread safety | Main thread | Main thread | Game Logic thread |

---

## C++ Source Files

```
src/engine/
├── event_bus.h             — EventBus<T>, AutoSub<T>, SubId, predefined engine events
├── event_bus.cpp           — Template instantiations for predefined events
src/engine/scripting/
├── lua_binding.h/cpp       — registers ctx.events userdata: emit, subscribe, unsubscribe
│                           — bridges SceneLoaded/Unloaded/NodeAdded/NodeRemoved to Lua
src/game/
├── lua_vm.h/cpp            — LuaVM: lua_subscribers_ map, dispatch, sub lifetime tracking
```

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 3** | C++ `EventBus<T>`, `AutoSub<T>`, predefined engine events, Lua string-dispatch, C++↔Lua bridges, `events.list_subscribers` IPC |
| **Phase 4** | EventBus subscriptions serialized across play/stop (for editor round-trip); network event replication stub |

---

## Related Docs

- [specs/lua-scripting.md](lua-scripting.md) — LuaJIT VM, `ctx` API, `on_stop` cleanup pattern
- [specs/scene-system.md](scene-system.md) — `SceneLoaded`, `SceneUnloaded` events, SceneCommand queue
- [specs/timer-system.md](timer-system.md) — Timer + EventBus integration (timers can emit events)
- [specs/tags-groups.md](tags-groups.md) — Tags: `NodeAdded`/`NodeRemoved` events carry tag info
