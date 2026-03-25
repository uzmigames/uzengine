/// UzEngine GPU Memory Tracker — Per-category allocation tracking and budget enforcement.
///
/// Tracks GPU memory allocations by category (textures, buffers, staging, render targets),
/// fires callbacks when budgets are exceeded, and provides telemetry for profiling.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.
///
/// # Usage
/// ```cpp
/// GpuMemoryTracker tracker;
/// tracker.Init(device);
/// tracker.SetBudget(GpuMemoryCategory::Textures, 512 * 1024 * 1024);
///
/// tracker.TrackAllocation(GpuMemoryCategory::Textures, textureHandle, sizeBytes);
/// tracker.TrackFree(GpuMemoryCategory::Textures, textureHandle);
///
/// tracker.SetPressureCallback([](GpuMemoryCategory cat, uint64_t over) {
///     // evict LRU resources from this category
/// });
/// ```

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <functional>
#include <unordered_map>

namespace uz {

class IRHIDevice;

// ─────────────────────────────────────────────────────────────────────────────
// GpuMemoryCategory — Budget categories for GPU memory tracking
// ─────────────────────────────────────────────────────────────────────────────

/// Categories for GPU memory budget tracking.
///
/// Each category has independent budget limits, allocation counters,
/// and peak-usage tracking. Categories are mutually exclusive — every
/// allocation belongs to exactly one category.
enum class GpuMemoryCategory : uint8_t {
    Textures,       ///< Color textures, normal maps, cubemaps, etc.
    RenderTargets,  ///< GBuffer surfaces, shadow maps, post-process targets
    Buffers,        ///< Vertex, index, constant, and structured buffers
    Staging,        ///< Upload-heap and readback staging buffers
    Count           ///< Total number of categories. Not a valid category.
};

/// Returns a human-readable name for a GPU memory category.
///
/// # Returns
/// A null-terminated string literal (e.g., "Textures", "Buffers").
/// Returns "Unknown" for out-of-range values.
[[nodiscard]] const char* GpuMemoryCategoryName(GpuMemoryCategory cat);

// ─────────────────────────────────────────────────────────────────────────────
// GpuMemoryCategoryStats — Per-category memory statistics
// ─────────────────────────────────────────────────────────────────────────────

/// Per-category memory statistics snapshot.
///
/// All byte counters are in bytes. `totalAllocatedBytes` and `totalFreedBytes`
/// are monotonically increasing lifetime counters.
struct GpuMemoryCategoryStats {
    uint64_t usedBytes = 0;           ///< Current bytes allocated in this category
    uint64_t budgetBytes = 0;         ///< Soft budget limit (0 = unlimited)
    uint32_t allocationCount = 0;     ///< Number of live allocations
    uint64_t peakUsedBytes = 0;       ///< High-water mark for usedBytes
    uint64_t totalAllocatedBytes = 0; ///< Lifetime total bytes allocated (monotonic)
    uint64_t totalFreedBytes = 0;     ///< Lifetime total bytes freed (monotonic)
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuMemorySnapshot — Full GPU memory state at a point in time
// ─────────────────────────────────────────────────────────────────────────────

/// Snapshot of all GPU memory stats, including adapter-reported budget.
///
/// Produced by `GpuMemoryTracker::TakeSnapshot()`. Includes both per-category
/// engine-tracked stats and adapter-level budget/usage from the RHI device.
struct GpuMemorySnapshot {
    /// Per-category stats indexed by `static_cast<size_t>(GpuMemoryCategory)`.
    GpuMemoryCategoryStats categories[static_cast<size_t>(GpuMemoryCategory::Count)];
    uint64_t totalUsedBytes = 0;     ///< Sum of all categories' usedBytes
    uint64_t deviceBudgetBytes = 0;  ///< Adapter-reported total budget
    uint64_t deviceUsageBytes = 0;   ///< Adapter-reported current usage
    float budgetUtilization = 0.0f;  ///< deviceUsageBytes / deviceBudgetBytes (0..1+)
};

// ─────────────────────────────────────────────────────────────────────────────
// GpuMemoryPressureCallback
// ─────────────────────────────────────────────────────────────────────────────

/// Callback fired when a category exceeds its budget.
///
/// # Parameters
/// - `category` — The category that is over budget.
/// - `overBudgetBytes` — How many bytes over budget (usedBytes - budgetBytes).
using GpuMemoryPressureCallback =
    std::function<void(GpuMemoryCategory category, uint64_t overBudgetBytes)>;

// ─────────────────────────────────────────────────────────────────────────────
// GpuMemoryTracker — Main tracking class
// ─────────────────────────────────────────────────────────────────────────────

/// Tracks GPU memory allocations by category with budget enforcement.
///
/// Designed for single-threaded use on the render thread. Stores allocation
/// entries keyed by a caller-provided uint64_t (typically a resource handle
/// or pointer cast). Supports per-category soft budgets with pressure callbacks.
///
/// # Lifecycle
/// 1. Construct, call `Init(device)`.
/// 2. Set budgets via `SetBudget()`, register callback via `SetPressureCallback()`.
/// 3. Call `TrackAllocation()`/`TrackFree()` as resources are created/destroyed.
/// 4. Call `CheckPressure()` once per frame.
/// 5. Call `Shutdown()` before destroying the device.
class GpuMemoryTracker {
public:
    GpuMemoryTracker() = default;
    ~GpuMemoryTracker() = default;

    // Non-copyable, non-movable — singleton-like usage on render thread.
    GpuMemoryTracker(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker& operator=(const GpuMemoryTracker&) = delete;
    GpuMemoryTracker(GpuMemoryTracker&&) = delete;
    GpuMemoryTracker& operator=(GpuMemoryTracker&&) = delete;

    /// Initializes the tracker with a device for adapter budget queries.
    ///
    /// # Parameters
    /// - `device` — Non-null pointer to the RHI device. Lifetime must exceed
    ///   the tracker's (caller retains ownership).
    ///
    /// # Returns
    /// `true` on success, `false` if `device` is null.
    [[nodiscard]] bool Init(IRHIDevice* device);

    /// Shuts down and clears all tracking data.
    ///
    /// Logs a warning if any allocations are still live (potential leak).
    void Shutdown();

    /// Sets the soft budget for a category in bytes.
    ///
    /// # Parameters
    /// - `category` — Which category to budget.
    /// - `budgetBytes` — Budget in bytes. Pass 0 to disable the budget (unlimited).
    void SetBudget(GpuMemoryCategory category, uint64_t budgetBytes);

    /// Records a new GPU allocation in a category.
    ///
    /// # Parameters
    /// - `category` — Which category this allocation belongs to.
    /// - `key` — Unique identifier (e.g., resource handle bits or pointer).
    ///   Must not already be tracked in this category.
    /// - `sizeBytes` — Size of the allocation in bytes. Must be > 0.
    ///
    /// # Side Effects
    /// Increments `GPU.MemoryAllocations` stat counter. Logs a warning if
    /// `key` is already tracked (duplicate allocation).
    void TrackAllocation(GpuMemoryCategory category, uint64_t key, uint64_t sizeBytes);

    /// Records a GPU deallocation.
    ///
    /// # Parameters
    /// - `category` — Which category the allocation was in.
    /// - `key` — The same key passed to `TrackAllocation`.
    ///
    /// # Side Effects
    /// Increments `GPU.MemoryFrees` stat counter. Logs a warning if
    /// `key` is not found (double-free or wrong category).
    void TrackFree(GpuMemoryCategory category, uint64_t key);

    /// Returns current stats for a single category.
    ///
    /// # Returns
    /// A copy of the category's stats. Safe to call at any time after Init.
    [[nodiscard]] GpuMemoryCategoryStats GetCategoryStats(GpuMemoryCategory category) const;

    /// Takes a full snapshot of all memory stats, including an adapter budget query.
    ///
    /// # Returns
    /// A `GpuMemorySnapshot` with per-category stats and device-level budget info.
    ///
    /// # Side Effects
    /// Calls `IRHIDevice::GetMemoryBudget()` which may be a slow OS/driver query.
    [[nodiscard]] GpuMemorySnapshot TakeSnapshot() const;

    /// Sets the callback fired when any category exceeds its budget.
    ///
    /// # Parameters
    /// - `callback` — The function to call. Pass an empty function to clear.
    void SetPressureCallback(GpuMemoryPressureCallback callback);

    /// Checks all categories against budgets and fires pressure callbacks.
    ///
    /// Should be called once per frame. Only fires if a pressure callback is set
    /// and a category's `usedBytes` exceeds its `budgetBytes`.
    ///
    /// # Side Effects
    /// Increments `GPU.MemoryPressureEvents` for each over-budget category.
    void CheckPressure();

    /// Logs a summary of all category stats at Info level.
    ///
    /// Includes used/budget/count/peak for each category plus device totals.
    void LogSummary() const;

    /// Returns true if the tracker has been initialized via `Init()`.
    [[nodiscard]] bool IsInitialized() const noexcept { return m_device != nullptr; }

private:
    /// Internal record for a single tracked allocation.
    struct AllocationEntry {
        uint64_t sizeBytes = 0;
    };

    /// All data for one category: stats + live allocation map.
    struct CategoryData {
        GpuMemoryCategoryStats stats;
        std::unordered_map<uint64_t, AllocationEntry> allocations;
    };

    IRHIDevice* m_device = nullptr;
    CategoryData m_categories[static_cast<size_t>(GpuMemoryCategory::Count)];
    GpuMemoryPressureCallback m_pressureCallback;
};

} // namespace uz
