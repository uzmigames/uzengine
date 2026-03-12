/// Vulkan Fence Implementation
///
/// Uses VK_KHR_timeline_semaphore for native timeline fence support.
/// Signal submits a timeline semaphore signal via vkQueueSubmit2.
/// WaitCPU uses vkWaitSemaphores with a timeout.
/// IsComplete/GetCompletedValue use vkGetSemaphoreCounterValue.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <volk.h>

#include "vulkan_fence.h"
#include "vulkan_device.h"

#include "log.h"
#include "profiler.h"

namespace uz {

VulkanFence::~VulkanFence() {
    if (m_device && m_timelineSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(m_device->GetVkDevice(), m_timelineSemaphore, nullptr);
    }
}

bool VulkanFence::Init(VulkanDevice* device) {
    UZ_PROFILE_FUNCTION();
    if (!device) {
        UZ_LOG(RHI, Error, "VulkanFence::Init -- null device");
        return false;
    }

    m_device = device;

    // Create timeline semaphore with initial value 0
    VkSemaphoreTypeCreateInfo timelineCI = {};
    timelineCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO;
    timelineCI.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
    timelineCI.initialValue = 0;

    VkSemaphoreCreateInfo semCI = {};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    semCI.pNext = &timelineCI;

    VkResult result = vkCreateSemaphore(device->GetVkDevice(), &semCI, nullptr, &m_timelineSemaphore);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateSemaphore (timeline) failed, result={}", static_cast<int>(result));
        return false;
    }

    UZ_LOG(RHI, Trace, "VulkanFence created (timeline semaphore)");
    return true;
}

// -----------------------------------------------------------------------------
// IRHIFence implementation
// -----------------------------------------------------------------------------

void VulkanFence::Signal(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    if (!m_device || m_timelineSemaphore == VK_NULL_HANDLE) return;

    // Submit an empty command with a timeline semaphore signal
    VkTimelineSemaphoreSubmitInfo timelineInfo = {};
    timelineInfo.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
    timelineInfo.signalSemaphoreValueCount = 1;
    timelineInfo.pSignalSemaphoreValues = &value;

    VkSubmitInfo submitInfo = {};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.pNext = &timelineInfo;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_timelineSemaphore;

    VkResult result = vkQueueSubmit(m_device->GetGraphicsQueue(), 1, &submitInfo, VK_NULL_HANDLE);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "VulkanFence::Signal -- vkQueueSubmit failed, result={}", static_cast<int>(result));
    }
}

void VulkanFence::WaitCPU(uint64_t value) {
    UZ_PROFILE_FUNCTION();
    if (!m_device || m_timelineSemaphore == VK_NULL_HANDLE) return;

    VkSemaphoreWaitInfo waitInfo = {};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
    waitInfo.semaphoreCount = 1;
    waitInfo.pSemaphores = &m_timelineSemaphore;
    waitInfo.pValues = &value;

    // 5 second timeout to avoid infinite hangs
    constexpr uint64_t kTimeoutNs = 5'000'000'000ULL;
    VkResult result = vkWaitSemaphores(m_device->GetVkDevice(), &waitInfo, kTimeoutNs);
    if (result == VK_TIMEOUT) {
        UZ_LOG(RHI, Error, "VulkanFence::WaitCPU -- timed out waiting for value {}", value);
    } else if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "VulkanFence::WaitCPU -- vkWaitSemaphores failed, result={}", static_cast<int>(result));
    }
}

bool VulkanFence::IsComplete(uint64_t value) const {
    if (!m_device || m_timelineSemaphore == VK_NULL_HANDLE) return false;

    uint64_t completedValue = 0;
    VkResult result = vkGetSemaphoreCounterValue(m_device->GetVkDevice(), m_timelineSemaphore, &completedValue);
    if (result != VK_SUCCESS) return false;
    return completedValue >= value;
}

uint64_t VulkanFence::GetCompletedValue() const {
    if (!m_device || m_timelineSemaphore == VK_NULL_HANDLE) return 0;

    uint64_t completedValue = 0;
    VkResult result = vkGetSemaphoreCounterValue(m_device->GetVkDevice(), m_timelineSemaphore, &completedValue);
    if (result != VK_SUCCESS) return 0;
    return completedValue;
}

} // namespace uz
