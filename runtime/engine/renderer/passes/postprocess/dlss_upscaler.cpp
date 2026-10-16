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
using PFN_NVSDK_NGX_D3D12_AllocateParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter**);
using PFN_NVSDK_NGX_D3D12_DestroyParameters = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Parameter*);
using PFN_NVSDK_NGX_D3D12_CreateFeature = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, NVSDK_NGX_Feature, NVSDK_NGX_Parameter*, NVSDK_NGX_Handle**);
using PFN_NVSDK_NGX_D3D12_ReleaseFeature = NVSDK_NGX_Result(NVSDK_CONV*)(NVSDK_NGX_Handle*);
// EvaluateFeature — matches the _nvngx.dll export signature exactly.
// Note: NVSDK_NGX_Parameter* is non-const (the function modifies internal state).
// PFN_NVSDK_NGX_ProgressCallback is a C++ callback — passing nullptr is safe.
using PFN_NVSDK_NGX_D3D12_EvaluateFeature_Fn = NVSDK_NGX_Result(NVSDK_CONV*)(
    ID3D12GraphicsCommandList*, const NVSDK_NGX_Handle*, NVSDK_NGX_Parameter*, PFN_NVSDK_NGX_ProgressCallback);

// C function wrappers for NVSDK_NGX_Parameter::Set — avoids C++ vtable ABI
// mismatch between Zig/mingw and NVIDIA's MSVC-compiled _nvngx.dll.
// @ref Root Cause 1 in docs/analysis/dlss-integration-analysis.md
using PFN_NGX_Param_SetD3d12Resource = NVSDK_NGX_Result(NVSDK_CONV*)(
    NVSDK_NGX_Parameter*, const char*, ID3D12Resource*);
using PFN_NGX_Param_SetF = NVSDK_NGX_Result(NVSDK_CONV*)(
    NVSDK_NGX_Parameter*, const char*, float);
using PFN_NGX_Param_SetUI = NVSDK_NGX_Result(NVSDK_CONV*)(
    NVSDK_NGX_Parameter*, const char*, unsigned int);
using PFN_NGX_Param_SetI = NVSDK_NGX_Result(NVSDK_CONV*)(
    NVSDK_NGX_Parameter*, const char*, int);
using PFN_NGX_Param_SetVoidPtr = NVSDK_NGX_Result(NVSDK_CONV*)(
    NVSDK_NGX_Parameter*, const char*, void*);

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
    PFN_NVSDK_NGX_D3D12_AllocateParameters pfnAllocParams = nullptr;
    PFN_NVSDK_NGX_D3D12_DestroyParameters pfnDestroyParams = nullptr;
    PFN_NVSDK_NGX_D3D12_CreateFeature pfnCreateFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_ReleaseFeature pfnReleaseFeature = nullptr;
    PFN_NVSDK_NGX_D3D12_EvaluateFeature_Fn pfnEvaluateFeature = nullptr;

    // C wrapper functions for NVSDK_NGX_Parameter — avoids vtable ABI mismatch.
    PFN_NGX_Param_SetD3d12Resource pfnSetD3d12Resource = nullptr;
    PFN_NGX_Param_SetF pfnSetF = nullptr;
    PFN_NGX_Param_SetUI pfnSetUI = nullptr;
    PFN_NGX_Param_SetI pfnSetI = nullptr;
    PFN_NGX_Param_SetVoidPtr pfnSetVoidPtr = nullptr;

    bool ngxInitialized = false;
    bool dlssSupported = false;
    NVSDK_NGX_Handle* dlssFeature = nullptr;
    NVSDK_NGX_Parameter* ngxParams = nullptr;     // Capability params (for queries + create)
    NVSDK_NGX_Parameter* ngxEvalParams = nullptr;  // Evaluate params (separate allocation)

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
        // Resolve System32 at runtime via GetSystemDirectoryW so discovery is
        // path-agnostic (do not assume the Windows system drive is C:).
        wchar_t systemDir[MAX_PATH];
        const UINT systemDirLen = ::GetSystemDirectoryW(systemDir, MAX_PATH);
        if (systemDirLen == 0 || systemDirLen >= MAX_PATH) return nullptr;
        wchar_t driverStoreBase[MAX_PATH];
        _snwprintf_s(driverStoreBase, MAX_PATH, _TRUNCATE,
                     L"%s\\DriverStore\\FileRepository", systemDir);
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
        pfnAllocParams = reinterpret_cast<PFN_NVSDK_NGX_D3D12_AllocateParameters>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_AllocateParameters"));
        pfnDestroyParams = reinterpret_cast<PFN_NVSDK_NGX_D3D12_DestroyParameters>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_DestroyParameters"));
        pfnCreateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_CreateFeature>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_CreateFeature"));
        pfnReleaseFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_ReleaseFeature>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_ReleaseFeature"));
        // EvaluateFeature — _nvngx.dll only exports the non-_C version.
        pfnEvaluateFeature = reinterpret_cast<PFN_NVSDK_NGX_D3D12_EvaluateFeature_Fn>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_D3D12_EvaluateFeature"));

        // C wrapper functions for NVSDK_NGX_Parameter — avoids vtable ABI mismatch.
        // These are exported from _nvngx.dll and bypass the C++ virtual dispatch.
        pfnSetD3d12Resource = reinterpret_cast<PFN_NGX_Param_SetD3d12Resource>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetD3d12Resource"));
        pfnSetF = reinterpret_cast<PFN_NGX_Param_SetF>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetF"));
        pfnSetUI = reinterpret_cast<PFN_NGX_Param_SetUI>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetUI"));
        pfnSetI = reinterpret_cast<PFN_NGX_Param_SetI>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetI"));
        pfnSetVoidPtr = reinterpret_cast<PFN_NGX_Param_SetVoidPtr>(
            ::GetProcAddress(ngxModule, "NVSDK_NGX_Parameter_SetVoidPointer"));

        if (!pfnInit || !pfnShutdown || !pfnGetCapParams || !pfnCreateFeature ||
            !pfnReleaseFeature || !pfnEvaluateFeature) {
            UZ_LOG(Renderer, Warning, "DLSS: _nvngx.dll loaded but missing required exports");
            UnloadDll();
            return false;
        }

        // C wrapper functions are optional — only in nvsdk_ngx_d.lib, not in _nvngx.dll.
        // When unavailable, vtable calls are used with explicit overload resolution.
        if (pfnSetD3d12Resource) {
            UZ_LOG(Renderer, Info, "DLSS: _nvngx.dll loaded with C parameter wrappers");
        } else {
            UZ_LOG(Renderer, Info, "DLSS: _nvngx.dll loaded (using vtable parameter dispatch)");
        }
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
        pfnSetD3d12Resource = nullptr;
        pfnSetF = nullptr;
        pfnSetUI = nullptr;
        pfnSetI = nullptr;
        pfnSetVoidPtr = nullptr;
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
            ngxParams = nullptr;
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

        // Allocate a SEPARATE parameter object for evaluate calls.
        // GetCapabilityParameters returns a read-only object for feature queries;
        // using it for EvaluateFeature causes InvalidParameter (0xBAD00005).
        if (pfnAllocParams) {
            NVSDK_NGX_Result allocResult = pfnAllocParams(&ngxEvalParams);
            if (NVSDK_NGX_FAILED(allocResult) || !ngxEvalParams) {
                UZ_LOG(Renderer, Warning, "DLSS: AllocateParameters failed (0x{:08X}), using capability params",
                       static_cast<uint32_t>(allocResult));
                ngxEvalParams = ngxParams; // Fallback
            } else {
                UZ_LOG(Renderer, Info, "DLSS: allocated separate eval params object");
            }
        } else {
            ngxEvalParams = ngxParams; // AllocateParameters not available
        }

        UZ_LOG(Renderer, Info, "DLSS: NGX initialized, DLSS is supported");
        return true;
    }

    void ShutdownNgx() {
        if (!ngxInitialized || !dx12Device || !pfnShutdown) return;
        // Destroy eval params first (if separately allocated)
        if (ngxEvalParams && ngxEvalParams != ngxParams && pfnDestroyParams) {
            pfnDestroyParams(ngxEvalParams);
        }
        ngxEvalParams = nullptr;
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

        // Set DLSS creation parameters via raw vtable access (ABI safety).
        // Slot 3 = Set(const char*, unsigned int)
        using FnSetUI = void(__thiscall*)(void*, const char*, unsigned int);
        void** vt = *reinterpret_cast<void***>(ngxParams);
        auto setUI = [&](const char* key, unsigned int val) {
            reinterpret_cast<FnSetUI>(vt[3])(ngxParams, key, val);
        };
        setUI(NVSDK_NGX_Parameter_Width, optimalRenderWidth);
        setUI(NVSDK_NGX_Parameter_Height, optimalRenderHeight);
        setUI(NVSDK_NGX_Parameter_OutWidth, dispW);
        setUI(NVSDK_NGX_Parameter_OutHeight, dispH);
        setUI(NVSDK_NGX_Parameter_PerfQualityValue,
              static_cast<unsigned int>(ToNgxQuality(quality)));
        setUI(NVSDK_NGX_Parameter_DLSS_Feature_Create_Flags,
              NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
              NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
              NVSDK_NGX_DLSS_Feature_Flags_DepthInverted);

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
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool DlssUpscaler::Init(IRHIDevice* device, PipelineLibrary*, CBRingAllocator*) {
    Shutdown();
    if (!device) { UZ_LOG(Renderer, Error, "DLSS: Init called with null device"); return false; }
    if (device->GetBackend() != RHIBackend::DX12) {
        UZ_LOG(Renderer, Warning, "DLSS: only DX12 backend is supported");
        return false;
    }

    if (!m_impl) m_impl = std::make_unique<Impl>();
    m_impl->device = device;
    m_impl->dx12Device = static_cast<DX12Device*>(device);

    // Step 1: Load _nvngx.dll dynamically.
    if (!m_impl->LoadDll()) {
        Shutdown();
        return false;
    }

    // Step 2: Initialize NGX SDK.
    ID3D12Device* nativeDevice = m_impl->dx12Device->GetDevice();
    if (!nativeDevice || !m_impl->InitNgx(nativeDevice)) {
        Shutdown();
        return false;
    }

    UZ_LOG(Renderer, Info, "DLSS: backend initialized, feature created on first frame");
    return true;
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

bool DlssUpscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("DLSS::Execute");
    if (!m_impl || !m_impl->dx12Device || !m_impl->dlssSupported) return false;

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
    if (!nativeCmdList) return false;

    // Lazily create or recreate feature.
    if (!m_impl->dlssFeature || m_impl->displayWidth != outputW ||
        m_impl->displayHeight != outputH || m_impl->currentQuality != quality) {
        if (outputW == 0 || outputH == 0) return false;
        if (!m_impl->CreateFeature(nativeCmdList, outputW, outputH, quality)) {
            Shutdown();
            return false;
        }
    }

    // Resolve RHI textures to native DX12 resources.
    ID3D12Resource* colorRes    = m_impl->GetNativeResource(input.sceneColorTexture);
    ID3D12Resource* depthRes    = m_impl->GetNativeResource(input.depthTexture);
    ID3D12Resource* velocityRes = m_impl->GetNativeResource(input.velocityTexture);
    ID3D12Resource* outputRes   = m_impl->GetNativeResource(input.historyWriteTexture);

    if (!colorRes || !depthRes || !velocityRes || !outputRes) {
        UZ_LOG(Renderer, Error, "DLSS: null textures — color={} depth={} vel={} out={}",
               (void*)colorRes, (void*)depthRes, (void*)velocityRes, (void*)outputRes);
        return false;
    }

    // Debug: log resource details on first evaluate only
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info, "DLSS eval: color={} depth={} vel={} out={} render={}x{} output={}x{} jitter={:.4f},{:.4f}",
               (void*)colorRes, (void*)depthRes, (void*)velocityRes, (void*)outputRes,
               inputW, inputH, outputW, outputH, input.jitterX, input.jitterY);
        loggedOnce = true;
    }

    // Set evaluation parameters via C wrapper functions to avoid vtable ABI mismatch
    // between Zig/mingw and NVIDIA's MSVC-compiled _nvngx.dll.
    // @ref docs/analysis/dlss-integration-analysis.md — Root Cause 1
    auto* p = m_impl->ngxEvalParams;

    // Raw vtable access for NVSDK_NGX_Parameter — bypasses C++ overload resolution
    // entirely. The vtable is compiled by NVIDIA's MSVC _nvngx.dll; Zig/mingw's
    // overload resolution may pick the wrong slot (e.g., void* instead of ID3D12Resource*).
    //
    // @ref nvsdk_ngx_params.h — vtable slot order (0-based, after implicit destructor):
    //   [0] Set(const char*, unsigned long long)
    //   [1] Set(const char*, float)
    //   [2] Set(const char*, double)
    //   [3] Set(const char*, unsigned int)
    //   [4] Set(const char*, int)
    //   [5] Set(const char*, ID3D11Resource*)
    //   [6] Set(const char*, ID3D12Resource*)
    //   [7] Set(const char*, void*)
    //   [8..15] Get overloads
    //   [16] Reset

    // MSVC vtable: no implicit destructor slot for abstract classes.
    // The first virtual function Set(ull) is at vtable[0].
    using FnSetFloat      = void(__thiscall*)(void*, const char*, float);
    using FnSetUI         = void(__thiscall*)(void*, const char*, unsigned int);
    using FnSetI          = void(__thiscall*)(void*, const char*, int);
    using FnSetD3D12Res   = void(__thiscall*)(void*, const char*, ID3D12Resource*);
    using FnSetVoidPtr    = void(__thiscall*)(void*, const char*, void*);

    void** vtable = *reinterpret_cast<void***>(p);

    auto setF = [&](const char* key, float val) {
        reinterpret_cast<FnSetFloat>(vtable[1])(p, key, val);
    };
    auto setUI = [&](const char* key, unsigned int val) {
        reinterpret_cast<FnSetUI>(vtable[3])(p, key, val);
    };
    auto setI = [&](const char* key, int val) {
        reinterpret_cast<FnSetI>(vtable[4])(p, key, val);
    };
    auto setRes = [&](const char* key, ID3D12Resource* res) {
        reinterpret_cast<FnSetD3D12Res>(vtable[6])(p, key, res);
    };
    auto setVoidPtr = [&](const char* key, void* val) {
        reinterpret_cast<FnSetVoidPtr>(vtable[7])(p, key, val);
    };

    // Core resources
    setRes(NVSDK_NGX_Parameter_Color, colorRes);
    setRes(NVSDK_NGX_Parameter_Output, outputRes);
    setRes(NVSDK_NGX_Parameter_Depth, depthRes);
    setRes(NVSDK_NGX_Parameter_MotionVectors, velocityRes);

    // Jitter (render pixel space, applied to projection matrix)
    setF(NVSDK_NGX_Parameter_Jitter_Offset_X, input.jitterX);
    setF(NVSDK_NGX_Parameter_Jitter_Offset_Y, input.jitterY);

    // Motion vector scale — converts from MV texture value to pixel offset.
    // UzEngine velocity_ps.hlsl outputs in pixels (currScreen - prevScreen),
    // so scale = 1.0 (no conversion needed). Positive Y = downward = DLSS convention.
    setF(NVSDK_NGX_Parameter_MV_Scale_X, 1.0f);
    setF(NVSDK_NGX_Parameter_MV_Scale_Y, 1.0f);

    setF(NVSDK_NGX_Parameter_Sharpness, 0.0f);
    setI(NVSDK_NGX_Parameter_Reset, input.firstFrame ? 1 : 0);

    // Render subrect dimensions — MANDATORY for DLSS evaluate.
    // @ref NVIDIA DLSS Programming Guide — "InRenderSubrectDimensions"
    setUI(NVSDK_NGX_Parameter_Width, inputW);
    setUI(NVSDK_NGX_Parameter_Height, inputH);
    setUI("DLSS.Render.Subrect.Dimensions.Width", inputW);
    setUI("DLSS.Render.Subrect.Dimensions.Height", inputH);

    // Subrect base offsets — (0,0) when rendering full viewport.
    setUI("DLSS.Input.Color.Subrect.Base.X", 0u);
    setUI("DLSS.Input.Color.Subrect.Base.Y", 0u);
    setUI("DLSS.Input.Depth.Subrect.Base.X", 0u);
    setUI("DLSS.Input.Depth.Subrect.Base.Y", 0u);
    setUI("DLSS.Input.MV.Subrect.Base.X", 0u);
    setUI("DLSS.Input.MV.Subrect.Base.Y", 0u);
    setUI("DLSS.Output.Subrect.Base.X", 0u);
    setUI("DLSS.Output.Subrect.Base.Y", 0u);

    // Pre-exposure — 1.0 (no pre-exposure applied to inputs).
    setF("DLSS.Pre.Exposure", 1.0f);
    setF("DLSS.Exposure.Scale", 1.0f);

    // Optional resources (nullptr to satisfy the parameter validator).
    setVoidPtr("DLSS.Input.Bias.Current.Color.Mask", nullptr);
    setVoidPtr(NVSDK_NGX_Parameter_TransparencyMask, nullptr);
    setVoidPtr(NVSDK_NGX_Parameter_ExposureTexture, nullptr);
    setVoidPtr("DLSS.Input.GBuffer.Albedo", nullptr);
    setVoidPtr("DLSS.Input.GBuffer.Roughness", nullptr);
    setVoidPtr("DLSS.Input.GBuffer.Metallic", nullptr);
    setVoidPtr("DLSS.Input.GBuffer.Normals", nullptr);

    NVSDK_NGX_Result result = m_impl->pfnEvaluateFeature(
        nativeCmdList, m_impl->dlssFeature, m_impl->ngxEvalParams, nullptr);

    if (NVSDK_NGX_FAILED(result)) {
        UZ_LOG(Renderer, Error, "DLSS: evaluate failed (result=0x{:08X})",
               static_cast<uint32_t>(result));
        return false;
    }
    return true;
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

bool DlssUpscaler::Init(IRHIDevice*, PipelineLibrary*, CBRingAllocator*) {
    UZ_LOG(Renderer, Warning,
           "DLSS is not available — build with UZ_DLSS_ENABLED=1 (run scripts/setup.ps1 to download the SDK)");
    return false;
}
void DlssUpscaler::Shutdown() {}
void DlssUpscaler::OnResize(uint32_t, uint32_t) {}
bool DlssUpscaler::Execute(IRHICommandList*, const UpscalerInput&) { return false; }

} // namespace uz

#endif // UZ_DLSS_ENABLED
