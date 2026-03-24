/// DX11 Device Implementation — Lifecycle, Initialization, Synchronization
///
/// Initializes the D3D11.1 stack: debug layer, DXGI factory, adapter, device.
/// Resource Create/Destroy methods are in dx11_resource_creation.cpp.
/// No explicit memory allocator, descriptor heaps, or root signatures — DX11
/// manages these implicitly.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx11_device.h"
#include "dx11_swapchain.h"
#include "dx11_fence.h"
#include "dx11_command_list.h"

#include "log.h"
#include "profiler.h"

#include <windows.h>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Converts a wide DXGI adapter description string to narrow UTF-8.
static std::string WideToUtf8(const wchar_t* wide) {
    if (!wide || wide[0] == L'\0') return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string result(static_cast<size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

/// Stringifies a D3D_FEATURE_LEVEL enum value.
static const char* FeatureLevelToString(D3D_FEATURE_LEVEL level) {
    switch (level) {
        case D3D_FEATURE_LEVEL_11_1: return "D3D_FEATURE_LEVEL_11_1";
        case D3D_FEATURE_LEVEL_11_0: return "D3D_FEATURE_LEVEL_11_0";
        case D3D_FEATURE_LEVEL_10_1: return "D3D_FEATURE_LEVEL_10_1";
        case D3D_FEATURE_LEVEL_10_0: return "D3D_FEATURE_LEVEL_10_0";
        default:                     return "D3D_FEATURE_LEVEL_UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DX11Device::~DX11Device() {
    WaitIdle();

    // Destroy Tracy GPU context before releasing the device
    if (m_tracyD3D11Ctx) {
        TracyD3D11Destroy(m_tracyD3D11Ctx);
        m_tracyD3D11Ctx = nullptr;
    }

    // Release all pooled resources
    for (auto& buf : m_bufferPool.Slots()) {
        if (buf.uav) { buf.uav->Release(); buf.uav = nullptr; }
        if (buf.srv) { buf.srv->Release(); buf.srv = nullptr; }
        if (buf.buffer) { buf.buffer->Release(); buf.buffer = nullptr; }
        if (buf.shadowCopy) { delete[] buf.shadowCopy; buf.shadowCopy = nullptr; }
    }
    for (auto& tex : m_texturePool.Slots()) {
        if (tex.uav) { tex.uav->Release(); tex.uav = nullptr; }
        if (tex.dsv) { tex.dsv->Release(); tex.dsv = nullptr; }
        if (tex.rtv) { tex.rtv->Release(); tex.rtv = nullptr; }
        if (tex.srv) { tex.srv->Release(); tex.srv = nullptr; }
        if (tex.texture) { tex.texture->Release(); tex.texture = nullptr; }
    }
    for (auto& shader : m_shaderPool.Slots()) {
        if (shader.vs) { shader.vs->Release(); shader.vs = nullptr; }
        if (shader.ps) { shader.ps->Release(); shader.ps = nullptr; }
        if (shader.cs) { shader.cs->Release(); shader.cs = nullptr; }
    }
    for (auto& sampler : m_samplerPool.Slots()) {
        if (sampler.sampler) { sampler.sampler->Release(); sampler.sampler = nullptr; }
    }
    for (auto& pipeline : m_pipelinePool.Slots()) {
        if (pipeline.inputLayout) { pipeline.inputLayout->Release(); pipeline.inputLayout = nullptr; }
        if (pipeline.rasterizer) { pipeline.rasterizer->Release(); pipeline.rasterizer = nullptr; }
        if (pipeline.depthStencil) { pipeline.depthStencil->Release(); pipeline.depthStencil = nullptr; }
        if (pipeline.blend) { pipeline.blend->Release(); pipeline.blend = nullptr; }
    }

    if (m_defaultSampler) { m_defaultSampler->Release(); m_defaultSampler = nullptr; }
    if (m_shadowComparisonSampler) { m_shadowComparisonSampler->Release(); m_shadowComparisonSampler = nullptr; }

    m_immediateContext.Reset();
    m_device.Reset();
    m_adapter3.Reset();
    m_adapter.Reset();
    m_factory.Reset();

    UZ_LOG(RHI, Info, "DX11Device destroyed");
}

bool DX11Device::Init(const RHIDeviceDesc& desc) {
    UZ_PROFILE_FUNCTION();
    m_validationEnabled = desc.enableValidation;

    if (desc.enableValidation) {
        if (!EnableDebugLayer()) {
            UZ_LOG(RHI, Warning, "DX11 debug layer not available, continuing without validation");
        }
    }

    if (!CreateFactory())               return false;
    if (!SelectAdapter(desc.adapterIndex)) return false;
    if (!CreateDevice())                return false;

    // DX11 is always SM5.0 or SM5.1
    if (m_featureLevel >= D3D_FEATURE_LEVEL_11_1) {
        m_shaderModel = ShaderModel::SM5_1;
    } else {
        m_shaderModel = ShaderModel::SM5_0;
    }

    // Override if forced
    if (desc.forceShaderModel.has_value() && *desc.forceShaderModel < m_shaderModel) {
        UZ_LOG(RHI, Warning, "Shader model forced: {} -> {}",
               ShaderModelToString(m_shaderModel), ShaderModelToString(*desc.forceShaderModel));
        m_shaderModel = *desc.forceShaderModel;
    }

    // Initialize per-pass GPU timestamp profiler (non-fatal on failure)
    if (!m_gpuProfiler.Init(this)) {
        UZ_LOG(RHI, Warning, "GPU timestamp profiler failed to initialize — per-pass timing unavailable");
    }

    // Initialize Tracy GPU profiling context
    m_tracyD3D11Ctx = TracyD3D11Context(m_device.Get(), m_immediateContext.Get());

    // Create default samplers matching DX12 static samplers s0 and s1
    {
        D3D11_SAMPLER_DESC sd = {};
        /// 16x anisotropic filtering — standard quality for material textures.
        sd.Filter = D3D11_FILTER_ANISOTROPIC;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.MaxAnisotropy = 16;
        sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        m_device->CreateSamplerState(&sd, &m_defaultSampler);
    }
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_BORDER;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_BORDER;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_BORDER;
        sd.MaxAnisotropy = 1;
        sd.ComparisonFunc = D3D11_COMPARISON_LESS; // Shadow maps use standard-Z (standard)
        sd.BorderColor[0] = 0.0f; // Outside shadow atlas = no occlusion
        sd.BorderColor[1] = 0.0f;
        sd.BorderColor[2] = 0.0f;
        sd.BorderColor[3] = 0.0f;
        sd.MaxLOD = D3D11_FLOAT32_MAX;
        m_device->CreateSamplerState(&sd, &m_shadowComparisonSampler);
    }

    // Build root layout mirroring DX12 root signature parameter → HLSL register mapping.
    // This allows DX11 SetRootCBV to bind to the correct HLSL register instead of
    // incorrectly using the root param index as the register number.
    {
        m_rootLayout.paramCount = 0;
        auto& p0 = m_rootLayout.params[m_rootLayout.paramCount++];
        p0.type = RHIRootParamDesc::Type::Constants;
        p0.shaderRegister = 0;   // b0
        p0.registerSpace = 0;
        p0.num32BitValues = 4;

        auto& p1 = m_rootLayout.params[m_rootLayout.paramCount++];
        p1.type = RHIRootParamDesc::Type::CBV;
        p1.shaderRegister = 1;   // b1 — PerFrame
        p1.registerSpace = 0;

        auto& p2 = m_rootLayout.params[m_rootLayout.paramCount++];
        p2.type = RHIRootParamDesc::Type::CBV;
        p2.shaderRegister = 2;   // b2 — PerObject
        p2.registerSpace = 0;

        auto& p3 = m_rootLayout.params[m_rootLayout.paramCount++];
        p3.type = RHIRootParamDesc::Type::CBV;
        p3.shaderRegister = 4;   // b4 — Shadow (NOT b3!)
        p3.registerSpace = 0;

        auto& p4 = m_rootLayout.params[m_rootLayout.paramCount++];
        p4.type = RHIRootParamDesc::Type::UAV_Table;
        p4.shaderRegister = 0;   // u0
        p4.registerSpace = 0;

        auto& p5 = m_rootLayout.params[m_rootLayout.paramCount++];
        p5.type = RHIRootParamDesc::Type::SRV_Table;
        p5.shaderRegister = 0;   // t0 — SM5 SRV table
        p5.registerSpace = 0;
    }

    RHIMemoryBudget budget = GetMemoryBudget();
    UZ_LOG(RHI, Info, "DX11Device initialized: adapter='{}', featureLevel={}, SM={}, VRAM={:.0f}MB, rootParams={}",
           m_adapterName, m_featureLevelStr, ShaderModelToString(m_shaderModel),
           static_cast<double>(budget.budgetBytes) / (1024.0 * 1024.0),
           m_rootLayout.paramCount);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Steps
// ─────────────────────────────────────────────────────────────────────────────

bool DX11Device::EnableDebugLayer() {
    // DX11 debug layer is enabled via D3D11_CREATE_DEVICE_DEBUG flag at device creation,
    // not via a separate call. This method just logs intent.
    UZ_LOG(RHI, Info, "DX11 debug layer will be enabled at device creation");
    return true;
}

bool DX11Device::CreateFactory() {
    UZ_PROFILE_FUNCTION();
    UINT flags = 0;
    if (m_validationEnabled) {
        flags = DXGI_CREATE_FACTORY_DEBUG;
    }

    HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateDXGIFactory2 failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    return true;
}

bool DX11Device::SelectAdapter(uint32_t adapterIndex) {
    UZ_PROFILE_FUNCTION();
    // Try EnumAdapterByGpuPreference via IDXGIFactory6 first
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(m_factory.As(&factory6))) {
        HRESULT hr = factory6->EnumAdapterByGpuPreference(
            adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_adapter));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "No GPU adapter found at index {}", adapterIndex);
            return false;
        }
    } else {
        // Fallback: enumerate by index
        HRESULT hr = m_factory->EnumAdapters1(adapterIndex, &m_adapter);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "No GPU adapter found at index {}", adapterIndex);
            return false;
        }
    }

    // Query IDXGIAdapter3 for memory budget queries
    m_adapter.As(&m_adapter3);

    DXGI_ADAPTER_DESC1 adapterDesc = {};
    m_adapter->GetDesc1(&adapterDesc);
    m_adapterName = WideToUtf8(adapterDesc.Description);

    UZ_LOG(RHI, Info, "Selected adapter [{}]: '{}'", adapterIndex, m_adapterName);
    return true;
}

bool DX11Device::CreateDevice() {
    UZ_PROFILE_FUNCTION();
    UINT createFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
    if (m_validationEnabled) {
        createFlags |= D3D11_CREATE_DEVICE_DEBUG;
    }

    static constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    ComPtr<ID3D11Device> baseDevice;
    ComPtr<ID3D11DeviceContext> baseContext;
    D3D_FEATURE_LEVEL achievedLevel = D3D_FEATURE_LEVEL_11_0;

    HRESULT hr = D3D11CreateDevice(
        m_adapter.Get(),
        D3D_DRIVER_TYPE_UNKNOWN,   // Must be UNKNOWN when passing an adapter
        nullptr,                    // No software rasterizer
        createFlags,
        kFeatureLevels,
        _countof(kFeatureLevels),
        D3D11_SDK_VERSION,
        &baseDevice,
        &achievedLevel,
        &baseContext);

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "D3D11CreateDevice failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Query the 11.1 interface
    hr = baseDevice.As(&m_device);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to query ID3D11Device1, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    hr = baseContext.As(&m_immediateContext);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to query ID3D11DeviceContext1, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    m_featureLevel = achievedLevel;
    m_featureLevelStr = FeatureLevelToString(achievedLevel);

    UZ_LOG(RHI, Info, "D3D11 device created at {}", m_featureLevelStr);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Object Creation — SwapChain, Fence, CommandList
// ─────────────────────────────────────────────────────────────────────────────

IRHISwapChain* DX11Device::CreateSwapChain(const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();
    auto* sc = new DX11SwapChain();
    if (!sc->Init(this, desc)) {
        delete sc;
        return nullptr;
    }
    return sc;
}

void DX11Device::DestroySwapChain(IRHISwapChain* swapChain) {
    delete static_cast<DX11SwapChain*>(swapChain);
}

IRHIFence* DX11Device::CreateFence() {
    UZ_PROFILE_FUNCTION();
    auto* fence = new DX11Fence();
    if (!fence->Init(this)) {
        delete fence;
        return nullptr;
    }
    return fence;
}

void DX11Device::DestroyFence(IRHIFence* fence) {
    delete static_cast<DX11Fence*>(fence);
}

IRHICommandList* DX11Device::CreateCommandList(RHIQueueType queue) {
    UZ_PROFILE_FUNCTION();
    auto* cmdList = new DX11CommandList();
    if (!cmdList->Init(this, queue)) {
        delete cmdList;
        return nullptr;
    }
    return cmdList;
}

void DX11Device::DestroyCommandList(IRHICommandList* cmdList) {
    delete static_cast<DX11CommandList*>(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// Synchronization
// ─────────────────────────────────────────────────────────────────────────────

void DX11Device::WaitIdle() {
    UZ_PROFILE_FUNCTION();
    if (!m_immediateContext || !m_device) return;

    m_immediateContext->Flush();

    // Create an event query to confirm all GPU work is complete
    D3D11_QUERY_DESC qd = {};
    qd.Query = D3D11_QUERY_EVENT;
    ComPtr<ID3D11Query> query;
    HRESULT hr = m_device->CreateQuery(&qd, &query);
    if (FAILED(hr)) return;

    m_immediateContext->End(query.Get());

    BOOL complete = FALSE;
    while (m_immediateContext->GetData(query.Get(), &complete, sizeof(complete), 0) == S_FALSE) {
        Sleep(0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Debug Messages
// ─────────────────────────────────────────────────────────────────────────────

void DX11Device::FlushDebugMessages() {
    if (!m_device) return;

    ComPtr<ID3D11InfoQueue> infoQueue;
    HRESULT hr = m_device->QueryInterface(IID_PPV_ARGS(&infoQueue));
    if (FAILED(hr) || !infoQueue) return;

    const UINT64 msgCount = infoQueue->GetNumStoredMessages();
    for (UINT64 i = 0; i < msgCount; ++i) {
        SIZE_T msgLen = 0;
        infoQueue->GetMessage(i, nullptr, &msgLen);
        if (msgLen == 0) continue;

        std::vector<uint8_t> buf(msgLen);
        auto* msg = reinterpret_cast<D3D11_MESSAGE*>(buf.data());
        if (SUCCEEDED(infoQueue->GetMessage(i, msg, &msgLen))) {
            switch (msg->Severity) {
                case D3D11_MESSAGE_SEVERITY_CORRUPTION:
                case D3D11_MESSAGE_SEVERITY_ERROR:
                    UZ_LOG(RHI, Error, "D3D11 Debug: {}", msg->pDescription);
                    break;
                case D3D11_MESSAGE_SEVERITY_WARNING:
                    UZ_LOG(RHI, Warning, "D3D11 Debug: {}", msg->pDescription);
                    break;
                default:
                    UZ_LOG(RHI, Debug, "D3D11 Debug: {}", msg->pDescription);
                    break;
            }
        }
    }
    infoQueue->ClearStoredMessages();
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Queries
// ─────────────────────────────────────────────────────────────────────────────

const char* DX11Device::GetAdapterName() const {
    return m_adapterName.c_str();
}

const char* DX11Device::GetFeatureLevel() const {
    return m_featureLevelStr.c_str();
}

RHIMemoryBudget DX11Device::GetMemoryBudget() const {
    RHIMemoryBudget budget = {};
    if (!m_adapter3) return budget;

    DXGI_QUERY_VIDEO_MEMORY_INFO memInfo = {};
    HRESULT hr = m_adapter3->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo);
    if (SUCCEEDED(hr)) {
        budget.budgetBytes = memInfo.Budget;
        budget.usageBytes = memInfo.CurrentUsage;
    }
    return budget;
}

} // namespace uz
