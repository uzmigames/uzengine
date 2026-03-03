#pragma once

// event_bus.h -- Type-safe synchronous event bus.
//
// Events are plain structs. Subscribers register a callback for a specific
// event type. When EventBus::emit<T>(event) is called, all registered handlers
// for T are invoked synchronously in registration order.
//
// Subscription tokens are returned as SubscriptionHandle. Call unsubscribe()
// to deregister.
//
// Re-entrant safety: subscribe/unsubscribe calls inside a handler callback
// are deferred until the current emit() finishes, preventing iterator
// invalidation during dispatch.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace uz {

using SubscriptionHandle = uint32_t;
inline constexpr SubscriptionHandle kSubHandleInvalid = 0;

class EventBus {
public:
    EventBus() = default;
    ~EventBus() = default;

    // Non-copyable, non-movable -- the bus is a singleton-style service object.
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
    EventBus(EventBus&&) = delete;
    EventBus& operator=(EventBus&&) = delete;

    // Subscribe to event type T. Returns a handle to unsubscribe later.
    // The callback receives a const T& reference to the emitted event.
    template<typename T>
    [[nodiscard]] SubscriptionHandle subscribe(std::function<void(const T&)> fn);

    // Unsubscribe a handler by handle. No-op for invalid or already-removed
    // handles. If called during emit(), the removal is deferred until the
    // current dispatch completes.
    void unsubscribe(SubscriptionHandle handle);

    // Emit an event of type T. Calls all registered handlers for T in
    // registration order. Safe to subscribe/unsubscribe within a handler
    // (deferred until after dispatch).
    template<typename T>
    void emit(const T& event);

    // Remove all subscriptions for event type T.
    template<typename T>
    void clear_type();

    // Remove all subscriptions across all event types.
    void clear();

    // Query how many active handlers are registered for type T.
    template<typename T>
    [[nodiscard]] uint32_t handler_count() const;

    // Query total active subscriptions across all event types.
    [[nodiscard]] uint32_t total_subscriptions() const;

    // Query if a specific handle is still actively subscribed.
    [[nodiscard]] bool is_subscribed(SubscriptionHandle handle) const;

private:
    struct HandlerEntry {
        SubscriptionHandle handle;
        std::function<void(const void*)> fn;  // Type-erased callback
        bool active = true;                   // false = pending removal
    };

    struct ChannelEntry {
        std::vector<HandlerEntry> handlers;
        bool dispatching = false;                        // True during emit
        std::vector<SubscriptionHandle> pending_unsub;   // Deferred unsubscribes
    };

    std::unordered_map<std::type_index, ChannelEntry> channels_;
    SubscriptionHandle next_handle_ = 1;
};

// ============================================================================
// Template implementations
// ============================================================================

template<typename T>
SubscriptionHandle EventBus::subscribe(std::function<void(const T&)> fn) {
    auto& ch = channels_[std::type_index(typeid(T))];
    const SubscriptionHandle h = next_handle_++;

    // Wrap the typed callback in a type-erased void* adapter.
    ch.handlers.push_back({h, [f = std::move(fn)](const void* ev) {
        f(*static_cast<const T*>(ev));
    }, true});

    return h;
}

template<typename T>
void EventBus::emit(const T& event) {
    auto it = channels_.find(std::type_index(typeid(T)));
    if (it == channels_.end()) return;

    auto& ch = it->second;
    ch.dispatching = true;

    // Iterate by index -- new handlers appended during dispatch will have
    // indices >= snapshot_size and are deliberately excluded from this emit.
    const auto snapshot_size = ch.handlers.size();
    for (size_t i = 0; i < snapshot_size; ++i) {
        if (ch.handlers[i].active) {
            ch.handlers[i].fn(static_cast<const void*>(&event));
        }
    }

    ch.dispatching = false;

    // Process deferred unsubscribes
    for (const SubscriptionHandle h : ch.pending_unsub) {
        for (auto& e : ch.handlers) {
            if (e.handle == h) {
                e.active = false;
                break;
            }
        }
    }
    ch.pending_unsub.clear();

    // Erase inactive entries
    ch.handlers.erase(
        std::remove_if(ch.handlers.begin(), ch.handlers.end(),
            [](const HandlerEntry& e) { return !e.active; }),
        ch.handlers.end()
    );
}

template<typename T>
void EventBus::clear_type() {
    channels_.erase(std::type_index(typeid(T)));
}

template<typename T>
uint32_t EventBus::handler_count() const {
    auto it = channels_.find(std::type_index(typeid(T)));
    if (it == channels_.end()) return 0;

    uint32_t count = 0;
    for (const auto& e : it->second.handlers) {
        if (e.active) ++count;
    }
    return count;
}

} // namespace uz
