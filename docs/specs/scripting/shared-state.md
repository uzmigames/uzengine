# Spec: Shared State (Global Game State Store)

> **Status**: Planned (Phase 1)
> **Owner**: `game.dll` (C++ `GameState` class + Lua `ctx.shared` proxy)
> **Thread safety**: Game Logic thread only; main-thread-only access enforced in debug builds
> **Serialization**: Integrated with `phase3-save-system` via msgpack hooks

---

## Overview

The **Shared State** system provides a centralized, typed key-value store for global game state — player health, score, quest flags, game mode settings, and any other data that must be accessible to multiple scripts without direct node-to-node coupling.

Without shared state, developers resort to:
```lua
-- ❌ Fragile: depends on "GameManager" node existing with get() method
local health = ctx.scene:find_node("GameManager"):get("health")
```

With shared state:
```lua
-- ✅ Clean: decoupled from node structure
local health = ctx.shared:get("health")
-- or simply:
local health = ctx.shared.health
```

### Design Goals

1. **Zero-Coupling**: Game logic does not depend on scene structure
2. **Type-Safe (C++)**: `get<int32_t>(key)`, `get<float>(key)`, no casting errors
3. **Type-Flexible (Lua)**: Type inference from Lua values (int vs float distinction preserved)
4. **Event-Driven**: Change notifications via EventBus for UI binding, audio snapshots, game logic
5. **Saveable**: Integrates cleanly with save/load system for game persistence
6. **Editor-Friendly**: IPC handlers allow editor inspection and runtime modification

### Compared to Other Engines

| Feature | Shared State | Godot Singleton | Unity Static | Unreal GameState |
|---------|--------------|-----------------|--------------|------------------|
| Decoupled | Yes | Via Signals | No (direct static refs) | No (PlayerController coupling) |
| Typed | Yes (C++ variant) | Mixed (GDScript typing) | Via Reflection | Via Blueprint/C++ |
| Change Events | Yes (EventBus) | Via Signals | Manual | Manual via delegates |
| Save/Load | Built-in | Manual | Manual | Manual |
| Thread-safe | Single-threaded by design | Via Mutex | Manual | Via Networking |

---

## C++ API

### Core Data Types

#### `GameStateValue`

A discriminated union (variant) holding any supported game state type:

```cpp
using GameStateValue = std::variant<bool, int32_t, float, std::string, Vec3>;
```

Supported types:
- **bool** — game mode flags, quest completion, feature toggles
- **int32_t** — counters, player level, inventory counts
- **float** — health points, stamina, ammo percentage
- **std::string** — current dialogue line, item description, mode name
- **Vec3** — last safe position, spawn point, checkpoint location

#### `GameState` Class

The main API for storing and retrieving global state. Single instance lives on the `LuaVM`:

```cpp
class GameState {
public:
    // Get value with optional type safety
    template<typename T>
    T get(std::string_view key, T default_value = T{}) const;

    // Set value and fire change event if different
    template<typename T>
    void set(std::string_view key, const T& value);

    // Test membership without constructing default
    bool has(std::string_view key) const;

    // Remove entry and fire remove event
    void remove(std::string_view key);

    // Clear all entries (called on scene unload)
    void clear();

    // Enumerate all keys for iteration, serialization
    std::vector<std::string> keys() const;

    // Get all keys and values as a map (editor inspection)
    std::unordered_map<std::string, GameStateValue> get_all() const;

    // Serialization hooks (internal, called by save/load system)
    void serialize(msgpack::packer<msgpack::sbuffer>& pk) const;
    void deserialize(const msgpack::object& obj);

private:
    std::unordered_map<std::string, GameStateValue> store_;
    uz::EventBus& event_bus_;
    std::vector<GameStateChangedEvent> deferred_events_;
};
```

### Change Events

#### `GameStateChangedEvent<T>`

Fired when a value changes (if new value differs from old):

```cpp
template<typename T>
struct GameStateChangedEvent {
    std::string key;
    T old_value;
    T new_value;
    // Fired via EventBus<GameStateChangedEvent<T>>::emit()
};
```

Example types:
```cpp
EventBus<GameStateChangedEvent<int32_t>>::subscribe([](const auto& e) {
    if (e.key == "health") {
        show_health_bar(e.new_value);
    }
});
```

#### `GameStateRemovedEvent`

Fired when a key is deleted:

```cpp
struct GameStateRemovedEvent {
    std::string key;
    GameStateValue last_value;
};

EventBus<GameStateRemovedEvent>::subscribe([](const auto& e) {
    if (e.key == "active_ability") {
        deactivate_ability();
    }
});
```

### Deferred Event Dispatch

Change events are **not fired immediately** during `set()`. Instead, they are queued and dispatched at the frame boundary (end of `uz_game_update()`).

**Why?**
- Avoids cascading change events during initialization (e.g., loading save file calls many `set()` calls)
- Allows UI and audio systems to batch updates efficiently
- Prevents recursive event handling if a handler calls `set()`

**Implementation:**

```cpp
void GameState::set(std::string_view key, const T& value) {
    auto it = store_.find(std::string(key));
    T old_value = (it != store_.end()) ? std::get<T>(it->second) : T{};

    if (value == old_value && it != store_.end()) {
        return;  // No change, no event
    }

    store_[std::string(key)] = value;

    // Queue event, dispatch later
    deferred_events_.push_back(GameStateChangedEvent<T>{
        .key = std::string(key),
        .old_value = old_value,
        .new_value = value,
    });
}

// Called at frame boundary by game loop
void GameState::dispatch_deferred_events() {
    for (const auto& event : deferred_events_) {
        event_bus_.emit(event);
    }
    deferred_events_.clear();
}
```

### Thread Safety

**GameState is main-thread-only.** All access must occur on the Game Logic thread.

```cpp
// game.cpp
void uz_game_update(...) {
    // OK — we are on Game Logic thread
    ctx_->shared_state()->set("frame_count", frame_count);

    // OK — Lua scripts run on Game Logic thread
    lua_vm_->update();
}

// xaudio2_voice.cpp (Audio thread)
void voice_update() {
    // ❌ ASSERT in debug builds
    // ctx_->shared_state()->get("music_volume");
}
```

Thread safety is enforced via assertions in debug builds:

```cpp
template<typename T>
T GameState::get(std::string_view key, T default_value) const {
    UZ_ASSERT_MAIN_THREAD("GameState::get called from wrong thread");
    // ...
}
```

---

## Lua API

Lua access is via the `ctx.shared` proxy object, which bridges to `GameState::get/set`.

### `ctx.shared[key]` / `ctx.shared.key` (Get)

Retrieve a value with Lua metatable `__index`:

```lua
local health = ctx.shared.health  -- implicit get()
local health = ctx.shared["health"]  -- explicit

-- If key not found, returns nil (not a default value)
local unknown = ctx.shared.nonexistent  -- nil
```

### `ctx.shared[key] = value` / `ctx.shared.key = value` (Set)

Store a value with metatable `__newindex`:

```lua
ctx.shared.health = 100
ctx.shared["score"] = ctx.shared.score + 10
ctx.shared.spawn_point = Vec3:new(0, 1, 0)  -- Vec3 userdata
```

**Type inference from Lua:**

| Lua Type | Inferred C++ Type | Notes |
|----------|-------------------|-------|
| `true` / `false` | `bool` | Exact match |
| Integer `42` | `int32_t` | Lua detects integer vs float |
| Float `3.14` | `float` | Lua detects integer vs float |
| String `"hello"` | `std::string` | UTF-8 encoded |
| `Vec3` userdata | `Vec3` | Type checked via metatable |

```lua
ctx.shared.int_val = 42      -- int32_t
ctx.shared.float_val = 3.14  -- float
ctx.shared.bool_val = true   -- bool
ctx.shared.str_val = "quest_001"  -- std::string
ctx.shared.pos = Vec3:new(10, 0, 5)  -- Vec3
```

### `ctx.shared:get(key, default)` (Explicit Get)

Get with explicit default:

```lua
local health = ctx.shared:get("health", 100)  -- returns 100 if not found
```

### `ctx.shared:has(key)` (Test Membership)

Test if a key exists without constructing a default:

```lua
if ctx.shared:has("tutorial_complete") then
    skip_tutorial()
end
```

### `ctx.shared:remove(key)`

Delete a key and fire `GameStateRemovedEvent`:

```lua
function M:on_stop(ctx)
    -- Clean up per-node state on unload
    ctx.shared:remove("active_dialogue_id")
end
```

### `ctx.shared:keys()` (Enumerate)

Get an array of all keys:

```lua
for _, key in ipairs(ctx.shared:keys()) do
    print(key)
end
```

### `ctx.shared:on_change(key, callback)` → `sub_id`

Subscribe to changes for a specific key. Returns a subscription ID for later unsubscription.

```lua
function M:on_start(ctx)
    -- Update UI when health changes
    self.health_sub = ctx.shared:on_change("health", function(old_val, new_val)
        self:update_health_bar(new_val)
    end)
end

function M:on_stop(ctx)
    ctx.shared:unsubscribe(self.health_sub)
end
```

**Under the hood:** `ctx.shared:on_change()` subscribes to `EventBus<GameStateChangedEvent<T>>` filtered by key. Type is inferred from the stored value.

### `ctx.shared:unsubscribe(sub_id)`

Unsubscribe from a change notification:

```lua
ctx.shared:unsubscribe(self.health_sub)
```

### `ctx.shared:set_persistent(key, is_persistent)`

Mark a key as persistent (included in save). By default, all keys are persistent.

```lua
ctx.shared:set_persistent("debug_god_mode", false)  -- not saved
ctx.shared:set_persistent("player_level", true)     -- saved (default)
```

---

## Usage Examples

### Pattern: Player Health System

```lua
-- player.lua
local M = {}

function M:on_start(ctx)
    ctx.shared:set("health", 100)
    ctx.shared:set("max_health", 100)

    -- Subscribe to health changes (e.g., show UI feedback)
    self.health_sub = ctx.shared:on_change("health", function(old, new)
        if new <= 0 then
            self:die(ctx)
        elseif new < old then
            self:play_hurt_animation()
        end
    end)
end

function M:take_damage(ctx, amount)
    local health = ctx.shared:get("health", 100)
    ctx.shared:set("health", math.max(0, health - amount))
end

function M:die(ctx)
    ctx.shared:set("player_dead", true)
    self:emit_on_event(ctx)
end

function M:on_stop(ctx)
    ctx.shared:unsubscribe(self.health_sub)
end

return M
```

### Pattern: Quest Flags

```lua
-- quest_trigger.lua
local M = {}

function M:on_start(ctx)
    self.quest_id = "slay_dragon"
end

function M:on_trigger_enter(ctx, other_node)
    if ctx.shared:has("quest_" .. self.quest_id .. "_completed") then
        return  -- Already done
    end

    -- Mark quest as complete, fire change event
    ctx.shared:set("quest_" .. self.quest_id .. "_completed", true)
    ctx.shared:set("quest_log_" .. self.quest_id, "Slayed the dragon!")

    -- Score reward
    local score = ctx.shared:get("score", 0)
    ctx.shared:set("score", score + 1000)
end

return M
```

### Pattern: Audio Snapshots (System Integration)

```cpp
// In audio-system code:
// When ctx.shared("music_snapshot") changes, switch audio snapshot

uz::EventBus<uz::GameStateChangedEvent<std::string>>::subscribe(
    [this](const auto& e) {
        if (e.key == "music_snapshot") {
            audio_engine_->activate_snapshot(e.new_value);
        }
    });
```

---

## Serialization

### Save Format (msgpack)

When saving, `GameState` is serialized as a single msgpack map block in the save file:

```
Save Slot = [
  { "version": 3 },
  { "node_transforms": [...] },
  { "game_state": {
      "health": 75,
      "score": 2500,
      "player_dead": false,
      "spawn_point": [10.5, 0, 0],
      "active_quest": "slay_dragon"
    }
  },
  { "lua_tables": {...} }
]
```

### Serialization API (C++)

```cpp
// Save: called by save-system on save
void GameState::serialize(msgpack::packer<msgpack::sbuffer>& pk) const {
    pk.pack_map(store_.size());
    for (const auto& [key, value] : store_) {
        pk.pack(key);
        // Pack variant via visitor
        std::visit([&](const auto& v) { pk.pack(v); }, value);
    }
}

// Load: called by save-system on load
void GameState::deserialize(const msgpack::object& obj) {
    clear();  // Reset state

    if (obj.type != msgpack::type::MAP) return;

    for (size_t i = 0; i < obj.via.map.size; ++i) {
        const auto& k = obj.via.map.ptr[i].key;
        const auto& v = obj.via.map.ptr[i].val;

        std::string key = k.as<std::string>();

        // Type-tag stored in value (implicit from msgpack type)
        switch (v.type) {
            case msgpack::type::BOOLEAN: set("_silent", key, v.as<bool>()); break;
            case msgpack::type::POSITIVE_INTEGER: set("_silent", key, v.as<int32_t>()); break;
            case msgpack::type::FLOAT: set("_silent", key, v.as<float>()); break;
            case msgpack::type::STR: set("_silent", key, v.as<std::string>()); break;
            // Vec3 = array [x, y, z]
            case msgpack::type::ARRAY: {
                if (v.via.array.size == 3) {
                    set("_silent", key, Vec3{
                        v.via.array.ptr[0].as<float>(),
                        v.via.array.ptr[1].as<float>(),
                        v.via.array.ptr[2].as<float>(),
                    });
                }
                break;
            }
        }
    }
}
```

### Deferred Dispatch During Deserialization

**Critical:** During `deserialize()`, change events are queued but **not** dispatched. This prevents game logic from running during save-file parsing.

Events are flushed after deserialization completes:

```cpp
// In save-system load path
void load_game_state(const msgpack::object& obj) {
    game_state->deserialize(obj);
    // Events queued but not yet fired

    // Resume the scene, scripts resume on_update()
    scene_manager->unpause();

    // Next frame boundary, dispatch events
    // game_state->dispatch_deferred_events() called at end of uz_game_update()
}
```

### Persistent Flag

Keys marked non-persistent are excluded from saves:

```lua
-- Code
ctx.shared:set_persistent("debug_speed_multiplier", false)
ctx.shared.debug_speed_multiplier = 2.0

-- Save file does NOT contain debug_speed_multiplier
```

Implementation:

```cpp
void GameState::serialize(msgpack::packer<>& pk) const {
    pk.pack_map(persistent_keys_.size());  // Only persistent keys
    for (const auto& key : persistent_keys_) {
        pk.pack(key);
        std::visit([&](const auto& v) { pk.pack(v); }, store_.at(key));
    }
}
```

---

## Editor IPC Integration

Three IPC handlers allow the editor to inspect and modify shared state:

### `scene.get_shared_state` → JSON

Returns full game state as JSON for inspector panel.

**Request:**
```json
{
  "namespace": "scene",
  "method": "get_shared_state"
}
```

**Response:**
```json
{
  "ok": true,
  "data": {
    "health": 75,
    "max_health": 100,
    "player_dead": false,
    "score": 2500,
    "spawn_point": { "x": 10.5, "y": 0, "z": 0 },
    "active_quest": "slay_dragon"
  }
}
```

### `scene.set_shared_state` (POST)

Inject or update a single key at runtime.

**Request:**
```json
{
  "namespace": "scene",
  "method": "set_shared_state",
  "key": "health",
  "value": 100,
  "type": "int"
}
```

Supported types: `"bool"`, `"int"`, `"float"`, `"string"`, `"vec3"` (as `[x, y, z]`).

**Response:**
```json
{
  "ok": true
}
```

### `scene.shared_state_changed` (Event)

Pushed to editor on every state change (throttled to 10 Hz to avoid flooding).

**Payload (sent by engine):**
```json
{
  "type": "scene.shared_state_changed",
  "key": "health",
  "old_value": 100,
  "new_value": 75,
  "value_type": "int"
}
```

**Editor Behavior:**
- Displays in "Shared State" inspector panel
- Refreshes `get_shared_state` query
- Highlights recently-changed keys

---

## Implementation Details

### File Structure

```
src/game/
├── game_state.h
├── game_state.cpp
├── shared_state_events.h        ← GameStateChangedEvent, GameStateRemovedEvent
└── lua_shared_state.h/cpp       ← ctx.shared Lua binding

src/engine/scripting/
└── lua_binding.cpp              ← Register ctx.shared on VM creation
```

### Lua Metatable Registration

```cpp
// lua_shared_state.cpp
int lua_shared_state_index(lua_State* L) {
    GameState* state = (GameState*)lua_touserdata(L, 1);
    const char* key = lua_tostring(L, 2);

    const auto& value = state->get(key);
    // Convert C++ GameStateValue to Lua value via visitor
    return lua_push_game_state_value(L, value);
}

int lua_shared_state_newindex(lua_State* L) {
    GameState* state = (GameState*)lua_touserdata(L, 1);
    const char* key = lua_tostring(L, 2);

    GameStateValue value = lua_to_game_state_value(L, 3);
    state->set(key, value);
    return 0;
}
```

### Error Handling

| Scenario | Behavior |
|----------|----------|
| Get with wrong type (e.g., bool stored, int requested) | Log warning, return default value |
| Set from non-main thread | Assert in debug, log error in release |
| Deserialize with missing/corrupted keys | Skip malformed entries, continue loading |
| Change event handler throws | Lua: caught by pcall, logged; C++: std::terminate |
| Key name collision (e.g., "player.health" vs "player" + ".health") | Document namespacing convention; no validation |

### Key Naming Convention

Recommend dot notation for clarity:

```lua
-- ✅ Good: semantic hierarchy
ctx.shared.player_health
ctx.shared.player_max_health
ctx.shared.player_level
ctx.shared.ui_volume
ctx.shared.music_volume
ctx.shared.sfx_volume
ctx.shared.quest_dragon_completed
ctx.shared.quest_dragon_reward_claimed

-- ❌ Avoid: flat and ambiguous
ctx.shared.health
ctx.shared.volume
ctx.shared.completed
```

---

## Test Coverage

### Unit Tests

- `GameState::get()` / `set()` — type preservation, defaults
- `GameState::has()` / `remove()` / `clear()` — membership and lifecycle
- `GameState::keys()` — enumeration
- Change event queuing and deferred dispatch
- Lua type inference (int vs float, Vec3, string)
- Serialization / deserialization round-trip
- Persistent key filtering
- Thread assertion (compile time check, runtime assertion)

### Integration Tests

- UI binding: `ctx.shared:on_change()` triggers update callback
- Save/load round-trip: state preserved across scene load
- Lua script hot-reload: state persists, subscriptions re-bound
- Editor IPC: `get_shared_state`, `set_shared_state` reflect changes in inspector

### Example Test (C++)

```cpp
TEST(GameState, SetAndGetPreservesType) {
    GameState state(event_bus);

    state.set("health", 75);
    EXPECT_EQ(state.get<int32_t>("health"), 75);
    EXPECT_EQ(state.get<float>("health", 1.0f), 1.0f);  // Wrong type, default
}

TEST(GameState, ChangedEventFiredForNewValue) {
    GameState state(event_bus);

    std::vector<GameStateChangedEvent<int32_t>> fired;
    event_bus.subscribe<GameStateChangedEvent<int32_t>>([&](const auto& e) {
        if (e.key == "health") fired.push_back(e);
    });

    state.set("health", 100);
    state.dispatch_deferred_events();

    EXPECT_EQ(fired.size(), 1);
    EXPECT_EQ(fired[0].new_value, 100);
}

TEST(GameState, NoEventFiredIfValueUnchanged) {
    GameState state(event_bus);
    state.set("health", 100);

    std::vector<GameStateChangedEvent<int32_t>> fired;
    event_bus.subscribe<GameStateChangedEvent<int32_t>>([&](const auto& e) {
        if (e.key == "health") fired.push_back(e);
    });

    state.set("health", 100);  // Same value
    state.dispatch_deferred_events();

    EXPECT_EQ(fired.size(), 0);  // No event
}
```

---

## Phase Roadmap

| Phase | Deliverable |
|-------|-------------|
| **Phase 1** | `GameState` C++ class, `GameStateChangedEvent`, `ctx.shared` Lua proxy, deferred event dispatch |
| **Phase 2** | Editor IPC handlers (`get_shared_state`, `set_shared_state`, `shared_state_changed` event) |
| **Phase 3** | Serialization hooks (msgpack), persistent key filtering, integration with `save-system` |
| **Phase 4** | Network replication stub (for multiplayer), cloud save integration |

---

## Related Docs

- [specs/scripting/lua-scripting.md](lua-scripting.md) — LuaJIT VM, `ctx` API, script lifecycle
- [specs/gameplay/event-bus.md](../gameplay/event-bus.md) — C++ `EventBus<T>`, predefined engine events
- [specs/gameplay/save-system.md](../gameplay/save-system.md) — Save/load integration, msgpack format
- [specs/ui/ui-system.md](../ui/ui-system.md) — Data binding to `ctx.shared` changes
- [specs/audio/audio-snapshots.md](../audio/audio-snapshots.md) — Audio state driven by `ctx.shared`

---

## Deferred Items (Phase 2+)

The following items are intentionally deferred to Phase 2 and later:

1. **Network Replication** (Phase 4): Sync `GameState` changes across multiplayer clients
2. **Cloud Save Integration** (Phase 4): Automatic backup of state to cloud storage
3. **State Validation Schema** (Phase 2): Optional `.uzschema` files to validate keys and types
4. **Advanced Type Support** (Phase 4): Custom Lua table serialization, nested structures beyond Vec3
5. **UI Binding Framework** (Phase 2): Declarative data binding syntax (e.g., `data-bind="player.health"`)
6. **Audio Snapshot Triggers** (Phase 3): Automatic audio snapshot switching based on state changes
