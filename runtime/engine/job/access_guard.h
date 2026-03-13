#pragma once

/// UzEngine Job System — Debug Data Race Detection
///
/// Provides `ReadAccess<T>` and `WriteAccess<T>` wrappers that detect
/// concurrent access violations at runtime in non-shipping builds.
///
/// Each wrapper acquires/releases an `AccessTracker` on construction/destruction.
/// The tracker uses an atomic counter with the following protocol:
///   - `0`  — no active access (free)
///   - `>0` — N concurrent readers active (readers may overlap)
///   - `-1` — exactly one writer active (exclusive)
///
/// Illegal combinations trigger `UZ_CHECK` failures:
///   - Writer + Writer (two exclusive locks)
///   - Writer + Reader (exclusive conflicts with shared)
///
/// # Shipping Builds
/// In `UZ_SHIPPING` builds, both types compile to zero-cost pass-throughs:
/// no atomic operations, no tracker pointer, no overhead. The constructor
/// takes only a raw pointer, so call sites do not need `#ifdef` guards —
/// the tracker parameter simply disappears.
///
/// # Ownership
/// The wrappers do NOT own the pointed-to data or the tracker. The caller
/// must ensure both outlive the wrapper. Wrappers are move-only in debug
/// builds (to prevent duplicated tracker bookkeeping) and trivially
/// copyable in shipping builds.
///
/// # Example
/// ```cpp
/// AccessTracker tracker;
/// int value = 42;
///
/// // Debug build — pass both pointer and tracker:
/// auto read = ReadAccess<int>(&value, &tracker);
/// assert(*read == 42);
///
/// // Shipping build — tracker parameter absent:
/// // auto read = ReadAccess<int>(&value);
/// ```

#include "../assertions/engine_assert.h"

#include <atomic>
#include <cstdint>

namespace uz {

#ifdef UZ_SHIPPING

// ─────────────────────────────────────────────────────────────────────────────
// Shipping Builds — Zero-Cost Pass-Through
// ─────────────────────────────────────────────────────────────────────────────

/// Read-only access wrapper (shipping build: zero overhead).
///
/// Wraps a raw pointer and provides const-only access.
/// No tracking, no atomics — compiles to a plain pointer.
template<typename T>
struct ReadAccess {
    T* ptr;

    /// Constructs a read accessor from a raw pointer.
    ///
    /// # Parameters
    /// * `p` — Non-null pointer to the data. Caller retains ownership.
    explicit ReadAccess(T* p) noexcept : ptr(p) {}

    /// Dereferences the wrapped pointer (const member access).
    const T* operator->() const noexcept { return ptr; }

    /// Dereferences the wrapped pointer (const reference).
    const T& operator*() const noexcept { return *ptr; }

    /// Returns the underlying raw pointer (const).
    [[nodiscard]] const T* get() const noexcept { return ptr; }
};

/// Read-write access wrapper (shipping build: zero overhead).
///
/// Wraps a raw pointer and provides mutable access.
/// No tracking, no atomics — compiles to a plain pointer.
template<typename T>
struct WriteAccess {
    T* ptr;

    /// Constructs a write accessor from a raw pointer.
    ///
    /// # Parameters
    /// * `p` — Non-null pointer to the data. Caller retains ownership.
    explicit WriteAccess(T* p) noexcept : ptr(p) {}

    /// Dereferences the wrapped pointer (mutable member access).
    T* operator->() noexcept { return ptr; }

    /// Dereferences the wrapped pointer (mutable reference).
    T& operator*() noexcept { return *ptr; }

    /// Returns the underlying raw pointer (mutable).
    [[nodiscard]] T* get() noexcept { return ptr; }
};

#else

// ─────────────────────────────────────────────────────────────────────────────
// Debug Builds — Atomic Race Detection
// ─────────────────────────────────────────────────────────────────────────────

/// Per-resource atomic state for concurrent access detection.
///
/// Maintains a single `int32_t` counter:
///   - `0`  — resource is free (no active accessors)
///   - `>0` — N readers are active (shared access)
///   - `-1` — one writer is active (exclusive access)
///
/// Each `ReadAccess` increments on construction, decrements on destruction.
/// Each `WriteAccess` CAS-swaps 0 → -1 on construction, stores 0 on destruction.
///
/// # Thread Safety
/// All operations use acquire/release ordering for correct visibility
/// of the guarded data across threads.
struct AccessTracker {
    std::atomic<int32_t> state{0};
};

/// Read-only access wrapper with debug race detection.
///
/// On construction, atomically increments the tracker's reader count.
/// Asserts that no writer is active (state >= 0). On destruction,
/// decrements the reader count.
///
/// Move-only — prevents double-decrement from copies.
///
/// # Parameters (constructor)
/// * `p` — Non-null pointer to the data. Caller retains ownership.
/// * `t` — Non-null pointer to the `AccessTracker` for this resource.
///         Must outlive the `ReadAccess` instance.
///
/// # Panics
/// `UZ_CHECK` fires if a writer is active when this reader is created.
template<typename T>
struct ReadAccess {
    T* ptr;
    AccessTracker* tracker;

    /// Acquires shared (read) access.
    ///
    /// Asserts no writer is active, then increments the reader count.
    ///
    /// # Panics
    /// Fires `UZ_CHECK` if `tracker->state` is `-1` (writer active).
    explicit ReadAccess(T* p, AccessTracker* t) noexcept
        : ptr(p), tracker(t)
    {
        const int32_t prev = tracker->state.load(std::memory_order_acquire);
        UZ_CHECK(prev >= 0);
        tracker->state.fetch_add(1, std::memory_order_acq_rel);
    }

    /// Releases shared (read) access by decrementing the reader count.
    ~ReadAccess() {
        if (tracker) {
            tracker->state.fetch_sub(1, std::memory_order_release);
        }
    }

    ReadAccess(const ReadAccess&) = delete;
    ReadAccess& operator=(const ReadAccess&) = delete;

    /// Move constructor — transfers tracker ownership, nulls the source.
    ReadAccess(ReadAccess&& other) noexcept
        : ptr(other.ptr), tracker(other.tracker)
    {
        other.tracker = nullptr;
    }

    /// Move assignment — releases current access, takes ownership from source.
    ReadAccess& operator=(ReadAccess&& other) noexcept {
        if (this != &other) {
            if (tracker) {
                tracker->state.fetch_sub(1, std::memory_order_release);
            }
            ptr = other.ptr;
            tracker = other.tracker;
            other.tracker = nullptr;
        }
        return *this;
    }

    /// Dereferences the wrapped pointer (const member access).
    const T* operator->() const noexcept { return ptr; }

    /// Dereferences the wrapped pointer (const reference).
    const T& operator*() const noexcept { return *ptr; }

    /// Returns the underlying raw pointer (const).
    [[nodiscard]] const T* get() const noexcept { return ptr; }
};

/// Read-write access wrapper with debug race detection.
///
/// On construction, atomically CAS-swaps the tracker from 0 (free) to -1
/// (writer active). Asserts the swap succeeds — any concurrent reader or
/// writer will cause the CAS to fail and trigger `UZ_CHECK`.
///
/// On destruction, stores 0 (free) back into the tracker.
///
/// Move-only — prevents double-release from copies.
///
/// # Parameters (constructor)
/// * `p` — Non-null pointer to the data. Caller retains ownership.
/// * `t` — Non-null pointer to the `AccessTracker` for this resource.
///         Must outlive the `WriteAccess` instance.
///
/// # Panics
/// `UZ_CHECK` fires if any reader or writer is active when this writer
/// is created (i.e., `tracker->state` is not 0).
template<typename T>
struct WriteAccess {
    T* ptr;
    AccessTracker* tracker;

    /// Acquires exclusive (write) access.
    ///
    /// Performs a compare-exchange: expects 0 (free), stores -1 (writer).
    /// If the state is not 0, another accessor is active — assertion fires.
    ///
    /// # Panics
    /// Fires `UZ_CHECK` if `tracker->state` is not 0.
    explicit WriteAccess(T* p, AccessTracker* t) noexcept
        : ptr(p), tracker(t)
    {
        int32_t expected = 0;
        const bool acquired = tracker->state.compare_exchange_strong(
            expected, -1, std::memory_order_acq_rel);
        UZ_CHECK(acquired);
    }

    /// Releases exclusive (write) access by resetting tracker to free.
    ~WriteAccess() {
        if (tracker) {
            tracker->state.store(0, std::memory_order_release);
        }
    }

    WriteAccess(const WriteAccess&) = delete;
    WriteAccess& operator=(const WriteAccess&) = delete;

    /// Move constructor — transfers tracker ownership, nulls the source.
    WriteAccess(WriteAccess&& other) noexcept
        : ptr(other.ptr), tracker(other.tracker)
    {
        other.tracker = nullptr;
    }

    /// Move assignment — releases current access, takes ownership from source.
    WriteAccess& operator=(WriteAccess&& other) noexcept {
        if (this != &other) {
            if (tracker) {
                tracker->state.store(0, std::memory_order_release);
            }
            ptr = other.ptr;
            tracker = other.tracker;
            other.tracker = nullptr;
        }
        return *this;
    }

    /// Dereferences the wrapped pointer (mutable member access).
    T* operator->() noexcept { return ptr; }

    /// Dereferences the wrapped pointer (mutable reference).
    T& operator*() noexcept { return *ptr; }

    /// Returns the underlying raw pointer (mutable).
    [[nodiscard]] T* get() noexcept { return ptr; }
};

#endif // UZ_SHIPPING

} // namespace uz
