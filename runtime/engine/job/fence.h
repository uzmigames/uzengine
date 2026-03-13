#pragma once

/// UzEngine Job System — Fence
///
/// Higher-level synchronization primitive built on `Counter`. A Fence
/// accumulates work via `AddWork()` and blocks on `Wait()` until all
/// added jobs complete. Unlike raw Counter usage, Fence handles the
/// counter management and job enqueue internally.
///
/// # Usage
/// ```cpp
/// uz::Fence fence(jobSystem);
/// fence.AddWork(uz::MakeJob([] { /* task A */ }));
/// fence.AddWork(uz::MakeJob([] { /* task B */ }));
/// fence.Wait();  // blocks until both A and B complete
/// ```
///
/// # Thread Safety
/// `AddWork()` is NOT thread-safe — call only from the submitting thread.
/// `Wait()` and `IsComplete()` may be called from any thread.

#include "counter.h"
#include "job_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class JobSystem;

/// Groups multiple jobs under a single wait point.
///
/// Internally accumulates jobs, then flushes them all at once when `Wait()`
/// is called (setting the counter to the total count before enqueue).
/// This avoids the race where a counter reaches zero before all jobs are submitted.
///
/// # Design
/// Jobs are buffered in a vector and flushed on `Wait()`. This ensures the
/// counter is set to the exact number of jobs before any worker can decrement it.
class Fence {
public:
    /// Constructs a fence bound to a job system.
    ///
    /// # Arguments
    /// * `system` - The job system to enqueue work into. Must outlive the fence.
    explicit Fence(JobSystem* system);

    /// Non-copyable (owns a Counter).
    Fence(const Fence&) = delete;
    Fence& operator=(const Fence&) = delete;
    Fence(Fence&&) = delete;
    Fence& operator=(Fence&&) = delete;

    /// Adds a job to be tracked by this fence.
    ///
    /// The job is buffered internally. Its `counter` field will be overwritten
    /// to point to the fence's internal counter when flushed.
    ///
    /// # Arguments
    /// * `job` - The job to enqueue. The `counter` field is ignored (overwritten).
    ///
    /// # Thread Safety
    /// NOT thread-safe. Call only from the submitting thread.
    void AddWork(Job job);

    /// Flushes all buffered jobs to the job system and blocks until completion.
    ///
    /// Sets the internal counter to the number of buffered jobs, assigns the
    /// counter to each job, enqueues them all, then waits for the counter to
    /// reach zero. After return, the fence is ready for reuse.
    ///
    /// No-op if no work was added since construction or last `Wait()`.
    void Wait();

    /// Non-blocking check for completion of the last `Wait()` batch.
    ///
    /// # Returns
    /// `true` if no work is pending (either `Wait()` returned or no work was added).
    [[nodiscard]] bool IsComplete() const;

private:
    JobSystem* system_;
    Counter counter_;
    std::vector<Job> pending_;
};

} // namespace uz
