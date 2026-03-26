#pragma once

/// UzEngine Job System — Worker Thread Pool
///
/// Multi-threaded work-stealing job scheduler. Workers drain per-priority queues
/// in priority order (Critical first), falling back to stealing from other workers
/// when idle. Uses `atomic_queue::AtomicQueue2` for lock-free MPMC queues.
///
/// # Architecture
/// - N worker threads (hardware_concurrency - 1, minimum 1)
/// - Each worker owns 4 queues (one per priority level)
/// - Round-robin enqueue distributes work across workers
/// - Work-stealing: idle workers steal from random peers
/// - Condition variable for sleep/wake (no busy-spin)
///
/// # Integration
/// Implements `ISubsystem` for lifecycle management. Initializes at `PreInit`
/// phase (before renderer, physics) so all systems can submit jobs.
///
/// # Example
/// ```cpp
/// auto* js = uz::JobSystem::Get();
/// uz::Counter counter;
/// counter.Reset(2);
/// js->Enqueue(uz::MakeJob([&] { DoWorkA(); }, uz::JobPriority::High, &counter));
/// js->Enqueue(uz::MakeJob([&] { DoWorkB(); }, uz::JobPriority::High, &counter));
/// counter.Wait();
/// ```

#include "../app/subsystem.h"
#include "../logging/log.h"
#include "counter.h"
#include "job_types.h"

#include <atomic>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <type_traits>
#include <vector>

#include <atomic_queue/atomic_queue.h>

namespace uz {

UZ_DECLARE_LOG_CATEGORY(Jobs);

/// Thread pool with per-worker priority queues and work-stealing.
///
/// Owns N worker threads, each with 4 lock-free MPMC queues (one per priority).
/// Enqueue distributes jobs via round-robin. Workers drain their own queues in
/// priority order, then steal from peers starting at a random index.
///
/// # Singleton
/// A global instance pointer is set during `Init()` and cleared during `Shutdown()`.
/// Access via `JobSystem::Get()`.
///
/// # Thread Safety
/// `Enqueue()` is thread-safe (lock-free). `Init()` and `Shutdown()` must be
/// called from the main thread only.
class JobSystem final : public ISubsystem {
public:
    static constexpr uint32_t kQueueCapacity = 4096;

    JobSystem() = default;
    ~JobSystem() override = default;

    // ── ISubsystem ───────────────────────────────────────────────────────

    /// Returns "JobSystem".
    [[nodiscard]] const char* GetName() const override { return "JobSystem"; }

    /// Returns PreInit — job system must be available before GPU/scene init.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::PreInit; }

    /// Job system does not tick — workers run independently.
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::None; }

    /// Depends on Logging subsystem for UZ_LOG.
    [[nodiscard]] std::vector<const char*> GetDependencies() const override { return {"Logging"}; }

    /// Spawns worker threads and sets the global instance pointer.
    ///
    /// Worker count = `std::thread::hardware_concurrency() - 1`, minimum 1.
    ///
    /// # Returns
    /// `true` on success. Currently cannot fail.
    [[nodiscard]] bool Init() override;

    /// Signals all workers to exit, joins threads, and clears the global instance.
    ///
    /// Any jobs still in queues are abandoned (not executed).
    void Shutdown() override;

    // ── Job submission ───────────────────────────────────────────────────

    /// Enqueues a job for execution by a worker thread.
    ///
    /// The job is placed into the target worker's queue for its priority level.
    /// Target worker is selected via round-robin to distribute load evenly.
    /// If the queue is full, the job is executed inline on the calling thread.
    ///
    /// # Arguments
    /// * `job` - The job to execute. Copied into the queue.
    ///
    /// # Thread Safety
    /// Lock-free. Safe to call from any thread, including worker threads.
    void Enqueue(Job job);

    /// Returns the number of worker threads (not counting the main thread).
    [[nodiscard]] uint32_t GetWorkerCount() const;

    /// Attempts to execute one pending job on the calling thread.
    ///
    /// Useful for the main thread to help drain work while waiting on a Counter.
    /// Tries all workers' queues in priority order.
    ///
    /// # Returns
    /// `true` if a job was executed, `false` if all queues were empty.
    bool Yield();

    /// Distributes work across all workers using a parallel-for pattern.
    ///
    /// Splits the range `[0, count)` into chunks (one per worker + main thread),
    /// enqueues each chunk as a job, and blocks until all chunks complete.
    /// The callable receives `(T& element)` for each item.
    ///
    /// # Arguments
    /// * `data` - Pointer to contiguous array of elements.
    /// * `count` - Number of elements to process.
    /// * `fn` - Callable invoked as `fn(data[i])` for each element.
    ///
    /// # Blocking
    /// Blocks the calling thread until all chunks complete. The calling thread
    /// also processes one chunk (so at least one chunk runs without queue overhead).
    ///
    /// # Example
    /// ```cpp
    /// std::vector<float> values(10000);
    /// jobSystem->ParallelFor(values.data(), values.size(),
    ///     [](float& v) { v = std::sqrt(v); });
    /// ```
    template <typename T, typename Fn>
    /// Dispatches `count` elements across worker threads and processes in parallel.
    void ParallelFor(T* data, size_t count, Fn&& fn);

    /// Returns the global JobSystem instance set during Init().
    ///
    /// # Returns
    /// Pointer to the active JobSystem, or nullptr if not initialized.
    [[nodiscard]] static JobSystem* Get();

private:
    /// Per-worker state: thread handle + priority queues + stats.
    struct WorkerData {
        std::thread thread;
        atomic_queue::AtomicQueue2<Job, kQueueCapacity> queues[static_cast<size_t>(JobPriority::Count)];
        std::atomic<uint64_t> executedJobs{0};
        std::atomic<uint64_t> stolenJobs{0};
    };

    std::vector<std::unique_ptr<WorkerData>> workers_;
    std::atomic<bool> running_{false};
    std::mutex wakeMutex_;
    std::condition_variable wakeCV_;
    std::atomic<uint32_t> pendingJobs_{0};
    std::atomic<uint32_t> nextWorker_{0};

    static inline JobSystem* instance_ = nullptr;

    /// Worker thread entry point. Loops until `running_` is false.
    void WorkerMain(uint32_t workerIndex);

    /// Tries to pop and execute one job from the given worker's queues.
    ///
    /// # Returns
    /// `true` if a job was found and executed.
    bool TryExecuteOne(uint32_t workerIndex);

    /// Tries to steal and execute one job from a different worker.
    ///
    /// Starts at a pseudo-random worker index to avoid thundering herd.
    ///
    /// # Returns
    /// `true` if a job was stolen and executed.
    bool TrySteal(uint32_t workerIndex);

    /// Executes a single job and decrements its counter if present.
    void ExecuteJob(Job& job);
};

// ─────────────────────────────────────────────────────────────────────────────
// ParallelFor — template implementation
// ─────────────────────────────────────────────────────────────────────────────

template <typename T, typename Fn>
void JobSystem::ParallelFor(T* data, size_t count, Fn&& fn) {
    if (count == 0) {
        return;
    }

    const uint32_t totalThreads = GetWorkerCount() + 1; // +1 for calling thread
    const size_t chunkSize = (count + totalThreads - 1) / totalThreads;

    /// Per-chunk partition descriptor stored inline in `Job::data`.
    ///
    /// Each ParallelFor worker receives one ChunkData describing its
    /// [begin, end) slice of the input array plus a pointer to the
    /// callable. Created on the calling thread's stack and copied into
    /// the Job inline buffer — must remain trivially copyable and fit
    /// within `sizeof(Job::data)`.
    ///
    /// # Thread Safety
    /// Each worker owns its ChunkData copy; no synchronization needed.
    struct ChunkData {
        T* ptr;
        size_t begin;
        size_t end;
        const std::decay_t<Fn>* fn;
    };
    static_assert(sizeof(ChunkData) <= sizeof(Job::data),
                  "ParallelFor ChunkData exceeds Job inline buffer. "
                  "Reduce captures in the callable.");
    static_assert(std::is_trivially_copyable_v<ChunkData>,
                  "ChunkData must be trivially copyable.");

    // Store fn on the stack — workers reference it via pointer (safe because we Wait() below)
    auto storedFn = std::decay_t<Fn>(std::forward<Fn>(fn));

    Counter counter;
    uint32_t actualChunks = 0;

    // Count valid chunks
    for (uint32_t i = 0; i < totalThreads; ++i) {
        size_t begin = static_cast<size_t>(i) * chunkSize;
        if (begin < count) {
            ++actualChunks;
        }
    }

    counter.Reset(actualChunks);

    // Enqueue worker chunks (skip chunk 0 — main thread processes it)
    for (uint32_t i = 1; i < totalThreads; ++i) {
        size_t begin = static_cast<size_t>(i) * chunkSize;
        if (begin >= count) {
            break;
        }
        size_t end = std::min(begin + chunkSize, count);

        Job job;
        job.function = [](void* raw) {
            auto* chunk = static_cast<ChunkData*>(raw);
            for (size_t idx = chunk->begin; idx < chunk->end; ++idx) {
                (*chunk->fn)(chunk->ptr[idx]);
            }
        };
        job.counter = &counter;
        job.priority = JobPriority::High;

        ChunkData chunk{data, begin, end, &storedFn};
        std::memcpy(job.data, &chunk, sizeof(ChunkData));

        Enqueue(job);
    }

    // Main thread processes chunk 0
    {
        size_t end = std::min(chunkSize, count);
        for (size_t idx = 0; idx < end; ++idx) {
            storedFn(data[idx]);
        }
        counter.Decrement();
    }

    counter.Wait();
}

} // namespace uz
