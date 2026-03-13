/// UzEngine GPU Defragmentation Pass — Incremental memory compaction.
///
/// Runs once per frame with a configurable byte budget. Identifies fragmented
/// regions and relocates resources to reduce gaps. Only processes a limited
/// amount per frame to avoid stalls.
///
/// # Design
/// - Tracks a "fragmentation score" (ratio of wasted space to total allocated)
/// - Each frame, selects up to `maxBytesPerFrame` of resources to relocate
/// - Resources are copied via the copy queue, then old allocations freed
/// - Actual GPU copies require RHI support (copy queue + resource aliasing)
///
/// # Thread Safety
/// NOT thread-safe. Render thread only.

#pragma once

#include <cstdint>
#include <functional>

namespace uz {

class IRHIDevice;

/// Configuration for the defrag pass.
struct GpuDefragConfig {
    uint64_t maxBytesPerFrame = 4 * 1024 * 1024;  ///< Max bytes to relocate per frame (4 MB default).
    float fragmentationThreshold = 0.15f;           ///< Min fragmentation ratio to trigger defrag (15%).
    bool enabled = true;                            ///< Master enable/disable.
};

/// Statistics from a defrag pass execution.
struct GpuDefragStats {
    uint64_t bytesRelocated = 0;         ///< Bytes moved this frame.
    uint32_t resourcesRelocated = 0;     ///< Number of resources moved.
    float fragmentationRatio = 0.0f;     ///< Current fragmentation estimate.
    bool didRun = false;                 ///< Whether defrag actually ran this frame.
};

/// Callback for when a resource is relocated (old handle -> new handle).
///
/// The caller must update all references to the resource after relocation.
///
/// # Arguments
/// * `resourceKey` — Opaque key identifying the resource being moved.
/// * `oldOffset` — Previous byte offset of the resource in the heap.
/// * `newOffset` — New byte offset after compaction.
using DefragRelocateCallback = std::function<void(uint64_t resourceKey, uint64_t oldOffset, uint64_t newOffset)>;

/// Incremental GPU memory defragmentation pass.
///
/// Monitors allocation fragmentation each frame and, when the fragmentation
/// ratio exceeds a configurable threshold, begins incremental compaction.
/// Currently operates in monitoring-only mode — actual GPU resource relocation
/// requires deeper RHI integration (copy queue aliasing, resource recreation).
///
/// # Lifecycle
/// 1. `Init()` — stores device pointer and configuration
/// 2. `UpdateFragmentationState()` — call each frame with current allocator stats
/// 3. `Execute()` — runs the defrag pass (monitoring + future compaction)
/// 4. `Shutdown()` — clears all state
///
/// # Thread Safety
/// NOT thread-safe. All methods must be called from the render thread.
class GpuDefragPass {
public:
    GpuDefragPass() = default;
    ~GpuDefragPass() = default;

    GpuDefragPass(const GpuDefragPass&) = delete;
    GpuDefragPass& operator=(const GpuDefragPass&) = delete;
    GpuDefragPass(GpuDefragPass&&) = delete;
    GpuDefragPass& operator=(GpuDefragPass&&) = delete;

    /// Initializes the defrag pass with a device and optional configuration.
    ///
    /// # Arguments
    /// * `device` — RHI device pointer (must remain valid for the lifetime of this object).
    /// * `config` — Defrag configuration. Uses defaults if omitted.
    ///
    /// # Returns
    /// `true` on success, `false` if `device` is null.
    bool Init(IRHIDevice* device, const GpuDefragConfig& config = {});

    /// Shuts down the defrag pass and clears all internal state.
    void Shutdown();

    /// Sets the relocation callback invoked when a resource is moved.
    ///
    /// # Arguments
    /// * `callback` — Function called with (resourceKey, oldOffset, newOffset).
    ///   Pass an empty function to clear the callback.
    void SetRelocateCallback(DefragRelocateCallback callback);

    /// Updates the fragmentation tracking with current allocation state.
    ///
    /// Call once per frame before `Execute()` with the latest allocator statistics.
    ///
    /// # Arguments
    /// * `totalAllocated` — Total bytes committed from the allocator (including gaps).
    /// * `totalUsed` — Total bytes actually occupied by live resources.
    /// * `freeGapCount` — Number of free gaps (fragmentation segments) in the heap.
    void UpdateFragmentationState(uint64_t totalAllocated, uint64_t totalUsed, uint32_t freeGapCount);

    /// Runs the defrag pass for this frame.
    ///
    /// If disabled or fragmentation is below threshold, sets `didRun = false` and
    /// returns immediately. Otherwise logs the fragmentation state and returns
    /// stats. Actual resource relocation is not yet implemented.
    ///
    /// # Returns
    /// Statistics about what was done (or not done) this frame.
    [[nodiscard]] GpuDefragStats Execute();

    /// Returns the current configuration (mutable for runtime tweaking).
    GpuDefragConfig& GetConfig() { return m_config; }

    /// Returns the current configuration (read-only).
    [[nodiscard]] const GpuDefragConfig& GetConfig() const { return m_config; }

    /// Returns the last frame's defrag statistics.
    [[nodiscard]] const GpuDefragStats& GetLastStats() const { return m_lastStats; }

    /// Returns true if the pass has been initialized with a valid device.
    [[nodiscard]] bool IsInitialized() const { return m_device != nullptr; }

private:
    IRHIDevice* m_device = nullptr;
    GpuDefragConfig m_config;
    GpuDefragStats m_lastStats;
    DefragRelocateCallback m_relocateCallback;

    // Fragmentation tracking — updated each frame via UpdateFragmentationState()
    uint64_t m_totalAllocated = 0;
    uint64_t m_totalUsed = 0;
    uint32_t m_freeGapCount = 0;
};

} // namespace uz
