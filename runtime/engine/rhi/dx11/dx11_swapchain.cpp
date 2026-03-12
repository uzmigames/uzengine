/// DX11 Swapchain Implementation
///
/// IDXGISwapChain1 with FLIP_DISCARD, triple-buffering, tearing support,
/// and resize handling. Unlike DX12, DX11 swap chains are created against
/// the device directly (not a command queue).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_swapchain.h"
#include "dx11_device.h"

#include "log.h"
#include "profiler.h"

#include <tracy/TracyD3D11.hpp>

#include <windows.h>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIFormat to DXGI_FORMAT for swap chain creation.
static DXGI_FORMAT ToDxgiFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::RGBA8Unorm:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::BGRA8Unorm:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:      return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::RGBA16Float:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:                        return DXGI_FORMAT_R8G8B8A8_UNORM;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DX11SwapChain::~DX11SwapChain() {
    ReleaseBackBuffers();
    m_swapChain.Reset();
    UZ_LOG(RHI, Info, "DX11SwapChain destroyed");
}

bool DX11SwapChain::Init(DX11Device* device, const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!device || !desc.windowHandle) {
        UZ_LOG(RHI, Error, "DX11SwapChain::Init — null device or window handle");
        return false;
    }

    m_device = device;
    m_width = desc.width;
    m_height = desc.height;
    m_bufferCount = (desc.bufferCount >= 2 && desc.bufferCount <= kMaxBackBuffers)
                        ? desc.bufferCount
                        : kMaxBackBuffers;
    m_format = desc.format;

    IDXGIFactory2* factory = m_device->GetFactory();
    if (!factory) {
        UZ_LOG(RHI, Error, "DX11SwapChain::Init — DXGI factory is null");
        return false;
    }

    // Check tearing support
    ComPtr<IDXGIFactory5> factory5;
    if (SUCCEEDED(factory->QueryInterface(IID_PPV_ARGS(&factory5)))) {
        BOOL tearingAllowed = FALSE;
        HRESULT hr = factory5->CheckFeatureSupport(
            DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingAllowed, sizeof(tearingAllowed));
        m_tearingSupported = SUCCEEDED(hr) && tearingAllowed;
    }

    if (m_tearingSupported) {
        UZ_LOG(RHI, Info, "DXGI tearing (variable refresh rate) supported");
    }

    // Build swap chain descriptor
    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = m_width;
    swapDesc.Height = m_height;
    swapDesc.Format = ToDxgiFormat(m_format);
    swapDesc.Stereo = FALSE;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = m_bufferCount;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;

    HWND hwnd = static_cast<HWND>(desc.windowHandle);

    // DX11 swap chains are created against the device, not a queue
    HRESULT hr = factory->CreateSwapChainForHwnd(
        m_device->GetDevice(), hwnd, &swapDesc, nullptr, nullptr, &m_swapChain);

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateSwapChainForHwnd failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Disable ALT+ENTER fullscreen toggle
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    AcquireBackBuffers();

    UZ_LOG(RHI, Info, "DX11SwapChain created: {}x{}, {} buffers, tearing={}",
           m_width, m_height, m_bufferCount, m_tearingSupported);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Back Buffer Management
// ─────────────────────────────────────────────────────────────────────────────

void DX11SwapChain::AcquireBackBuffers() {
    // For FLIP_DISCARD with DX11, we can only get buffer index 0.
    // DX11 does not expose GetCurrentBackBufferIndex() — the driver manages rotation.

    ComPtr<ID3D11Texture2D> backBuffer;
    HRESULT hr = m_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "GetBuffer(0) failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    hr = m_device->GetDevice()->CreateRenderTargetView(backBuffer.Get(), nullptr, &m_rtvs[0]);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateRenderTargetView for back buffer failed, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return;
    }

    // Register in device texture pool so ClearRenderTarget / BeginRenderPass can find the RTV
    uint32_t generation = 0;
    uint32_t poolIdx = m_device->GetTexturePool().Allocate(generation);
    auto* tex = m_device->GetTexturePool().Get(poolIdx, generation);
    if (tex) {
        tex->texture = backBuffer.Get();
        tex->rtv = m_rtvs[0];
        tex->srv = nullptr;
        tex->dsv = nullptr;
        tex->uav = nullptr;
        tex->srvIndex = UINT32_MAX;
        tex->generation = generation;
        tex->desc.width = m_width;
        tex->desc.height = m_height;
        tex->desc.format = m_format;
    }
    m_backBufferHandles[0] = {poolIdx, generation};
    m_currentBackBuffer = 0;
}

void DX11SwapChain::ReleaseBackBuffers() {
    for (uint32_t i = 0; i < kMaxBackBuffers; ++i) {
        if (m_backBufferHandles[i].IsValid()) {
            auto* tex = m_device->GetTexturePool().Get(
                m_backBufferHandles[i].index, m_backBufferHandles[i].generation);
            if (tex) {
                tex->texture = nullptr;  // swapchain owns the texture, don't release
                tex->rtv = nullptr;      // will be released below
            }
            m_device->GetTexturePool().Free(m_backBufferHandles[i].index);
        }
        if (m_rtvs[i]) {
            m_rtvs[i]->Release();
            m_rtvs[i] = nullptr;
        }
        m_backBufferHandles[i] = RHITextureHandle::Invalid();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain Implementation
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle DX11SwapChain::GetCurrentBackBuffer() {
    return m_backBufferHandles[0];
}

uint32_t DX11SwapChain::GetCurrentBackBufferIndex() const {
    // DX11 FLIP model manages rotation internally — always present buffer 0
    return 0;
}

void DX11SwapChain::Present(bool vsync) {
    UZ_PROFILE_FUNCTION();
    UINT syncInterval = vsync ? 1 : 0;
    UINT flags = (!vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    HRESULT hr = m_swapChain->Present(syncInterval, flags);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "DX11SwapChain::Present failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
    }

    // Flush debug layer messages after each present to surface warnings/errors
    m_device->FlushDebugMessages();

    // Collect pending Tracy GPU timestamps after each present
    if (m_device->GetTracyGpuContext()) {
        TracyD3D11Collect(m_device->GetTracyGpuContext());
    }
}

void DX11SwapChain::Resize(uint32_t width, uint32_t height) {
    UZ_PROFILE_FUNCTION();
    if (width == 0 || height == 0) {
        return; // Minimized window — skip resize
    }

    ReleaseBackBuffers();

    UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0u;
    HRESULT hr = m_swapChain->ResizeBuffers(m_bufferCount, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "ResizeBuffers failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    m_width = width;
    m_height = height;

    AcquireBackBuffers();

    UZ_LOG(RHI, Info, "DX11SwapChain resized to {}x{}", m_width, m_height);
}

uint32_t DX11SwapChain::GetWidth() const {
    return m_width;
}

uint32_t DX11SwapChain::GetHeight() const {
    return m_height;
}

RHIFormat DX11SwapChain::GetFormat() const {
    return m_format;
}

} // namespace uz
