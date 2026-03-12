#pragma once

/// Vulkan Swapchain — VkSwapchainKHR wrapper with mailbox presentation.
///
/// Uses VK_KHR_swapchain for presentation. Prefers VK_PRESENT_MODE_MAILBOX_KHR
/// (no vsync, no tearing) with fallback to FIFO (vsync). Triple-buffered.
///
/// Swapchain images are registered in the device's texture pool so command
/// lists can reference them via RHITextureHandle (same pattern as DX12).
///
/// # Lifecycle
/// 1. Construct -> `Init(device, desc)` -> use in render loop -> destructor
/// 2. `Resize()` destroys and recreates the swapchain and all back buffers
///
/// # Thread Safety
/// NOT thread-safe. Must be used from the main/render thread only.

#include "../rhi.h"

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>

namespace uz {

class VulkanDevice;

// ─────────────────────────────────────────────────────────────────────────────
// VulkanSwapChain
// ─────────────────────────────────────────────────────────────────────────────

/// Vulkan implementation of IRHISwapChain using VkSwapchainKHR.
///
/// Uses VK_PRESENT_MODE_MAILBOX_KHR for low-latency no-vsync presentation,
/// falling back to VK_PRESENT_MODE_FIFO_KHR (guaranteed available).
class VulkanSwapChain final : public IRHISwapChain {
public:
    VulkanSwapChain() = default;
    ~VulkanSwapChain() override;

    /// Creates the Vulkan surface, swapchain, and acquires back buffer images.
    ///
    /// # Arguments
    /// * `device` — Parent Vulkan device (must outlive this swap chain).
    /// * `desc` — Swap chain descriptor (HWND, dimensions, buffer count, format).
    ///
    /// # Returns
    /// `true` on success, `false` on failure (logs detailed error).
    bool Init(VulkanDevice* device, const RHISwapChainDesc& desc);

    // ── IRHISwapChain overrides ──────────────────────────────────────────

    /// Returns a handle to the current back buffer texture.
    RHITextureHandle GetCurrentBackBuffer() override;

    /// Returns the index of the current back buffer (0..bufferCount-1).
    uint32_t GetCurrentBackBufferIndex() const override;

    /// Presents the current back buffer to the display.
    void Present(bool vsync) override;

    /// Resizes all back buffers. Invalidates existing back buffer handles.
    void Resize(uint32_t width, uint32_t height) override;

    /// Returns the current back buffer width.
    uint32_t GetWidth() const override;

    /// Returns the current back buffer height.
    uint32_t GetHeight() const override;

    /// Returns the back buffer format.
    RHIFormat GetFormat() const override;

private:
    static constexpr uint32_t kMaxBackBuffers = 3;

    /// Creates the VkSwapchainKHR and acquires back buffer images.
    bool CreateSwapChain();

    /// Destroys back buffer image views and releases texture pool entries.
    void DestroySwapChainResources();

    VulkanDevice*        m_device = nullptr;
    VkSurfaceKHR         m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR       m_swapchain = VK_NULL_HANDLE;

    VkImage              m_images[kMaxBackBuffers] = {};
    VkImageView          m_imageViews[kMaxBackBuffers] = {};
    RHITextureHandle     m_backBufferHandles[kMaxBackBuffers] = {};

    /// Binary semaphores for image acquisition and presentation sync.
    VkSemaphore          m_imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore          m_renderFinishedSemaphore = VK_NULL_HANDLE;

    uint32_t             m_imageCount = 0;
    uint32_t             m_currentImageIndex = 0;
    uint32_t             m_width = 0;
    uint32_t             m_height = 0;
    RHIFormat            m_format = RHIFormat::BGRA8Unorm;
    VkFormat             m_vkFormat = VK_FORMAT_B8G8R8A8_UNORM;
    VkPresentModeKHR     m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
};

} // namespace uz
