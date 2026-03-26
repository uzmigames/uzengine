#pragma once

/// RHI swapchain interface for presenting rendered frames to a window.

#include "rhi_types.h"

#include <cstdint>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain — presentation to a window surface
// ─────────────────────────────────────────────────────────────────────────────

/// Manages back buffers and presentation for a window surface.
///
/// Triple-buffered by default (3 back buffers). The swap chain owns the
/// back buffer textures — they are valid as long as the swap chain exists
/// and are invalidated on `Resize()`.
///
/// # Thread Safety
/// `Present()` must be called from the thread that created the swap chain.
/// `GetWidth()`, `GetHeight()`, `GetFormat()` are safe to call from any thread.
///
/// # Lifetime
/// Created via `IRHIDevice::CreateSwapChain()`, destroyed via
/// `IRHIDevice::DestroySwapChain()`. Must be destroyed before the device.
///
/// # Examples
/// ```cpp
/// RHISwapChainDesc scDesc{};
/// scDesc.windowHandle = hwnd;
/// scDesc.width = 1280;
/// scDesc.height = 720;
/// auto* swapChain = device->CreateSwapChain(scDesc);
///
/// // Render loop
/// auto backBuffer = swapChain->GetCurrentBackBuffer();
/// // ... record commands targeting backBuffer ...
/// swapChain->Present(true);
///
/// device->DestroySwapChain(swapChain);
/// ```
class IRHISwapChain {
public:
    virtual ~IRHISwapChain() = default;

    /// Returns a handle to the current back buffer texture.
    ///
    /// # Returns
    /// A valid `RHITextureHandle` for the current back buffer. The handle
    /// becomes invalid after `Resize()` is called.
    virtual RHITextureHandle GetCurrentBackBuffer() = 0;

    /// Returns the index of the current back buffer.
    ///
    /// # Returns
    /// Index in range `[0, bufferCount)`.
    virtual uint32_t GetCurrentBackBufferIndex() const = 0;

    /// Presents the current back buffer to the display.
    ///
    /// # Arguments
    /// * `vsync` — If `true`, waits for vertical blank (sync interval 1).
    ///   If `false`, presents immediately (may tear unless tearing is supported).
    virtual void Present(bool vsync) = 0;

    /// Resizes all back buffers. Invalidates all existing back buffer handles.
    ///
    /// Call after the window is resized. Passing `(0, 0)` is a no-op (minimized window).
    /// The GPU must be idle before calling this (call `WaitIdle()` first).
    ///
    /// # Arguments
    /// * `width` — New back buffer width in pixels.
    /// * `height` — New back buffer height in pixels.
    virtual void Resize(uint32_t width, uint32_t height) = 0;

    /// Returns the current back buffer width in pixels.
    virtual uint32_t GetWidth() const = 0;

    /// Returns the current back buffer height in pixels.
    virtual uint32_t GetHeight() const = 0;

    /// Returns the back buffer pixel format.
    virtual RHIFormat GetFormat() const = 0;
};

} // namespace uz
