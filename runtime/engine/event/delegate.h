#pragma once

/// UzEngine MulticastDelegate — type-safe multi-listener event dispatch.
///
/// Lightweight alternative to std::function callbacks scattered across systems.
/// Each call to Add() returns a stable DelegateHandle that can be used to
/// remove that specific listener at any later point without affecting others.
///
/// # Thread Safety
/// NOT thread-safe. Invoke, Add, Remove, and Clear must all be called from the
/// same thread (typically the main/game thread).
///
/// # Example
/// ```cpp
/// uz::MulticastDelegate<float, const Vec3&> onHit;
/// auto h = onHit.Add([](float damage, const Vec3& pos) { /* ... */ });
/// onHit.Invoke(10.f, hitPos);
/// onHit.Remove(h);
/// ```

#include <cstdint>
#include <functional>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// DelegateHandle — opaque token for listener identity
// ─────────────────────────────────────────────────────────────────────────────

/// Opaque handle returned by MulticastDelegate::Add().
///
/// Store this to unregister a specific listener later via Remove().
/// A default-constructed (or cleared) handle has id == 0 and IsValid() == false.
struct DelegateHandle {
    uint32_t id = 0;

    /// Returns true if this handle represents a registered listener.
    /// A handle with id == 0 is always invalid (never returned by Add()).
    [[nodiscard]] bool IsValid() const noexcept { return id != 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// MulticastDelegate<Args...> — fan-out event dispatch
// ─────────────────────────────────────────────────────────────────────────────

/// Multi-listener delegate that invokes all registered callbacks in insertion order.
///
/// Listeners are stored as std::function objects associated with a monotonically
/// increasing integer id. The id counter starts at 1, so id == 0 is permanently
/// reserved as the invalid sentinel.
///
/// Invoke() iterates a snapshot-stable copy of the listener list, so it is safe
/// for a callback to call Remove() on its own handle during Invoke() without
/// invalidating the iteration — the removal takes effect after the current
/// Invoke() completes.
///
/// # Type Parameters
/// * `Args...` — argument types passed to each callback on Invoke().
template <typename... Args>
class MulticastDelegate {
public:
    MulticastDelegate() = default;

    // Non-copyable: listener ownership is intentionally exclusive to one owner.
    MulticastDelegate(const MulticastDelegate&) = delete;
    MulticastDelegate& operator=(const MulticastDelegate&) = delete;

    // Movable: transfers ownership of all listeners and the id counter.
    MulticastDelegate(MulticastDelegate&&) noexcept = default;
    MulticastDelegate& operator=(MulticastDelegate&&) noexcept = default;

    ~MulticastDelegate() = default;

    // ── Registration ──────────────────────────────────────────────────────────

    /// Registers a callback and returns a stable handle for later removal.
    ///
    /// The callback is stored by move, so passing a lambda or std::function
    /// rvalue avoids a copy. The returned handle is valid (IsValid() == true)
    /// as long as it has not been passed to Remove() or Clear() has not been
    /// called.
    ///
    /// # Arguments
    /// * `callback` — Callable with signature `void(Args...)`. Must not be null.
    ///
    /// # Returns
    /// A DelegateHandle with a unique, non-zero id for this delegate instance.
    DelegateHandle Add(std::function<void(Args...)> callback) {
        const uint32_t id = ++m_nextId;
        m_listeners.push_back({id, std::move(callback)});
        return {id};
    }

    /// Removes the listener identified by `handle`.
    ///
    /// No-op if `handle` is invalid (id == 0) or has already been removed.
    /// Runs in O(n) time relative to the number of registered listeners.
    ///
    /// # Arguments
    /// * `handle` — Handle previously returned by Add(). May be invalid.
    void Remove(DelegateHandle handle) {
        if (!handle.IsValid()) {
            return;
        }
        for (auto it = m_listeners.begin(); it != m_listeners.end(); ++it) {
            if (it->id == handle.id) {
                m_listeners.erase(it);
                return;
            }
        }
    }

    // ── Dispatch ──────────────────────────────────────────────────────────────

    /// Invokes all registered callbacks with the given arguments.
    ///
    /// Callbacks are called in the order they were Add()ed. A snapshot of the
    /// listener list is taken before iteration begins, so a callback that calls
    /// Remove() on itself will not cause use-after-free or iterator invalidation.
    ///
    /// If no listeners are registered, this is a no-op.
    ///
    /// # Arguments
    /// * `args` — Arguments forwarded to every registered callback.
    void Invoke(Args... args) const {
        // Copy the list to guard against self-removal during dispatch.
        const auto snapshot = m_listeners;
        for (const auto& entry : snapshot) {
            entry.callback(args...);
        }
    }

    // ── Management ────────────────────────────────────────────────────────────

    /// Removes all registered callbacks.
    ///
    /// All existing handles become invalid after this call. Does NOT reset the
    /// id counter, so handles created before Clear() will never alias handles
    /// created after it.
    void Clear() noexcept { m_listeners.clear(); }

    /// Returns the number of currently registered callbacks.
    [[nodiscard]] size_t Count() const noexcept { return m_listeners.size(); }

private:
    struct Entry {
        uint32_t                    id;
        std::function<void(Args...)> callback;
    };

    std::vector<Entry> m_listeners;
    uint32_t           m_nextId = 0; // 0 is reserved as the invalid sentinel
};

} // namespace uz
