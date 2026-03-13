#pragma once

/// UzEngine Job System — Completion Counter
///
/// Atomic counter for synchronizing on job completion. A counter is initialized
/// with a count (number of outstanding jobs), decremented by each completing job,
/// and waited on by the submitting thread. Uses mutex + condition variable for
/// portable blocking (no platform-specific futex).
///
/// # Usage
/// ```cpp
/// uz::Counter counter;
/// counter.Reset(3);
/// // ... enqueue 3 jobs with &counter ...
/// counter.Wait();  // blocks until all 3 complete
/// ```
///
/// # Thread Safety
/// All methods are thread-safe. Multiple workers may call `Decrement()` concurrently.
/// `Wait()` may be called from any thread (typically the submitting thread).

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>

namespace uz {

/// Atomic completion counter for job synchronization.
///
/// Tracks outstanding work items and provides blocking wait until all
/// items complete. The counter is reusable — call `Reset()` to start
/// a new batch after `Wait()` returns.
///
/// # Invariants
/// - Count is monotonically decreasing between `Reset()` and `Wait()`.
/// - `Decrement()` must not be called more times than the reset count.
class Counter {
public:
    Counter() = default;

    /// Non-copyable, non-movable (contains mutex + condvar).
    Counter(const Counter&) = delete;
    Counter& operator=(const Counter&) = delete;
    Counter(Counter&&) = delete;
    Counter& operator=(Counter&&) = delete;

    /// Initializes the counter with the number of outstanding work items.
    ///
    /// # Arguments
    /// * `count` - Number of items to wait for. Must be > 0 for `Wait()` to block.
    ///
    /// # Precondition
    /// Must not be called while another thread is in `Wait()` or `Decrement()`.
    void Reset(uint32_t count);

    /// Decrements the counter by one. Called by workers upon job completion.
    ///
    /// If the counter reaches zero, all threads blocked in `Wait()` are notified.
    ///
    /// # Thread Safety
    /// Safe to call from multiple threads concurrently.
    void Decrement();

    /// Blocks the calling thread until the counter reaches zero.
    ///
    /// Returns immediately if the counter is already zero.
    /// Uses condition variable internally — does not spin.
    void Wait();

    /// Non-blocking check for completion.
    ///
    /// # Returns
    /// `true` if the counter has reached zero, `false` otherwise.
    [[nodiscard]] bool IsComplete() const;

private:
    std::atomic<uint32_t> count_{0};
    std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace uz
