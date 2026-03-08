#pragma once

// game_state.h -- Centralized, typed key-value store for global game state.
//
// Provides GameState: a typed key-value store accessible from C++ and Lua.
// Supports bool, int32_t, float, std::string, Vec3 values.
//
// Change events are deferred and dispatched at the frame boundary via
// dispatch_deferred_events(). Thread safety: main-thread-only access
// enforced by debug assertions.
//
// Lua access is via uz.shared (see register_shared_state_bindings).

#include "engine/core/event_bus.h"
#include "engine/core/math.h"

#include <cassert>
#include <cstdint>
#include <functional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

struct lua_State;

namespace uz {

// ---------------------------------------------------------------------------
// GameStateValue — discriminated union of supported types
// ---------------------------------------------------------------------------

using GameStateValue = std::variant<bool, int32_t, float, std::string, Vec3>;

// ---------------------------------------------------------------------------
// Change events
// ---------------------------------------------------------------------------

/// Fired when a value changes (if new value differs from old).
/// Uses the variant type so a single event struct covers all types.
struct GameStateChangedEvent {
    std::string     key;
    GameStateValue  old_value;
    GameStateValue  new_value;
};

/// Fired when a key is removed.
struct GameStateRemovedEvent {
    std::string     key;
    GameStateValue  last_value;
};

// ---------------------------------------------------------------------------
// GameState class
// ---------------------------------------------------------------------------

class GameState {
public:
    explicit GameState(EventBus& event_bus);
    ~GameState() = default;

    // Non-copyable, non-movable.
    GameState(const GameState&) = delete;
    GameState& operator=(const GameState&) = delete;
    GameState(GameState&&) = delete;
    GameState& operator=(GameState&&) = delete;

    // -----------------------------------------------------------------------
    // Core API
    // -----------------------------------------------------------------------

    /// Get a typed value. Returns default_value if key is absent or wrong type.
    template<typename T>
    T get(std::string_view key, T default_value = T{}) const;

    /// Get the raw variant value. Returns nullptr if absent.
    const GameStateValue* get_raw(std::string_view key) const;

    /// Set a typed value. Queues a GameStateChangedEvent if value differs.
    template<typename T>
    void set(std::string_view key, const T& value);

    /// Set from a variant directly.
    void set_value(std::string_view key, const GameStateValue& value);

    /// Test if a key exists.
    bool has(std::string_view key) const;

    /// Remove a key. Queues a GameStateRemovedEvent.
    void remove(std::string_view key);

    /// Remove all entries. Does NOT emit per-key events.
    void clear();

    /// Return sorted list of all keys.
    std::vector<std::string> keys() const;

    /// Return all key-value pairs (for editor inspection).
    const std::unordered_map<std::string, GameStateValue>& get_all() const { return store_; }

    // -----------------------------------------------------------------------
    // Persistence
    // -----------------------------------------------------------------------

    /// Mark a key as persistent (included in save) or non-persistent.
    /// By default, all keys are persistent.
    void set_persistent(std::string_view key, bool is_persistent);

    /// Check if a key is persistent.
    bool is_persistent(std::string_view key) const;

    // -----------------------------------------------------------------------
    // Change subscriptions
    // -----------------------------------------------------------------------

    /// Subscribe to changes for a specific key.
    /// Callback receives (old_value, new_value) as variants.
    using ChangeCallback = std::function<void(const GameStateValue&, const GameStateValue&)>;
    uint32_t on_change(std::string_view key, ChangeCallback fn);

    /// Unsubscribe from a change notification.
    void unsubscribe(uint32_t sub_id);

    // -----------------------------------------------------------------------
    // Deferred event dispatch
    // -----------------------------------------------------------------------

    /// Dispatch all queued change/remove events. Call at frame boundary.
    void dispatch_deferred_events();

    /// Suppress event queuing (used during deserialization).
    void set_silent(bool silent) { silent_ = silent; }
    bool is_silent() const { return silent_; }

    /// Number of pending deferred events (for testing).
    uint32_t pending_event_count() const;

private:
    std::unordered_map<std::string, GameStateValue> store_;
    std::set<std::string>                           non_persistent_keys_;
    EventBus&                                       event_bus_;
    bool                                            silent_ = false;

    // Deferred events
    std::vector<GameStateChangedEvent>  deferred_changed_;
    std::vector<GameStateRemovedEvent>  deferred_removed_;

    // Per-key subscriptions (internal, independent of EventBus)
    struct KeySub {
        uint32_t       id;
        std::string    key;
        ChangeCallback fn;
    };
    std::vector<KeySub> key_subs_;
    uint32_t            next_sub_id_ = 1;
};

// ---------------------------------------------------------------------------
// Lua binding registration
// ---------------------------------------------------------------------------

/// Register uz.shared on the given Lua state.
/// GameState* is stored in the Lua registry for retrieval by bindings.
void register_shared_state_bindings(lua_State* L, GameState* state);

// ---------------------------------------------------------------------------
// Template implementations
// ---------------------------------------------------------------------------

template<typename T>
T GameState::get(std::string_view key, T default_value) const {
    auto it = store_.find(std::string(key));
    if (it == store_.end()) return default_value;

    const T* val = std::get_if<T>(&it->second);
    if (!val) return default_value;  // Wrong type stored
    return *val;
}

template<typename T>
void GameState::set(std::string_view key, const T& value) {
    static_assert(
        std::is_same_v<T, bool> ||
        std::is_same_v<T, int32_t> ||
        std::is_same_v<T, float> ||
        std::is_same_v<T, std::string> ||
        std::is_same_v<T, Vec3>,
        "GameState::set only supports bool, int32_t, float, std::string, Vec3"
    );

    std::string key_str(key);
    auto it = store_.find(key_str);

    if (it != store_.end()) {
        // Check if same type and same value
        const T* existing = std::get_if<T>(&it->second);
        if (existing && *existing == value) {
            return;  // No change
        }

        // Queue event with old value
        GameStateValue old_val = it->second;
        it->second = value;

        if (!silent_) {
            deferred_changed_.push_back({key_str, old_val, GameStateValue{value}});
        }
    } else {
        // New key
        store_.emplace(key_str, GameStateValue{value});

        if (!silent_) {
            deferred_changed_.push_back({key_str, GameStateValue{T{}}, GameStateValue{value}});
        }
    }
}

} // namespace uz
