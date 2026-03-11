#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_device.h"
#include "dx12_swapchain.h"
#include "dx12_fence.h"
#include "dx12_command_list.h"
#include "dx12_resource_creation.h"

#include "D3D12MemAlloc.h"
#include "d3dx12.h"
#include "uz_log.h"

#include <windows.h>
#include <dxgidebug.h>
#include <string>

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
        case D3D_FEATURE_LEVEL_12_2: return "D3D_FEATURE_LEVEL_12_2";
        case D3D_FEATURE_LEVEL_12_1: return "D3D_FEATURE_LEVEL_12_1";
        case D3D_FEATURE_LEVEL_12_0: return "D3D_FEATURE_LEVEL_12_0";
        default:                     return "D3D_FEATURE_LEVEL_UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

DX12Device::~DX12Device() {
    WaitIdle();

    // Close the fence event handle
    if (m_idleFenceEvent) {
        CloseHandle(m_idleFenceEvent);
        m_idleFenceEvent = nullptr;
    }

    // Release D3D12MA allocator before device
    if (m_allocator) {
        m_allocator->Release();
        m_allocator = nullptr;
    }

    // Release heaps, queues (ComPtr handles automatically)
    m_cbvSrvUavHeap.Reset();
    m_samplerHeap.Reset();
    m_rtvHeap.Reset();
    m_dsvHeap.Reset();
    m_idleFence.Reset();
    m_directQueue.Reset();
    m_copyQueue.Reset();

    // Report live objects in debug builds before releasing device
    if (m_validationEnabled && m_device) {
        ComPtr<ID3D12DebugDevice> debugDevice;
        if (SUCCEEDED(m_device.As(&debugDevice))) {
            debugDevice->ReportLiveDeviceObjects(
                D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL);
        }
    }

    m_device.Reset();
    m_adapter.Reset();
    m_factory.Reset();

    UZ_LOG(RHI, Info, "DX12Device destroyed");
}

bool DX12Device::Init(const RHIDeviceDesc& desc) {
    m_validationEnabled = desc.enableValidation;

    // Order matters: debug layer must be enabled before device creation
    if (desc.enableValidation) {
        if (!EnableDebugLayer(desc.enableGpuValidation)) {
            UZ_LOG(RHI, Warning, "Failed to enable DX12 debug layer, continuing without validation");
        }
    }

    if (!CreateFactory(desc.enableValidation))  return false;
    if (!SelectAdapter(desc.adapterIndex))      return false;
    if (!CreateDevice())                        return false;

    CheckSM66Support();

    if (!CreateAllocator())                     return false;
    if (!CreateDescriptorHeaps())               return false;
    InitDescriptorAllocators();
    if (!CreateCommandQueues())                 return false;

    // WaitIdle fence
    HRESULT hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_idleFence));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create idle fence, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    m_idleFenceEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!m_idleFenceEvent) {
        UZ_LOG(RHI, Error, "Failed to create idle fence event");
        return false;
    }

    if (desc.enableValidation) {
        EnableDRED();
    }

    // Log device info
    RHIMemoryBudget budget = GetMemoryBudget();
    UZ_LOG(RHI, Info, "DX12Device initialized: adapter='{}', featureLevel={}, VRAM={:.0f}MB (used {:.0f}MB)",
           m_adapterName, m_featureLevelStr,
           static_cast<double>(budget.budgetBytes) / (1024.0 * 1024.0),
           static_cast<double>(budget.usageBytes) / (1024.0 * 1024.0));

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization Steps
// ─────────────────────────────────────────────────────────────────────────────

bool DX12Device::EnableDebugLayer(bool gpuValidation) {
    ComPtr<ID3D12Debug3> debug;
    HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Warning, "D3D12GetDebugInterface failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    debug->EnableDebugLayer();
    UZ_LOG(RHI, Info, "DX12 debug layer enabled");

    if (gpuValidation) {
        debug->SetEnableGPUBasedValidation(TRUE);
        UZ_LOG(RHI, Info, "DX12 GPU-based validation enabled (performance will be significantly reduced)");
    }

    return true;
}

bool DX12Device::CreateFactory(bool validation) {
    UINT flags = 0;
    if (validation) {
        flags = DXGI_CREATE_FACTORY_DEBUG;
    }

    HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateDXGIFactory2 failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    return true;
}

bool DX12Device::SelectAdapter(uint32_t adapterIndex) {
    ComPtr<IDXGIAdapter1> adapter1;
    HRESULT hr = m_factory->EnumAdapterByGpuPreference(
        adapterIndex, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter1));

    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "No GPU adapter found at index {}", adapterIndex);
        return false;
    }

    hr = adapter1.As(&m_adapter);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to query IDXGIAdapter4, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    DXGI_ADAPTER_DESC1 adapterDesc = {};
    m_adapter->GetDesc1(&adapterDesc);
    m_adapterName = WideToUtf8(adapterDesc.Description);

    UZ_LOG(RHI, Info, "Selected adapter [{}]: '{}'", adapterIndex, m_adapterName);
    return true;
}

bool DX12Device::CreateDevice() {
    // Try feature levels in descending order
    static constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };

    for (D3D_FEATURE_LEVEL level : kFeatureLevels) {
        HRESULT hr = D3D12CreateDevice(m_adapter.Get(), level, IID_PPV_ARGS(&m_device));
        if (SUCCEEDED(hr)) {
            m_featureLevel = level;
            m_featureLevelStr = FeatureLevelToString(level);
            UZ_LOG(RHI, Info, "D3D12 device created at {}", m_featureLevelStr);
            return true;
        }
        UZ_LOG(RHI, Warning, "D3D12CreateDevice failed at {}, hr=0x{:08X}",
               FeatureLevelToString(level), static_cast<uint32_t>(hr));
    }

    UZ_LOG(RHI, Error, "Failed to create D3D12 device at any feature level (minimum is 12_0)");
    return false;
}

bool DX12Device::CheckSM66Support() {
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    HRESULT hr = m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Warning, "Failed to query D3D12_OPTIONS feature support");
        return false;
    }

    if (options.ResourceBindingTier < D3D12_RESOURCE_BINDING_TIER_3) {
        UZ_LOG(RHI, Warning, "Resource binding tier {} < 3 — SM6.6 bindless may not be fully supported",
               static_cast<int>(options.ResourceBindingTier));
        return false;
    }

    UZ_LOG(RHI, Info, "Resource binding tier 3 confirmed (SM6.6 bindless supported)");
    return true;
}

bool DX12Device::CreateAllocator() {
    D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
    allocatorDesc.pDevice = m_device.Get();
    allocatorDesc.pAdapter = m_adapter.Get();
    allocatorDesc.Flags = D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED;

    HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_allocator);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "D3D12MA::CreateAllocator failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    UZ_LOG(RHI, Info, "D3D12MA allocator created");
    return true;
}

bool DX12Device::CreateDescriptorHeaps() {
    if (!CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
                              kMaxCbvSrvUavDescriptors, true, m_cbvSrvUavHeap))
        return false;

    if (!CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER,
                              kMaxSamplerDescriptors, true, m_samplerHeap))
        return false;

    if (!CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                              kMaxRtvDescriptors, false, m_rtvHeap))
        return false;

    if (!CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE_DSV,
                              kMaxDsvDescriptors, false, m_dsvHeap))
        return false;

    UZ_LOG(RHI, Info, "Descriptor heaps created: CBV_SRV_UAV={}, Sampler={}, RTV={}, DSV={}",
           kMaxCbvSrvUavDescriptors, kMaxSamplerDescriptors, kMaxRtvDescriptors, kMaxDsvDescriptors);
    return true;
}

bool DX12Device::CreateDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint32_t count,
                                      bool shaderVisible, ComPtr<ID3D12DescriptorHeap>& outHeap) {
    D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
    heapDesc.Type = type;
    heapDesc.NumDescriptors = count;
    heapDesc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE
                                   : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    heapDesc.NodeMask = 0;

    HRESULT hr = m_device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&outHeap));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateDescriptorHeap failed for type={}, count={}, hr=0x{:08X}",
               static_cast<int>(type), count, static_cast<uint32_t>(hr));
        return false;
    }
    return true;
}

bool DX12Device::CreateCommandQueues() {
    // Direct (graphics) queue
    D3D12_COMMAND_QUEUE_DESC directDesc = {};
    directDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    directDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    directDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    HRESULT hr = m_device->CreateCommandQueue(&directDesc, IID_PPV_ARGS(&m_directQueue));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create direct command queue, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Copy queue
    D3D12_COMMAND_QUEUE_DESC copyDesc = {};
    copyDesc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    copyDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    copyDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = m_device->CreateCommandQueue(&copyDesc, IID_PPV_ARGS(&m_copyQueue));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create copy command queue, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    UZ_LOG(RHI, Info, "Command queues created: Direct + Copy");
    return true;
}

bool DX12Device::EnableDRED() {
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;
    HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Warning, "DRED not available on this system, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    UZ_LOG(RHI, Info, "DRED enabled (auto-breadcrumbs + page fault tracking)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Allocator Init
// ─────────────────────────────────────────────────────────────────────────────

void DX12Device::InitDescriptorAllocators() {
    uint32_t cbvSrvUavIncrement = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    uint32_t samplerIncrement = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
    uint32_t rtvIncrement = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    uint32_t dsvIncrement = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_DSV);

    m_srvUavAllocator.Init(m_cbvSrvUavHeap.Get(), kMaxCbvSrvUavDescriptors, cbvSrvUavIncrement);
    m_samplerAllocator.Init(m_samplerHeap.Get(), kMaxSamplerDescriptors, samplerIncrement);
    // Reserve first 3 RTV slots for swapchain back buffers
    m_rtvAllocator.Init(m_rtvHeap.Get(), kMaxRtvDescriptors, rtvIncrement, 3);
    m_dsvAllocator.Init(m_dsvHeap.Get(), kMaxDsvDescriptors, dsvIncrement);
}

DescriptorAllocator& DX12Device::GetDescriptorAllocator(D3D12_DESCRIPTOR_HEAP_TYPE type) {
    switch (type) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return m_srvUavAllocator;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:      return m_samplerAllocator;
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:          return m_rtvAllocator;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:          return m_dsvAllocator;
        default:                                      return m_srvUavAllocator;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource Creation (Task 2.3)
// ─────────────────────────────────────────────────────────────────────────────

RHIBufferHandle DX12Device::CreateBuffer(const RHIBufferDesc& desc) {
    uint32_t generation = 0;
    uint32_t index = m_bufferPool.Allocate(generation);
    DX12Buffer* slot = m_bufferPool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateBuffer: pool allocation failed");
        return RHIBufferHandle::Invalid();
    }

    if (!CreateDX12Buffer(m_device.Get(), m_allocator, m_srvUavAllocator, desc, *slot)) {
        m_bufferPool.Free(index);
        return RHIBufferHandle::Invalid();
    }

    slot->generation = generation;
    return {index, generation};
}

void DX12Device::DestroyBuffer(RHIBufferHandle handle) {
    DX12Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) {
        UZ_LOG(RHI, Warning, "DestroyBuffer: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    DestroyDX12Buffer(*buf, m_srvUavAllocator);
    m_bufferPool.Free(handle.index);
}

RHITextureHandle DX12Device::CreateTexture(const RHITextureDesc& desc) {
    uint32_t generation = 0;
    uint32_t index = m_texturePool.Allocate(generation);
    DX12Texture* slot = m_texturePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateTexture: pool allocation failed");
        return RHITextureHandle::Invalid();
    }

    if (!CreateDX12Texture(m_device.Get(), m_allocator, m_srvUavAllocator,
                           m_rtvAllocator, m_dsvAllocator, desc, *slot)) {
        m_texturePool.Free(index);
        return RHITextureHandle::Invalid();
    }

    slot->generation = generation;
    return {index, generation};
}

void DX12Device::DestroyTexture(RHITextureHandle handle) {
    DX12Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) {
        UZ_LOG(RHI, Warning, "DestroyTexture: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    DestroyDX12Texture(*tex, m_srvUavAllocator, m_rtvAllocator, m_dsvAllocator);
    m_texturePool.Free(handle.index);
}

RHIShaderHandle DX12Device::CreateShader(const RHIShaderDesc& desc) {
    if (!desc.bytecode || desc.bytecodeSize == 0) {
        UZ_LOG(RHI, Error, "CreateShader: null/empty bytecode (name='{}')",
               desc.debugName ? desc.debugName : "");
        return RHIShaderHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_shaderPool.Allocate(generation);
    DX12Shader* slot = m_shaderPool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateShader: pool allocation failed");
        return RHIShaderHandle::Invalid();
    }

    // Copy bytecode — the caller's pointer may not persist
    auto* bytes = static_cast<const uint8_t*>(desc.bytecode);
    slot->bytecode.assign(bytes, bytes + desc.bytecodeSize);
    slot->stage = desc.stage;
    slot->generation = generation;

    UZ_LOG(RHI, Debug, "CreateShader: stage={}, size={} (name='{}')",
           static_cast<int>(desc.stage), desc.bytecodeSize,
           desc.debugName ? desc.debugName : "");

    return {index, generation};
}

void DX12Device::DestroyShader(RHIShaderHandle handle) {
    DX12Shader* shader = m_shaderPool.Get(handle.index, handle.generation);
    if (!shader) {
        UZ_LOG(RHI, Warning, "DestroyShader: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    shader->bytecode.clear();
    shader->bytecode.shrink_to_fit();
    m_shaderPool.Free(handle.index);
}

RHIPipelineHandle DX12Device::CreateGraphicsPipeline(const RHIGraphicsPipelineDesc& desc) {
    UZ_LOG(RHI, Warning, "CreateGraphicsPipeline not yet implemented — Phase 2.4 (name='{}')",
           desc.debugName ? desc.debugName : "");
    return RHIPipelineHandle::Invalid();
}

void DX12Device::DestroyPipeline(RHIPipelineHandle handle) {
    UZ_LOG(RHI, Warning, "DestroyPipeline not yet implemented — Phase 2.4");
}

RHISamplerHandle DX12Device::CreateSampler(const RHISamplerDesc& desc) {
    uint32_t generation = 0;
    uint32_t index = m_samplerPool.Allocate(generation);
    DX12Sampler* slot = m_samplerPool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateSampler: pool allocation failed");
        return RHISamplerHandle::Invalid();
    }

    if (!CreateDX12Sampler(m_device.Get(), m_samplerAllocator, desc, *slot)) {
        m_samplerPool.Free(index);
        return RHISamplerHandle::Invalid();
    }

    slot->generation = generation;
    return {index, generation};
}

void DX12Device::DestroySampler(RHISamplerHandle handle) {
    DX12Sampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) {
        UZ_LOG(RHI, Warning, "DestroySampler: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    DestroyDX12Sampler(*sampler, m_samplerAllocator);
    m_samplerPool.Free(handle.index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Object Stubs (Phase 3)
// ─────────────────────────────────────────────────────────────────────────────

IRHISwapChain* DX12Device::CreateSwapChain(const RHISwapChainDesc& desc) {
    auto* sc = new DX12SwapChain();
    if (!sc->Init(this, desc)) {
        delete sc;
        return nullptr;
    }
    return sc;
}

void DX12Device::DestroySwapChain(IRHISwapChain* swapChain) {
    delete static_cast<DX12SwapChain*>(swapChain);
}

IRHIFence* DX12Device::CreateFence() {
    auto* fence = new DX12Fence();
    if (!fence->Init(m_device.Get())) {
        delete fence;
        return nullptr;
    }
    return fence;
}

void DX12Device::DestroyFence(IRHIFence* fence) {
    delete static_cast<DX12Fence*>(fence);
}

IRHICommandList* DX12Device::CreateCommandList(RHIQueueType queue) {
    auto* cmdList = new DX12CommandList();
    if (!cmdList->Init(this, queue)) {
        delete cmdList;
        return nullptr;
    }
    return cmdList;
}

void DX12Device::DestroyCommandList(IRHICommandList* cmdList) {
    delete static_cast<DX12CommandList*>(cmdList);
}

// ─────────────────────────────────────────────────────────────────────────────
// Synchronization
// ─────────────────────────────────────────────────────────────────────────────

void DX12Device::WaitIdle() {
    if (!m_directQueue || !m_idleFence || !m_idleFenceEvent) return;

    m_idleFenceValue++;
    HRESULT hr = m_directQueue->Signal(m_idleFence.Get(), m_idleFenceValue);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "WaitIdle: Signal failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return;
    }

    if (m_idleFence->GetCompletedValue() < m_idleFenceValue) {
        hr = m_idleFence->SetEventOnCompletion(m_idleFenceValue, m_idleFenceEvent);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "WaitIdle: SetEventOnCompletion failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
            return;
        }
        WaitForSingleObject(m_idleFenceEvent, INFINITE);
    }

    // Also drain the copy queue
    if (m_copyQueue) {
        m_idleFenceValue++;
        hr = m_copyQueue->Signal(m_idleFence.Get(), m_idleFenceValue);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "WaitIdle: Copy queue signal failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
            return;
        }
        if (m_idleFence->GetCompletedValue() < m_idleFenceValue) {
            m_idleFence->SetEventOnCompletion(m_idleFenceValue, m_idleFenceEvent);
            WaitForSingleObject(m_idleFenceEvent, INFINITE);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Queries
// ─────────────────────────────────────────────────────────────────────────────

const char* DX12Device::GetAdapterName() const {
    return m_adapterName.c_str();
}

const char* DX12Device::GetFeatureLevel() const {
    return m_featureLevelStr.c_str();
}

RHIMemoryBudget DX12Device::GetMemoryBudget() const {
    RHIMemoryBudget budget = {};
    if (!m_adapter) return budget;

    DXGI_QUERY_VIDEO_MEMORY_INFO memInfo = {};
    HRESULT hr = m_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &memInfo);
    if (SUCCEEDED(hr)) {
        budget.budgetBytes = memInfo.Budget;
        budget.usageBytes = memInfo.CurrentUsage;
    }
    return budget;
}

ID3D12DescriptorHeap* DX12Device::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE type) const {
    switch (type) {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return m_cbvSrvUavHeap.Get();
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:      return m_samplerHeap.Get();
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV:          return m_rtvHeap.Get();
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV:          return m_dsvHeap.Get();
        default:                                      return nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Factory Functions (rhi_backend.h)
// ─────────────────────────────────────────────────────────────────────────────

IRHIDevice* CreateRHIDevice(const RHIDeviceDesc& desc) {
    if (desc.backend != RHIBackend::DX12) {
        UZ_LOG(RHI, Error, "CreateRHIDevice: requested backend is not DX12 (compiled with UZ_BACKEND_DX12)");
        return nullptr;
    }

    auto* device = new DX12Device();
    if (!device->Init(desc)) {
        UZ_LOG(RHI, Error, "CreateRHIDevice: DX12Device::Init failed");
        delete device;
        return nullptr;
    }
    return device;
}

void DestroyRHIDevice(IRHIDevice* device) {
    delete device;
}

} // namespace uz
