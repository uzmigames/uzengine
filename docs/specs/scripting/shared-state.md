# Spec: Shared State System

> **Status**: Planned (Phase 3)
> **Subsystem**: Game State Management, Scripting Runtime
> **Dependencies**: Lua Scripting, Event Bus, Save System
> **Related Docs**: [lua-scripting.md](lua-scripting.md), [event-bus.md](event-bus.md), [save-system.md](save-system.md), [ui-system.md](ui-system.md)

---

## Overview

The Shared State System provides a **global, observable game state store** that is:
- **Centralized**: Single source of truth for game data (health, inventory, quest state, UI bindings)
- **Reactive**: Lua scripts and UI listen to state changes via change notifications
- **Type-safe**: Hierarchical namespace with Lua value constraints (string, number, table, bool)
- **Serializable**: Integrates with save/load system for persistence
- **Thread-safe**: Reads are lock-free, writes are queued for consistency
- **Auditable**: All changes logged and inspectable via debug IPC

This is critical for **isolated scripts** where multiple node scripts need to coordinate without direct coupling (player health affects UI, audio, physics ragdoll, etc.).

---

## Architecture

```
game.dll (main thread)
│
├── GameState (singleton, owned by game.dll)
│   ├── Hierarchical namespace: "player.health", "inventory.weapons", "ui.menu_open"
│   ├── State map: path → Lua value (string, number, table, bool, nil)
│   ├── Version counter: incremented on each write (for dirty detection)
│   └── Change history: [{ key, old_val, new_val, timestamp }, ...] (100 entries)
│
├── GameStateChangedEvent (fired on EventBus)
│   ├── key: "player.health"
│   ├── old_value: 100
│   ├── new_value: 80
│   └── timestamp_ms: 1709424000000
│
└── Subscribers
    ├── Lua scripts via ctx.shared:on_change(key, callback)
    ├── UI system (UIDataBinding listens to state keys)
    ├── Audio system (triggers snapshots on state transitions)
    └── Editor (overlay displays live state changes)
```

---

## GameState C++ API

```cpp
// In game_state.hpp

namespace uz {

// Opaque variant that holds Lua values (string, number, bool, table, nil)
class StateValue {
public:
    enum class Type { Nil, Bool, Number, String, Table };

    StateValue() : type_(Type::Nil) {}
    explicit StateValue(bool b) : type_(Type::Bool), bool_val(b) {}
    explicit StateValue(double n) : type_(Type::Number), number_val(n) {}
    explicit StateValue(std::string s) : type_(Type::String), string_val(std::move(s)) {}

    // For tables, store as msgpack binary
    explicit StateValue(const std::vector<uint8_t>& table_bytes)
        : type_(Type::Table), table_bytes(table_bytes) {}

    [[nodiscard]] Type type() const noexcept { return type_; }
    [[nodiscard]] bool as_bool() const { return bool_val; }
    [[nodiscard]] double as_number() const { return number_val; }
    [[nodiscard]] std::string_view as_string() const { return string_val; }
    [[nodiscard]] std::span<const uint8_t> as_table_bytes() const { return table_bytes; }

    [[nodiscard]] std::string to_string() const;  // For debugging

private:
    Type type_;
    bool bool_val = false;
    double number_val = 0.0;
    std::string string_val;
    std::vector<uint8_t> table_bytes;  // msgpack-encoded Lua table
};

// Event fired when state changes
struct GameStateChangedEvent {
    std::string key;
    StateValue old_value;
    StateValue new_value;
    uint64_t timestamp_ms;
};

class GameState {
public:
    // Get state by key, returns default value if not found
    [[nodiscard]] StateValue get(std::string_view key) const;

    // Set state value (queued write)
    void set(std::string_view key, const StateValue& value);

    // Bulk set (useful for load_state)
    void set_bulk(const std::map<std::string, StateValue>& updates);

    // Delete key
    void delete_key(std::string_view key);

    // Check if key exists
    [[nodiscard]] bool has(std::string_view key) const noexcept;

    // List all keys with prefix
    [[nodiscard]] std::vector<std::string> keys_with_prefix(std::string_view prefix) const;

    // Get all state (snapshot for serialization)
    [[nodiscard]] std::map<std::string, StateValue> snapshot() const;

    // Get change history (last N entries)
    [[nodiscard]] std::vector<GameStateChangedEvent> history(size_t limit = 100) const;

    // Version counter (incremented on each write)
    // Useful for dirty detection in UI data binding
    [[nodiscard]] uint32_t version() const noexcept;

    // Subscribe to state changes (returns subscription ID for later unsubscribe)
    [[nodiscard]] uint32_t subscribe(
        std::string_view key_prefix,
        std::function<void(const GameStateChangedEvent&)> callback
    );

    // Unsubscribe
    void unsubscribe(uint32_t subscription_id);

    // Purge old history entries
    void clear_history();

private:
    std::map<std::string, StateValue> state_;
    std::vector<GameStateChangedEvent> history_;  // Last 100
    uint32_t version_ = 0;
    mutable std::shared_mutex state_mutex_;
    std::vector<std::pair<uint32_t, std::function<void(const GameStateChangedEvent&)>>> subscribers_;
};

} // namespace uz
```

---

## Lua Binding: ctx.shared

The Lua scripting environment accesses state via a transparent proxy table.

```lua
-- Set values
ctx.shared:set("player.health", 100)
ctx.shared:set("player.mana", 50)
ctx.shared:set("inventory.weapon", "sword")
ctx.shared:set("ui.menu_open", false)

-- Get values
local health = ctx.shared:get("player.health")
assert(health == 100)

-- Check existence
if ctx.shared:has("player.health") then
    print("Player has health key")
end

-- Delete key
ctx.shared:delete("player.health")

-- Get all keys with prefix
local player_keys = ctx.shared:keys_with_prefix("player")
-- Returns: {"player.health", "player.mana", "player.armor", ...}

-- Listen for changes (returns subscription ID)
local sub_id = ctx.shared:on_change("player.health", function(event)
    print("Health changed: " .. event.old_value .. " -> " .. event.new_value)
    -- Update UI, play sound, etc.
end)

-- Unsubscribe
ctx.shared:unsubscribe(sub_id)

-- Bulk get/set (useful for save/load)
local player_state = {
    ["player.health"] = 100,
    ["player.mana"] = 50,
    ["inventory.weapon"] = "sword",
    ["ui.menu_open"] = false,
}
ctx.shared:set_bulk(player_state)

-- Get snapshot (all state at this moment)
local all_state = ctx.shared:snapshot()
for key, value in pairs(all_state) do
    print(key .. " = " .. tostring(value))
end

-- Observe state as read-only (subscribe pattern)
ctx.shared:on_change("inventory", function(event)
    print("Inventory updated: " .. event.key .. " = " .. tostring(event.new_value))
    refresh_ui_inventory()
end)
```

**Constraint:** Scripts can only write to their allowed namespace:
```lua
-- In script for node "Player"
-- Allowed write: ctx.shared:set("player.*", value)
-- Blocked write: ctx.shared:set("enemy.health", value)  -- Error!
-- Allowed read: ctx.shared:get("enemy.health")  -- Read-only OK
```

---

## State Namespace Conventions

Follow a consistent naming scheme for state keys:

```
player.health          (number 0-100)
player.mana            (number)
player.position        (table {x, y, z} or string serialized)
player.is_alive        (bool)

inventory.weapon_slots       (table [id1, id2, ...])
inventory.consumables        (table {name -> count})

ui.menu_open           (bool)
ui.active_panel        (string: "inventory" | "map" | "stats")
ui.selected_item       (number: item_id or -1)

quest.active_quests    (table {id -> progress})
quest.completed        (table {id -> true})

audio.current_music    (string: track name or nil)
audio.music_volume     (number 0-1)

physics.gravity        (number: 9.81 or custom)
physics.debug_draw     (bool)
```

---

## Integration with Save System

GameState serializes to msgpack via the save system.

```cpp
// In game.dll On_Save_State callback

void on_save_state(SaveWriter& writer, const GameState& game_state) {
    // Get snapshot of all state
    auto state_snapshot = game_state.snapshot();

    // Write to msgpack via SaveWriter
    writer.write_key("game_state");
    writer.begin_object(state_snapshot.size());

    for (const auto& [key, value] : state_snapshot) {
        writer.write_key(key);

        switch (value.type()) {
            case StateValue::Type::Bool:
                writer.write_value(value.as_bool());
                break;
            case StateValue::Type::Number:
                writer.write_value(value.as_number());
                break;
            case StateValue::Type::String:
                writer.write_value(std::string(value.as_string()));
                break;
            case StateValue::Type::Table:
                // Already msgpack-encoded, copy directly
                writer.write_raw_msgpack(value.as_table_bytes());
                break;
            case StateValue::Type::Nil:
                writer.write_value(nullptr);
                break;
        }
    }
    writer.end_object();
}

void on_load_state(SaveReader& reader, GameState& game_state) {
    // Read msgpack blob into GameState
    auto game_state_obj = reader.read_object("game_state");

    std::map<std::string, StateValue> updates;
    for (auto& [key, value] : game_state_obj.items()) {
        if (value.is_boolean()) {
            updates[key] = StateValue(value.as_bool());
        } else if (value.is_number()) {
            updates[key] = StateValue(value.as_double());
        } else if (value.is_string()) {
            updates[key] = StateValue(std::string(value.as_string()));
        } else if (value.is_object() || value.is_array()) {
            // Re-encode table as msgpack
            auto bytes = msgpack::to_bytes(value);
            updates[key] = StateValue(bytes);
        }
    }

    game_state.set_bulk(updates);
}
```

**Lua Integration:**
```lua
-- Load state from save file
function on_load_save()
    local saved_state = ctx.saves:load_state()
    if saved_state then
        ctx.shared:set_bulk(saved_state)
        print("Loaded " .. count_keys(saved_state) .. " state values")
    end
end

-- Save current state
function on_save_game()
    local state = ctx.shared:snapshot()
    ctx.saves:save_state(state)
    print("Saved game state")
end
```

---

## UI Data Binding

The UI system binds to state keys for automatic updates.

**HTML Example:**
```html
<div class="hud">
    <div class="health-bar">
        <div class="health-fill" style="width: var(--health-percent)"></div>
        <span class="health-text" data-bind="player.health">100/100</span>
    </div>

    <div class="mana-bar">
        <div class="mana-fill" style="width: var(--mana-percent)"></div>
        <span class="mana-text" data-bind="player.mana">50/50</span>
    </div>

    <div class="inventory" data-bind="ui.menu_open">
        <!-- Shown/hidden based on ui.menu_open state -->
    </div>
</div>

<style>
    /* CSS variables updated by shared state changes */
    :root {
        --health-percent: calc(var(--player-health) / 100 * 100%);
        --mana-percent: calc(var(--player-mana) / 50 * 100%);
    }
</style>
```

**UI System Implementation:**
```cpp
class UIDataBinding {
public:
    // Called when state changes
    void on_state_changed(const GameStateChangedEvent& event) {
        // Update all HTML elements with data-bind="event.key"

        // 1. Update text content
        auto elements = html_doc.query_selector_all(
            std::format("[data-bind=\"{}\"]", event.key)
        );
        for (auto elem : elements) {
            elem.set_text_content(event.new_value.to_string());
        }

        // 2. Update CSS custom properties
        auto css_var_name = std::format("--{}", escape_css_identifier(event.key));
        doc.root_element().style.set_property(css_var_name, event.new_value.to_string());

        // 3. Update visibility (if key is ui.menu_open, show/hide)
        if (event.key == "ui.menu_open") {
            auto menu = html_doc.query_selector(".inventory");
            menu.style.display = event.new_value.as_bool() ? "block" : "none";
        }
    }
};
```

---

## Audio Snapshot Binding

Audio snapshots trigger based on state changes.

```lua
-- Register audio snapshots that respond to state
ctx.audio:register_snapshot("player.health", {
    -- When health is high, play energetic music
    [100] = "res://audio/music_confident.ogg",
    -- When health is low, switch to tense music
    [50] = "res://audio/music_warning.ogg",
    [20] = "res://audio/music_critical.ogg",
})

-- Register bus volume snapshots
ctx.audio:register_snapshot("audio.music_volume", {
    [0.0] = { bus = "music", volume = 0.0 },
    [0.5] = { bus = "music", volume = 0.5 },
    [1.0] = { bus = "music", volume = 1.0 },
})

-- When state changes, snapshots automatically apply
function on_damage(amount)
    local health = ctx.shared:get("player.health")
    health = health - amount
    ctx.shared:set("player.health", health)  -- Triggers audio snapshot
end
```

---

## Thread Safety

The state system ensures consistency across threads while minimizing lock contention.

```cpp
class GameState {
private:
    // Lock-free reads: atomic snapshots
    // Writes: queued and batched, applied once per frame

    std::map<std::string, StateValue> state_;  // Protected by shared_mutex
    mutable std::shared_mutex state_mutex_;

    std::queue<std::pair<std::string, StateValue>> write_queue_;  // Main thread only

public:
    // Read: takes shared lock (multiple readers can proceed)
    [[nodiscard]] StateValue get(std::string_view key) const {
        std::shared_lock lock(state_mutex_);
        auto it = state_.find(std::string(key));
        return (it != state_.end()) ? it->second : StateValue();
    }

    // Write: queued (only main thread writes)
    void set(std::string_view key, const StateValue& value) {
        // Game thread (main) checks state before queuing
        auto old_value = get(key);  // Shared lock

        if (old_value.type() == value.type()) {
            write_queue_.push({std::string(key), value});
        }
        // On next frame update, process queue:
        // - Acquire exclusive lock
        // - Apply all writes
        // - Fire GameStateChangedEvents
    }

    // Flush writes (called once per frame, main thread)
    void flush_writes() {
        std::unique_lock lock(state_mutex_);

        while (!write_queue_.empty()) {
            auto [key, new_value] = write_queue_.front();
            write_queue_.pop();

            auto old_value = state_[key];
            state_[key] = new_value;

            // Fire event (to EventBus, which notifies Lua subscribers)
            event_bus.fire<GameStateChangedEvent>({
                .key = key,
                .old_value = old_value,
                .new_value = new_value,
                .timestamp_ms = get_time_ms(),
            });
        }
    }
};
```

---

## Script Sandbox Restrictions

Scripts are restricted in which state keys they can write to.

```cpp
class LuaScriptingSandbox {
private:
    std::string node_namespace_;  // e.g., "player_node"

public:
    // From script "player_node", only allow writes to "player.*" prefix
    bool can_write(std::string_view key) const {
        // Extract namespace from node name
        auto prefix = extract_namespace(node_namespace_);
        return key.starts_with(prefix);
    }

    void on_shared_set(std::string_view key, const StateValue& value) {
        if (!can_write(key)) {
            throw std::runtime_error(std::format(
                "Script '{}' cannot write to '{}', only '{}.*'",
                node_namespace_, key, extract_namespace(node_namespace_)
            ));
        }
    }
};
```

**Example:** Node "Enemy_001" script:
```lua
-- Allowed: enemy_001.health, enemy_001.position, etc.
ctx.shared:set("enemy_001.health", 50)  -- OK

-- Blocked: player.health
ctx.shared:set("player.health", 100)    -- Error: Restricted write

-- Allowed: read from anywhere
local player_health = ctx.shared:get("player.health")  -- OK
```

---

## Debug IPC Commands

```
# Get current game state (snapshot)
shared_state.get_snapshot → {
    "player.health": 100,
    "player.mana": 50,
    "inventory.weapon": "sword",
    ...
}

# Get state change history
shared_state.get_history {limit: 20} → [
    {key: "player.health", old: 100, new: 80, timestamp: 1709424000000},
    ...
]

# Set state key (for testing)
shared_state.set {"key": "player.health", "value": 50}

# Watch state key (stream changes)
shared_state.watch_key "player.health"
# Output (streaming):
# {key: "player.health", old: 80, new: 70, timestamp: 1709424001000}

# Get all keys with prefix
shared_state.list_keys "player" → ["player.health", "player.mana", ...]

# Get version (for dirty detection in UI)
shared_state.get_version → 45

# Clear history
shared_state.clear_history

# Enable/disable logging (logs all changes to console)
shared_state.set_logging true
```

---

## Test Coverage

### Unit Tests

```cpp
// tests/shared_state_test.cpp

TEST(GameStateTest, SetAndGet) {
    GameState state;
    state.set("player.health", StateValue(100.0));

    auto value = state.get("player.health");
    EXPECT_EQ(value.as_number(), 100.0);
}

TEST(GameStateTest, KeyExists) {
    GameState state;
    state.set("player.health", StateValue(100.0));
    EXPECT_TRUE(state.has("player.health"));
    EXPECT_FALSE(state.has("player.mana"));
}

TEST(GameStateTest, DeleteKey) {
    GameState state;
    state.set("player.health", StateValue(100.0));
    state.delete_key("player.health");
    EXPECT_FALSE(state.has("player.health"));
}

TEST(GameStateTest, KeysWithPrefix) {
    GameState state;
    state.set("player.health", StateValue(100.0));
    state.set("player.mana", StateValue(50.0));
    state.set("enemy.health", StateValue(75.0));

    auto keys = state.keys_with_prefix("player");
    EXPECT_EQ(keys.size(), 2);
    EXPECT_TRUE(std::find(keys.begin(), keys.end(), "player.health") != keys.end());
}

TEST(GameStateTest, ChangeNotification) {
    GameState state;
    std::vector<GameStateChangedEvent> events;

    state.subscribe("player", [&](const GameStateChangedEvent& e) {
        events.push_back(e);
    });

    state.set("player.health", StateValue(100.0));
    state.flush_writes();

    EXPECT_EQ(events.size(), 1);
    EXPECT_EQ(events[0].key, "player.health");
    EXPECT_EQ(events[0].new_value.as_number(), 100.0);
}

TEST(GameStateTest, Snapshot) {
    GameState state;
    state.set("player.health", StateValue(100.0));
    state.set("player.mana", StateValue(50.0));

    auto snap = state.snapshot();
    EXPECT_EQ(snap.size(), 2);
    EXPECT_EQ(snap["player.health"].as_number(), 100.0);
}

TEST(LuaBindingTest, ContextSharedSet) {
    LuaScriptingEngine lua;

    std::string script = R"(
        ctx.shared:set("player.health", 100)
        local h = ctx.shared:get("player.health")
        assert(h == 100)
        return true
    )";

    auto result = lua.eval(script);
    EXPECT_TRUE(result.as_bool());
}

TEST(LuaBindingTest, ContextSharedOnChange) {
    LuaScriptingEngine lua;

    std::string script = R"(
        local fired = false
        ctx.shared:on_change("player.health", function(event)
            fired = true
            assert(event.old_value == 100)
            assert(event.new_value == 80)
        end)
        ctx.shared:set("player.health", 100)
        ctx.shared:set("player.health", 80)
        -- Process event loop
        return fired
    )";

    auto result = lua.eval(script);
    EXPECT_TRUE(result.as_bool());
}

TEST(SandboxTest, ScriptCannotWriteOtherNamespace) {
    LuaScriptingEngine lua;

    // Script is in "enemy_001" node
    std::string script = R"(
        -- This should fail
        ctx.shared:set("player.health", 100)
        return false  -- Should not reach here
    )";

    EXPECT_THROW(lua.eval(script), std::runtime_error);
}

TEST(SerializationTest, SaveLoadGameState) {
    GameState state;
    state.set("player.health", StateValue(100.0));
    state.set("player.weapon", StateValue("sword"));
    state.set("ui.menu_open", StateValue(false));

    // Serialize
    auto snap = state.snapshot();
    auto msgpack_bytes = serialize_to_msgpack(snap);

    // Deserialize
    GameState state2;
    auto updates = deserialize_from_msgpack(msgpack_bytes);
    state2.set_bulk(updates);

    EXPECT_EQ(state2.get("player.health").as_number(), 100.0);
    EXPECT_EQ(state2.get("player.weapon").as_string(), "sword");
    EXPECT_FALSE(state2.get("ui.menu_open").as_bool());
}
```

### Integration Tests

- UI bindings react to state changes
- Audio snapshots trigger on state transitions
- Script sandbox enforces namespace restrictions
- Save/load round-trip preserves all state

---

## Phase Roadmap

**Phase 3 (Initial):**
- GameState C++ core (set, get, subscribe, snapshot)
- Lua binding (ctx.shared with on_change callbacks)
- Change event notifications via EventBus
- Integration with save system

**Phase 4 (Enhancement):**
- UI data binding system (HTML elements sync to state)
- Audio snapshot registration
- Debug overlay with live state inspector
- State versioning for efficient dirty detection

---

## Related Docs

- [lua-scripting.md](lua-scripting.md) — Lua scripting engine, ctx object
- [event-bus.md](event-bus.md) — EventBus system for broadcasting GameStateChangedEvent
- [save-system.md](save-system.md) — Integration with save/load (GameState serialization)
- [ui-system.md](ui-system.md) — HTML UI, data-bind attribute, reactive updates
- [lua-scripting-guide.md](../guides/lua-scripting-guide.md) — Practical examples of state usage in scripts
