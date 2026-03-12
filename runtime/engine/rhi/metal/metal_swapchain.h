#pragma once

/// Metal backend implementation of IRHISwapChain using CAMetalLayer.
///
/// Uses CAMetalLayer for presentation. Triple-buffered by default via
/// `layer.maximumDrawableCount = 3`. VSync is controlled per-frame via
/// `layer.displaySyncEnabled`.
///
/// Unlike Vulkan/DX12 where back buffer images are persistent and acquired
/// by index, Metal provides a transient drawable each frame via `nextDrawable`.
/// The drawable's texture is registered in the device texture pool as a
/// non-owning entry (the drawable pool manages the texture lifetime).
///
/// # Lifecycle
/// 1. Construct -> `Init(device, desc)` -> use in render loop -> destructor
/// 2. `Resize()` updates the layer's drawableSize (no full recreation needed)
///
/// # Thread Safety
/// NOT thread-safe. Must be used from the main/render thread only.

#include "../rhi.h"

#include <cstdint>

namespace uz {

class MetalDevice;

// ─────────────────────────────────────────────────────────────────────────────
// MetalSwapChain
// ─────────────────────────────────────────────────────────────────────────────

/// Metal implementation of IRHISwapChain using CAMetalLayer.
///
/// Wraps a CAMetalLayer attached to the window's content view. Each frame,
/// `GetCurrentBackBuffer()` calls `[layer nextDrawable]` to acquire a
/// transient drawable whose texture is registered in the device texture pool.
/// `Present()` commits the drawable and advances the back buffer index.
class MetalSwapChain final : public IRHISwapChain {
public:
    MetalSwapChain() = default;
    ~MetalSwapChain() override;

    /// Creates the CAMetalLayer and configures it for the given window.
    ///
    /// Attaches a CAMetalLayer to the NSWindow's contentView. Configures
    /// pixel format, drawable size, buffer count, and VSync based on the
    /// descriptor. The layer uses `framebufferOnly = YES` for optimal
    /// render target performance.
    ///
    /// # Arguments
    /// * `device` — Parent Metal device (must outlive this swap chain).
    /// * `desc` — Swap chain descriptor (NSWindow handle, dimensions, format).
    ///
    /// # Returns
    /// `true` on success, `false` if the window handle is null or layer setup fails.
    bool Init(MetalDevice* device, const RHISwapChainDesc& desc);

    // ── IRHISwapChain overrides ──────────────────────────────────────────

    /// Acquires the next drawable and returns a handle to its texture.
    ///
    /// Calls `[CAMetalLayer nextDrawable]` to get the current frame's drawable.
    /// The drawable's texture is registered (or updated) in the device texture
    /// pool as a non-owning MetalTexture (`ownsTexture = false`).
    ///
    /// # Returns
    /// A valid `RHITextureHandle` for the current drawable's texture, or
    /// `RHITextureHandle::Invalid()` if `nextDrawable` fails (e.g., timeout).
    RHITextureHandle GetCurrentBackBuffer() override;

    /// Returns the index of the current back buffer (0..bufferCount-1).
    uint32_t GetCurrentBackBufferIndex() const override;

    /// Presents the current drawable to the display.
    ///
    /// Calls `[drawable present]` on the drawable acquired by the last
    /// `GetCurrentBackBuffer()` call. Updates VSync on-the-fly via
    /// `layer.displaySyncEnabled`. Advances the back buffer index and
    /// releases the drawable reference.
    ///
    /// # Arguments
    /// * `vsync` — If `true`, enables display sync. If `false`, disables it.
    void Present(bool vsync) override;

    /// Resizes the drawable surface. Invalidates existing back buffer handles.
    ///
    /// Updates `layer.drawableSize` to the new dimensions. Passing `(0, 0)`
    /// is a no-op (minimized window). Frees any existing back buffer handles
    /// from the texture pool.
    ///
    /// # Arguments
    /// * `width` — New back buffer width in pixels.
    /// * `height` — New back buffer height in pixels.
    void Resize(uint32_t width, uint32_t height) override;

    /// Returns the current back buffer width in pixels.
    uint32_t GetWidth() const override;

    /// Returns the current back buffer height in pixels.
    uint32_t GetHeight() const override;

    /// Returns the back buffer pixel format.
    RHIFormat GetFormat() const override;

private:
    static constexpr uint32_t kMaxBackBuffers = 3;

    /// Releases all back buffer handles from the texture pool.
    void DestroyBackBufferHandles();

    MetalDevice* m_device = nullptr;
    void* m_layer = nullptr;           ///< CAMetalLayer* (stored as void* to avoid ObjC in header)
    void* m_currentDrawable = nullptr; ///< id<CAMetalDrawable> (current frame's drawable)

    RHITextureHandle m_backBufferHandles[kMaxBackBuffers] = {};
    uint32_t m_currentBackBufferIndex = 0;
    uint32_t m_width = 0;
    uint32_t m_height = 0;
    RHIFormat m_format = RHIFormat::BGRA8Unorm;
};

} // namespace uz
