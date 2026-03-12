#pragma once

/// Vulkan backend implementation of IRHIFence using VK_KHR_timeline_semaphore.
///
/// Timeline semaphores provide a direct 1:1 mapping to IRHIFence's monotonic
/// fence value model. Unlike DX11's emulated queries, this is a native hardware
/// primitive with zero CPU overhead for polling.
///
/// # Thread Safety
/// All methods are thread-safe (Vulkan timeline semaphore operations are atomic).

#include "../rhi.h"

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>

namespace uz {

class VulkanDevice;

// -----------------------------------------------------------------------------
// VulkanFence
// -----------------------------------------------------------------------------

/// Vulkan implementation of IRHIFence using timeline semaphores.
///
/// Timeline semaphores (VK_KHR_timeline_semaphore) are the Vulkan equivalent
/// of D3D12 fences -- monotonically increasing 64-bit counters that the GPU
/// signals and the CPU can wait on or poll.
class VulkanFence final : public IRHIFence {
public:
    VulkanFence() = default;
    ~VulkanFence() override;

    /// Creates the timeline semaphore with initial value 0.
    ///
    /// # Arguments
    /// * `device` -- Parent Vulkan device (must outlive this fence).
    ///
    /// # Returns
    /// `true` on success, `false` on failure.
    bool Init(VulkanDevice* device);

    // -- IRHIFence overrides --

    /// Signals the fence by submitting a timeline semaphore signal to the GPU queue.
    void Signal(uint64_t value) override;

    /// Blocks the calling CPU thread until the timeline semaphore reaches the given value.
    void WaitCPU(uint64_t value) override;

    /// Returns true if the timeline semaphore has reached or passed the given value.
    bool IsComplete(uint64_t value) const override;

    /// Returns the current timeline semaphore counter value.
    uint64_t GetCompletedValue() const override;

private:
    VulkanDevice*    m_device = nullptr;
    VkSemaphore      m_timelineSemaphore = VK_NULL_HANDLE;
};

} // namespace uz
