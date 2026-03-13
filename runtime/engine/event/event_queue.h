#pragma once

/// UzEngine EventQueue — deferred event dispatch, flushed once per frame.
///
/// Events are enqueued during the frame and dispatched to all subscribers
/// during a single `Flush()` call at the frame sync point. This decouples
/// event producers from consumers and avoids re-entrant dispatch issues.
///
/// # Thread Safety
/// NOT thread-safe. Enqueue and Flush MUST be called from the same thread
/// (typically the main/game thread). Mixing threads without external
/// synchronization is undefined behavior.
///
/// # Example
/// ```cpp
/// struct DamageEvent { entt::entity target; float amount; };
///
/// uz::EventQueue<DamageEvent> damageQueue;
/// auto h = damageQueue.Subscribe([](const DamageEvent& e) {
///     applyDamage(e.target, e.amount);
/// });
///
/// // During frame update:
/// damageQueue.Enqueue({player, 50.f});
///
/// // At frame sync point:
/// damageQueue.Flush(); // dispatches all pending events, then clears queue
///
/// // Later:
/// damageQueue.Unsubscribe(h);
/// ```

#include "delegate.h"
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// EventQueue<TEvent> — deferred single-type event bus
// ─────────────────────────────────────────────────────────────────────────────

/// Deferred event queue that collects events during a frame and dispatches
/// them all at a designated sync point via Flush().
///
/// Subscribers register via Subscribe() and receive events of type `TEvent`
/// by const reference during Flush(). Events are dispatched in the order
/// they were enqueued (FIFO).
///
/// # Type Parameters
/// * `TEvent` — The event payload type. Must be move-constructible.
template <typename TEvent>
class EventQueue {
public:
    EventQueue() = default;

    // Non-copyable: listener and queue ownership belongs to one owner.
    EventQueue(const EventQueue&)            = delete;
    EventQueue& operator=(const EventQueue&) = delete;

    // Movable: transfers listener list, pending queue, and id counter.
    EventQueue(EventQueue&&) noexcept            = default;
    EventQueue& operator=(EventQueue&&) noexcept = default;

    ~EventQueue() = default;

    // ── Subscription ──────────────────────────────────────────────────────────

    /// Registers a callback to be invoked for every event during Flush().
    ///
    /// The callback receives each pending event by const reference in the
    /// order it was enqueued. Multiple subscribers all receive every event.
    ///
    /// # Arguments
    /// * `callback` — Callable with signature `void(const TEvent&)`. Must not be null.
    ///
    /// # Returns
    /// A DelegateHandle with a unique non-zero id. Pass it to Unsubscribe()
    /// to stop delivery to this callback.
    DelegateHandle Subscribe(std::function<void(const TEvent&)> callback) {
        return m_delegate.Add(std::move(callback));
    }

    /// Unregisters the subscriber identified by `handle`.
    ///
    /// No-op if `handle` is invalid (id == 0) or has already been removed.
    /// After this call the callback will no longer receive events during Flush().
    ///
    /// # Arguments
    /// * `handle` — Handle previously returned by Subscribe(). May be invalid.
    void Unsubscribe(DelegateHandle handle) {
        m_delegate.Remove(handle);
    }

    // ── Production ────────────────────────────────────────────────────────────

    /// Appends an event to the pending queue for delivery at the next Flush().
    ///
    /// The event is stored by move, avoiding a copy when passing temporaries
    /// or rvalues. Enqueue does NOT dispatch immediately — call Flush() to
    /// deliver all pending events to subscribers.
    ///
    /// # Arguments
    /// * `event` — Event value to enqueue. Moved into the internal buffer.
    void Enqueue(TEvent event) {
        m_pending.push_back(std::move(event));
    }

    // ── Dispatch ──────────────────────────────────────────────────────────────

    /// Dispatches all pending events to every registered subscriber, then
    /// clears the pending queue.
    ///
    /// Events are delivered in FIFO order (the order they were Enqueue()d).
    /// All subscribers receive each event before the next event is dispatched.
    /// If no events are pending or no subscribers are registered, this is a no-op.
    ///
    /// After Flush() returns, PendingCount() == 0. Calling Flush() a second
    /// time immediately after is safe and does nothing.
    void Flush() {
        for (const auto& event : m_pending) {
            m_delegate.Invoke(event);
        }
        m_pending.clear();
    }

    // ── Inspection ────────────────────────────────────────────────────────────

    /// Returns the number of events currently waiting to be dispatched.
    ///
    /// This count is non-zero only between Enqueue() and Flush() calls.
    /// Resets to zero after each successful Flush().
    ///
    /// # Returns
    /// Number of pending events. Zero after Flush() or before any Enqueue().
    [[nodiscard]] size_t PendingCount() const noexcept {
        return m_pending.size();
    }

    /// Returns the number of currently registered subscribers.
    ///
    /// # Returns
    /// Number of active subscribers. Zero if no calls to Subscribe() have
    /// been made or if all handles have been passed to Unsubscribe().
    [[nodiscard]] size_t SubscriberCount() const noexcept {
        return m_delegate.Count();
    }

private:
    MulticastDelegate<const TEvent&> m_delegate;
    std::vector<TEvent>              m_pending;
};

} // namespace uz
