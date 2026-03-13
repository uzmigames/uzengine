#pragma once

/// DX12 Swapchain — IDXGISwapChain4 wrapper with FLIP_DISCARD presentation.
///
/// Wraps IDXGISwapChain4 with the modern FLIP_DISCARD presentation model for optimal
/// compositor throughput. Triple-buffered by default. Supports `DXGI_PRESENT_ALLOW_TEARING`
/// for variable refresh rate (VRR/FreeSync/G-Sync) when vsync is disabled.
///
/// # Lifecycle
/// 1. Construct → `Init(device, desc)` → use in render loop → destructor releases
/// 2. `Resize()` releases and recreates back buffers (invalidates existing handles)
///
/// # Thread Safety
/// NOT thread-safe. Must be used from the main/render thread only.
/// Present must not overlap with command list recording that targets back buffers.
///
/// # Back Buffer Rotation
/// After `Present()`, `GetCurrentBackBufferIndex()` advances automatically.
/// The render loop must re-query the current back buffer each frame.

#include "../rhi.h"
#include "../rhi_frame_limiter.h"

#include <wrl/client.h>
#include <d3d12.h>
#include <dxgi1_6.h>

namespace uz {

using Microsoft::WRL::ComPtr;

class DX12Device;

// ─────────────────────────────────────────────────────────────────────────────
// DX12SwapChain
// ─────────────────────────────────────────────────────────────────────────────

/// DX12 implementation of IRHISwapChain using IDXGISwapChain4.
///
/// Uses DXGI_SWAP_EFFECT_FLIP_DISCARD for optimal performance.
/// Supports DXGI_PRESENT_ALLOW_TEARING for variable refresh rate displays
/// when vsync is disabled.
class DX12SwapChain final : public IRHISwapChain {
public:
    DX12SwapChain() = default;
    ~DX12SwapChain() override;

    /// Creates the DXGI swap chain, acquires back buffer resources, and creates RTVs.
    ///
    /// # Arguments
    /// * `device` — Parent DX12 device (must outlive this swap chain).
    /// * `desc` — Swap chain descriptor (HWND, dimensions, buffer count, format).
    ///
    /// # Returns
    /// `true` on success, `false` on failure (logs detailed error).
    bool Init(DX12Device* device, const RHISwapChainDesc& desc);

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

    // ── DX12-specific accessors ──────────────────────────────────────────

    /// Returns the native `ID3D12Resource` for the current back buffer.
    ///
    /// Used for barrier transitions via `DX12CommandList::TransitionBarrier()`.
    ID3D12Resource* GetCurrentBackBufferResource() const;

    /// Returns the RTV CPU descriptor handle for the current back buffer.
    ///
    /// Used for `DX12CommandList::ClearRTV()` and `OMSetRenderTargets()`.
    D3D12_CPU_DESCRIPTOR_HANDLE GetCurrentRTV() const;

private:
    /// Maximum number of back buffers (triple-buffered).
    static constexpr uint32_t kMaxBackBuffers = 3;

    /// Acquires ID3D12Resource references for each back buffer from the swap chain.
    void AcquireBackBuffers();

    /// Releases all back buffer resource references and invalidates handles.
    void ReleaseBackBuffers();

    ComPtr<IDXGISwapChain4>  m_swapChain;
    ComPtr<ID3D12Resource>   m_backBuffers[kMaxBackBuffers];
    RHITextureHandle         m_backBufferHandles[kMaxBackBuffers] = {};

    D3D12_CPU_DESCRIPTOR_HANDLE m_rtvHandles[kMaxBackBuffers] = {};
    uint32_t                 m_rtvDescriptorSize = 0;

    DX12Device*              m_device = nullptr;
    uint32_t                 m_width = 0;
    uint32_t                 m_height = 0;
    uint32_t                 m_bufferCount = 0;
    RHIFormat                m_format = RHIFormat::RGBA8Unorm;
    bool                     m_tearingSupported = false;

    /// Hybrid sleep/spin-wait frame limiter (500 FPS default cap).
    FrameLimiter             m_frameLimiter;
};

} // namespace uz
