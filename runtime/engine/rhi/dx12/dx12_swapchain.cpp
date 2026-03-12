/// DX12 Swapchain Implementation
///
/// IDXGISwapChain4 with FLIP_DISCARD, triple-buffering, tearing support,
/// and resize handling (release + recreate back buffers and RTVs).

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_swapchain.h"
#include "dx12_compat.h"
#include "dx12_device.h"

#include "log.h"
#include "profiler.h"

#include <tracy/TracyD3D12.hpp>

#include <windows.h>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Format Conversion
// ─────────────────────────────────────────────────────────────────────────────

/// Maps RHIFormat to DXGI_FORMAT for swap chain and resource creation.
static DXGI_FORMAT ToDXGIFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::R8Unorm:        return DXGI_FORMAT_R8_UNORM;
        case RHIFormat::RG8Unorm:       return DXGI_FORMAT_R8G8_UNORM;
        case RHIFormat::RGBA8Unorm:     return DXGI_FORMAT_R8G8B8A8_UNORM;
        case RHIFormat::RGBA8Srgb:      return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
        case RHIFormat::BGRA8Unorm:     return DXGI_FORMAT_B8G8R8A8_UNORM;
        case RHIFormat::BGRA8Srgb:      return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
        case RHIFormat::R16Float:       return DXGI_FORMAT_R16_FLOAT;
        case RHIFormat::RG16Float:      return DXGI_FORMAT_R16G16_FLOAT;
        case RHIFormat::RGBA16Float:    return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case RHIFormat::R32Float:       return DXGI_FORMAT_R32_FLOAT;
        case RHIFormat::RG32Float:      return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::RGB32Float:     return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::RGBA32Float:    return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::R32Uint:        return DXGI_FORMAT_R32_UINT;
        case RHIFormat::RG32Uint:       return DXGI_FORMAT_R32G32_UINT;
        case RHIFormat::RGBA32Uint:     return DXGI_FORMAT_R32G32B32A32_UINT;
        case RHIFormat::D16Unorm:       return DXGI_FORMAT_D16_UNORM;
        case RHIFormat::D32Float:       return DXGI_FORMAT_D32_FLOAT;
        case RHIFormat::D24UnormS8Uint: return DXGI_FORMAT_D24_UNORM_S8_UINT;
        case RHIFormat::D32FloatS8Uint: return DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
        default:                        return DXGI_FORMAT_UNKNOWN;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DX12SwapChain::~DX12SwapChain() {
    ReleaseBackBuffers();
    m_swapChain.Reset();
    UZ_LOG(RHI, Info, "DX12SwapChain destroyed");
}

bool DX12SwapChain::Init(DX12Device* device, const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();
    if (!device || !desc.windowHandle) {
        UZ_LOG(RHI, Error, "DX12SwapChain::Init — null device or window handle");
        return false;
    }

    m_device = device;
    m_width = desc.width;
    m_height = desc.height;
    m_bufferCount = (desc.bufferCount >= 2 && desc.bufferCount <= kMaxBackBuffers)
                        ? desc.bufferCount
                        : kMaxBackBuffers;
    m_format = desc.format;

    IDXGIFactory6* factory = m_device->GetFactory();
    if (!factory) {
        UZ_LOG(RHI, Error, "DX12SwapChain::Init — DXGI factory is null");
        return false;
    }

    // Check tearing support for variable refresh rate
    BOOL tearingAllowed = FALSE;
    HRESULT hr = factory->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &tearingAllowed, sizeof(tearingAllowed));
    m_tearingSupported = SUCCEEDED(hr) && tearingAllowed;

    if (m_tearingSupported) {
        UZ_LOG(RHI, Info, "DXGI tearing (variable refresh rate) supported");
    }

    // Build swap chain descriptor
    DXGI_SWAP_CHAIN_DESC1 swapDesc = {};
    swapDesc.Width = m_width;
    swapDesc.Height = m_height;
    swapDesc.Format = ToDXGIFormat(m_format);
    swapDesc.Stereo = FALSE;
    swapDesc.SampleDesc.Count = 1;
    swapDesc.SampleDesc.Quality = 0;
    swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swapDesc.BufferCount = m_bufferCount;
    swapDesc.Scaling = DXGI_SCALING_STRETCH;
    swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    swapDesc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swapDesc.Flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

    HWND hwnd = static_cast<HWND>(desc.windowHandle);
    ComPtr<IDXGISwapChain1> swapChain1;
    hr = factory->CreateSwapChainForHwnd(
        m_device->GetDirectQueue(), hwnd, &swapDesc, nullptr, nullptr, &swapChain1);

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateSwapChainForHwnd failed, HRESULT=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Disable ALT+ENTER fullscreen toggle — the engine manages fullscreen itself
    factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

    hr = swapChain1.As(&m_swapChain);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "QueryInterface to IDXGISwapChain4 failed, HRESULT=0x{:08X}",
               static_cast<uint32_t>(hr));
        return false;
    }

    AcquireBackBuffers();

    UZ_LOG(RHI, Info, "DX12SwapChain created: {}x{}, {} buffers, tearing={}",
           m_width, m_height, m_bufferCount, m_tearingSupported);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Back Buffer Management
// ─────────────────────────────────────────────────────────────────────────────

void DX12SwapChain::AcquireBackBuffers() {
    UZ_PROFILE_FUNCTION();
    ID3D12Device* device = m_device->GetDevice();
    auto& rtvAlloc = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    m_rtvDescriptorSize = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (uint32_t i = 0; i < m_bufferCount; ++i) {
        HRESULT hr = m_swapChain->GetBuffer(i, IID_PPV_ARGS(&m_backBuffers[i]));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "GetBuffer({}) failed, HRESULT=0x{:08X}", i, static_cast<uint32_t>(hr));
            continue;
        }

        // Allocate RTV descriptor via the device's allocator
        uint32_t rtvIdx = rtvAlloc.Allocate();
        m_rtvHandles[i] = rtvAlloc.CpuHandle(rtvIdx);
        device->CreateRenderTargetView(m_backBuffers[i].Get(), nullptr, m_rtvHandles[i]);

        // Register in the device's texture pool so abstract RHI handles work
        uint32_t generation = 0;
        uint32_t poolIdx = m_device->GetTexturePool().Allocate(generation);
        auto* tex = m_device->GetTexturePool().Get(poolIdx, generation);
        if (tex) {
            tex->allocation = nullptr;  // swapchain-owned, not D3D12MA
            tex->resource = m_backBuffers[i].Get();
            tex->srvIndex = UINT32_MAX;
            tex->uavIndex = UINT32_MAX;
            tex->rtvIndex = rtvIdx;
            tex->dsvIndex = UINT32_MAX;
            tex->generation = generation;
            tex->desc.width = m_width;
            tex->desc.height = m_height;
            tex->desc.format = m_format;
        }
        m_backBufferHandles[i] = {poolIdx, generation};
    }
}

void DX12SwapChain::ReleaseBackBuffers() {
    UZ_PROFILE_FUNCTION();
    if (!m_device) {
        for (uint32_t i = 0; i < kMaxBackBuffers; ++i) {
            m_backBuffers[i].Reset();
            m_backBufferHandles[i] = RHITextureHandle::Invalid();
        }
        return;
    }
    auto& rtvAlloc = m_device->GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    for (uint32_t i = 0; i < kMaxBackBuffers; ++i) {
        // Free texture pool entry (don't destroy the resource — swapchain owns it)
        if (m_backBufferHandles[i].IsValid()) {
            auto* tex = m_device->GetTexturePool().Get(
                m_backBufferHandles[i].index, m_backBufferHandles[i].generation);
            if (tex) {
                if (tex->rtvIndex != UINT32_MAX) {
                    rtvAlloc.Free(tex->rtvIndex);
                }
                tex->resource = nullptr;  // don't release — ComPtr handles it
            }
            m_device->GetTexturePool().Free(m_backBufferHandles[i].index);
        }
        m_backBuffers[i].Reset();
        m_backBufferHandles[i] = RHITextureHandle::Invalid();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// IRHISwapChain Implementation
// ─────────────────────────────────────────────────────────────────────────────

RHITextureHandle DX12SwapChain::GetCurrentBackBuffer() {
    uint32_t idx = m_swapChain->GetCurrentBackBufferIndex();
    return m_backBufferHandles[idx];
}

uint32_t DX12SwapChain::GetCurrentBackBufferIndex() const {
    return m_swapChain->GetCurrentBackBufferIndex();
}

void DX12SwapChain::Present(bool vsync) {
    UZ_PROFILE_FUNCTION();
    UINT syncInterval = vsync ? 1 : 0;
    UINT flags = (!vsync && m_tearingSupported) ? DXGI_PRESENT_ALLOW_TEARING : 0;

    HRESULT hr = m_swapChain->Present(syncInterval, flags);
    if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
        m_device->LogDeviceRemoved();
    }

    TracyD3D12Collect(m_device->GetTracyGpuContext());
    TracyD3D12NewFrame(m_device->GetTracyGpuContext());
}

void DX12SwapChain::Resize(uint32_t width, uint32_t height) {
    UZ_PROFILE_FUNCTION();
    if (width == 0 || height == 0) {
        return; // Minimized window — skip resize
    }

    ReleaseBackBuffers();

    UINT flags = m_tearingSupported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
    HRESULT hr = m_swapChain->ResizeBuffers(m_bufferCount, width, height, DXGI_FORMAT_UNKNOWN, flags);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "ResizeBuffers failed, HRESULT=0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    m_width = width;
    m_height = height;

    AcquireBackBuffers();

    UZ_LOG(RHI, Info, "DX12SwapChain resized to {}x{}", m_width, m_height);
}

uint32_t DX12SwapChain::GetWidth() const {
    return m_width;
}

uint32_t DX12SwapChain::GetHeight() const {
    return m_height;
}

RHIFormat DX12SwapChain::GetFormat() const {
    return m_format;
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12-Specific Accessors
// ─────────────────────────────────────────────────────────────────────────────

ID3D12Resource* DX12SwapChain::GetCurrentBackBufferResource() const {
    uint32_t idx = m_swapChain->GetCurrentBackBufferIndex();
    return m_backBuffers[idx].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE DX12SwapChain::GetCurrentRTV() const {
    uint32_t idx = m_swapChain->GetCurrentBackBufferIndex();
    return m_rtvHandles[idx];
}

} // namespace uz
