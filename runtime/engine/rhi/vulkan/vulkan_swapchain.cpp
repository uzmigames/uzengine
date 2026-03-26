/// Vulkan Swapchain Implementation
///
/// VkSwapchainKHR with mailbox presentation, triple-buffering, and resize
/// handling. Swapchain images are registered in the device texture pool
/// as non-owning textures (swapchain owns the VkImage lifetime).

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined(__linux__)
#define VK_USE_PLATFORM_XLIB_KHR
#include <X11/Xlib.h>
#endif
#include <volk.h>

#include "vulkan_swapchain.h"
#include "vulkan_device.h"

#include "log.h"
#include "profiler.h"

#ifdef _WIN32
#include <windows.h>
#endif

#include <algorithm>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIFormat to VkFormat for swapchain creation.
static VkFormat ToVkSwapChainFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::RGBA8Unorm:  return VK_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:   return VK_FORMAT_R8G8B8A8_SRGB;
        case RHIFormat::BGRA8Unorm:  return VK_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:   return VK_FORMAT_B8G8R8A8_SRGB;
        case RHIFormat::RGBA16Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
        default:                     return VK_FORMAT_B8G8R8A8_UNORM;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

VulkanSwapChain::~VulkanSwapChain() {
    if (!m_device) return;

    VkDevice device = m_device->GetVkDevice();

    DestroySwapChainResources();

    if (m_swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    }
    if (m_imageAvailableSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, m_imageAvailableSemaphore, nullptr);
    }
    if (m_renderFinishedSemaphore != VK_NULL_HANDLE) {
        vkDestroySemaphore(device, m_renderFinishedSemaphore, nullptr);
    }
    if (m_surface != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(m_device->GetVkInstance(), m_surface, nullptr);
    }

    UZ_LOG(RHI, Info, "VulkanSwapChain destroyed");
}

bool VulkanSwapChain::Init(VulkanDevice* device, const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!device || !desc.windowHandle) {
        UZ_LOG(RHI, Error, "VulkanSwapChain::Init — null device or window handle");
        return false;
    }

    m_device = device;
    m_width = desc.width;
    m_height = desc.height;
    m_format = desc.format;
    m_vkFormat = ToVkSwapChainFormat(desc.format);

    // Create surface
#ifdef _WIN32
    VkWin32SurfaceCreateInfoKHR surfaceCI = {};
    surfaceCI.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR;
    surfaceCI.hwnd = static_cast<HWND>(desc.windowHandle);
    surfaceCI.hinstance = GetModuleHandle(nullptr);

    VkResult surfaceResult = vkCreateWin32SurfaceKHR(device->GetVkInstance(), &surfaceCI, nullptr, &m_surface);
    if (surfaceResult != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateWin32SurfaceKHR failed, result={}", static_cast<int>(surfaceResult));
        return false;
    }
#elif defined(__linux__)
    if (!desc.displayHandle) {
        UZ_LOG(RHI, Error, "VulkanSwapChain::Init — null display handle (X11 Display*)");
        return false;
    }

    VkXlibSurfaceCreateInfoKHR surfaceCI = {};
    surfaceCI.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR;
    surfaceCI.dpy = static_cast<Display*>(desc.displayHandle);
    surfaceCI.window = reinterpret_cast<::Window>(desc.windowHandle);

    VkResult result = vkCreateXlibSurfaceKHR(device->GetVkInstance(), &surfaceCI, nullptr, &m_surface);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateXlibSurfaceKHR failed, result={}", static_cast<int>(result));
        return false;
    }
#else
    UZ_LOG(RHI, Error, "VulkanSwapChain: surface creation not implemented for this platform");
    return false;
#endif

    // Verify surface support
    VkBool32 presentSupported = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(device->GetVkPhysicalDevice(),
                                          device->GetGraphicsQueueFamily(),
                                          m_surface, &presentSupported);
    if (!presentSupported) {
        UZ_LOG(RHI, Error, "Graphics queue family does not support presentation to this surface");
        return false;
    }

    // Select present mode (prefer mailbox for low-latency no-vsync)
    uint32_t presentModeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(device->GetVkPhysicalDevice(), m_surface, &presentModeCount, nullptr);
    std::vector<VkPresentModeKHR> presentModes(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device->GetVkPhysicalDevice(), m_surface, &presentModeCount, presentModes.data());

    m_presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed fallback (vsync)
    for (auto mode : presentModes) {
        if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
            m_presentMode = VK_PRESENT_MODE_MAILBOX_KHR;
            break;
        }
    }

    UZ_LOG(RHI, Info, "Vulkan present mode: {}", (m_presentMode == VK_PRESENT_MODE_MAILBOX_KHR) ? "MAILBOX" : "FIFO");

    // Create semaphores for presentation sync
    VkSemaphoreCreateInfo semCI = {};
    semCI.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkResult result = vkCreateSemaphore(device->GetVkDevice(), &semCI, nullptr, &m_imageAvailableSemaphore);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "Failed to create image available semaphore, result={}", static_cast<int>(result));
        return false;
    }

    result = vkCreateSemaphore(device->GetVkDevice(), &semCI, nullptr, &m_renderFinishedSemaphore);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "Failed to create render finished semaphore, result={}", static_cast<int>(result));
        return false;
    }

    if (!CreateSwapChain()) return false;

    UZ_LOG(RHI, Info, "VulkanSwapChain created: {}x{}, {} images",
           m_width, m_height, m_imageCount);
    m_frameLimiter.LogConfig();

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Swapchain Creation
// ─────────────────────────────────────────────────────────────────────────────

bool VulkanSwapChain::CreateSwapChain() {
    VkDevice device = m_device->GetVkDevice();
    VkPhysicalDevice physDevice = m_device->GetVkPhysicalDevice();

    // Query surface capabilities
    VkSurfaceCapabilitiesKHR caps = {};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physDevice, m_surface, &caps);

    // Clamp image count to capabilities
    uint32_t imageCount = kMaxBackBuffers;
    if (imageCount < caps.minImageCount) imageCount = caps.minImageCount;
    if (caps.maxImageCount > 0 && imageCount > caps.maxImageCount) imageCount = caps.maxImageCount;

    // Clamp extent to capabilities
    VkExtent2D extent = {m_width, m_height};
    extent.width = std::clamp(extent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    extent.height = std::clamp(extent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    m_width = extent.width;
    m_height = extent.height;

    // Query supported surface formats and validate our requested format.
    // Prefer UNORM to avoid hardware sRGB conversion (shader does LinearToSrgb manually).
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> surfaceFormats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physDevice, m_surface, &formatCount, surfaceFormats.data());

    VkFormat chosenFormat = m_vkFormat;
    VkColorSpaceKHR chosenColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    bool formatFound = false;

    // First pass: try exact match with UNORM
    for (const auto& sf : surfaceFormats) {
        if (sf.format == m_vkFormat) {
            chosenFormat = sf.format;
            chosenColorSpace = sf.colorSpace;
            formatFound = true;
            break;
        }
    }

    // Second pass: if exact not found, prefer any UNORM variant (B8G8R8A8_UNORM is common)
    if (!formatFound) {
        for (const auto& sf : surfaceFormats) {
            if (sf.format == VK_FORMAT_B8G8R8A8_UNORM || sf.format == VK_FORMAT_R8G8B8A8_UNORM) {
                chosenFormat = sf.format;
                chosenColorSpace = sf.colorSpace;
                formatFound = true;
                UZ_LOG(RHI, Info, "Swapchain: requested format not supported, using UNORM fallback (format={})",
                       static_cast<int>(sf.format));
                break;
            }
        }
    }

    // Last resort: use whatever the surface prefers
    if (!formatFound && !surfaceFormats.empty()) {
        chosenFormat = surfaceFormats[0].format;
        chosenColorSpace = surfaceFormats[0].colorSpace;
        UZ_LOG(RHI, Warning, "Swapchain: no UNORM format found, using surface default (format={})",
               static_cast<int>(chosenFormat));
    }

    m_vkFormat = chosenFormat;

    // Update RHI format to reflect actual swapchain format (may differ from requested).
    // This ensures downstream code (screenshot readback, render passes) knows the real channel order.
    if (m_vkFormat == VK_FORMAT_B8G8R8A8_UNORM) {
        m_format = RHIFormat::BGRA8Unorm;
    } else if (m_vkFormat == VK_FORMAT_R8G8B8A8_UNORM) {
        m_format = RHIFormat::RGBA8Unorm;
    } else if (m_vkFormat == VK_FORMAT_B8G8R8A8_SRGB) {
        m_format = RHIFormat::BGRA8Srgb;
    } else if (m_vkFormat == VK_FORMAT_R8G8B8A8_SRGB) {
        m_format = RHIFormat::RGBA8Srgb;
    }

    UZ_LOG(RHI, Info, "Swapchain format: {} (colorSpace={}, rhiFormat={})",
           static_cast<int>(m_vkFormat), static_cast<int>(chosenColorSpace),
           static_cast<int>(m_format));

    VkSwapchainCreateInfoKHR swapCI = {};
    swapCI.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapCI.surface = m_surface;
    swapCI.minImageCount = imageCount;
    swapCI.imageFormat = m_vkFormat;
    swapCI.imageColorSpace = chosenColorSpace;
    swapCI.imageExtent = extent;
    swapCI.imageArrayLayers = 1;
    swapCI.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    swapCI.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapCI.preTransform = caps.currentTransform;
    swapCI.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapCI.presentMode = m_presentMode;
    swapCI.clipped = VK_TRUE;
    swapCI.oldSwapchain = m_swapchain; // Allows recreation with old swapchain

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VkResult result = vkCreateSwapchainKHR(device, &swapCI, nullptr, &newSwapchain);
    if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkCreateSwapchainKHR failed, result={}", static_cast<int>(result));
        return false;
    }

    // Destroy old swapchain if recreating
    if (m_swapchain != VK_NULL_HANDLE) {
        DestroySwapChainResources();
        vkDestroySwapchainKHR(device, m_swapchain, nullptr);
    }
    m_swapchain = newSwapchain;

    // Get swapchain images
    vkGetSwapchainImagesKHR(device, m_swapchain, &m_imageCount, nullptr);
    if (m_imageCount > kMaxBackBuffers) m_imageCount = kMaxBackBuffers;
    vkGetSwapchainImagesKHR(device, m_swapchain, &m_imageCount, m_images);

    // Create image views and register in texture pool
    for (uint32_t i = 0; i < m_imageCount; ++i) {
        VkImageViewCreateInfo viewCI = {};
        viewCI.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewCI.image = m_images[i];
        viewCI.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewCI.format = m_vkFormat;
        viewCI.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewCI.subresourceRange.baseMipLevel = 0;
        viewCI.subresourceRange.levelCount = 1;
        viewCI.subresourceRange.baseArrayLayer = 0;
        viewCI.subresourceRange.layerCount = 1;

        result = vkCreateImageView(device, &viewCI, nullptr, &m_imageViews[i]);
        if (result != VK_SUCCESS) {
            UZ_LOG(RHI, Error, "vkCreateImageView failed for swapchain image {}, result={}",
                   i, static_cast<int>(result));
            return false;
        }

        // Register in texture pool as non-owning (swapchain owns the VkImage)
        uint32_t generation = 0;
        uint32_t poolIndex = m_device->GetTexturePool().Allocate(generation);
        auto* slot = m_device->GetTexturePool().Get(poolIndex, generation);
        if (slot) {
            slot->image = m_images[i];
            slot->allocation = nullptr;
            slot->imageView = m_imageViews[i];
            slot->currentLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            slot->desc.width = m_width;
            slot->desc.height = m_height;
            slot->desc.format = m_format;
            slot->desc.usage = RHITextureUsage::RenderTarget;
            slot->generation = generation;
            slot->ownsImage = false; // Swapchain owns the image
            m_backBufferHandles[i] = {poolIndex, generation};
        }
    }

    return true;
}

void VulkanSwapChain::DestroySwapChainResources() {
    VkDevice device = m_device->GetVkDevice();

    for (uint32_t i = 0; i < kMaxBackBuffers; ++i) {
        if (m_backBufferHandles[i].IsValid()) {
            // Clear the pool slot (image view is destroyed separately)
            auto* slot = m_device->GetTexturePool().Get(
                m_backBufferHandles[i].index, m_backBufferHandles[i].generation);
            if (slot) {
                slot->imageView = VK_NULL_HANDLE;
                slot->image = VK_NULL_HANDLE;
            }
            m_device->GetTexturePool().Free(m_backBufferHandles[i].index);
            m_backBufferHandles[i] = RHITextureHandle::Invalid();
        }
        if (m_imageViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_imageViews[i], nullptr);
            m_imageViews[i] = VK_NULL_HANDLE;
        }
        m_images[i] = VK_NULL_HANDLE;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain Implementation
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle VulkanSwapChain::GetCurrentBackBuffer() {
    // Acquire next image — signals m_imageAvailableSemaphore when the presentation
    // engine is done reading the image (i.e., safe to render into it).
    VkResult result = vkAcquireNextImageKHR(
        m_device->GetVkDevice(), m_swapchain, UINT64_MAX,
        m_imageAvailableSemaphore, VK_NULL_HANDLE, &m_currentImageIndex);

    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        UZ_LOG(RHI, Warning, "Swapchain out of date during acquire — resize needed");
        return RHITextureHandle::Invalid();
    }
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
        UZ_LOG(RHI, Error, "vkAcquireNextImageKHR failed, result={}", static_cast<int>(result));
        return RHITextureHandle::Invalid();
    }

    // Store pending semaphores on the device so the next VulkanCommandList::Execute()
    // can wire them into vkQueueSubmit (wait on imageAvailable, signal renderFinished).
    // This avoids changing the abstract IRHICommandList interface.
    m_device->m_pendingImageAvailableSem = m_imageAvailableSemaphore;
    m_device->m_pendingRenderFinishedSem = m_renderFinishedSemaphore;

    return m_backBufferHandles[m_currentImageIndex];
}

uint32_t VulkanSwapChain::GetCurrentBackBufferIndex() const {
    return m_currentImageIndex;
}

void VulkanSwapChain::Present(bool vsync) {
    UZ_PROFILE_FUNCTION();
    (void)vsync; // Present mode was chosen at init time (mailbox vs fifo)

    // Wait for rendering to finish before presenting. The renderFinishedSemaphore
    // is signaled by vkQueueSubmit in VulkanCommandList::Execute().
    VkPresentInfoKHR presentInfo = {};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore;
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapchain;
    presentInfo.pImageIndices = &m_currentImageIndex;

    VkResult result = vkQueuePresentKHR(m_device->GetGraphicsQueue(), &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
        UZ_LOG(RHI, Warning, "Swapchain out of date or suboptimal during present — resize needed");
    } else if (result != VK_SUCCESS) {
        UZ_LOG(RHI, Error, "vkQueuePresentKHR failed, result={}", static_cast<int>(result));
    }

    m_frameLimiter.WaitForTargetFrameTime();
}

void VulkanSwapChain::Resize(uint32_t width, uint32_t height) {
    UZ_PROFILE_FUNCTION();
    if (width == 0 || height == 0) return; // Minimized — skip

    m_width = width;
    m_height = height;

    m_device->WaitIdle();

    if (!CreateSwapChain()) {
        UZ_LOG(RHI, Error, "VulkanSwapChain::Resize — failed to recreate swapchain");
    }

    UZ_LOG(RHI, Info, "VulkanSwapChain resized to {}x{}", m_width, m_height);
}

uint32_t VulkanSwapChain::GetWidth() const {
    return m_width;
}

uint32_t VulkanSwapChain::GetHeight() const {
    return m_height;
}

RHIFormat VulkanSwapChain::GetFormat() const {
    return m_format;
}

} // namespace uz
