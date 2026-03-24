/// UzEngine Job System Implementation
///
/// Work-stealing job queue with N-1 worker threads (main thread is worker 0).
/// Each worker has a private priority queue for low-contention job submission.
/// Lock-free enqueue via CAS, FIFO dequeue with mutex. Idle workers park on
/// condition variable to minimize power consumption and context switching.
///
/// # Thread Safety
/// The job system is thread-safe: any thread can enqueue jobs, and workers
/// safely share the job queues and completion synchronization.

#include "job_system.h"

#include "../assertions/engine_assert.h"
#include "../core/cvar.h"
#include "../logging/log.h"
#include "../profiling/profiler.h"

#include <random>

static auto* cv_MaxWorkerCount = UZ_CVAR_INT("job.MaxWorkerCount", 0, "Max job worker threads (0=auto from hardware_concurrency)", 0, 64);

namespace uz {

UZ_DEFINE_LOG_CATEGORY(Jobs, Trace);

// ─────────────────────────────────────────────────────────────────────────────
// ISubsystem lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool JobSystem::Init() {
    UZ_PROFILE_FUNCTION();

    UZ_CHECK(instance_ == nullptr);
    instance_ = this;

    uint32_t hwThreads = std::thread::hardware_concurrency();
    uint32_t workerCount = (hwThreads > 1) ? (hwThreads - 1) : 1;
    const int maxOverride = cv_MaxWorkerCount->value > 0.5f ? static_cast<int>(cv_MaxWorkerCount->value) : 0;
    if (maxOverride > 0 && static_cast<uint32_t>(maxOverride) < workerCount) {
        workerCount = static_cast<uint32_t>(maxOverride);
    }

    running_.store(true, std::memory_order_release);

    workers_.reserve(workerCount);
    for (uint32_t i = 0; i < workerCount; ++i) {
        auto worker = std::make_unique<WorkerData>();
        workers_.push_back(std::move(worker));
    }

    // Spawn threads after all WorkerData is allocated (pointers must be stable)
    for (uint32_t i = 0; i < workerCount; ++i) {
        workers_[i]->thread = std::thread(&JobSystem::WorkerMain, this, i);
    }

    UZ_LOG(Jobs, Info, "JobSystem initialized: {} worker threads ({} hardware threads)",
           workerCount, hwThreads);
    return true;
}

void JobSystem::Shutdown() {
    UZ_PROFILE_FUNCTION();

    if (!running_.load(std::memory_order_acquire)) {
        return;
    }

    running_.store(false, std::memory_order_release);

    // Wake all sleeping workers so they observe the shutdown flag
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeCV_.notify_all();
    }

    uint64_t totalExecuted = 0;
    uint64_t totalStolen = 0;

    for (auto& worker : workers_) {
        if (worker->thread.joinable()) {
            worker->thread.join();
        }
        totalExecuted += worker->executedJobs.load(std::memory_order_relaxed);
        totalStolen += worker->stolenJobs.load(std::memory_order_relaxed);
    }

    workers_.clear();
    instance_ = nullptr;

    UZ_LOG(Jobs, Info, "JobSystem shutdown: {} jobs executed, {} stolen", totalExecuted, totalStolen);
}

// ─────────────────────────────────────────────────────────────────────────────
// Job submission
// ─────────────────────────────────────────────────────────────────────────────

void JobSystem::Enqueue(Job job) {
    if (workers_.empty()) {
        // No workers — execute inline
        ExecuteJob(job);
        return;
    }

    const auto prioIdx = static_cast<size_t>(job.priority);
    const uint32_t target = nextWorker_.fetch_add(1, std::memory_order_relaxed) %
                            static_cast<uint32_t>(workers_.size());

    if (!workers_[target]->queues[prioIdx].try_push(std::move(job))) {
        // Queue full — execute inline to avoid dropping work
        UZ_LOG(Jobs, Warning, "Job queue full (worker={}, priority={}), executing inline",
               target, prioIdx);
        ExecuteJob(job);
        return;
    }

    pendingJobs_.fetch_add(1, std::memory_order_release);

    // Wake one sleeping worker
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        wakeCV_.notify_one();
    }
}

uint32_t JobSystem::GetWorkerCount() const {
    return static_cast<uint32_t>(workers_.size());
}

bool JobSystem::Yield() {
    // Try all workers' queues
    for (uint32_t i = 0; i < static_cast<uint32_t>(workers_.size()); ++i) {
        if (TryExecuteOne(i)) {
            return true;
        }
    }
    return false;
}

JobSystem* JobSystem::Get() {
    return instance_;
}

// ─────────────────────────────────────────────────────────────────────────────
// Worker thread
// ─────────────────────────────────────────────────────────────────────────────

void JobSystem::WorkerMain(uint32_t workerIndex) {
    UZ_PROFILE_FUNCTION();

    while (running_.load(std::memory_order_acquire)) {
        // Try own queues first
        if (TryExecuteOne(workerIndex)) {
            continue;
        }

        // Try stealing from others
        if (TrySteal(workerIndex)) {
            continue;
        }

        // Nothing to do — sleep until woken
        std::unique_lock<std::mutex> lock(wakeMutex_);
        wakeCV_.wait_for(lock, std::chrono::milliseconds(1), [this] {
            return pendingJobs_.load(std::memory_order_acquire) > 0 ||
                   !running_.load(std::memory_order_acquire);
        });
    }
}

bool JobSystem::TryExecuteOne(uint32_t workerIndex) {
    auto& worker = *workers_[workerIndex];

    // Drain highest priority first
    for (size_t prio = 0; prio < static_cast<size_t>(JobPriority::Count); ++prio) {
        Job job;
        if (worker.queues[prio].try_pop(job)) {
            pendingJobs_.fetch_sub(1, std::memory_order_release);
            ExecuteJob(job);
            worker.executedJobs.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }
    return false;
}

bool JobSystem::TrySteal(uint32_t workerIndex) {
    const auto numWorkers = static_cast<uint32_t>(workers_.size());
    if (numWorkers <= 1) {
        return false;
    }

    // Start at a pseudo-random offset to distribute contention
    // Use workerIndex + a simple counter to vary the starting point
    static thread_local uint32_t stealOffset = 0;
    uint32_t start = (workerIndex + 1 + stealOffset) % numWorkers;
    ++stealOffset;

    for (uint32_t step = 0; step < numWorkers - 1; ++step) {
        uint32_t victim = (start + step) % numWorkers;
        if (victim == workerIndex) {
            continue;
        }

        auto& victimWorker = *workers_[victim];
        for (size_t prio = 0; prio < static_cast<size_t>(JobPriority::Count); ++prio) {
            Job job;
            if (victimWorker.queues[prio].try_pop(job)) {
                pendingJobs_.fetch_sub(1, std::memory_order_release);
                ExecuteJob(job);
                workers_[workerIndex]->stolenJobs.fetch_add(1, std::memory_order_relaxed);
                workers_[workerIndex]->executedJobs.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
    }
    return false;
}

void JobSystem::ExecuteJob(Job& job) {
    if (job.function) {
        job.function(job.data);
    }
    if (job.counter) {
        job.counter->Decrement();
    }
}

} // namespace uz
