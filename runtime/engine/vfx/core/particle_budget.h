#pragma once

/// Particle Budget -- global particle count limiter for GPU memory control.
///
/// Tracks how many particles are allocated across all emitters and enforces
/// a hard cap to prevent GPU memory exhaustion. Uses lock-free atomics so
/// multiple emitters can request allocations concurrently.
///
/// # Design
/// The budget tracks allocated (reserved) particles, not alive particles.
/// An emitter with capacity=10000 consumes 10000 from the budget even if
/// only 500 are currently alive. This guarantees worst-case memory bounds.
///
/// # Thread Safety
/// All methods are thread-safe via atomic CAS operations. Multiple emitters
/// may call Allocate() and Return() concurrently from different threads.
///
/// # Example
/// ```cpp
/// ParticleBudget budget(500000);
/// uint32_t allocated = 0;
/// if (budget.Allocate(10000, allocated)) {
///     // Got 'allocated' particles (may be < 10000 if near limit)
/// }
/// budget.Return(allocated);  // When emitter shuts down
/// ```

#include <atomic>
#include <cstdint>

namespace uz {

// ---------------------------------------------------------------------------
// ParticleBudget -- lock-free global particle allocation limiter
// ---------------------------------------------------------------------------

/// Global particle budget enforcer.
///
/// Limits total GPU particle allocations across all active emitters.
/// Thread-safe via atomic operations -- multiple emitters can request
/// allocations concurrently without locking.
class ParticleBudget {
public:
    /// Constructs a budget with the given maximum particle count.
    ///
    /// # Parameters
    /// - `maxParticles`: Upper limit on total particles across all emitters.
    ///   Typical values: 100K (low-end), 500K (default), 1M+ (high-end).
    ///   Default: 500,000 (~60 MB GPU memory at 120 bytes/particle).
    explicit ParticleBudget(uint32_t maxParticles = 500000) noexcept;

    /// Requests particle allocation from the global budget.
    ///
    /// Uses a lock-free CAS loop. If the budget is partially available,
    /// allocates as many as possible up to `requested`.
    ///
    /// # Parameters
    /// - `requested`: Number of particles desired. Must be > 0.
    /// - `allocated`: [out] Actual number allocated (may be < requested).
    ///
    /// # Returns
    /// `true` if any particles were allocated (`allocated > 0`).
    /// `false` if budget is fully exhausted (logs a warning).
    bool Allocate(uint32_t requested, uint32_t& allocated) noexcept;

    /// Returns particles to the budget (e.g., when an emitter shuts down).
    ///
    /// # Parameters
    /// - `count`: Number of particles to return. Must not exceed current usage.
    ///
    /// # Preconditions
    /// Fatal assert if returning more than currently in use (debug only).
    void Return(uint32_t count) noexcept;

    /// Returns the current number of particles in use across all emitters.
    [[nodiscard]] uint32_t GetUsed() const noexcept {
        return used_.load(std::memory_order_relaxed);
    }

    /// Returns the remaining available particle count.
    [[nodiscard]] uint32_t GetAvailable() const noexcept;

    /// Returns the maximum budget.
    [[nodiscard]] uint32_t GetMax() const noexcept { return max_; }

    /// Sets a new maximum budget (e.g., quality level change).
    ///
    /// Does not affect existing allocations. If the new max is less than
    /// current usage, no further allocations are possible until particles
    /// are returned.
    void SetMax(uint32_t newMax) noexcept { max_ = newMax; }

    /// Returns the allocation ratio (0.0 = empty, 1.0 = fully allocated).
    [[nodiscard]] float GetUsageRatio() const noexcept;

private:
    uint32_t max_;
    std::atomic<uint32_t> used_{0};
};

}  // namespace uz
