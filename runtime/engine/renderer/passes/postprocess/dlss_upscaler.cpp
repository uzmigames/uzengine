/// UzEngine DLSS Temporal Upscaler — Implementation
///
/// Integrates NVIDIA DLSS via dynamic loading of the NGX SDK DLL (_nvngx.dll).
/// Instead of linking the MSVC-compiled nvsdk_ngx_d.lib (which has CRT
/// incompatibility with Zig's mingw libc), we use LoadLibrary/GetProcAddress
/// to resolve NGX functions at runtime. This makes DLSS entirely optional —
/// if the DLL is not present, the upscaler gracefully degrades to a stub.
///
/// @ref NVIDIA 2020 "Deep Learning Super Sampling"

#ifndef UZ_DLSS_ENABLED
#define UZ_DLSS_ENABLED 0
#endif

#include "dlss_upscaler.h"
#include "log.h"
#include "cvar.h"

#if UZ_DLSS_ENABLED

#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_types.h"
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine/rhi/dx12/dx12_resources.h"
#include "profiler.h"
#include "engine/paths/paths.h"

// NGX SDK headers — types and constants only, no linking required.
// We avoid nvsdk_ngx_helpers.h which uses inline functions that reference
// symbols from the static lib (NVSDK_NGX_Parameter_Set*/Get*).
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>

#include <cstring>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ─────────────────────────────────────────────────────────────────────────────
// CVars
// ─────────────────────────────────────────────────────────────────────────────

static auto* cv_DLSSQuality = UZ_CVAR_INT("r.DLSS.Quality", 3,
    "DLSS quality: 0=UltraPerf, 1=Performance, 2=Balanced, 3=Quality, 4=DLAA", 0, 4);

static auto* cv_DLSSSharpness = UZ_CVAR_FLOAT("r.DLSS.Sharpness", 0.0f,
    "DLSS sharpness [0, 1]", 0.0f, 1.0f);

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic NGX function typedefs
// ─────────────────────────────────────────────────────────────────────────────

using PFN_NVSDK_NGX_D3D12_Init = NVSDK_NGX_Result(NVSDK_CONV*)(
    unsigned long long, const wchar_t*, ID3D12Device*, const NVSDK_NGX_FeatureCommonInfo*, NVSDK_NGX_Version);
using PFN_NVSDK_NGX_D3D12_Shutdown1 = NVSDK_NGX_Result(NVSDK_CONV*)(ID3D12Device*);
using PFN_NVSDK_NGX_D3D12_GetCapabilityParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
using PFN_NVSDK_NGX_D3D12_DestroyParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
using PFN_NVSDK_NGX_D3D12_CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using PFN_NVSDK_NGX_D3D12_ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
using PFN_NVSDK_NGX_D3D12_EvaluateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static NVSDK_NGX_PerfQuality_Value ToNgxQuality(uz::DlssQuality q) {
    switch (q) {
    case uz::DlssQuality::UltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case uz::DlssQuality::Performance:      return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case uz::DlssQuality::Balanced:         return NVSDK_NGX_PerfQuality_Value_Balanced;
    case uz::DlssQuality::Quality:          return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case uz::DlssQuality::DLAA:             return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
    return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

static constexpr uint64_t kNgxAppId = 0x557A456E67696E65ULL;

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PIMPL
// ─────────────────────────────────────────────────────────────────────────────

struct DlssUpscaler::Impl {
    IRHIDevice* device = nullptr;
    DX12Device* dx12Device = nullptr;

    // Dynamic library handle
    HMODULE ngxModule = nullptr;

    // Dynamically loaded NGX function pointers
    PFN_NVSDK_NGX_D3D12_Init pfnInit = nullptr;
    PFN_NVSDK_NGX_D3D12_Shutdown1 pfnShutdown = nullptr;
    PFN_NVSDK_NGX_D3D12_GetCapabilityParameters pfnGetCapParams = nullptr;
    PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroyParams = nullptr;
    PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreateFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnReleaseFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_EvaluateFeature pfnEvaluateFeature = nullptr;

    bool ngxInitialized = false;
    bool dlssSupported = false;
    NVSDK_NGX_Handle* dlssFeature = nullptr;
    NVSDK_NGX_Parameter* ngxParams = nullptr;

    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;
    uint32_t optimalRenderWidth = 0;
    uint32_t optimalRenderHeight = 0;
    DlssQuality currentQuality = DlssQuality::Quality;

    ~Impl() {
        DestroyFeature();
        ShutdownNgx();
        UnloadDll();
    }

    /// Searches the NVIDIA DriverStore for _nvngx.dll.
    ///
    /// _nvngx.dll is NOT in System32 — it lives in the driver package folder:
    /// C:\Windows\System32\DriverStore\FileRepository\nv_dispi.inf_amd64_*\_nvngx.dll
    /// @ref NVIDIA NGX Programming Guide — NGX core library is driver-provided.
    static HMODULE FindNvNgxInDriverStore() {
        // Enumerate DriverStore directories matching NVIDIA display driver pattern.
        const wchar_t* driverStoreBase = L"C:\\Windows\\System32\\DriverStore\\FileRepository";
        WIN32_FIND_DATAW findData{};
        wchar_t searchPattern[MAX_PATH];
        _snwprintf_s(searchPattern, MAX_PATH, _TRUNCATE, L"%s\\nv_dispi.inf_amd64_*", driverStoreBase);

        HANDLE hFind = ::FindFirstFileW(searchPattern, &findData);
        if (hFind == INVALID_HANDLE_VALUE) return nullptr;

        HMODULE result = nullptr;
        do {
            if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;

            wchar_t dllPath[MAX_PATH];
            _snwprintf_s(dllPath, MAX_PATH, _TRUNCATE, L"%s\\%s\\_nvngx.dll",
                         driverStoreBase, findData.cFileName);

            result = ::LoadLibraryW(dllPath);
            if (result) {
                UZ_LOG(Renderer, Info, "DLSS: loaded _nvngx.dll from DriverStore: {}",
                       std::string(dllPath, dllPath + wcslen(dllPath)));
                break;
            }
        } while (::FindNextFileW(hFind, &findData));

        ::FindClose(hFind);
        return result;
    }

    /// Loads _nvngx.dll and resolves function pointers.
    bool LoadDll() {
        // 1. Try system LoadLibrary (works if driver puts it in a searchable path).
        ngxModule = ::LoadLibraryW(L"_nvngx.dll");

        // 2. Search NVIDIA DriverStore — _nvngx.dll lives inside the driver package.
        // @ref NVIDIA Developer Forums: "Path to driverStore found using QAI:
        //   C:\WINDOWS\System32\DriverStore\FileRepository\nv_dispi.inf_amd64_*"
        if (!ngxModule) {
            ngxModule = FindNvNgxInDriverStore();
        }

        if (!ngxModule) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: _nvngx.dll not found (error={}) — install/update NVIDIA GeForce driver",
                   ::GetLastError());
            return false;
        }

        pfnInit = reinterpret_cast<PFN_NVSDK_NGX_D3D12_Init>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_Init"));
        pfnShutdown = reinterpret_cast<PFN_NVSDK_NGX_D3D12_Shutdown1>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_Shutdown1"));
        pfnGetCapParams = reinterpret_cast<PFN_NVSDK_NGX_D3D12_GetCapabilityParameters>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_GetCapabilityParameters"));
        pfnDestroyParams = reinterpret_cast<PFN_NVSDK_NGX_D3D12_DestroyParameters>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_DestroyParameters"));
        pfnCreateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_CreateFeature>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_CreateFeature"));
        pfnReleaseFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_ReleaseFeature>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_ReleaseFeature"));
        pfnEvaluateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_EvaluateFeature"));

        if (!pfnInit || !pfnShutdown || !pfnGetCapParams || !pfnCreateFeature ||
            !pfnReleaseFeature || !pfnEvaluateFeature) {
            UZ_LOG(Renderer, Warning, "DLSS: _nvngx.dll loaded but missing required exports");
            UnloadDll();
            return false;
        }

        UZ_LOG(Renderer, Info, "DLSS: _nvngx.dll loaded successfully");
        return true;
    }

    void UnloadDll() {
        if (ngxModule) {
            ::FreeLibrary(ngxModule);
            ngxModule = nullptr;
        }
        pfnInit = nullptr;
        pfnShutdown = nullptr;
        pfnGetCapParams = nullptr;
        pfnDestroyParams = nullptr;
        pfnCreateFeature = nullptr;
        pfnReleaseFeature = nullptr;
        pfnEvaluateFeature = nullptr;
    }

    bool InitNgx(ID3D12Device* nativeDevice) {
        std::wstring dataPath = uz::GetExeDir().wstring();

        NVSDK_NGX_Result result = pfnInit(kNgxAppId, dataPath.c_str(), nativeDevice,
                                          nullptr, NVSDK_NGX_Version_API);
        if (NVSDK_NGX_FAILED(result)) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: NGX init failed (result=0x{:08X}) — falling back",
                   static_cast<uint32_t>(result));
            return false;
        }

        result = pfnGetCapParams(&ngxParams);
        if (NVSDK_NGX_FAILED(result)) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: failed to get NGX capability parameters (result=0x{:08X})",
                   static_cast<uint32_t>(result));
            pfnShutdown(nativeDevice);
            return false;
        }

        // Check DLSS availability via NVSDK_NGX_Parameter virtual method table.
        unsigned int dlssAvailable = 0;
        ngxParams->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
        if (dlssAvailable == 0) {
            UZ_LOG(Renderer, Warning, "DLSS: not supported on this GPU/driver");
            if (pfnDestroyParams) pfnDestroyParams(ngxParams);
            ngxParams = nullptr;
            pfnShutdown(nativeDevice);
            return false;
        }

        ngxInitialized = true;
        dlssSupported = true;
        UZ_LOG(Renderer, Info, "DLSS: NGX initialized, DLSS is supported");
        return true;
    }

    void ShutdownNgx() {
        if (!ngxInitialized || !dx12Device || !pfnShutdown) return;
        if (ngxParams && pfnDestroyParams) {
            pfnDestroyParams(ngxParams);
            ngxParams = nullptr;
        }
        pfnShutdown(dx12Device->GetDevice());
        ngxInitialized = false;
        dlssSupported = false;
    }

    bool QueryOptimalSettings(uint32_t dispW, uint32_t dispH, DlssQuality quality) {
        if (!ngxParams) return false;

        // Use NVSDK_NGX_Parameter virtual methods to query optimal settings.
        ngxParams->Set(NVSDK_NGX_Parameter_Width, dispW);
        ngxParams->Set(NVSDK_NGX_Parameter_Height, dispH);
        ngxParams->Set(NVSDK_NGX_Parameter_PerfQualityValue, static_cast<unsigned int>(ToNgxQuality(quality)));

        unsigned int optW = 0, optH = 0;
        ngxParams->Get(NVSDK_NGX_Parameter_OutWidth, &optW);
        ngxParams->Get(NVSDK_NGX_Parameter_OutHeight, &optH);

        // Fallback: compute from quality preset ratio.
        if (optW == 0 || optH == 0) {
            float scale = 1.0f;
            switch (quality) {
            case DlssQuality::UltraPerformance: scale = 3.0f; break;
            case DlssQuality::Performance:      scale = 2.0f; break;
            case DlssQuality::Balanced:         scale = 1.7f; break;
            case DlssQuality::Quality:          scale = 1.5f; break;
            case DlssQuality::DLAA:             scale = 1.0f; break;
            }
            optW = static_cast<uint32_t>(static_cast<float>(dispW) / scale);
            optH = static_cast<uint32_t>(static_cast<float>(dispH) / scale);
        }

        optimalRenderWidth = optW;
        optimalRenderHeight = optH;
        UZ_LOG(Renderer, Info, "DLSS: optimal render {}x{} for display {}x{}",
               optW, optH, dispW, dispH);
        return true;
    }

    bool CreateFeature(ID3D12GraphicsCommandList* cmdList,
                       uint32_t dispW, uint32_t dispH, DlssQuality quality) {
        if (dlssFeature) DestroyFeature();
        if (!QueryOptimalSettings(dispW, dispH, quality)) return false;

        displayWidth = dispW;
        displayHeight = dispH;
        currentQuality = quality;

        // Set DLSS creation parameters via the NVSDK_NGX_Parameter interface.
        ngxParams->Set(NVSDK_NGX_Parameter_Width, optimalRenderWidth);
        ngxParams->Set(NVSDK_NGX_Parameter_Height, optimalRenderHeight);
        ngxParams->Set(NVSDK_NGX_Parameter_OutWidth, dispW);
        ngxParams->Set(NVSDK_NGX_Parameter_OutHeight, dispH);
        ngxParams->Set(NVSDK_NGX_Parameter_PerfQualityValue,
                       static_cast<unsigned int>(ToNgxQuality(quality)));
        ngxParams->Set(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
                       NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
                       NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
                       NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
                       NVSDK_NGX_DLSS_Feature_Flags_AutoExposure);

        NVSDK_NGX_Result result = pfnCreateFeature(
            cmdList, NVSDK_NGX_Feature_SuperSampling, ngxParams, &dlssFeature);

        if (NVSDK_NGX_FAILED(result)) {
            UZ_LOG(Renderer, Error, "DLSS: feature creation failed (result=0x{:08X})",
                   static_cast<uint32_t>(result));
            dlssFeature = nullptr;
            return false;
        }

        UZ_LOG(Renderer, Info, "DLSS: feature created — render {}x{} → display {}x{}",
               optimalRenderWidth, optimalRenderHeight, dispW, dispH);
        return true;
    }

    void DestroyFeature() {
        if (!dlssFeature || !pfnReleaseFeature) return;
        pfnReleaseFeature(dlssFeature);
        dlssFeature = nullptr;
    }

    ID3D12Resource* GetNativeResource(RHITextureHandle handle) const {
        if (!handle.IsValid() || !dx12Device) return nullptr;
        auto* dx12Tex = dx12Device->GetTexturePool().Get(handle.index, handle.generation);
        if (!dx12Tex) return nullptr;
        return dx12Tex->resource;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Constructor / Destructor / Move
// ─────────────────────────────────────────────────────────────────────────────

DlssUpscaler::DlssUpscaler()
    : m_impl(std::make_unique<Impl>())
{
}

DlssUpscaler::~DlssUpscaler() { Shutdown(); }
DlssUpscaler::DlssUpscaler(DlssUpscaler&&) noexcept = default;
DlssUpscaler& DlssUpscaler::operator=(DlssUpscaler&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

UpscalerType DlssUpscaler::GetType() const noexcept { return UpscalerType::DLSS; }
const char* DlssUpscaler::GetName() const noexcept { return "DLSS"; }
float DlssUpscaler::GetMinScreenPercentage() const noexcept { return 33.3f; }
float DlssUpscaler::GetMaxScreenPercentage() const noexcept { return 100.f; }
bool DlssUpscaler::IsAvailable() const noexcept {
    return m_impl && m_impl->dlssSupported && m_impl->ngxInitialized;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DlssUpscaler::Init(IRHIDevice* device, PipelineLibrary*, CBRingAllocator*) {
    if (!device) { UZ_LOG(Renderer, Error, "DLSS: Init called with null device"); return; }
    if (device->GetBackend() != RHIBackend::DX12) {
        UZ_LOG(Renderer, Warning, "DLSS: only DX12 backend is supported");
        return;
    }

    m_impl->device = device;
    m_impl->dx12Device = static_cast<DX12Device*>(device);

    // Step 1: Load _nvngx.dll dynamically.
    if (!m_impl->LoadDll()) return;

    // Step 2: Initialize NGX SDK.
    ID3D12Device* nativeDevice = m_impl->dx12Device->GetDevice();
    if (!nativeDevice || !m_impl->InitNgx(nativeDevice)) {
        m_impl->UnloadDll();
        return;
    }

    UZ_LOG(Renderer, Info, "DLSS: backend initialized, feature created on first frame");
}

void DlssUpscaler::Shutdown() {
    if (m_impl) {
        m_impl->DestroyFeature();
        m_impl->ShutdownNgx();
        m_impl->UnloadDll();
        m_impl->device = nullptr;
        m_impl->dx12Device = nullptr;
    }
}

void DlssUpscaler::OnResize(uint32_t displayWidth, uint32_t displayHeight) {
    if (!m_impl || !m_impl->dlssSupported) return;
    if (m_impl->dlssFeature && m_impl->displayWidth == displayWidth &&
        m_impl->displayHeight == displayHeight) return;
    m_impl->DestroyFeature();
    m_impl->displayWidth = displayWidth;
    m_impl->displayHeight = displayHeight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

void DlssUpscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("DLSS::Execute");
    if (!m_impl || !m_impl->dx12Device || !m_impl->dlssSupported) return;

    const auto outputW = static_cast<uint32_t>(input.outputWidth);
    const auto outputH = static_cast<uint32_t>(input.outputHeight);
    const auto inputW  = static_cast<uint32_t>(input.renderWidth);
    const auto inputH  = static_cast<uint32_t>(input.renderHeight);

    // If render resolution == display resolution (no screen percentage), force DLAA (1.0x).
    // Other DLSS quality presets expect a lower render resolution.
    auto quality = static_cast<DlssQuality>(cv_DLSSQuality->value);
    if (inputW >= outputW && inputH >= outputH) {
        quality = DlssQuality::DLAA;
    }

    auto* dx12Cmd = static_cast<DX12CommandList*>(cmd);
    ID3D12GraphicsCommandList* nativeCmdList = dx12Cmd->GetNative();
    if (!nativeCmdList) return;

    // Lazily create or recreate feature.
    if (!m_impl->dlssFeature || m_impl->displayWidth != outputW ||
        m_impl->displayHeight != outputH || m_impl->currentQuality != quality) {
        if (outputW == 0 || outputH == 0) return;
        if (!m_impl->CreateFeature(nativeCmdList, outputW, outputH, quality)) return;
    }

    // Resolve RHI textures to native DX12 resources.
    ID3D12Resource* colorRes    = m_impl->GetNativeResource(input.sceneColorTexture);
    ID3D12Resource* depthRes    = m_impl->GetNativeResource(input.depthTexture);
    ID3D12Resource* velocityRes = m_impl->GetNativeResource(input.velocityTexture);
    ID3D12Resource* outputRes   = m_impl->GetNativeResource(input.historyWriteTexture);

    if (!colorRes || !depthRes || !velocityRes || !outputRes) {
        UZ_LOG(Renderer, Error, "DLSS: one or more required textures are null");
        return;
    }

    // Set evaluation parameters via NVSDK_NGX_Parameter virtual method table.
    // This avoids using nvsdk_ngx_helpers.h inline functions (which require static linking).
    auto* p = m_impl->ngxParams;
    p->Set(NVSDK_NGX_Parameter_Color, colorRes);
    p->Set(NVSDK_NGX_Parameter_Output, outputRes);
    p->Set(NVSDK_NGX_Parameter_Depth, depthRes);
    p->Set(NVSDK_NGX_Parameter_MotionVectors, velocityRes);
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_X, input.jitterX);
    p->Set(NVSDK_NGX_Parameter_Jitter_Offset_Y, input.jitterY);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    p->Set(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);
    p->Set(NVSDK_NGX_Parameter_Sharpness, cv_DLSSSharpness->value);
    p->Set(NVSDK_NGX_Parameter_Reset, static_cast<int>(input.firstFrame ? 1 : 0));

    // Render subrect dimensions — MANDATORY for DLSS evaluate.
    // Without these, DLSS returns NVSDK_NGX_Result_FAIL_InvalidParameter (0xBAD00005).
    // @ref NVIDIA DLSS Programming Guide — "InRenderSubrectDimensions" must match
    //   the actual rendered region within the input textures.
    const auto renderW = static_cast<unsigned int>(input.renderWidth);
    const auto renderH = static_cast<unsigned int>(input.renderHeight);
    p->Set(NVSDK_NGX_Parameter_Width, renderW);
    p->Set(NVSDK_NGX_Parameter_Height, renderH);
    p->Set("DLSS.Render.Subrect.Dimensions.Width", renderW);
    p->Set("DLSS.Render.Subrect.Dimensions.Height", renderH);

    // Subrect base offsets — (0,0) when rendering full viewport.
    p->Set("DLSS.Input.Color.Subrect.Base.X", static_cast<unsigned int>(0));
    p->Set("DLSS.Input.Color.Subrect.Base.Y", static_cast<unsigned int>(0));
    p->Set("DLSS.Input.Depth.Subrect.Base.X", static_cast<unsigned int>(0));
    p->Set("DLSS.Input.Depth.Subrect.Base.Y", static_cast<unsigned int>(0));
    p->Set("DLSS.Input.MV.Subrect.Base.X", static_cast<unsigned int>(0));
    p->Set("DLSS.Input.MV.Subrect.Base.Y", static_cast<unsigned int>(0));
    p->Set("DLSS.Output.Subrect.Base.X", static_cast<unsigned int>(0));
    p->Set("DLSS.Output.Subrect.Base.Y", static_cast<unsigned int>(0));

    // Pre-exposure — 1.0 when auto-exposure is handled externally.
    p->Set("DLSS.Pre.Exposure", 1.0f);
    p->Set("DLSS.Exposure.Scale", 1.0f);

    NVSDK_NGX_Result result = m_impl->pfnEvaluateFeature(
        nativeCmdList, m_impl->dlssFeature, m_impl->ngxParams, nullptr);

    if (NVSDK_NGX_FAILED(result)) {
        UZ_LOG(Renderer, Error, "DLSS: evaluate failed (result=0x{:08X})",
               static_cast<uint32_t>(result));
    }
}

} // namespace uz

#else // !UZ_DLSS_ENABLED

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

struct DlssUpscaler::Impl {};

DlssUpscaler::DlssUpscaler() = default;
DlssUpscaler::~DlssUpscaler() = default;
DlssUpscaler::DlssUpscaler(DlssUpscaler&&) noexcept = default;
DlssUpscaler& DlssUpscaler::operator=(DlssUpscaler&&) noexcept = default;

UpscalerType DlssUpscaler::GetType() const noexcept { return UpscalerType::DLSS; }
const char* DlssUpscaler::GetName() const noexcept { return "DLSS (stub)"; }
float DlssUpscaler::GetMinScreenPercentage() const noexcept { return 33.f; }
float DlssUpscaler::GetMaxScreenPercentage() const noexcept { return 100.f; }
bool DlssUpscaler::IsAvailable() const noexcept { return false; }

void DlssUpscaler::Init(IRHIDevice*, PipelineLibrary*, CBRingAllocator*) {
    UZ_LOG(Renderer, Warning,
           "DLSS is not available — build with UZ_DLSS_ENABLED=1 (run scripts/setup.ps1 to download the SDK)");
}
void DlssUpscaler::Shutdown() {}
void DlssUpscaler::OnResize(uint32_t, uint32_t) {}
void DlssUpscaler::Execute(IRHICommandList*, const UpscalerInput&) {}

} // namespace uz

#endif // UZ_DLSS_ENABLED
