#pragma once

/// UzEngine Parallel Command Recorder — Thread-safe multi-command-list recording.
///
/// Provides a pool of command lists (one per recording thread) and supports
/// recording independent render passes on separate threads. Recorded command
/// lists are collected and submitted in acquisition order on the main render
/// thread.
///
/// # Design
/// - Each slot owns one `IRHICommandList` with its own command allocator
/// - Command lists are recorded independently, then submitted in order
/// - Slots are pre-allocated at init for a fixed maximum thread count
///
/// # Thread Safety
/// `AcquireCommandList()` and `ReleaseCommandList()` are thread-safe.
/// `BeginFrame()` and `SubmitAll()` must be called from the main render thread
/// only, with no active acquisitions outstanding.
///
/// # Usage
/// ```cpp
/// ParallelCommandRecorder recorder;
/// recorder.Init(device, {.maxThreads = 4});
///
/// // Each frame:
/// recorder.BeginFrame();
///
/// // On worker threads:
/// auto* cmd = recorder.AcquireCommandList();
/// cmd->BeginRenderPass(desc);
/// cmd->SetPipeline(pso);
/// cmd->DrawIndexed(36);
/// cmd->EndRenderPass();
/// cmd->End();
/// recorder.ReleaseCommandList(cmd);
///
/// // On main thread, after all workers finish:
/// recorder.SubmitAll();
/// ```

#include "rhi.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace uz {

/// Thread contention statistics for parallel command list acquisition.
///
/// Tracks how often threads compete for the mutex in `AcquireCommandList()`,
/// which indicates whether the pool has enough slots for the workload.
struct ContentionStats {
    uint32_t acquisitionAttempts = 0;   ///< Total AcquireCommandList calls.
    uint32_t contentionEvents = 0;      ///< Times a thread had to wait (try_lock failed).
    uint32_t maxConcurrentWorkers = 0;  ///< Peak number of simultaneously acquired slots.
    float avgAcquireTimeUs = 0.0f;      ///< Average time to acquire a slot, in microseconds.
    float maxAcquireTimeUs = 0.0f;      ///< Worst-case acquire time, in microseconds.
};

/// Configuration for the parallel command recorder.
struct ParallelRecorderConfig {
    /// Maximum number of concurrent recording threads.
    /// Each slot creates one `IRHICommandList` at init time.
    uint32_t maxThreads = 4;

    /// Queue type for all command lists in the pool.
    RHIQueueType queueType = RHIQueueType::Graphics;
};

/// Manages parallel command list recording across multiple threads.
///
/// Pre-allocates a fixed pool of command lists at init. Worker threads
/// acquire a list, record GPU commands, call `End()`, then release.
/// The main thread calls `SubmitAll()` to execute all recorded lists
/// in acquisition order.
///
/// # Ownership
/// The recorder owns all command lists. They are created on `Init()`
/// and destroyed on `Shutdown()`. The caller must not destroy them.
///
/// # Ordering
/// Command lists are executed in the order they were acquired (not
/// released). This ensures deterministic rendering order when passes
/// are distributed across threads by priority.
class ParallelCommandRecorder {
public:
    ParallelCommandRecorder() = default;
    ~ParallelCommandRecorder();

    ParallelCommandRecorder(const ParallelCommandRecorder&) = delete;
    ParallelCommandRecorder& operator=(const ParallelCommandRecorder&) = delete;
    ParallelCommandRecorder(ParallelCommandRecorder&&) = delete;
    ParallelCommandRecorder& operator=(ParallelCommandRecorder&&) = delete;

    /// Initializes the recorder, creating `config.maxThreads` command lists.
    ///
    /// # Arguments
    /// * `device` — RHI device used to create command lists. Must outlive the recorder.
    /// * `config` — Pool configuration (thread count, queue type).
    ///
    /// # Returns
    /// `true` on success, `false` if any command list creation fails.
    /// On failure, all successfully created lists are cleaned up.
    bool Init(IRHIDevice* device, const ParallelRecorderConfig& config = {});

    /// Destroys all command lists and resets state.
    void Shutdown();

    /// Begins a new recording frame. Resets all slots and calls `Begin()`
    /// on each command list so they are ready for recording.
    ///
    /// Must be called from the main render thread with no outstanding
    /// acquired command lists.
    void BeginFrame();

    /// Acquires a command list for recording on the calling thread.
    ///
    /// The returned list has already had `Begin()` called (during `BeginFrame()`).
    /// The caller records commands, then calls `End()` on the list before
    /// releasing it.
    ///
    /// Thread-safe. Multiple threads may call this concurrently.
    ///
    /// # Returns
    /// A valid `IRHICommandList*`, or `nullptr` if all slots are in use.
    [[nodiscard]] IRHICommandList* AcquireCommandList();

    /// Releases a command list back to the pool after recording.
    ///
    /// The caller must have called `End()` on the command list before
    /// releasing it. Thread-safe.
    ///
    /// # Arguments
    /// * `cmdList` — A pointer previously returned by `AcquireCommandList()`.
    ///   Passing `nullptr` or an unknown pointer logs a warning and returns.
    void ReleaseCommandList(IRHICommandList* cmdList);

    /// Submits all recorded command lists in acquisition order.
    ///
    /// Calls `Execute()` on each command list that was acquired and released
    /// during this frame. Must be called from the main render thread after
    /// all worker threads have released their command lists.
    ///
    /// # Returns
    /// The number of command lists submitted.
    uint32_t SubmitAll();

    /// Returns the number of command lists currently acquired (not yet released).
    [[nodiscard]] uint32_t GetActiveCount() const;

    /// Returns the total number of command list slots in the pool.
    [[nodiscard]] uint32_t GetMaxSlots() const { return m_config.maxThreads; }

    /// Returns true if the recorder has been initialized.
    [[nodiscard]] bool IsInitialized() const { return m_device != nullptr; }

    /// Returns a snapshot of thread contention statistics since last reset.
    [[nodiscard]] ContentionStats GetContentionStats() const;

    /// Resets all contention counters to zero.
    void ResetContentionStats();

private:
    /// Tracks the state of a single command list slot in the pool.
    struct CommandListSlot {
        IRHICommandList* cmdList = nullptr;  ///< Owned command list.
        bool acquired = false;               ///< True while a thread holds this slot.
        bool recorded = false;               ///< True after the thread released (End() called).
        uint32_t acquisitionOrder = 0;       ///< Order in which this slot was acquired.
    };

    IRHIDevice* m_device = nullptr;
    ParallelRecorderConfig m_config;
    std::vector<CommandListSlot> m_slots;
    mutable std::mutex m_mutex;
    uint32_t m_nextOrder = 0;  ///< Monotonic counter for acquisition ordering.

    // Contention tracking — updated atomically since AcquireCommandList is hot.
    std::atomic<uint32_t> m_acquisitionAttempts{0};
    std::atomic<uint32_t> m_contentionEvents{0};
    std::atomic<uint32_t> m_currentWorkers{0};
    std::atomic<uint32_t> m_peakWorkers{0};
    std::atomic<float> m_totalAcquireTimeUs{0.0f};
    std::atomic<float> m_maxAcquireTimeUs{0.0f};
};

} // namespace uz
