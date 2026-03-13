#pragma once

/// UzEngine Job System — Core Types
///
/// Defines the fundamental value types for the job system: priority levels and
/// the `Job` struct itself. A Job is a small closure (function pointer + inline data)
/// designed to avoid heap allocation on enqueue. The 48-byte inline buffer fits
/// most lambda captures without spilling to the heap.
///
/// # Design
/// - Jobs are value types — copied into lock-free queues on enqueue
/// - No `std::function` or virtual dispatch — raw function pointer for zero overhead
/// - Inline data buffer avoids heap allocation for small closures
/// - Optional `Counter*` for synchronization (caller-owned lifetime)

#include <cstdint>
#include <cstring>
#include <type_traits>

namespace uz {

class Counter;

// ─────────────────────────────────────────────────────────────────────────────
// JobPriority — determines scheduling urgency
// ─────────────────────────────────────────────────────────────────────────────

/// Priority level for job scheduling.
///
/// Workers drain higher-priority queues first. Within a priority level,
/// execution order is FIFO. Work-stealing also respects priority order.
enum class JobPriority : uint8_t {
    Critical = 0, ///< Frame-critical work (rendering, physics). Drained first.
    High = 1,     ///< Important but not frame-blocking (animation, audio).
    Normal = 2,   ///< Default priority for general-purpose work.
    Low = 3,      ///< Background work (asset streaming, compression).
    Count = 4     ///< Sentinel — number of priority levels.
};

// ─────────────────────────────────────────────────────────────────────────────
// Job — small closure for lock-free work queues
// ─────────────────────────────────────────────────────────────────────────────

/// A self-contained unit of work for the job system.
///
/// Jobs are designed for zero-allocation enqueue: the work function and its
/// captured data are stored inline. The 48-byte data buffer accommodates most
/// lambda captures (pointers, indices, small structs). Larger payloads must be
/// heap-allocated by the caller and referenced via pointer in the data buffer.
///
/// # Lifecycle
/// 1. Caller constructs a Job (typically via `MakeJob` helper)
/// 2. Job is copied into a lock-free queue (value semantics)
/// 3. Worker thread invokes `function(data)` when dequeued
/// 4. If `counter` is non-null, it is decremented after execution
///
/// # Thread Safety
/// Jobs are immutable after enqueue. The `counter` pointer must remain valid
/// until the job completes (typically stack-allocated with `Counter::Wait()`).
struct Job {
    /// Work function signature. Receives a pointer to the inline `data` buffer.
    using WorkFn = void (*)(void* data);

    WorkFn function = nullptr;             ///< Work function (null = no-op job)
    alignas(16) uint8_t data[48] = {};     ///< Inline closure data (copied on enqueue)
    Counter* counter = nullptr;            ///< Optional completion counter (caller-owned)
    JobPriority priority = JobPriority::Normal; ///< Scheduling priority
};

/// Creates a Job from a callable that fits in the inline data buffer.
///
/// The callable is placement-copied into `Job::data`. It must be trivially
/// copyable and fit within 48 bytes. A `static_assert` enforces both constraints.
///
/// # Arguments
/// * `fn` - A callable (lambda, functor) to execute. Must be trivially copyable.
/// * `prio` - Scheduling priority (default: Normal).
/// * `ctr` - Optional completion counter to decrement after execution (default: nullptr).
///
/// # Returns
/// A fully initialized Job ready for enqueue.
///
/// # Example
/// ```cpp
/// int result = 0;
/// auto job = uz::MakeJob([&result]() { result = 42; });
/// jobSystem->Enqueue(job);
/// ```
template <typename Fn>
[[nodiscard]] Job MakeJob(Fn&& fn, JobPriority prio = JobPriority::Normal, Counter* ctr = nullptr) {
    using DecayedFn = std::decay_t<Fn>;
    static_assert(sizeof(DecayedFn) <= sizeof(Job::data),
                  "Callable exceeds Job inline data buffer (48 bytes). "
                  "Heap-allocate the payload and capture a pointer instead.");
    static_assert(std::is_trivially_copyable_v<DecayedFn>,
                  "Callable must be trivially copyable to store in Job::data. "
                  "Avoid capturing non-trivial types by value.");

    Job job;
    job.function = [](void* raw) {
        auto* callable = static_cast<DecayedFn*>(raw);
        (*callable)();
    };
    std::memcpy(job.data, &fn, sizeof(DecayedFn));
    job.priority = prio;
    job.counter = ctr;
    return job;
}

} // namespace uz
