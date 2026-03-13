/// Vulkan GPU Timestamp Profiler Implementation
///
/// Uses VkQueryPool (TIMESTAMP) to measure GPU pass timing. Triple-buffered:
/// each frame writes to its own region of the query pool. Results are read
/// back via vkGetQueryPoolResults for frame N-2.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>

#include "vulkan_gpu_profiler.h"
#include "vulkan_device.h"

#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

/// Creates the timestamp query pool and queries GPU timestamp properties.
bool VulkanGpuProfiler::Init(VulkanDevice* device) {
    UZ_PROFILE_FUNCTION();
    m_device = device;

    VkPhysicalDeviceProperties props = {};
    vkGetPhysicalDeviceProperties(device->GetVkPhysicalDevice(), &props);

    m_timestampPeriodNs = props.limits.timestampPeriod;
    if (m_timestampPeriodNs == 0.0F) {
        UZ_LOG(RHI, Warning, "GPU timestamps not supported (timestampPeriod == 0)");
        return false;
    }

    if (!props.limits.timestampComputeAndGraphics) {
        UZ_LOG(RHI, Warning, "GPU timestamps not supported on all queue families");
        // Continue anyway — graphics queue typically supports timestamps even
        // when this flag is false.
    }

    // Create query pool for all frames
    VkQueryPoolCreateInfo poolCI = {};
    poolCI.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    poolCI.queryType = VK_QUERY_TYPE_TIMESTAMP;
    poolCI.queryCount = kTimestampsPerFrame * kProfilerFramesInFlight;

    VkResult result = vkCreateQueryPool(device->GetVkDevice(), &poolCI, nullptr, &m_queryPool);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "Failed to create timestamp query pool, result={}",
               static_cast<int>(result));
        return false;
    }

    m_frameCounts.fill(0);

    UZ_LOG(RHI, Info, "Vulkan GPU profiler initialized: timestampPeriod={:.2f}ns, maxScopes={}/frame",
           m_timestampPeriodNs, kMaxGpuTimestamps);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-Frame Recording
// ─────────────────────────────────────────────────────────────────────────────

/// Resets timestamp counter and query pool region for the current frame.
void VulkanGpuProfiler::BeginFrame(VkCommandBuffer cmdBuf) {
    m_timestampCount = 0;

    // Reset this frame's query region so timestamps are valid
    uint32_t baseQuery = m_currentFrame * kTimestampsPerFrame;
    vkCmdResetQueryPool(cmdBuf, m_queryPool, baseQuery, kTimestampsPerFrame);
}

/// Records a begin timestamp at the current pipeline stage.
void VulkanGpuProfiler::BeginTimestamp(VkCommandBuffer cmdBuf, const char* name) {
    if (m_timestampCount >= kMaxGpuTimestamps) return;

    m_names[m_timestampCount] = name;

    uint32_t baseQuery = m_currentFrame * kTimestampsPerFrame;
    uint32_t queryIdx = baseQuery + (m_timestampCount * 2);

    // VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT waits for all prior work to complete
    vkCmdWriteTimestamp2(cmdBuf, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queryPool, queryIdx);
}

/// Records an end timestamp for the most recently begun scope.
void VulkanGpuProfiler::EndTimestamp(VkCommandBuffer cmdBuf, const char* /*name*/) {
    if (m_timestampCount >= kMaxGpuTimestamps) return;

    uint32_t baseQuery = m_currentFrame * kTimestampsPerFrame;
    uint32_t queryIdx = baseQuery + (m_timestampCount * 2) + 1;

    vkCmdWriteTimestamp2(cmdBuf, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, m_queryPool, queryIdx);

    m_timestampCount++;
}

/// Stores this frame's count and advances the frame index.
void VulkanGpuProfiler::EndFrame() {
    UZ_PROFILE_FUNCTION();
    m_frameCounts[m_currentFrame] = m_timestampCount;
    m_currentFrame = (m_currentFrame + 1) % kProfilerFramesInFlight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Readback & Resolution
// ─────────────────────────────────────────────────────────────────────────────

/// Reads timestamps from frame N-2 and computes millisecond deltas.
void VulkanGpuProfiler::ResolveFrame() {
    UZ_PROFILE_FUNCTION();
    m_results.clear();
    m_frameTimeMs = 0.0F;

    if (m_queryPool == VK_NULL_HANDLE || m_timestampPeriodNs == 0.0F) return;

    // Read from the oldest frame (currentFrame was already advanced in EndFrame)
    uint32_t readFrame = m_currentFrame;
    uint32_t readCount = m_frameCounts[readFrame];
    if (readCount == 0) return;

    uint32_t baseQuery = readFrame * kTimestampsPerFrame;
    uint32_t queryCount = readCount * 2;

    // Read timestamps directly from the query pool
    std::array<uint64_t, kTimestampsPerFrame> timestamps = {};
    VkResult result = vkGetQueryPoolResults(
        m_device->GetVkDevice(), m_queryPool,
        baseQuery, queryCount,
        queryCount * sizeof(uint64_t), timestamps.data(),
        sizeof(uint64_t),
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Warning, "vkGetQueryPoolResults failed, result={}",
               static_cast<int>(result));
        return;
    }

    // Convert ticks to milliseconds: ms = ticks * timestampPeriod(ns) / 1,000,000
    double nsToMs = static_cast<double>(m_timestampPeriodNs) / 1'000'000.0;
    uint64_t frameBegin = UINT64_MAX;
    uint64_t frameEnd = 0;

    for (uint32_t i = 0; i < readCount; ++i) {
        uint64_t begin = timestamps[i * 2];
        uint64_t end   = timestamps[i * 2 + 1];

        float ms = static_cast<float>(static_cast<double>(end - begin) * nsToMs);
        m_results.push_back({m_names[i], ms});

        if (begin < frameBegin) frameBegin = begin;
        if (end > frameEnd) frameEnd = end;
    }

    if (frameBegin < frameEnd) {
        m_frameTimeMs = static_cast<float>(static_cast<double>(frameEnd - frameBegin) * nsToMs);
    }
}

} // namespace uz
