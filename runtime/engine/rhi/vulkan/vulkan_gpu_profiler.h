#pragma once

/// Vulkan GPU Timestamp Profiler
///
/// Records GPU timestamps via VkQueryPool for per-pass and per-frame timing.
/// Triple-buffered: frame N writes timestamps, frame N-2 reads results via
/// `vkGetQueryPoolResults`. Results are resolved in milliseconds using the
/// physical device's `timestampPeriod`.
///
/// # Usage
/// ```cpp
/// profiler.BeginFrame(cmdBuf);
/// profiler.BeginTimestamp(cmdBuf, "GBuffer");
/// // ... render GBuffer ...
/// profiler.EndTimestamp(cmdBuf, "GBuffer");
/// profiler.EndFrame(cmdBuf);
/// // ... after fence wait for frame N-2 ...
/// profiler.ResolveFrame();
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Must be used from the render thread only.

#include "../gpu_profiler_types.h"

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <array>
#include <cstdint>
#include <vector>

namespace uz {

class VulkanDevice;

// ─────────────────────────────────────────────────────────────────────────────
// VulkanGpuProfiler
// ─────────────────────────────────────────────────────────────────────────────

/// Records GPU timestamps and resolves them to milliseconds.
///
/// Uses a VkQueryPool with `VK_QUERY_TYPE_TIMESTAMP` queries. Each frame gets
/// its own region of the pool (`kTimestampsPerFrame` queries). Timestamps are
/// written via `vkCmdWriteTimestamp2` (VK_KHR_synchronization2) and read back
/// via `vkGetQueryPoolResults` with `VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT`.
///
/// The query pool is reset per-frame via `vkCmdResetQueryPool` at the start
/// of each frame's recording.
class VulkanGpuProfiler {
public:
    /// Initializes the timestamp query pool.
    ///
    /// Queries `VkPhysicalDeviceProperties::limits::timestampPeriod` and checks
    /// `timestampComputeAndGraphics`. Returns `false` if the GPU does not
    /// support timestamps on the graphics queue.
    ///
    /// # Arguments
    /// * `device` — Parent Vulkan device (must outlive this profiler).
    [[nodiscard]] bool Init(VulkanDevice* device);

    /// Resets the timestamp counter and resets queries for the current frame.
    ///
    /// Must be called once per frame before any `BeginTimestamp()`.
    ///
    /// # Arguments
    /// * `cmdBuf` — Active command buffer to record the query pool reset into.
    void BeginFrame(VkCommandBuffer cmdBuf);

    /// Records a begin timestamp for a named scope.
    ///
    /// Must be paired with a subsequent `EndTimestamp()` call with the same name.
    /// Silently drops timestamps beyond `kMaxGpuTimestamps` per frame.
    ///
    /// # Arguments
    /// * `cmdBuf` — Active command buffer to record the timestamp into.
    /// * `name` — Human-readable scope name (string literal, not copied).
    void BeginTimestamp(VkCommandBuffer cmdBuf, const char* name);

    /// Records an end timestamp for the most recently begun scope.
    ///
    /// # Arguments
    /// * `cmdBuf` — Active command buffer to record the timestamp into.
    /// * `name` — Scope name (kept for symmetry with `BeginTimestamp`).
    void EndTimestamp(VkCommandBuffer cmdBuf, const char* name);

    /// Advances the internal frame index.
    ///
    /// Unlike DX12, Vulkan does not need an explicit resolve command — timestamps
    /// are read directly via `vkGetQueryPoolResults`. This method only rotates
    /// the frame counter.
    void EndFrame();

    /// Reads back resolved timestamps from 2 frames ago and computes millisecond deltas.
    ///
    /// Call after the GPU has completed the frame whose timestamps are being read
    /// (i.e., after a fence wait for frame N-2).
    void ResolveFrame();

    /// Returns the most recent resolved timing results.
    ///
    /// # Returns
    /// A vector of `GpuTimingResult` entries (one per begin/end pair).
    /// Empty if no timestamps were recorded or `ResolveFrame()` has not been called.
    [[nodiscard]] const std::vector<GpuTimingResult>& GetResults() const { return m_results; }

    /// Returns the total GPU frame time spanning all profiled scopes.
    ///
    /// # Returns
    /// Time in milliseconds from the first `BeginTimestamp` to the last `EndTimestamp`.
    /// Returns `0.0f` if no timestamps were resolved.
    [[nodiscard]] float GetFrameTimeMs() const { return m_frameTimeMs; }

private:
    /// Total timestamp queries per frame (2 per scope: begin + end).
    static constexpr uint32_t kTimestampsPerFrame = kMaxGpuTimestamps * 2;

    VulkanDevice* m_device = nullptr;
    VkQueryPool   m_queryPool = VK_NULL_HANDLE;

    /// Nanoseconds per GPU timestamp tick.
    float    m_timestampPeriodNs = 0.0F;
    uint32_t m_currentFrame = 0;
    uint32_t m_timestampCount = 0;

    /// Names for each timestamp pair, indexed by pair index.
    std::array<const char*, kMaxGpuTimestamps> m_names = {};

    /// Per-frame timestamp count, so ResolveFrame knows how many to read.
    std::array<uint32_t, kProfilerFramesInFlight> m_frameCounts = {};

    std::vector<GpuTimingResult> m_results;
    float m_frameTimeMs = 0.0F;
};

} // namespace uz
