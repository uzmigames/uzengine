#pragma once

/// DX11 Swapchain — IDXGISwapChain1 wrapper with FLIP_DISCARD presentation.
///
/// Uses the modern FLIP_DISCARD or FLIP_SEQUENTIAL swap model on Windows 10+.
/// Triple-buffered by default. Supports DXGI_PRESENT_ALLOW_TEARING for
/// variable refresh rate displays when vsync is disabled.
///
/// # Lifecycle
/// 1. Construct -> `Init(device, desc)` -> use in render loop -> destructor releases
/// 2. `Resize()` releases and recreates back buffers (invalidates existing handles)
///
/// # Thread Safety
/// NOT thread-safe. Must be used from the main/render thread only.

#include "../rhi.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_6.h>

namespace uz {

using Microsoft::WRL::ComPtr;

class DX11Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX11SwapChain
// ─────────────────────────────────────────────────────────────────────────────

/// DX11 implementation of IRHISwapChain using IDXGISwapChain1.
///
/// Uses DXGI_SWAP_EFFECT_FLIP_DISCARD for optimal performance on Windows 10+.
/// Falls back to DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL if needed.
class DX11SwapChain final : public IRHISwapChain {
public:
    DX11SwapChain() = default;
    ~DX11SwapChain() override;

    /// Creates the DXGI swap chain and acquires back buffer RTVs.
    ///
    /// # Arguments
    /// * `device` — Parent DX11 device (must outlive this swap chain).
    /// * `desc` — Swap chain descriptor (HWND, dimensions, buffer count, format).
    ///
    /// # Returns
    /// `true` on success, `false` on failure (logs detailed error).
    bool Init(DX11Device* device, const RHISwapChainDesc& desc);

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
    /// Maximum number of back buffers (triple-buffered).
    static constexpr uint32_t kMaxBackBuffers = 3;

    /// Acquires back buffer textures and creates RTVs.
    void AcquireBackBuffers();

    /// Releases all back buffer references and invalidates handles.
    void ReleaseBackBuffers();

    ComPtr<IDXGISwapChain1>      m_swapChain;
    ID3D11RenderTargetView*      m_rtvs[kMaxBackBuffers] = {};
    RHITextureHandle             m_backBufferHandles[kMaxBackBuffers] = {};

    DX11Device*                  m_device = nullptr;
    uint32_t                     m_width = 0;
    uint32_t                     m_height = 0;
    uint32_t                     m_bufferCount = 0;
    uint32_t                     m_currentBackBuffer = 0;
    RHIFormat                    m_format = RHIFormat::RGBA8Unorm;
    bool                         m_tearingSupported = false;
};

} // namespace uz
