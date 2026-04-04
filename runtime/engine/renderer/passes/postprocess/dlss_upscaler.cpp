/// UzEngine DLSS Temporal Upscaler — Implementation
///
/// Integrates NVIDIA DLSS via the NGX SDK C API (nvsdk_ngx.h) and the
/// DX12 backend (nvsdk_ngx_helpers_dx12.h). The implementation:
///   1. Initializes NGX with NVSDK_NGX_D3D12_Init (locates nvngx_dlss.dll)
///   2. Queries DLSS capability and optimal settings
///   3. Creates the DLSS feature with NVSDK_NGX_D3D12_CreateFeature
///   4. Each frame, evaluates DLSS with NVSDK_NGX_D3D12_EvaluateFeature
///   5. On shutdown, releases feature and shuts down NGX
///
/// @ref NVIDIA 2020 "Deep Learning Super Sampling"

// DLSS requires the NVIDIA NGX SDK (nvsdk_ngx.h + nvngx_dlss.dll).
// Define UZ_DLSS_ENABLED=1 when the SDK is available (auto-detected by build.zig).
// Without it, DlssUpscaler compiles as a no-op stub that logs a warning on Init().
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
#include "platform/platform.h"

#include <nvsdk_ngx.h>
#include <nvsdk_ngx_defs.h>
#include <nvsdk_ngx_helpers.h>

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ─────────────────────────────────────────────────────────────────────────────
// CVars
// ─────────────────────────────────────────────────────────────────────────────

/// DLSS quality preset: 0=UltraPerf, 1=Performance, 2=Balanced, 3=Quality, 4=DLAA.
/// @ref NVIDIA DLSS SDK — quality presets table.
static auto* cv_DLSSQuality = UZ_CVAR_INT("r.DLSS.Quality", 3,
    "DLSS quality: 0=UltraPerf, 1=Performance, 2=Balanced, 3=Quality, 4=DLAA", 0, 4);

/// RCAS-style sharpening applied after DLSS temporal accumulation.
/// 0.0 = no sharpening, 1.0 = maximum sharpening.
static auto* cv_DLSSSharpness = UZ_CVAR_FLOAT("r.DLSS.Sharpness", 0.0f,
    "DLSS sharpness [0, 1]", 0.0f, 1.0f);

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps DlssQuality enum to NVSDK_NGX_PerfQuality_Value.
static NVSDK_NGX_PerfQuality_Value ToNgxQuality(DlssQuality q) {
    switch (q) {
    case DlssQuality::UltraPerformance: return NVSDK_NGX_PerfQuality_Value_UltraPerformance;
    case DlssQuality::Performance:      return NVSDK_NGX_PerfQuality_Value_MaxPerf;
    case DlssQuality::Balanced:         return NVSDK_NGX_PerfQuality_Value_Balanced;
    case DlssQuality::Quality:          return NVSDK_NGX_PerfQuality_Value_MaxQuality;
    case DlssQuality::DLAA:             return NVSDK_NGX_PerfQuality_Value_DLAA;
    }
    return NVSDK_NGX_PerfQuality_Value_MaxQuality;
}

/// UzEngine application ID for NGX registration (arbitrary, non-zero).
static constexpr uint64_t kNgxAppId = 0x557A456E67696E65ULL; // "UzEngine" as hex

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// PIMPL
// ─────────────────────────────────────────────────────────────────────────────

struct DlssUpscaler::Impl {
    /// RHI device reference.
    IRHIDevice* device = nullptr;

    /// DX12 device pointer — cached from Init().
    DX12Device* dx12Device = nullptr;

    /// NGX initialization succeeded.
    bool ngxInitialized = false;

    /// DLSS feature is supported on this GPU/driver.
    bool dlssSupported = false;

    /// DLSS feature handle (created by NGX).
    NVSDK_NGX_Handle* dlssFeature = nullptr;

    /// NGX parameter block for querying DLSS capabilities.
    NVSDK_NGX_Parameter* ngxParams = nullptr;

    /// Display resolution the feature was created for.
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;

    /// Optimal render resolution queried from NGX for current quality preset.
    uint32_t optimalRenderWidth = 0;
    uint32_t optimalRenderHeight = 0;

    /// Current quality preset the feature was created with.
    DlssQuality currentQuality = DlssQuality::Quality;

    ~Impl() {
        DestroyFeature();
        ShutdownNgx();
    }

    /// Initializes the NGX SDK with the application directory.
    bool InitNgx(ID3D12Device* nativeDevice) {
        // Use the executable directory as the NGX data path.
        // nvngx_dlss.dll must be present in thirdparty/dlss/lib/Windows_x86_64/rel/
        // or alongside the executable.
        std::wstring dataPath = uz::GetExeDirW();

        NVSDK_NGX_Result result = NVSDK_NGX_D3D12_Init(
            kNgxAppId,
            dataPath.c_str(),
            nativeDevice);

        if (NVSDK_NGX_FAILED(result)) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: NGX init failed (result=0x{:08X}) — falling back to alternative upscaler",
                   static_cast<uint32_t>(result));
            return false;
        }

        // Get NGX capability parameters.
        result = NVSDK_NGX_D3D12_GetCapabilityParameters(&ngxParams);
        if (NVSDK_NGX_FAILED(result)) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: failed to get NGX capability parameters (result=0x{:08X})",
                   static_cast<uint32_t>(result));
            NVSDK_NGX_D3D12_Shutdown1(nativeDevice);
            return false;
        }

        // Check if DLSS is supported.
        unsigned int dlssAvailable = 0;
        result = ngxParams->Get(NVSDK_NGX_Parameter_SuperSampling_Available, &dlssAvailable);
        if (NVSDK_NGX_FAILED(result) || dlssAvailable == 0) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: not supported on this GPU/driver — falling back to alternative upscaler");
            NVSDK_NGX_D3D12_DestroyParameters(ngxParams);
            ngxParams = nullptr;
            NVSDK_NGX_D3D12_Shutdown1(nativeDevice);
            return false;
        }

        ngxInitialized = true;
        dlssSupported = true;
        UZ_LOG(Renderer, Info, "DLSS: NGX initialized, DLSS is supported");
        return true;
    }

    /// Shuts down the NGX SDK.
    void ShutdownNgx() {
        if (!ngxInitialized || !dx12Device) return;

        if (ngxParams) {
            NVSDK_NGX_D3D12_DestroyParameters(ngxParams);
            ngxParams = nullptr;
        }

        NVSDK_NGX_D3D12_Shutdown1(dx12Device->GetDevice());
        ngxInitialized = false;
        dlssSupported = false;
        UZ_LOG(Renderer, Info, "DLSS: NGX shut down");
    }

    /// Queries optimal render resolution for the given quality preset and display resolution.
    bool QueryOptimalSettings(uint32_t dispW, uint32_t dispH, DlssQuality quality) {
        if (!ngxParams) return false;

        unsigned int optW = 0, optH = 0;
        unsigned int minW = 0, minH = 0;
        unsigned int maxW = 0, maxH = 0;
        float sharpness = 0.0f;

        NVSDK_NGX_Result result = NGX_DLSS_GET_OPTIMAL_SETTINGS(
            ngxParams,
            dispW, dispH,
            ToNgxQuality(quality),
            &optW, &optH,
            &maxW, &maxH,
            &minW, &minH,
            &sharpness);

        if (NVSDK_NGX_FAILED(result) || optW == 0 || optH == 0) {
            UZ_LOG(Renderer, Warning,
                   "DLSS: failed to query optimal settings (result=0x{:08X})",
                   static_cast<uint32_t>(result));
            return false;
        }

        optimalRenderWidth = optW;
        optimalRenderHeight = optH;

        UZ_LOG(Renderer, Info,
               "DLSS: optimal render {}x{} for display {}x{} (quality={})",
               optW, optH, dispW, dispH, static_cast<uint32_t>(quality));
        return true;
    }

    /// Creates the DLSS feature for the given display resolution and quality.
    bool CreateFeature(ID3D12GraphicsCommandList* cmdList,
                       uint32_t dispW, uint32_t dispH, DlssQuality quality) {
        if (dlssFeature) {
            DestroyFeature();
        }

        if (!QueryOptimalSettings(dispW, dispH, quality)) {
            return false;
        }

        displayWidth = dispW;
        displayHeight = dispH;
        currentQuality = quality;

        unsigned int creationFlags =
            NVSDK_NGX_DLSS_Feature_Flags_IsHDR |
            NVSDK_NGX_DLSS_Feature_Flags_MVLowRes |
            NVSDK_NGX_DLSS_Feature_Flags_DepthInverted |
            NVSDK_NGX_DLSS_Feature_Flags_AutoExposure;

        NVSDK_NGX_DLSS_Create_Params dlssParams{};
        dlssParams.Feature.InWidth = optimalRenderWidth;
        dlssParams.Feature.InHeight = optimalRenderHeight;
        dlssParams.Feature.InTargetWidth = dispW;
        dlssParams.Feature.InTargetHeight = dispH;
        dlssParams.InFeatureCreateFlags = creationFlags;

        NVSDK_NGX_Result result = NGX_D3D12_CREATE_DLSS_EXT(
            cmdList,
            1, 1,
            &dlssFeature,
            ngxParams,
            &dlssParams);

        if (NVSDK_NGX_FAILED(result)) {
            UZ_LOG(Renderer, Error,
                   "DLSS: feature creation failed (result=0x{:08X})",
                   static_cast<uint32_t>(result));
            dlssFeature = nullptr;
            return false;
        }

        UZ_LOG(Renderer, Info,
               "DLSS: feature created — render {}x{} → display {}x{}",
               optimalRenderWidth, optimalRenderHeight, dispW, dispH);
        return true;
    }

    /// Destroys the DLSS feature.
    void DestroyFeature() {
        if (!dlssFeature) return;

        NVSDK_NGX_D3D12_ReleaseFeature(dlssFeature);
        dlssFeature = nullptr;
        UZ_LOG(Renderer, Info, "DLSS: feature destroyed");
    }

    /// Resolves an RHI texture handle to a native ID3D12Resource pointer.
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

DlssUpscaler::~DlssUpscaler() {
    Shutdown();
}

DlssUpscaler::DlssUpscaler(DlssUpscaler&&) noexcept = default;
DlssUpscaler& DlssUpscaler::operator=(DlssUpscaler&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

UpscalerType DlssUpscaler::GetType() const noexcept {
    return UpscalerType::DLSS;
}

const char* DlssUpscaler::GetName() const noexcept {
    return "DLSS";
}

float DlssUpscaler::GetMinScreenPercentage() const noexcept {
    return 33.3f; // Ultra Performance = 3x
}

float DlssUpscaler::GetMaxScreenPercentage() const noexcept {
    return 100.f; // DLAA = native
}

bool DlssUpscaler::IsAvailable() const noexcept {
    return m_impl && m_impl->dlssSupported && m_impl->ngxInitialized;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void DlssUpscaler::Init(IRHIDevice* device, PipelineLibrary* /*pipelines*/,
                         CBRingAllocator* /*cbRing*/) {
    if (!device) {
        UZ_LOG(Renderer, Error, "DLSS: Init called with null device");
        return;
    }

    // DLSS requires DX12 backend.
    if (device->GetBackend() != RHIBackend::DX12) {
        UZ_LOG(Renderer, Warning, "DLSS: only DX12 backend is supported");
        return;
    }

    m_impl->device = device;
    m_impl->dx12Device = static_cast<DX12Device*>(device);

    ID3D12Device* nativeDevice = m_impl->dx12Device->GetDevice();
    if (!nativeDevice) {
        UZ_LOG(Renderer, Error, "DLSS: failed to get native ID3D12Device");
        return;
    }

    // Initialize NGX SDK (locates nvngx_dlss.dll, queries DLSS support).
    if (!m_impl->InitNgx(nativeDevice)) {
        return;
    }

    // DLSS feature is created lazily in Execute() when display resolution is known,
    // or in OnResize() when the resolution changes.
    UZ_LOG(Renderer, Info, "DLSS: backend initialized, feature will be created on first frame");
}

void DlssUpscaler::Shutdown() {
    if (m_impl) {
        m_impl->DestroyFeature();
        m_impl->ShutdownNgx();
        m_impl->device = nullptr;
        m_impl->dx12Device = nullptr;
    }
}

void DlssUpscaler::OnResize(uint32_t displayWidth, uint32_t displayHeight) {
    if (!m_impl || !m_impl->dx12Device || !m_impl->dlssSupported) return;

    // If resolution hasn't changed, skip.
    if (m_impl->dlssFeature &&
        m_impl->displayWidth == displayWidth &&
        m_impl->displayHeight == displayHeight) {
        return;
    }

    // Feature will be recreated on next Execute() with the new resolution.
    m_impl->DestroyFeature();
    m_impl->displayWidth = displayWidth;
    m_impl->displayHeight = displayHeight;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execution
// ─────────────────────────────────────────────────────────────────────────────

void DlssUpscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("DLSS::Execute");

    if (!m_impl || !m_impl->dx12Device || !m_impl->dlssSupported) {
        return;
    }

    const auto outputW = static_cast<uint32_t>(input.outputWidth);
    const auto outputH = static_cast<uint32_t>(input.outputHeight);
    const auto quality = static_cast<DlssQuality>(cv_DLSSQuality->value);

    // Get native DX12 command list.
    auto* dx12Cmd = static_cast<DX12CommandList*>(cmd);
    ID3D12GraphicsCommandList* nativeCmdList = dx12Cmd->GetNative();
    if (!nativeCmdList) {
        UZ_LOG(Renderer, Error, "DLSS: failed to get native command list");
        return;
    }

    // Lazily create or recreate feature if resolution/quality changed.
    if (!m_impl->dlssFeature ||
        m_impl->displayWidth != outputW ||
        m_impl->displayHeight != outputH ||
        m_impl->currentQuality != quality) {

        if (outputW == 0 || outputH == 0) {
            UZ_LOG(Renderer, Warning, "DLSS: invalid output resolution {}x{}",
                   outputW, outputH);
            return;
        }

        if (!m_impl->CreateFeature(nativeCmdList, outputW, outputH, quality)) {
            return;
        }
    }

    // Resolve RHI texture handles to native DX12 resources.
    ID3D12Resource* sceneColorRes = m_impl->GetNativeResource(input.sceneColorTexture);
    ID3D12Resource* depthRes      = m_impl->GetNativeResource(input.depthTexture);
    ID3D12Resource* velocityRes   = m_impl->GetNativeResource(input.velocityTexture);
    ID3D12Resource* outputRes     = m_impl->GetNativeResource(input.historyWriteTexture);

    if (!sceneColorRes || !depthRes || !velocityRes || !outputRes) {
        UZ_LOG(Renderer, Error,
               "DLSS: one or more required textures are null "
               "(color={}, depth={}, velocity={}, output={})",
               sceneColorRes != nullptr, depthRes != nullptr,
               velocityRes != nullptr, outputRes != nullptr);
        return;
    }

    // Fill DLSS evaluation parameters.
    NVSDK_NGX_D3D12_DLSS_Eval_Params evalParams{};
    evalParams.Feature.pInColor = sceneColorRes;
    evalParams.Feature.pInOutput = outputRes;
    evalParams.pInDepth = depthRes;
    evalParams.pInMotionVectors = velocityRes;
    evalParams.pInExposureTexture = nullptr; // auto-exposure mode

    // Jitter offset in pixels — same convention as TAA/FSR2.
    evalParams.InJitterOffsetX = input.jitterX;
    evalParams.InJitterOffsetY = input.jitterY;

    // Render resolution for this frame.
    evalParams.InRenderSubrectDimensions.Width  = static_cast<uint32_t>(input.renderWidth);
    evalParams.InRenderSubrectDimensions.Height = static_cast<uint32_t>(input.renderHeight);

    // Motion vector scale — UzEngine velocity is in pixel units.
    evalParams.InMVScaleX = 1.0f;
    evalParams.InMVScaleY = 1.0f;

    // Sharpness from CVar.
    evalParams.InSharpness = cv_DLSSSharpness->value;

    // Reset flag — discard history on first frame or camera cut.
    evalParams.InReset = input.firstFrame ? 1 : 0;

    // Pre-exposure value.
    evalParams.InPreExposure = 1.0f;

    // Evaluate DLSS.
    NVSDK_NGX_Result result = NGX_D3D12_EVALUATE_DLSS_EXT(
        nativeCmdList,
        m_impl->dlssFeature,
        m_impl->ngxParams,
        &evalParams);

    if (NVSDK_NGX_FAILED(result)) {
        UZ_LOG(Renderer, Error,
               "DLSS: evaluate failed (result=0x{:08X})",
               static_cast<uint32_t>(result));
    }
}

} // namespace uz

#else // !UZ_DLSS_ENABLED — stub implementation

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
