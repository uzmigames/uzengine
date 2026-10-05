/// DX12 Device Implementation
///
/// Initializes the full D3D12 stack: debug layer, DXGI factory, adapter, device,
/// D3D12MA allocator, descriptor heaps, command queues, and bindless root signature.
/// All resource Create/Destroy methods delegate to dx12_resource_creation helpers.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "dx12_device.h"
#include "dx12_swapchain.h"
#include "dx12_fence.h"
#include "dx12_command_list.h"
#include "dx12_resource_creation.h"
#include "dx12_compat.h"

#include "D3D12MemAlloc.h"
#include "d3dx12.h"
#include "log.h"
#include "profiler.h"

#include <cstdio>  // fprintf/fflush for leak diag (bypasses spdlog async sinks)
#include "engine/paths/paths.h"

#include <cstring>
#include <windows.h>
#include <dxgidebug.h>
#include <d3d12sdklayers.h>
#include <string>

namespace uz {

UZ_DEFINE_STAT(RHI, PipelineCreations);

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
    UZ_PROFILE_FUNCTION();

    if (m_tracyD3D12Ctx) {
        TracyD3D12Destroy(m_tracyD3D12Ctx);
        m_tracyD3D12Ctx = nullptr;
    }

    WaitIdle();

    // Close the fence event handle
    if (m_idleFenceEvent) {
        CloseHandle(m_idleFenceEvent);
        m_idleFenceEvent = nullptr;
    }

    // Release root signature
    if (m_rootSignature) {
        m_rootSignature->Release();
        m_rootSignature = nullptr;
    }

    // Free the Agility SDK D3D12Core.dll module
    if (m_agilityCoreModule) {
        FreeLibrary(m_agilityCoreModule);
        m_agilityCoreModule = nullptr;
    }

    // Report any live allocations before releasing the allocator.
    //
    // Bypasses the engine's spdlog wrapper because the named-allocation
    // diag may be the LAST output before D3D12MA's `~NormalBlock` fires
    // its `m_pMetadata->IsEmpty()` assertion + abort. spdlog's async
    // sinks can drop the queued messages on abort; `fprintf` + explicit
    // `fflush` are synchronous and survive the imminent crash.
    if (m_allocator) {
        D3D12MA::TotalStatistics stats{};
        m_allocator->CalculateStatistics(&stats);
        const auto& total = stats.Total.Stats;
        if (total.AllocationCount > 0) {
            std::fprintf(stderr,
                "[D3D12MA-LEAK] %u live allocations (%llu bytes) at shutdown -- LEAK!\n",
                total.AllocationCount,
                static_cast<unsigned long long>(total.AllocationBytes));

            // Log per-pool live objects to identify the leak
            static const char* kHeapNames[] = {"DEFAULT", "UPLOAD", "READBACK"};
            for (uint32_t heapType = 0; heapType < 3; ++heapType) {
                const auto& heap = stats.HeapType[heapType].Stats;
                if (heap.AllocationCount > 0) {
                    std::fprintf(stderr,
                        "[D3D12MA-LEAK]   Heap %s: %u allocs, %llu bytes\n",
                        kHeapNames[heapType], heap.AllocationCount,
                        static_cast<unsigned long long>(heap.AllocationBytes));
                }
            }

            // Dump live textures for leak diagnosis
            {
                auto& slots = m_texturePool.Slots();
                for (uint32_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].allocation) {
                        std::fprintf(stderr,
                            "[D3D12MA-LEAK]   LEAKED texture [%u]: '%s' %ux%u fmt=%d (%llu bytes)\n",
                            i,
                            slots[i].desc.debugName ? slots[i].desc.debugName : "(null)",
                            slots[i].desc.width, slots[i].desc.height,
                            static_cast<int>(slots[i].desc.format),
                            static_cast<unsigned long long>(slots[i].allocation->GetSize()));
                    }
                }
            }
            // Dump live buffers
            {
                auto& slots = m_bufferPool.Slots();
                for (uint32_t i = 0; i < slots.size(); ++i) {
                    if (slots[i].allocation) {
                        std::fprintf(stderr,
                            "[D3D12MA-LEAK]   LEAKED buffer [%u]: '%s' (%llu bytes)\n",
                            i,
                            slots[i].desc.debugName ? slots[i].desc.debugName : "(null)",
                            static_cast<unsigned long long>(slots[i].allocation->GetSize()));
                    }
                }
            }
            std::fflush(stderr);
        }
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
    m_computeQueue.Reset();

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
    UZ_PROFILE_FUNCTION();
    m_validationEnabled = desc.enableValidation;

    // Try to load Agility SDK programmatically (DLL-hosted mode on Win10).
    // Must happen BEFORE any D3D12 device/factory creation so the runtime
    // picks up our D3D12Core.dll instead of the system one.
    TryLoadAgilitySdk();

    // Order matters: debug layer must be enabled before device creation
    if (desc.enableValidation) {
        if (!EnableDebugLayer(desc.enableGpuValidation)) {
            UZ_LOG(RHI, Warning, "Failed to enable DX12 debug layer, continuing without validation");
        }
    }

    // DRED settings are latched by the D3D12 runtime when the device is created,
    // so they MUST be configured before D3D12CreateDevice — configuring them
    // afterwards leaves auto-breadcrumbs permanently disabled and every
    // device-removed report empty. Breadcrumbs are useful without the debug
    // layer (they are the only way to diagnose a TDR in a release build), so
    // --gpu-breadcrumbs enables them independently of --gpu-validation.
    if (desc.enableValidation || desc.enableBreadcrumbs) {
        EnableDRED();
    }

    if (!CreateFactory(desc.enableValidation))  return false;
    if (!SelectAdapter(desc.adapterIndex))      return false;
    if (!CreateDevice())                        return false;

    NegotiateShaderModel(desc.forceShaderModel);

    // Detect root signature version support (1.0 vs 1.1).
    // D3DX12SerializeVersionedRootSignature uses this to automatically downgrade
    // 1.1 descriptors to 1.0 on older drivers that lack 1.1 support.
    {
        D3D12_FEATURE_DATA_ROOT_SIGNATURE rootSigCaps = {};
        rootSigCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
        if (FAILED(m_device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE,
                                                  &rootSigCaps, sizeof(rootSigCaps)))) {
            rootSigCaps.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
        }
        m_rootSigVersion = rootSigCaps.HighestVersion;

        // When the Agility SDK was loaded via CreateDeviceFactory, the device
        // reports root signature 1.1 support, but the GLOBAL serialization API
        // (D3D12SerializeVersionedRootSignature from system d3d12.dll) still
        // rejects SM6.6 flags (DIRECTLY_INDEXED). Force 1.0 so the d3dx12
        // helper uses D3D12SerializeRootSignature (1.0 API) which accepts
        // arbitrary flags in the Flags field without validation.
        if (m_deviceFactory && m_rootSigVersion >= D3D_ROOT_SIGNATURE_VERSION_1_1) {
            m_rootSigVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
            UZ_LOG(RHI, Info, "Root signature version: 1.0 (forced — Agility SDK device factory mode)");
        } else {
            UZ_LOG(RHI, Info, "Root signature version: {}",
                   m_rootSigVersion == D3D_ROOT_SIGNATURE_VERSION_1_1 ? "1.1" : "1.0");
        }
    }

    if (!CreateAllocator())                     return false;
    if (!CreateDescriptorHeaps())               return false;
    InitDescriptorAllocators();
    if (!CreateCommandQueues())                 return false;

    // Global root signature (adapts to negotiated shader model)
    m_rootSignature = CreateBindlessRootSignature(m_device.Get(), m_shaderModel,
                                                    m_rootSigVersion);
    if (!m_rootSignature) return false;

    // Indirect command signatures for GPU-driven rendering
    {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        HRESULT hr = m_device->CreateCommandSignature(
            &sigDesc, nullptr, IID_PPV_ARGS(&m_drawIndexedIndirectSig));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "Failed to create DrawIndexedIndirect command signature, hr=0x{:08X}",
                   static_cast<uint32_t>(hr));
            return false;
        }
    }
    {
        D3D12_INDIRECT_ARGUMENT_DESC argDesc = {};
        argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;
        D3D12_COMMAND_SIGNATURE_DESC sigDesc = {};
        sigDesc.ByteStride = sizeof(D3D12_DISPATCH_ARGUMENTS);
        sigDesc.NumArgumentDescs = 1;
        sigDesc.pArgumentDescs = &argDesc;
        HRESULT hr = m_device->CreateCommandSignature(
            &sigDesc, nullptr, IID_PPV_ARGS(&m_dispatchIndirectSig));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "Failed to create DispatchIndirect command signature, hr=0x{:08X}",
                   static_cast<uint32_t>(hr));
            return false;
        }
    }
    UZ_LOG(RHI, Info, "Created indirect command signatures (DrawIndexed + Dispatch)");

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
        // Suppress noisy debug messages and limit stored count to prevent memory exhaustion
        ComPtr<ID3D12InfoQueue> infoQueue;
        if (SUCCEEDED(m_device.As(&infoQueue))) {
            D3D12_MESSAGE_ID suppressedIds[] = {
                D3D12_MESSAGE_ID_CLEARRENDERTARGETVIEW_MISMATCHINGCLEARVALUE,
                D3D12_MESSAGE_ID_CLEARDEPTHSTENCILVIEW_MISMATCHINGCLEARVALUE,
            };
            D3D12_INFO_QUEUE_FILTER filter = {};
            filter.DenyList.NumIDs = _countof(suppressedIds);
            filter.DenyList.pIDList = suppressedIds;
            infoQueue->AddStorageFilterEntries(&filter);
            infoQueue->SetMessageCountLimit(1024);
            UZ_LOG(RHI, Info, "D3D12 info queue configured: suppressed {} noisy IDs, limit=1024 messages",
                   _countof(suppressedIds));
        }
    }

    // Detect DXR (ray tracing) support
    {
        D3D12_FEATURE_DATA_D3D12_OPTIONS5 opts5 = {};
        if (SUCCEEDED(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                     &opts5, sizeof(opts5)))) {
            m_supportsRayTracing = (opts5.RaytracingTier >= D3D12_RAYTRACING_TIER_1_0);
        }
        UZ_LOG(RHI, Info, "DXR ray tracing: {}", m_supportsRayTracing ? "supported" : "not supported");
    }

    // Initialize GPU timestamp profiler (non-fatal)
    if (!m_gpuProfiler.Init(this)) {
        UZ_LOG(RHI, Warning, "GPU profiler init failed — GPU timing disabled");
    }

    // Initialize Tracy GPU profiling context
    m_tracyD3D12Ctx = TracyD3D12Context(m_device.Get(), m_directQueue.Get());

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

bool DX12Device::TryLoadAgilitySdk() {
    // Use the programmatic SDK configuration API (Win10 20H2+ / Build 19042+)
    // to point the D3D12 runtime at our bundled D3D12Core.dll. This is the only
    // way to get SM6.6+ when running as a DLL inside a host process (VSCode),
    // because the __declspec(dllexport) D3D12SDKVersion trick requires the
    // symbols to be exported from the main EXE.

    // Load D3D12GetInterface via GetProcAddress for compatibility with older
    // Win10 builds where the function does not exist in d3d12.dll.
    HMODULE d3d12Module = GetModuleHandleA("d3d12.dll");
    if (!d3d12Module) {
        d3d12Module = LoadLibraryA("d3d12.dll");
    }
    if (!d3d12Module) {
        UZ_LOG(RHI, Info, "Agility SDK: d3d12.dll not loaded, skipping programmatic configuration");
        return false;
    }

    auto pfnGetInterface = reinterpret_cast<PFN_D3D12_GET_INTERFACE>(
        GetProcAddress(d3d12Module, "D3D12GetInterface"));
    if (!pfnGetInterface) {
        // D3D12GetInterface is unavailable — pre-20H2 Windows 10.
        // Fall back to the system D3D12 runtime (dllexport path may still work for EXE builds).
        UZ_LOG(RHI, Info, "Agility SDK: D3D12GetInterface not found, using system D3D12");
        return false;
    }

    // Query the SDK configuration interface
    ID3D12SDKConfiguration* sdkConfig = nullptr;
    HRESULT hr = pfnGetInterface(CLSID_D3D12SDKConfiguration,
                                 IID_ID3D12SDKConfiguration,
                                 reinterpret_cast<void**>(&sdkConfig));
    if (FAILED(hr) || !sdkConfig) {
        UZ_LOG(RHI, Info, "Agility SDK: ID3D12SDKConfiguration unavailable (hr=0x{:08X})",
               static_cast<uint32_t>(hr));
        return false;
    }

    // Try ID3D12SDKConfiguration1 which supports CreateDeviceFactory.
    // This works even when another D3D12 device already exists in the process
    // (e.g., VSCode/Electron), where SetSDKVersion fails with 0x887A0001
    // because the global SDK version is already locked by the host's device.
    ID3D12SDKConfiguration1* sdkConfig1 = nullptr;
    hr = sdkConfig->QueryInterface(IID_ID3D12SDKConfiguration1,
                                   reinterpret_cast<void**>(&sdkConfig1));
    sdkConfig->Release();

    if (FAILED(hr) || !sdkConfig1) {
        UZ_LOG(RHI, Info, "Agility SDK: ID3D12SDKConfiguration1 not available — using system D3D12");
        return false;
    }

    // Build the SDK path — the directory containing D3D12Core.dll.
    // GetExeDir() respects the SetEngineDir() override, so in DLL-hosted mode
    // it returns the addon directory (where D3D12Core.dll is deployed), not
    // the host process directory (e.g., VS Code's install dir).
    std::string sdkPath = GetExeDir().string() + "\\";

    // Create a device factory scoped to our Agility SDK version.
    // This bypasses the global SetSDKVersion limitation where another device
    // in the process (VSCode/Electron) prevents SDK version changes.
    ID3D12DeviceFactory* factory = nullptr;
    hr = sdkConfig1->CreateDeviceFactory(D3D12_SDK_VERSION, sdkPath.c_str(),
                                         IID_ID3D12DeviceFactory,
                                         reinterpret_cast<void**>(&factory));
    sdkConfig1->Release();

    if (FAILED(hr) || !factory) {
        UZ_LOG(RHI, Warning, "Agility SDK CreateDeviceFactory failed (hr=0x{:08X}), "
               "falling back to system D3D12", static_cast<uint32_t>(hr));
        return false;
    }

    m_deviceFactory.Attach(factory);
    UZ_LOG(RHI, Info, "Agility SDK loaded via CreateDeviceFactory (version={}, path='{}')",
           D3D12_SDK_VERSION, sdkPath);

    // Keep the D3D12Core.dll loaded so the Agility SDK runtime remains active
    // for the lifetime of the device. Root signature serialization is now handled
    // by D3DX12SerializeVersionedRootSignature (d3dx12 helper) which automatically
    // falls back from 1.1 to 1.0 when needed — no manual DLL function resolution required.
    std::string coreDllPath = sdkPath + "D3D12Core.dll";
    m_agilityCoreModule = LoadLibraryA(coreDllPath.c_str());
    if (m_agilityCoreModule) {
        UZ_LOG(RHI, Info, "Loaded D3D12Core.dll from '{}'", coreDllPath);
    } else {
        UZ_LOG(RHI, Warning, "Failed to load D3D12Core.dll from '{}'", coreDllPath);
    }

    return true;
}

bool DX12Device::EnableDebugLayer(bool gpuValidation) {
    UZ_PROFILE_FUNCTION();
    // Same runtime-mismatch caveat as EnableDRED(): with the Agility SDK active
    // the debug layer must be enabled on the device factory's runtime, not on
    // the system d3d12.dll, or validation silently never engages.
    ComPtr<ID3D12Debug3> debug;
    HRESULT hr;
    if (m_deviceFactory) {
        hr = m_deviceFactory->GetConfigurationInterface(CLSID_D3D12Debug, IID_PPV_ARGS(&debug));
    } else {
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    }
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "D3D12 debug interface unavailable, hr=0x{:08X}{}",
               static_cast<uint32_t>(hr),
               m_deviceFactory ? " (via Agility device factory)" : "");
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
    UZ_PROFILE_FUNCTION();

    // Try DXGI_CREATE_FACTORY_DEBUG first when validation is requested. On
    // hosts without the DXGI Debug runtime installed (some Win11 SKUs, fresh
    // dev images) this returns DXGI_ERROR_INVALID_CALL (0x887A0001) — fall
    // back to flags=0 so the D3D12 debug layer (already enabled separately)
    // still surfaces InfoQueue messages without forcing every dev to install
    // the optional Graphics Tools feature.
    UINT flags = validation ? DXGI_CREATE_FACTORY_DEBUG : 0u;
    HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&m_factory));
    if (FAILED(hr) && validation) {
        UZ_LOG(RHI, Warning,
               "CreateDXGIFactory2 with DXGI_CREATE_FACTORY_DEBUG failed "
               "(hr=0x{:08X}); retrying without DXGI debug flag — D3D12 "
               "debug layer + InfoQueue still available.",
               static_cast<uint32_t>(hr));
        hr = CreateDXGIFactory2(0u, IID_PPV_ARGS(&m_factory));
    }
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateDXGIFactory2 failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }
    return true;
}

bool DX12Device::SelectAdapter(uint32_t adapterIndex) {
    UZ_PROFILE_FUNCTION();

    // Log all available adapters for diagnostics
    for (UINT i = 0; ; i++) {
        ComPtr<IDXGIAdapter1> enumAdapter;
        if (FAILED(m_factory->EnumAdapterByGpuPreference(
                i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&enumAdapter)))) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc = {};
        enumAdapter->GetDesc1(&desc);
        bool isSoftware = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        UZ_LOG(RHI, Info, "  GPU [{}]: '{}' VRAM={} MB{}",
               i, WideToUtf8(desc.Description),
               desc.DedicatedVideoMemory / (1024 * 1024),
               isSoftware ? " (SOFTWARE)" : "");
    }

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
    UZ_PROFILE_FUNCTION();
    static constexpr D3D_FEATURE_LEVEL kFeatureLevels[] = {
        D3D_FEATURE_LEVEL_12_2,
        D3D_FEATURE_LEVEL_12_1,
        D3D_FEATURE_LEVEL_12_0,
    };

    for (D3D_FEATURE_LEVEL level : kFeatureLevels) {
        HRESULT hr;
        if (m_deviceFactory) {
            // Use the Agility SDK device factory — this creates a device using our
            // bundled D3D12Core.dll even when a host process (VSCode/Electron)
            // already has a device on the system runtime.
            hr = m_deviceFactory->CreateDevice(m_adapter.Get(), level, IID_PPV_ARGS(&m_device));
        } else {
            hr = D3D12CreateDevice(m_adapter.Get(), level, IID_PPV_ARGS(&m_device));
        }
        if (SUCCEEDED(hr)) {
            m_featureLevel = level;
            m_featureLevelStr = FeatureLevelToString(level);
            UZ_LOG(RHI, Info, "D3D12 device created at {}{}", m_featureLevelStr,
                   m_deviceFactory ? " (via Agility SDK factory)" : "");
            return true;
        }
        UZ_LOG(RHI, Warning, "D3D12CreateDevice failed at {}, hr=0x{:08X}",
               FeatureLevelToString(level), static_cast<uint32_t>(hr));
    }

    UZ_LOG(RHI, Error, "Failed to create D3D12 device at any feature level (minimum is 12_0)");
    return false;
}

/// Probes GPU capabilities and negotiates the highest supported shader model.
///
/// Probe order: SM6.9 → SM6.8 → SM6.7 → SM6.6 → SM6.0 → SM5.1 → SM5.0.
/// SM6.6+ requires Resource Binding Tier 3 (bindless).
/// SM6.0 requires D3D_SHADER_MODEL_6_0.
/// SM5.1 requires feature level 12_0 (always true if device was created).
/// SM5.0 is the absolute fallback.
void DX12Device::NegotiateShaderModel(std::optional<ShaderModel> forceModel) {
    UZ_PROFILE_FUNCTION();
    // Check resource binding tier for SM6.6+ bindless
    D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
    bool hasTier3 = false;
    HRESULT hr = m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options));
    if (SUCCEEDED(hr)) {
        hasTier3 = (options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3);
    }

    // Probe shader model progressively — older d3d12.dll (Windows 10 without
    // Agility SDK) rejects unknown SM values instead of clamping them down.
    // Try from highest to lowest until CheckFeatureSupport succeeds.
    static constexpr D3D_SHADER_MODEL kShaderModelProbe[] = {
        D3D_SHADER_MODEL_6_9,
        D3D_SHADER_MODEL_6_8,
        D3D_SHADER_MODEL_6_7,
        D3D_SHADER_MODEL_6_6,
        D3D_SHADER_MODEL_6_5,
        D3D_SHADER_MODEL_6_4,
        D3D_SHADER_MODEL_6_3,
        D3D_SHADER_MODEL_6_2,
        D3D_SHADER_MODEL_6_1,
        D3D_SHADER_MODEL_6_0,
        D3D_SHADER_MODEL_5_1,
    };
    D3D12_FEATURE_DATA_SHADER_MODEL smData = {};
    smData.HighestShaderModel = D3D_SHADER_MODEL_5_1;
    for (D3D_SHADER_MODEL probe : kShaderModelProbe) {
        D3D12_FEATURE_DATA_SHADER_MODEL test = {};
        test.HighestShaderModel = probe;
        hr = m_device->CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL, &test, sizeof(test));
        if (SUCCEEDED(hr)) {
            smData.HighestShaderModel = test.HighestShaderModel;
            break;
        }
    }

    // Negotiate: pick the highest SM the hardware actually supports
    if (hasTier3 && smData.HighestShaderModel >= D3D_SHADER_MODEL_6_9) {
        m_shaderModel = ShaderModel::SM6_9;
    } else if (hasTier3 && smData.HighestShaderModel >= D3D_SHADER_MODEL_6_8) {
        m_shaderModel = ShaderModel::SM6_8;
    } else if (hasTier3 && smData.HighestShaderModel >= D3D_SHADER_MODEL_6_7) {
        m_shaderModel = ShaderModel::SM6_7;
    } else if (hasTier3 && smData.HighestShaderModel >= D3D_SHADER_MODEL_6_6) {
        m_shaderModel = ShaderModel::SM6_6;
    } else if (smData.HighestShaderModel >= D3D_SHADER_MODEL_6_0) {
        m_shaderModel = ShaderModel::SM6_0;
    } else if (smData.HighestShaderModel >= D3D_SHADER_MODEL_5_1) {
        m_shaderModel = ShaderModel::SM5_1;
    } else {
        m_shaderModel = ShaderModel::SM5_0;
    }

    // Force override — allows both downgrade (testing fallback paths) and upgrade
    // (Win10 without Agility SDK reports SM6.5 max but hardware supports SM6.6+).
    if (forceModel.has_value() && *forceModel != m_shaderModel) {
        UZ_LOG(RHI, Warning, "Shader model forced: {} → {} (override active)",
               ShaderModelToString(m_shaderModel), ShaderModelToString(*forceModel));
        m_shaderModel = *forceModel;
    }

    UZ_LOG(RHI, Info, "Shader model negotiated: {} (binding tier={}, device max=0x{:X})",
           ShaderModelToString(m_shaderModel),
           static_cast<int>(options.ResourceBindingTier),
           static_cast<int>(smData.HighestShaderModel));
}

bool DX12Device::CreateAllocator() {
    UZ_PROFILE_FUNCTION();
    D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
    allocatorDesc.pDevice = m_device.Get();
    allocatorDesc.pAdapter = m_adapter.Get();
    allocatorDesc.Flags = static_cast<D3D12MA::ALLOCATOR_FLAGS>(
        D3D12MA::ALLOCATOR_FLAG_DEFAULT_POOLS_NOT_ZEROED
        | D3D12MA::ALLOCATOR_FLAG_DONT_USE_TIGHT_ALIGNMENT);

    HRESULT hr = D3D12MA::CreateAllocator(&allocatorDesc, &m_allocator);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "D3D12MA::CreateAllocator failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    UZ_LOG(RHI, Info, "D3D12MA allocator created");
    return true;
}

bool DX12Device::CreateDescriptorHeaps() {
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
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

    // Copy fence for async upload → graphics synchronization
    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_copyFence));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create copy fence, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Compute queue
    D3D12_COMMAND_QUEUE_DESC computeDesc = {};
    computeDesc.Type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
    computeDesc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    computeDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;

    hr = m_device->CreateCommandQueue(&computeDesc, IID_PPV_ARGS(&m_computeQueue));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create compute command queue, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    // Compute fence for async compute → graphics synchronization
    hr = m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_computeFence));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "Failed to create compute fence, hr=0x{:08X}", static_cast<uint32_t>(hr));
        return false;
    }

    UZ_LOG(RHI, Info, "Command queues created: Direct + Copy + Compute (with fences)");
    return true;
}

bool DX12Device::EnableDRED() {
    UZ_PROFILE_FUNCTION();
    ComPtr<ID3D12DeviceRemovedExtendedDataSettings1> dredSettings;

    // The global D3D12GetDebugInterface configures the SYSTEM d3d12.dll runtime.
    // When the Agility SDK is active the device comes from the device factory,
    // backed by our bundled D3D12Core.dll — a different runtime that never sees
    // those settings, leaving DRED disarmed and every GetAutoBreadcrumbsOutput1
    // returning DXGI_ERROR_UNSUPPORTED. Configure the factory's own runtime.
    HRESULT hr;
    if (m_deviceFactory) {
        hr = m_deviceFactory->GetConfigurationInterface(CLSID_D3D12DeviceRemovedExtendedData,
                                                        IID_PPV_ARGS(&dredSettings));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DRED unavailable via Agility device factory, hr=0x{:08X}",
                   static_cast<uint32_t>(hr));
            return false;
        }
    } else {
        hr = D3D12GetDebugInterface(IID_PPV_ARGS(&dredSettings));
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "DRED not available on this system, hr=0x{:08X}",
                   static_cast<uint32_t>(hr));
            return false;
        }
    }

    dredSettings->SetAutoBreadcrumbsEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dredSettings->SetPageFaultEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dredSettings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    UZ_LOG(RHI, Info, "DRED enabled (auto-breadcrumbs + page fault tracking)");
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Device Removed Diagnostics
// ─────────────────────────────────────────────────────────────────────────────

/// Converts D3D12_AUTO_BREADCRUMB_OP to human-readable string.
static const char* BreadcrumbOpToString(D3D12_AUTO_BREADCRUMB_OP op) {
    switch (op) {
        case D3D12_AUTO_BREADCRUMB_OP_SETMARKER:           return "SetMarker";
        case D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT:          return "BeginEvent";
        case D3D12_AUTO_BREADCRUMB_OP_ENDEVENT:            return "EndEvent";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED:       return "DrawInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED: return "DrawIndexedInstanced";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT:     return "ExecuteIndirect";
        case D3D12_AUTO_BREADCRUMB_OP_DISPATCH:            return "Dispatch";
        case D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION:    return "CopyBufferRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION:   return "CopyTextureRegion";
        case D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE:        return "CopyResource";
        case D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE:  return "ResolveSubresource";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW: return "ClearRTV";
        case D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW: return "ClearDSV";
        case D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER:     return "ResourceBarrier";
        case D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE:       return "ExecuteBundle";
        case D3D12_AUTO_BREADCRUMB_OP_PRESENT:             return "Present";
        default: return "Unknown";
    }
}

void DX12Device::LogDeviceRemoved() {
    UZ_PROFILE_FUNCTION();

    // Guard: only log device removal diagnostics once. Subsequent calls are
    // no-ops to prevent infinite log spam when the render loop hasn't stopped yet.
    if (m_deviceRemoved) return;
    m_deviceRemoved = true;

    HRESULT removedReason = m_device->GetDeviceRemovedReason();
    UZ_LOG(RHI, Error, "=== DEVICE REMOVED ===");
    UZ_LOG(RHI, Error, "Adapter: '{}'", m_adapterName);
    UZ_LOG(RHI, Error, "Removed reason: hr=0x{:08X}", static_cast<uint32_t>(removedReason));

    // Query DRED auto-breadcrumbs.
    //
    // Every branch below reports at Error level, including the "nothing to
    // report" ones. The default runtime log level is Error, so a Warning here
    // is invisible in exactly the situation this report exists for, and a
    // branch that logs nothing at all is indistinguishable from a DRED query
    // that failed. Both make an intermittent TDR undiagnosable.
    ComPtr<ID3D12DeviceRemovedExtendedData1> dred;
    HRESULT dredHr = m_device->QueryInterface(IID_PPV_ARGS(&dred));
    if (SUCCEEDED(dredHr)) {
        D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs = {};
        HRESULT crumbHr = dred->GetAutoBreadcrumbsOutput1(&breadcrumbs);
        if (FAILED(crumbHr)) {
            UZ_LOG(RHI, Error,
                   "DRED: GetAutoBreadcrumbsOutput1 failed, hr=0x{:08X} "
                   "(auto-breadcrumbs were not armed before device creation — "
                   "pass --gpu-breadcrumbs or --gpu-validation)",
                   static_cast<uint32_t>(crumbHr));
        } else {
            const auto* node = breadcrumbs.pHeadAutoBreadcrumbNode;
            uint32_t nodeIdx = 0;
            while (node) {
                UZ_LOG(RHI, Error, "Breadcrumb node [{}]: cmdList='{}', cmdQueue='{}'",
                       nodeIdx,
                       node->pCommandListDebugNameW ? WideToUtf8(node->pCommandListDebugNameW) : "(unnamed)",
                       node->pCommandQueueDebugNameW ? WideToUtf8(node->pCommandQueueDebugNameW) : "(unnamed)");

                // Log completed operations
                if (node->pLastBreadcrumbValue && node->pCommandHistory) {
                    uint32_t lastCompleted = *node->pLastBreadcrumbValue;
                    for (uint32_t i = 0; i < node->BreadcrumbCount; ++i) {
                        const char* status = (i < lastCompleted) ? "DONE" : (i == lastCompleted) ? ">>> LAST" : "PENDING";
                        UZ_LOG(RHI, Error, "  [{:3}] {} — {}", i,
                               BreadcrumbOpToString(node->pCommandHistory[i]), status);
                    }
                }
                node = node->pNext;
                nodeIdx++;
            }
            if (nodeIdx == 0) {
                UZ_LOG(RHI, Error, "DRED: no breadcrumb nodes available (no GPU work was in flight)");
            }
        }

        // Query page fault info
        D3D12_DRED_PAGE_FAULT_OUTPUT1 pageFault = {};
        HRESULT faultHr = dred->GetPageFaultAllocationOutput1(&pageFault);
        if (FAILED(faultHr)) {
            UZ_LOG(RHI, Error, "DRED: GetPageFaultAllocationOutput1 failed, hr=0x{:08X}",
                   static_cast<uint32_t>(faultHr));
        } else {
            if (pageFault.PageFaultVA == 0) {
                UZ_LOG(RHI, Error, "DRED: no page fault recorded (removal was not an invalid memory access)");
            } else {
                UZ_LOG(RHI, Error, "Page fault at VA=0x{:016X}", pageFault.PageFaultVA);

                const auto* allocNode = pageFault.pHeadExistingAllocationNode;
                while (allocNode) {
                    UZ_LOG(RHI, Error, "  Existing alloc: '{}'",
                           allocNode->ObjectNameW ? WideToUtf8(allocNode->ObjectNameW) : "(unnamed)");
                    allocNode = allocNode->pNext;
                }
                const auto* freedNode = pageFault.pHeadRecentFreedAllocationNode;
                while (freedNode) {
                    UZ_LOG(RHI, Error, "  Recently freed: '{}'",
                           freedNode->ObjectNameW ? WideToUtf8(freedNode->ObjectNameW) : "(unnamed)");
                    freedNode = freedNode->pNext;
                }
            }
        }
    } else {
        UZ_LOG(RHI, Error, "DRED query interface not available, hr=0x{:08X} — no detailed diagnostics",
               static_cast<uint32_t>(dredHr));
    }

    UZ_LOG(RHI, Error, "=== END DEVICE REMOVED ===");
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor Allocator Init
// ─────────────────────────────────────────────────────────────────────────────

void DX12Device::InitDescriptorAllocators() {
    UZ_PROFILE_FUNCTION();
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
    m_rtvAllocator.Init(m_rtvHeap.Get(), kMaxRtvDescriptors, rtvIncrement);
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
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    DX12Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) {
        UZ_LOG(RHI, Warning, "DestroyBuffer: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    DestroyDX12Buffer(*buf, m_srvUavAllocator);
    m_bufferPool.Free(handle.index);
}

void* DX12Device::MapBuffer(RHIBufferHandle handle) {
    DX12Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return nullptr;
    return buf->mappedPtr;
}

void DX12Device::UnmapBuffer(RHIBufferHandle) {
    // DX12 uses persistent mapping — no explicit unmap needed.
}

uint32_t DX12Device::GetBufferSRVIndex(RHIBufferHandle handle) const {
    const DX12Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->srvIndex;
}

uint32_t DX12Device::GetBufferUAVIndex(RHIBufferHandle handle) const {
    const DX12Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf) return UINT32_MAX;
    return buf->uavIndex;
}

uint64_t DX12Device::GetBufferGPUAddress(RHIBufferHandle handle) const {
    const DX12Buffer* buf = m_bufferPool.Get(handle.index, handle.generation);
    if (!buf || !buf->resource) return 0;
    return buf->resource->GetGPUVirtualAddress();
}

uint32_t DX12Device::GetTextureSRVIndex(RHITextureHandle handle) const {
    const DX12Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return UINT32_MAX;
    return tex->srvIndex;
}

uint32_t DX12Device::GetTextureUAVIndex(RHITextureHandle handle) const {
    const DX12Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return UINT32_MAX;
    return tex->uavIndex;
}

RHITextureDesc DX12Device::GetTextureDesc(RHITextureHandle handle) const {
    const DX12Texture* tex = m_texturePool.Get(handle.index, handle.generation);
    if (!tex) return {};
    return tex->desc;
}

RHITextureHandle DX12Device::CreateTexture(const RHITextureDesc& desc) {
    UZ_PROFILE_FUNCTION();
    uint32_t generation = 0;
    uint32_t index = m_texturePool.Allocate(generation);
    DX12Texture* slot = m_texturePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateTexture: pool allocation failed");
        return RHITextureHandle::Invalid();
    }

    if (!CreateDX12Texture(m_device.Get(), m_allocator, m_srvUavAllocator,
                           m_rtvAllocator, m_dsvAllocator, desc, *slot)) {
        UZ_LOG(RHI, Error, "CreateTexture: CreateDX12Texture failed for '{}' ({}x{}, fmt={})",
               desc.debugName ? desc.debugName : "<unnamed>",
               desc.width, desc.height, static_cast<int>(desc.format));
        m_texturePool.Free(index);
        return RHITextureHandle::Invalid();
    }

    slot->generation = generation;
    return {index, generation};
}

void DX12Device::DestroyTexture(RHITextureHandle handle) {
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    DX12Pipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateGraphicsPipeline: pool allocation failed");
        return RHIPipelineHandle::Invalid();
    }

    if (!CreateDX12GraphicsPipeline(m_device.Get(), m_rootSignature, desc, m_shaderPool, *slot)) {
        m_pipelinePool.Free(index);
        return RHIPipelineHandle::Invalid();
    }

    slot->generation = generation;
    UZ_STAT_INCREMENT(RHI, PipelineCreations);
    return RHIPipelineHandle{index, generation};
}

RHIPipelineHandle DX12Device::CreateComputePipeline(const RHIComputePipelineDesc& desc) {
    UZ_PROFILE_FUNCTION();
    uint32_t generation = 0;
    uint32_t index = m_pipelinePool.Allocate(generation);
    DX12Pipeline* slot = m_pipelinePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateComputePipeline: pool allocation failed");
        return RHIPipelineHandle::Invalid();
    }

    if (!CreateDX12ComputePipeline(m_device.Get(), m_rootSignature, desc, m_shaderPool, *slot)) {
        m_pipelinePool.Free(index);
        return RHIPipelineHandle::Invalid();
    }

    slot->generation = generation;
    UZ_STAT_INCREMENT(RHI, PipelineCreations);
    return RHIPipelineHandle{index, generation};
}

void DX12Device::DestroyPipeline(RHIPipelineHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX12Pipeline* pipeline = m_pipelinePool.Get(handle.index, handle.generation);
    if (!pipeline) {
        UZ_LOG(RHI, Warning, "DestroyPipeline: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    DestroyDX12Pipeline(*pipeline);
    m_pipelinePool.Free(handle.index);
}

std::vector<uint8_t> DX12Device::GetPipelineCachedBlob(RHIPipelineHandle handle) {
    UZ_PROFILE_FUNCTION();
    DX12Pipeline* pipeline = m_pipelinePool.Get(handle.index, handle.generation);
    if (!pipeline || !pipeline->pso) {
        UZ_LOG(RHI, Warning, "GetPipelineCachedBlob: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return {};
    }

    ID3DBlob* blob = nullptr;
    HRESULT hr = pipeline->pso->GetCachedBlob(&blob);
    if (FAILED(hr) || !blob) {
        UZ_LOG(RHI, Warning, "GetPipelineCachedBlob: GetCachedBlob failed (hr=0x{:08X})",
               static_cast<uint32_t>(hr));
        return {};
    }

    std::vector<uint8_t> result(blob->GetBufferSize());
    std::memcpy(result.data(), blob->GetBufferPointer(), blob->GetBufferSize());
    blob->Release();

    UZ_LOG(RHI, Debug, "GetPipelineCachedBlob: extracted {} bytes", result.size());
    return result;
}

RHISamplerHandle DX12Device::CreateSampler(const RHISamplerDesc& desc) {
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    DX12Sampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) {
        UZ_LOG(RHI, Warning, "DestroySampler: invalid handle (idx={}, gen={})",
               handle.index, handle.generation);
        return;
    }
    DestroyDX12Sampler(*sampler, m_samplerAllocator);
    m_samplerPool.Free(handle.index);
}

uint32_t DX12Device::GetSamplerIndex(RHISamplerHandle handle) const {
    const DX12Sampler* sampler = m_samplerPool.Get(handle.index, handle.generation);
    if (!sampler) return UINT32_MAX;
    return sampler->samplerIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// Object Stubs (Phase 3)
// ─────────────────────────────────────────────────────────────────────────────

IRHISwapChain* DX12Device::CreateSwapChain(const RHISwapChainDesc& desc) {
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    auto* fence = new DX12Fence();
    if (!fence->Init(m_device.Get(), m_directQueue.Get())) {
        delete fence;
        return nullptr;
    }
    return fence;
}

void DX12Device::DestroyFence(IRHIFence* fence) {
    delete static_cast<DX12Fence*>(fence);
}

IRHICommandList* DX12Device::CreateCommandList(RHIQueueType queue) {
    UZ_PROFILE_FUNCTION();
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
    UZ_PROFILE_FUNCTION();
    if (!m_directQueue || !m_idleFence || !m_idleFenceEvent) return;

    if (m_idleFenceValue == UINT64_MAX) {
        UZ_LOG(RHI, Warning, "Fence value at UINT64_MAX — resetting after full GPU drain");
        m_idleFenceValue = 0;
    }
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
        // Watchdog: a GPU that never completes (device removed, wedged queue) used to hang the
        // process silently in an INFINITE wait (H-71 hang, 1800 s with nothing in the log).
        // The wait still never gives up, but every kGpuWaitWatchdogMs it says where it is.
        uint32_t waited = 0;
        while (WaitForSingleObject(m_idleFenceEvent, kGpuWaitWatchdogMs) == WAIT_TIMEOUT) {
            waited += kGpuWaitWatchdogMs;
            UZ_LOG(RHI, Error, "WaitIdle: still waiting after {} ms for fence value {} (completed {}, device removed reason 0x{:08X})",
                   waited, m_idleFenceValue, m_idleFence->GetCompletedValue(),
                   static_cast<uint32_t>(m_device->GetDeviceRemovedReason()));
        }
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

    // Also drain the compute queue
    if (m_computeQueue) {
        m_idleFenceValue++;
        hr = m_computeQueue->Signal(m_idleFence.Get(), m_idleFenceValue);
        if (FAILED(hr)) {
            UZ_LOG(RHI, Error, "WaitIdle: Compute queue signal failed, hr=0x{:08X}", static_cast<uint32_t>(hr));
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

bool DX12Device::SupportsVariableRefreshRate() const {
    if (!m_factory) return false;
    BOOL allowTearing = FALSE;
    HRESULT hr = m_factory->CheckFeatureSupport(
        DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allowTearing, sizeof(allowTearing));
    return SUCCEEDED(hr) && allowTearing;
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
// Copy Queue — Async upload synchronization
// ─────────────────────────────────────────────────────────────────────────────

uint64_t DX12Device::SignalCopyQueue() {
    ++m_copyFenceValue;
    HRESULT hr = m_copyQueue->Signal(m_copyFence.Get(), m_copyFenceValue);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "SignalCopyQueue failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        return 0;
    }
    return m_copyFenceValue;
}

void DX12Device::GraphicsWaitForCopy(uint64_t value) {
    HRESULT hr = m_directQueue->Wait(m_copyFence.Get(), value);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "GraphicsWaitForCopy failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Compute Queue — Async compute synchronization
// ─────────────────────────────────────────────────────────────────────────────

uint64_t DX12Device::SignalComputeQueue() {
    ++m_computeFenceValue;
    HRESULT hr = m_computeQueue->Signal(m_computeFence.Get(), m_computeFenceValue);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "SignalComputeQueue failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
        return 0;
    }
    return m_computeFenceValue;
}

void DX12Device::GraphicsWaitForCompute(uint64_t value) {
    HRESULT hr = m_directQueue->Wait(m_computeFence.Get(), value);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "GraphicsWaitForCompute failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
    }
}

void DX12Device::ComputeWaitForGraphics(uint64_t value) {
    HRESULT hr = m_computeQueue->Wait(m_idleFence.Get(), value);
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "ComputeWaitForGraphics failed: hr=0x{:08X}", static_cast<uint32_t>(hr));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU Profiler (IRHIDevice overrides)
// ─────────────────────────────────────────────────────────────────────────────

void DX12Device::GpuProfilerBeginFrame(IRHICommandList* cmdList) {
    auto* dx12CmdList = static_cast<DX12CommandList*>(cmdList);
    m_gpuProfiler.BeginFrame(dx12CmdList ? dx12CmdList->GetNative() : nullptr);
}

void DX12Device::GpuProfilerEndFrame(IRHICommandList* cmdList) {
    auto* dx12CmdList = static_cast<DX12CommandList*>(cmdList);
    if (dx12CmdList) {
        m_gpuProfiler.EndFrame(dx12CmdList->GetNative());
    }
}

void DX12Device::GpuProfilerResolve() {
    m_gpuProfiler.ResolveFrame();
}

void DX12Device::GpuProfilerBeginTimestamp(IRHICommandList* cmdList, const char* name) {
    auto* dx12CmdList = static_cast<DX12CommandList*>(cmdList);
    if (dx12CmdList) {
        m_gpuProfiler.BeginTimestamp(dx12CmdList->GetNative(), name);
    }
}

void DX12Device::GpuProfilerEndTimestamp(IRHICommandList* cmdList, const char* name) {
    auto* dx12CmdList = static_cast<DX12CommandList*>(cmdList);
    if (dx12CmdList) {
        m_gpuProfiler.EndTimestamp(dx12CmdList->GetNative(), name);
    }
}

const std::vector<GpuTimingResult>& DX12Device::GpuProfilerGetResults() const {
    return m_gpuProfiler.GetResults();
}

float DX12Device::GpuProfilerGetFrameTimeMs() const {
    return m_gpuProfiler.GetFrameTimeMs();
}

// ─────────────────────────────────────────────────────────────────────────────
// Ray Tracing — BLAS / TLAS / RT Pipeline
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Helper to convert RHI AS build flags to DX12 build flags.
D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS ToDX12ASBuildFlags(RHIASBuildFlags flags) {
    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS result =
        D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_NONE;
    if (HasFlag(flags, RHIASBuildFlags::AllowUpdate))
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE;
    if (HasFlag(flags, RHIASBuildFlags::AllowCompaction))
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_COMPACTION;
    if (HasFlag(flags, RHIASBuildFlags::PreferFastTrace))
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    if (HasFlag(flags, RHIASBuildFlags::PreferFastBuild))
        result |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    return result;
}

/// Helper to convert RHIFormat to DXGI_FORMAT for vertex positions.
DXGI_FORMAT ToDX12VertexFormat(RHIFormat format) {
    switch (format) {
        case RHIFormat::RGB32Float:  return DXGI_FORMAT_R32G32B32_FLOAT;
        case RHIFormat::RG32Float:   return DXGI_FORMAT_R32G32_FLOAT;
        case RHIFormat::RGBA32Float: return DXGI_FORMAT_R32G32B32A32_FLOAT;
        case RHIFormat::RGBA16Float: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        default:                     return DXGI_FORMAT_R32G32B32_FLOAT;
    }
}

/// Helper to convert RHIIndexFormat to DXGI_FORMAT.
DXGI_FORMAT ToDX12IndexFormat(RHIIndexFormat format) {
    return format == RHIIndexFormat::Uint16 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
}

} // anonymous namespace

RHIBLASHandle DX12Device::CreateBLAS(const RHIBLASDesc& desc) {
    if (!m_supportsRayTracing || !desc.geometries || desc.geometryCount == 0) {
        return RHIBLASHandle::Invalid();
    }

    // Build DX12 geometry descriptions
    std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> geomDescs(desc.geometryCount);
    for (uint32_t i = 0; i < desc.geometryCount; ++i) {
        const auto& src = desc.geometries[i];
        auto& dst = geomDescs[i];
        dst.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
        dst.Flags = src.isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE
                                 : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
        auto& tri = dst.Triangles;
        tri.VertexBuffer.StartAddress  = GetBufferGPUAddress(src.vertexBuffer);
        tri.VertexBuffer.StrideInBytes = src.vertexStride;
        tri.VertexFormat               = ToDX12VertexFormat(src.vertexFormat);
        tri.VertexCount                = src.vertexCount;
        if (src.indexCount > 0 && src.indexBuffer.IsValid()) {
            tri.IndexBuffer = GetBufferGPUAddress(src.indexBuffer);
            tri.IndexFormat = ToDX12IndexFormat(src.indexFormat);
            tri.IndexCount  = src.indexCount;
        }
        tri.Transform3x4 = 0; // no per-geometry transform
    }

    // Query prebuild info for buffer sizes
    auto dx12BuildFlags = ToDX12ASBuildFlags(desc.buildFlags);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags         = dx12BuildFlags;
    inputs.NumDescs      = desc.geometryCount;
    inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = geomDescs.data();

    ComPtr<ID3D12Device5> device5;
    if (FAILED(m_device.As(&device5))) {
        UZ_LOG(RHI, Error, "CreateBLAS: Failed to query ID3D12Device5 — DXR not supported");
        return RHIBLASHandle::Invalid();
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

    // Allocate result buffer
    D3D12_RESOURCE_DESC resultDesc = {};
    resultDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resultDesc.Width              = prebuild.ResultDataMaxSizeInBytes;
    resultDesc.Height             = 1;
    resultDesc.DepthOrArraySize   = 1;
    resultDesc.MipLevels          = 1;
    resultDesc.SampleDesc.Count   = 1;
    resultDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resultDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    DX12BLAS blas = {};
    blas.resultSizeBytes  = prebuild.ResultDataMaxSizeInBytes;
    blas.scratchSizeBytes = prebuild.ScratchDataSizeInBytes;
    blas.geometryDescs    = std::move(geomDescs);
    blas.buildFlags       = dx12BuildFlags;

    HRESULT hr = m_allocator->CreateResource(
        &allocDesc, &resultDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr,
        &blas.resultAllocation,
        IID_PPV_ARGS(&blas.resultResource));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateBLAS: Failed to allocate result buffer ({} bytes), hr=0x{:08X}",
               blas.resultSizeBytes, static_cast<uint32_t>(hr));
        return RHIBLASHandle::Invalid();
    }

    // Allocate scratch buffer
    D3D12_RESOURCE_DESC scratchDesc = resultDesc;
    scratchDesc.Width = prebuild.ScratchDataSizeInBytes;

    hr = m_allocator->CreateResource(
        &allocDesc, &scratchDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        &blas.scratchAllocation,
        IID_PPV_ARGS(&blas.scratchResource));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateBLAS: Failed to allocate scratch buffer ({} bytes), hr=0x{:08X}",
               blas.scratchSizeBytes, static_cast<uint32_t>(hr));
        blas.resultAllocation->Release();
        return RHIBLASHandle::Invalid();
    }

    if (desc.debugName) {
        std::wstring wname(desc.debugName, desc.debugName + strlen(desc.debugName));
        blas.resultResource->SetName((wname + L"_Result").c_str());
        blas.scratchResource->SetName((wname + L"_Scratch").c_str());
    }

    uint32_t generation = 0;
    uint32_t index = m_blasPool.Allocate(generation);
    auto* slot = m_blasPool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateBLAS: pool allocation failed");
        blas.scratchAllocation->Release();
        blas.resultAllocation->Release();
        return RHIBLASHandle::Invalid();
    }
    *slot = std::move(blas);
    slot->generation = generation;

    UZ_LOG(RHI, Info, "Created BLAS '{}': result={}B, scratch={}B",
           desc.debugName ? desc.debugName : "(unnamed)",
           prebuild.ResultDataMaxSizeInBytes, prebuild.ScratchDataSizeInBytes);
    return {index, generation};
}

void DX12Device::DestroyBLAS(RHIBLASHandle handle) {
    auto* blas = m_blasPool.Get(handle.index, handle.generation);
    if (!blas) return;

    if (blas->scratchAllocation) blas->scratchAllocation->Release();
    if (blas->resultAllocation)  blas->resultAllocation->Release();
    m_blasPool.Free(handle.index);
}

uint64_t DX12Device::GetBLASGPUAddress(RHIBLASHandle handle) const {
    const auto* blas = m_blasPool.Get(handle.index, handle.generation);
    if (!blas || !blas->resultResource) return 0;
    return blas->resultResource->GetGPUVirtualAddress();
}

RHITLASHandle DX12Device::CreateTLAS(const RHITLASDesc& desc) {
    if (!m_supportsRayTracing || desc.maxInstanceCount == 0) {
        return RHITLASHandle::Invalid();
    }

    auto dx12BuildFlags = ToDX12ASBuildFlags(desc.buildFlags);

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
    inputs.Type          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags         = dx12BuildFlags;
    inputs.NumDescs      = desc.maxInstanceCount;
    inputs.DescsLayout   = D3D12_ELEMENTS_LAYOUT_ARRAY;

    ComPtr<ID3D12Device5> device5;
    if (FAILED(m_device.As(&device5))) {
        return RHITLASHandle::Invalid();
    }

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild = {};
    device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);

    // Allocate result buffer
    D3D12_RESOURCE_DESC resultDesc = {};
    resultDesc.Dimension          = D3D12_RESOURCE_DIMENSION_BUFFER;
    resultDesc.Width              = prebuild.ResultDataMaxSizeInBytes;
    resultDesc.Height             = 1;
    resultDesc.DepthOrArraySize   = 1;
    resultDesc.MipLevels          = 1;
    resultDesc.SampleDesc.Count   = 1;
    resultDesc.Layout             = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    resultDesc.Flags              = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    D3D12MA::ALLOCATION_DESC allocDesc = {};
    allocDesc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

    DX12TLAS tlas = {};
    tlas.resultSizeBytes  = prebuild.ResultDataMaxSizeInBytes;
    tlas.scratchSizeBytes = prebuild.ScratchDataSizeInBytes;
    tlas.maxInstanceCount = desc.maxInstanceCount;
    tlas.buildFlags       = dx12BuildFlags;

    HRESULT hr = m_allocator->CreateResource(
        &allocDesc, &resultDesc,
        D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
        nullptr,
        &tlas.resultAllocation,
        IID_PPV_ARGS(&tlas.resultResource));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateTLAS: Failed to allocate result buffer, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return RHITLASHandle::Invalid();
    }

    // Allocate scratch buffer
    D3D12_RESOURCE_DESC scratchDesc = resultDesc;
    scratchDesc.Width = prebuild.ScratchDataSizeInBytes;

    hr = m_allocator->CreateResource(
        &allocDesc, &scratchDesc,
        D3D12_RESOURCE_STATE_COMMON,
        nullptr,
        &tlas.scratchAllocation,
        IID_PPV_ARGS(&tlas.scratchResource));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateTLAS: Failed to allocate scratch buffer, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        tlas.resultAllocation->Release();
        return RHITLASHandle::Invalid();
    }

    // Create SRV for shader access (RaytracingAccelerationStructure SRV type)
    tlas.srvIndex = m_srvUavAllocator.Allocate();
    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                                   = DXGI_FORMAT_UNKNOWN;
    srvDesc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    srvDesc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.RaytracingAccelerationStructure.Location = tlas.resultResource->GetGPUVirtualAddress();

    auto cpuHandle = uz::dx12::GetCPUStart(m_cbvSrvUavHeap.Get());
    cpuHandle.ptr += static_cast<SIZE_T>(tlas.srvIndex) *
                     m_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    m_device->CreateShaderResourceView(nullptr, &srvDesc, cpuHandle);

    if (desc.debugName) {
        std::wstring wname(desc.debugName, desc.debugName + strlen(desc.debugName));
        tlas.resultResource->SetName((wname + L"_Result").c_str());
        tlas.scratchResource->SetName((wname + L"_Scratch").c_str());
    }

    uint32_t generation = 0;
    uint32_t index = m_tlasPool.Allocate(generation);
    auto* slot = m_tlasPool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateTLAS: pool allocation failed");
        if (tlas.srvIndex != UINT32_MAX) m_srvUavAllocator.Free(tlas.srvIndex);
        tlas.scratchAllocation->Release();
        tlas.resultAllocation->Release();
        return RHITLASHandle::Invalid();
    }
    uint32_t savedSrvIndex = tlas.srvIndex;
    *slot = std::move(tlas);
    slot->generation = generation;

    UZ_LOG(RHI, Info, "Created TLAS '{}': maxInstances={}, result={}B, scratch={}B, srvIndex={}",
           desc.debugName ? desc.debugName : "(unnamed)",
           desc.maxInstanceCount, prebuild.ResultDataMaxSizeInBytes,
           prebuild.ScratchDataSizeInBytes, savedSrvIndex);
    return {index, generation};
}

void DX12Device::DestroyTLAS(RHITLASHandle handle) {
    auto* tlas = m_tlasPool.Get(handle.index, handle.generation);
    if (!tlas) return;

    if (tlas->srvIndex != UINT32_MAX) {
        m_srvUavAllocator.Free(tlas->srvIndex);
    }
    if (tlas->scratchAllocation) tlas->scratchAllocation->Release();
    if (tlas->resultAllocation)  tlas->resultAllocation->Release();
    m_tlasPool.Free(handle.index);
}

uint32_t DX12Device::GetTLASSRVIndex(RHITLASHandle handle) const {
    const auto* tlas = m_tlasPool.Get(handle.index, handle.generation);
    if (!tlas) return UINT32_MAX;
    return tlas->srvIndex;
}

RHIRTPipelineHandle DX12Device::CreateRaytracingPipeline(const RHIRTPipelineDesc& desc) {
    if (!m_supportsRayTracing || !desc.libraryShaders || desc.libraryCount == 0) {
        return RHIRTPipelineHandle::Invalid();
    }

    ComPtr<ID3D12Device5> device5;
    if (FAILED(m_device.As(&device5))) {
        return RHIRTPipelineHandle::Invalid();
    }

    // Count subobjects: libraries + hit groups + shader config + pipeline config + global root sig
    const uint32_t subobjectCount = desc.libraryCount + desc.hitGroupCount + 3;
    std::vector<D3D12_STATE_SUBOBJECT> subobjects(subobjectCount);
    uint32_t idx = 0;

    // Storage for DXIL library descs (must stay alive until CreateStateObject)
    std::vector<D3D12_DXIL_LIBRARY_DESC> libDescs(desc.libraryCount);
    for (uint32_t i = 0; i < desc.libraryCount; ++i) {
        auto shaderHandle = desc.libraryShaders[i];
        auto* shader = m_shaderPool.Get(shaderHandle.index, shaderHandle.generation);
        if (!shader) {
            UZ_LOG(RHI, Error, "CreateRaytracingPipeline: Invalid library shader handle at index {}", i);
            return RHIRTPipelineHandle::Invalid();
        }
        libDescs[i].DXILLibrary.pShaderBytecode = shader->bytecode.data();
        libDescs[i].DXILLibrary.BytecodeLength  = shader->bytecode.size();
        libDescs[i].NumExports                  = 0; // export all entry points
        libDescs[i].pExports                    = nullptr;

        subobjects[idx].Type  = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        subobjects[idx].pDesc = &libDescs[i];
        ++idx;
    }

    // Hit groups
    std::vector<D3D12_HIT_GROUP_DESC> hitGroupDescs(desc.hitGroupCount);
    for (uint32_t i = 0; i < desc.hitGroupCount; ++i) {
        const auto& src = desc.hitGroups[i];
        auto& dst = hitGroupDescs[i];
        dst.HitGroupExport           = src.hitGroupName;
        dst.Type                     = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        dst.ClosestHitShaderImport   = src.closestHitExport;
        dst.AnyHitShaderImport       = src.anyHitExport;
        dst.IntersectionShaderImport = src.intersectionExport;

        subobjects[idx].Type  = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobjects[idx].pDesc = &hitGroupDescs[i];
        ++idx;
    }

    // Shader config (payload + attribute sizes)
    D3D12_RAYTRACING_SHADER_CONFIG shaderConfig = {};
    shaderConfig.MaxPayloadSizeInBytes   = desc.maxPayloadSizeInBytes;
    shaderConfig.MaxAttributeSizeInBytes = desc.maxAttributeSizeInBytes;
    subobjects[idx].Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
    subobjects[idx].pDesc = &shaderConfig;
    ++idx;

    // Pipeline config (max recursion depth)
    D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig = {};
    pipelineConfig.MaxTraceRecursionDepth = desc.maxRecursionDepth;
    subobjects[idx].Type  = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
    subobjects[idx].pDesc = &pipelineConfig;
    ++idx;

    // Global root signature (use the same bindless root sig as graphics/compute)
    D3D12_GLOBAL_ROOT_SIGNATURE globalRS = {};
    globalRS.pGlobalRootSignature = m_rootSignature;
    subobjects[idx].Type  = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
    subobjects[idx].pDesc = &globalRS;
    ++idx;

    // Create state object
    D3D12_STATE_OBJECT_DESC stateObjectDesc = {};
    stateObjectDesc.Type            = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
    stateObjectDesc.NumSubobjects   = idx;
    stateObjectDesc.pSubobjects     = subobjects.data();

    DX12RTPipeline rtPipeline = {};
    HRESULT hr = device5->CreateStateObject(&stateObjectDesc,
                                             IID_PPV_ARGS(&rtPipeline.stateObject));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateRaytracingPipeline: CreateStateObject failed, hr=0x{:08X}",
               static_cast<uint32_t>(hr));
        return RHIRTPipelineHandle::Invalid();
    }

    // Query properties interface for shader identifier retrieval
    hr = rtPipeline.stateObject->QueryInterface(IID_PPV_ARGS(&rtPipeline.properties));
    if (FAILED(hr)) {
        UZ_LOG(RHI, Error, "CreateRaytracingPipeline: Failed to query ID3D12StateObjectProperties");
        rtPipeline.stateObject->Release();
        return RHIRTPipelineHandle::Invalid();
    }

    uint32_t generation = 0;
    uint32_t index = m_rtPipelinePool.Allocate(generation);
    auto* slot = m_rtPipelinePool.Get(index, generation);
    if (!slot) {
        UZ_LOG(RHI, Error, "CreateRaytracingPipeline: pool allocation failed");
        rtPipeline.properties->Release();
        rtPipeline.stateObject->Release();
        return RHIRTPipelineHandle::Invalid();
    }
    *slot = std::move(rtPipeline);
    slot->generation = generation;

    UZ_LOG(RHI, Info, "Created RT pipeline '{}' with {} libraries, {} hit groups",
           desc.debugName ? desc.debugName : "(unnamed)",
           desc.libraryCount, desc.hitGroupCount);
    return {index, generation};
}

void DX12Device::DestroyRaytracingPipeline(RHIRTPipelineHandle handle) {
    auto* pipeline = m_rtPipelinePool.Get(handle.index, handle.generation);
    if (!pipeline) return;

    if (pipeline->properties) pipeline->properties->Release();
    if (pipeline->stateObject) pipeline->stateObject->Release();
    m_rtPipelinePool.Free(handle.index);
}

bool DX12Device::GetShaderIdentifier(RHIRTPipelineHandle pipeline,
                                     const wchar_t* exportName,
                                     void* outIdentifier) {
    auto* rtPipeline = m_rtPipelinePool.Get(pipeline.index, pipeline.generation);
    if (!rtPipeline || !rtPipeline->properties || !exportName || !outIdentifier) {
        return false;
    }

    const void* id = rtPipeline->properties->GetShaderIdentifier(exportName);
    if (!id) {
        UZ_LOG(RHI, Error, "GetShaderIdentifier: Export not found in RT pipeline");
        return false;
    }

    memcpy(outIdentifier, id, kShaderIdentifierSizeInBytes);
    return true;
}

// Factory functions moved to rhi_factory.cpp (unified dispatch for all backends).

} // namespace uz
