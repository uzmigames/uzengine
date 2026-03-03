// event_bus.cpp -- Non-template methods for EventBus.
//
// Template methods (subscribe, emit, clear_type, handler_count) live in the
// header because they need the event type T at compile time. This file
// contains only the type-agnostic operations that iterate all channels.

#include "engine/core/event_bus.h"

namespace uz {

void EventBus::unsubscribe(SubscriptionHandle handle) {
    if (handle == kSubHandleInvalid) return;

    for (auto& [type, ch] : channels_) {
        for (auto& entry : ch.handlers) {
            if (entry.handle == handle && entry.active) {
                if (ch.dispatching) {
                    // Defer removal until dispatch completes to avoid
                    // invalidating the iteration in emit().
                    ch.pending_unsub.push_back(handle);
                } else {
                    entry.active = false;
                }
                // Eagerly erase if not dispatching, to keep the vector clean.
                if (!ch.dispatching) {
                    ch.handlers.erase(
                        std::remove_if(ch.handlers.begin(), ch.handlers.end(),
                            [](const HandlerEntry& e) { return !e.active; }),
                        ch.handlers.end()
                    );
                }
                return;
            }
        }
    }
}

void EventBus::clear() {
    channels_.clear();
}

uint32_t EventBus::total_subscriptions() const {
    uint32_t total = 0;
    for (const auto& [type, ch] : channels_) {
        for (const auto& e : ch.handlers) {
            if (e.active) ++total;
        }
    }
    return total;
}

bool EventBus::is_subscribed(SubscriptionHandle handle) const {
    if (handle == kSubHandleInvalid) return false;

    for (const auto& [type, ch] : channels_) {
        for (const auto& e : ch.handlers) {
            if (e.handle == handle && e.active) return true;
        }
    }
    return false;
}

} // namespace uz
