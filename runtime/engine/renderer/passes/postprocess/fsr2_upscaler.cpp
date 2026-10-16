/// UzEngine FSR 2.x Temporal Upscaler -- Implementation
///
/// Integrates AMD FidelityFX Super Resolution 2 via the C API (ffx_fsr2.h)
/// and the DX12 backend (ffx_dx12.h). The implementation:
///   1. Allocates scratch memory for the FFX backend
///   2. Creates the FfxInterface via ffxGetInterfaceDX12()
///   3. Creates the FfxFsr2Context with appropriate flags
///   4. Each frame, wraps RHI textures into FfxApiResource and dispatches
///   5. On shutdown, destroys context and frees scratch memory
///
/// @ref AMD 2022 "FidelityFX Super Resolution 2"

// FSR2 requires the AMD FidelityFX SDK prebuilt library (ffx_fsr2.lib).
// Define UZ_FSR2_ENABLED=1 when the lib is available. Without it, FSR2Upscaler
// compiles as a no-op stub that logs a warning on Init().
#ifndef UZ_FSR2_ENABLED
#define UZ_FSR2_ENABLED 0
#endif

#include "fsr2_upscaler.h"
#include "log.h"
#include "cvar.h"

#if UZ_FSR2_ENABLED

#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_types.h"
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_command_list.h"
#include "engine/rhi/dx12/dx12_resources.h"
#include "profiler.h"

// FFX SDK: FSR2 context API (create, dispatch, destroy, jitter helpers).
#include "thirdparty/amd-fidelityfx/Kits/FidelityFX/upscalers/fsr3/include/ffx_fsr2.h"

// FFX DX12 backend function declarations.
//
// We forward-declare instead of including ffx_dx12.h because that header
// transitively pulls in ffx_api_dx12.h which contains an inline helper
// (ffxApiGetResourceDX12) that calls ID3D12Resource::GetDesc() using the
// old COM return-by-value convention. UzEngine uses the Agility SDK headers
// where GetDesc(D3D12_RESOURCE_DESC*) takes an output pointer, causing a
// compilation error in that inline function. We never call that inline --
// we use the compiled .cpp functions declared below.
extern "C" {
FFX_API size_t ffxGetScratchMemorySizeDX12(size_t maxContexts);
FFX_API FfxDevice ffxGetDeviceDX12(ID3D12Device* device);
FFX_API FfxErrorCode ffxGetInterfaceDX12(
    FfxInterface* backendInterface,
    FfxDevice device,
    void* scratchBuffer,
    size_t scratchBufferSize,
    size_t maxContexts);
FFX_API FfxCommandList ffxGetCommandListDX12(ID3D12CommandList* cmdList);
FFX_API FfxApiResource ffxGetResourceDX12(
    const ID3D12Resource* dx12Resource,
    FfxApiResourceDescription ffxResDescription,
    const wchar_t* ffxResName,
    uint32_t state);
FFX_API FfxApiResourceDescription ffxGetResourceDescriptionDX12(
    const ID3D12Resource* pResource,
    FfxApiResourceUsage additionalUsages);
FFX_API FfxApiSurfaceFormat ffxGetSurfaceFormatDX12(DXGI_FORMAT format);
} // extern "C"

#include <cstring>
#include <cstdlib>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// -------------------------------------------------------------------------
// CVars
// -------------------------------------------------------------------------

/// Enable FSR 2.x temporal upscaling (replaces TAA/TSR when active).
static auto* cv_FSR2 = UZ_CVAR_BOOL("r.FSR2", false, "FSR 2.x temporal upscaling");

/// FSR2 quality preset: 0=NativeAA, 1=Quality (1.5x), 2=Balanced (1.7x),
/// 3=Performance (2.0x), 4=Ultra Performance (3.0x).
/// @ref AMD 2022 "FidelityFX Super Resolution 2" -- quality presets table.
static auto* cv_FSR2Quality = UZ_CVAR_INT("r.FSR2.Quality", 1,
    "FSR2 quality: 0=NativeAA, 1=Quality, 2=Balanced, 3=Performance, 4=UltraPerf", 0, 4);

/// RCAS sharpening strength applied after temporal accumulation.
/// 0.0 = no sharpening, 1.0 = maximum sharpening.
/// @ref AMD 2022 "FidelityFX Super Resolution 2" -- RCAS pass.
static auto* cv_FSR2Sharpness = UZ_CVAR_FLOAT("r.FSR2.Sharpness", 0.0f,
    "FSR2 RCAS sharpness [0, 1]", 0.0f, 1.0f);

namespace uz {

// -------------------------------------------------------------------------
// Constants
// -------------------------------------------------------------------------

/// Maximum number of simultaneous effect contexts sharing the FFX backend.
/// FSR2 itself uses 1 context, plus headroom for potential future effects
/// sharing the same backend.
static constexpr size_t kMaxFfxContexts = 4;

// -------------------------------------------------------------------------
// PIMPL
// -------------------------------------------------------------------------

struct FSR2Upscaler::Impl {
    /// RHI device reference -- used to extract native DX12 handles.
    IRHIDevice* device = nullptr;

    /// DX12 device pointer -- cached from Init().
    DX12Device* dx12Device = nullptr;

    /// FFX backend scratch memory. Owned by this struct, freed in destructor.
    void* scratchBuffer = nullptr;
    size_t scratchBufferSize = 0;

    /// FFX backend interface -- function pointers for the DX12 backend.
    FfxInterface backendInterface{};

    /// FSR2 context -- opaque state for the FSR2 algorithm.
    FfxFsr2Context fsr2Context{};

    /// True once the FSR2 context has been successfully created.
    bool contextCreated = false;

    /// Display resolution the context was created for.
    uint32_t displayWidth = 0;
    uint32_t displayHeight = 0;

    /// Maximum render resolution (set to display resolution -- FSR2 renders
    /// at a fraction and upscales to display).
    uint32_t maxRenderWidth = 0;
    uint32_t maxRenderHeight = 0;

    /// Frame index for jitter sequence tracking.
    uint32_t frameIndex = 0;

    ~Impl() {
        DestroyContext();
        FreeScratchBuffer();
    }

    /// Allocates the scratch buffer for the FFX DX12 backend.
    bool AllocateScratchBuffer() {
        scratchBufferSize = ffxGetScratchMemorySizeDX12(kMaxFfxContexts);
        if (scratchBufferSize == 0) {
            UZ_LOG(Renderer, Error, "FSR2: ffxGetScratchMemorySizeDX12 returned 0");
            return false;
        }

        scratchBuffer = std::malloc(scratchBufferSize);
        if (!scratchBuffer) {
            UZ_LOG(Renderer, Error, "FSR2: failed to allocate {} bytes for FFX scratch buffer",
                   scratchBufferSize);
            return false;
        }

        std::memset(scratchBuffer, 0, scratchBufferSize);
        return true;
    }

    /// Frees the scratch buffer.
    void FreeScratchBuffer() {
        if (scratchBuffer) {
            std::free(scratchBuffer);
            scratchBuffer = nullptr;
            scratchBufferSize = 0;
        }
    }

    /// Creates the FFX DX12 backend interface.
    bool CreateBackendInterface(ID3D12Device* nativeDevice) {
        FfxDevice ffxDevice = ffxGetDeviceDX12(nativeDevice);

        FfxErrorCode err = ffxGetInterfaceDX12(
            &backendInterface,
            ffxDevice,
            scratchBuffer,
            scratchBufferSize,
            kMaxFfxContexts);

        if (err != FFX_OK) {
            UZ_LOG(Renderer, Error, "FSR2: ffxGetInterfaceDX12 failed with error {}",
                   static_cast<int32_t>(err));
            return false;
        }

        return true;
    }

    /// Creates the FSR2 context for the given display resolution.
    ///
    /// Flags:
    ///   - HDR: UzEngine renders in RGBA16F HDR.
    ///   - DEPTH_INVERTED: UzEngine uses reversed-Z (near=1, far=0).
    ///   - DEPTH_INFINITE: UzEngine uses infinite far plane.
    ///   - AUTO_EXPOSURE: Let FSR2 compute its own exposure internally.
    bool CreateContext(uint32_t dispW, uint32_t dispH) {
        if (contextCreated) {
            DestroyContext();
        }

        displayWidth    = dispW;
        displayHeight   = dispH;
        maxRenderWidth  = dispW;
        maxRenderHeight = dispH;

        FfxFsr2ContextDescription desc{};
        desc.flags = FFX_FSR2_ENABLE_HIGH_DYNAMIC_RANGE
                   | FFX_FSR2_ENABLE_DEPTH_INVERTED
                   | FFX_FSR2_ENABLE_DEPTH_INFINITE
                   | FFX_FSR2_ENABLE_AUTO_EXPOSURE;

        desc.maxRenderSize.width  = maxRenderWidth;
        desc.maxRenderSize.height = maxRenderHeight;
        desc.displaySize.width    = displayWidth;
        desc.displaySize.height   = displayHeight;
        desc.fpMessage            = nullptr;
        desc.backendInterface     = backendInterface;

        FfxErrorCode err = ffxFsr2ContextCreate(&fsr2Context, &desc);
        if (err != FFX_OK) {
            UZ_LOG(Renderer, Error, "FSR2: ffxFsr2ContextCreate failed with error {}",
                   static_cast<int32_t>(err));
            return false;
        }

        contextCreated = true;
        frameIndex = 0;

        UZ_LOG(Renderer, Info,
               "FSR2: context created -- display {}x{}, max render {}x{}",
               displayWidth, displayHeight, maxRenderWidth, maxRenderHeight);

        return true;
    }

    /// Destroys the FSR2 context if it was created.
    void DestroyContext() {
        if (!contextCreated) return;

        FfxErrorCode err = ffxFsr2ContextDestroy(&fsr2Context);
        if (err != FFX_OK) {
            UZ_LOG(Renderer, Warning, "FSR2: ffxFsr2ContextDestroy returned error {}",
                   static_cast<int32_t>(err));
        }

        std::memset(&fsr2Context, 0, sizeof(fsr2Context));
        contextCreated = false;
        frameIndex = 0;

        UZ_LOG(Renderer, Info, "FSR2: context destroyed");
    }

    /// Resolves an RHI texture handle to a native ID3D12Resource pointer.
    ///
    /// Returns nullptr if the handle is invalid or the texture was destroyed.
    ID3D12Resource* GetNativeResource(RHITextureHandle handle) const {
        if (!handle.IsValid() || !dx12Device) return nullptr;

        auto* dx12Tex = dx12Device->GetTexturePool().Get(handle.index, handle.generation);
        if (!dx12Tex) return nullptr;

        return dx12Tex->resource;
    }

    /// Wraps a native DX12 resource as an FfxApiResource for FSR2 dispatch.
    ///
    /// Uses ffxGetResourceDescriptionDX12() to auto-populate the resource
    /// description from the D3D12 resource descriptor, then calls
    /// ffxGetResourceDX12() to produce the FfxApiResource with the correct
    /// state for the current usage.
    FfxApiResource WrapResource(ID3D12Resource* resource, const wchar_t* name,
                                uint32_t state) const {
        if (!resource) {
            FfxApiResource nullRes{};
            return nullRes;
        }

        FfxApiResourceDescription desc = ffxGetResourceDescriptionDX12(
            resource, FFX_API_RESOURCE_USAGE_READ_ONLY);
        return ffxGetResourceDX12(resource, desc, name, state);
    }
};

// -------------------------------------------------------------------------
// Constructor / Destructor / Move
// -------------------------------------------------------------------------

FSR2Upscaler::FSR2Upscaler()
    : m_impl(std::make_unique<Impl>())
{
}

FSR2Upscaler::~FSR2Upscaler() {
    Shutdown();
}

FSR2Upscaler::FSR2Upscaler(FSR2Upscaler&&) noexcept = default;
FSR2Upscaler& FSR2Upscaler::operator=(FSR2Upscaler&&) noexcept = default;

// -------------------------------------------------------------------------
// Queries
// -------------------------------------------------------------------------

UpscalerType FSR2Upscaler::GetType() const noexcept {
    return UpscalerType::FSR2;
}

const char* FSR2Upscaler::GetName() const noexcept {
    return "FSR2";
}

bool FSR2Upscaler::IsAvailable() const noexcept { return true; }

float FSR2Upscaler::GetMinScreenPercentage() const noexcept {
    // Ultra Performance mode = 3x upscale = render at 33.3% of display.
    return 33.3f;
}

float FSR2Upscaler::GetMaxScreenPercentage() const noexcept {
    // NativeAA mode = 1x = render at 100% of display.
    return 100.f;
}

// -------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------

bool FSR2Upscaler::Init(IRHIDevice* device, PipelineLibrary* /*pipelines*/,
                         CBRingAllocator* /*cbRing*/) {
    Shutdown();
    if (!device) {
        UZ_LOG(Renderer, Error, "FSR2: Init called with null device");
        return false;
    }

    // FSR2 DX12 backend requires a DX12 device.
    if (device->GetBackend() != RHIBackend::DX12) {
        UZ_LOG(Renderer, Error, "FSR2: only DX12 backend is supported");
        return false;
    }

    if (!m_impl) m_impl = std::make_unique<Impl>();
    m_impl->device     = device;
    m_impl->dx12Device = static_cast<DX12Device*>(device);

    ID3D12Device* nativeDevice = m_impl->dx12Device->GetDevice();
    if (!nativeDevice) {
        UZ_LOG(Renderer, Error, "FSR2: failed to get native ID3D12Device");
        Shutdown();
        return false;
    }

    // Allocate scratch memory for the FFX DX12 backend.
    if (!m_impl->AllocateScratchBuffer()) {
        Shutdown();
        return false;
    }

    // Create the backend interface.
    if (!m_impl->CreateBackendInterface(nativeDevice)) {
        Shutdown();
        return false;
    }

    UZ_LOG(Renderer, Info, "FSR2: backend initialized (scratch={} bytes)",
           m_impl->scratchBufferSize);

    // The FSR2 context is created lazily in Execute() when the display
    // resolution is first known, or in OnResize() when the resolution changes.
    return true;
}

void FSR2Upscaler::Shutdown() {
    if (m_impl) {
        m_impl->DestroyContext();
        m_impl->FreeScratchBuffer();
        m_impl->device     = nullptr;
        m_impl->dx12Device = nullptr;
        m_impl->displayWidth = 0;
        m_impl->displayHeight = 0;
        m_impl->maxRenderWidth = 0;
        m_impl->maxRenderHeight = 0;
        std::memset(&m_impl->backendInterface, 0, sizeof(FfxInterface));
    }
}

void FSR2Upscaler::OnResize(uint32_t displayWidth, uint32_t displayHeight) {
    if (!m_impl || !m_impl->dx12Device) return;

    // If the display resolution has not changed, skip recreation.
    if (m_impl->contextCreated &&
        m_impl->displayWidth  == displayWidth &&
        m_impl->displayHeight == displayHeight) {
        return;
    }

    // Recreate the context at the new display resolution.
    // The backend interface persists -- only the effect context needs recreation.
    if (displayWidth > 0 && displayHeight > 0) {
        if (!m_impl->CreateContext(displayWidth, displayHeight)) Shutdown();
    } else {
        m_impl->DestroyContext();
    }
}

// -------------------------------------------------------------------------
// Execution
// -------------------------------------------------------------------------

bool FSR2Upscaler::Execute(IRHICommandList* cmd, const UpscalerInput& input) {
    UZ_PROFILE_SCOPE("FSR2::Execute");

    if (!m_impl || !m_impl->dx12Device) {
        UZ_LOG(Renderer, Error, "FSR2: Execute called before Init");
        return false;
    }

    const auto outputW = static_cast<uint32_t>(input.outputWidth);
    const auto outputH = static_cast<uint32_t>(input.outputHeight);

    // Lazily create or recreate context if display resolution changed.
    if (!m_impl->contextCreated ||
        m_impl->displayWidth  != outputW ||
        m_impl->displayHeight != outputH) {

        if (outputW == 0 || outputH == 0) {
            UZ_LOG(Renderer, Warning, "FSR2: invalid output resolution {}x{}",
                   outputW, outputH);
            return false;
        }

        if (!m_impl->CreateContext(outputW, outputH)) {
            Shutdown();
            return false;
        }
    }

    // Resolve RHI texture handles to native ID3D12Resource pointers.
    ID3D12Resource* sceneColorRes = m_impl->GetNativeResource(input.sceneColorTexture);
    ID3D12Resource* depthRes      = m_impl->GetNativeResource(input.depthTexture);
    ID3D12Resource* velocityRes   = m_impl->GetNativeResource(input.velocityTexture);
    ID3D12Resource* outputRes     = m_impl->GetNativeResource(input.historyWriteTexture);

    if (!sceneColorRes || !depthRes || !velocityRes || !outputRes) {
        UZ_LOG(Renderer, Error,
               "FSR2: one or more required textures are null "
               "(color={}, depth={}, velocity={}, output={})",
               sceneColorRes != nullptr, depthRes != nullptr,
               velocityRes != nullptr, outputRes != nullptr);
        return false;
    }

    // Get the native DX12 command list for the FFX dispatch.
    auto* dx12Cmd = static_cast<DX12CommandList*>(cmd);
    ID3D12GraphicsCommandList* nativeCmdList = dx12Cmd->GetNative();
    if (!nativeCmdList) {
        UZ_LOG(Renderer, Error, "FSR2: failed to get native command list");
        return false;
    }

    FfxCommandList ffxCmdList = ffxGetCommandListDX12(nativeCmdList);

    // Wrap resources for FSR2.
    // Scene color, depth, and velocity are in SRV state (caller ensures this).
    // The output is in UAV state (caller ensures this).
    FfxApiResource ffxColor = m_impl->WrapResource(
        sceneColorRes, L"FSR2_InputColor", FFX_API_RESOURCE_STATE_COMPUTE_READ);
    FfxApiResource ffxDepth = m_impl->WrapResource(
        depthRes, L"FSR2_InputDepth", FFX_API_RESOURCE_STATE_COMPUTE_READ);
    FfxApiResource ffxVelocity = m_impl->WrapResource(
        velocityRes, L"FSR2_InputMotionVectors", FFX_API_RESOURCE_STATE_COMPUTE_READ);
    FfxApiResource ffxOutput = m_impl->WrapResource(
        outputRes, L"FSR2_Output", FFX_API_RESOURCE_STATE_UNORDERED_ACCESS);

    // Fill the dispatch description.
    FfxFsr2DispatchDescription dispatchDesc{};
    dispatchDesc.commandList   = ffxCmdList;
    dispatchDesc.color         = ffxColor;
    dispatchDesc.depth         = ffxDepth;
    dispatchDesc.motionVectors = ffxVelocity;
    dispatchDesc.output        = ffxOutput;

    // Exposure -- let FSR2 handle it via auto-exposure (we passed
    // FFX_FSR2_ENABLE_AUTO_EXPOSURE). Provide a null resource.
    dispatchDesc.exposure = {};

    // Reactive and transparency masks -- not currently generated by UzEngine.
    // Pass null resources; FSR2 handles missing masks gracefully.
    dispatchDesc.reactive                   = {};
    dispatchDesc.transparencyAndComposition = {};

    // Jitter offset in pixels -- FSR2 expects the jitter that was applied to
    // the camera's projection matrix for this frame.
    dispatchDesc.jitterOffset.x = input.jitterX;
    dispatchDesc.jitterOffset.y = input.jitterY;

    // Motion vector scale.
    // UzEngine's velocity buffer stores screen-space pixel displacement.
    // FSR2 internally computes: pixelOffset = MV * motionVectorScale.
    // Since our MVs are already in pixel units, we set scale to (1, 1).
    // @ref AMD 2022 "FidelityFX Super Resolution 2" -- motion vector setup.
    dispatchDesc.motionVectorScale.x = 1.0f;
    dispatchDesc.motionVectorScale.y = 1.0f;

    // Render resolution for this frame.
    dispatchDesc.renderSize.width  = static_cast<uint32_t>(input.renderWidth);
    dispatchDesc.renderSize.height = static_cast<uint32_t>(input.renderHeight);

    // RCAS sharpening from CVar.
    const float sharpness = cv_FSR2Sharpness->value;
    dispatchDesc.enableSharpening = (sharpness > 0.001f);
    dispatchDesc.sharpness        = sharpness;

    // Frame timing -- FSR2 uses this for temporal stability heuristics.
    // Approximate at 16.67ms (60 FPS target). A proper implementation would
    // pass the actual delta time from the frame timer.
    dispatchDesc.frameTimeDelta = 16.67f;

    // Pre-exposure value -- the exposure multiplier already applied to the
    // scene color before it reaches FSR2. 1.0 means no pre-exposure.
    dispatchDesc.preExposure = 1.0f;

    // Reset -- signals FSR2 to discard history (camera cut, teleport, etc.).
    dispatchDesc.reset = input.firstFrame;

    // Camera parameters -- used by FSR2 for depth reconstruction and velocity
    // confidence heuristics. UzEngine uses reversed-Z infinite far plane.
    dispatchDesc.cameraNear             = 10.0f;   // 10cm near plane (UzEngine default)
    dispatchDesc.cameraFar              = 0.0f;    // Infinite far plane (reversed-Z)
    dispatchDesc.cameraFovAngleVertical = 1.0472f; // ~60 degrees in radians (pi/3)

    // View space to meters conversion. UzEngine uses centimeters as base
    // units, so 1 view-space unit = 0.01 meters.
    dispatchDesc.viewSpaceToMetersFactor = 0.01f;

    // Auto-reactive generation (experimental FSR2 feature) -- disabled.
    dispatchDesc.enableAutoReactive = false;

    // Dispatch FSR2.
    FfxErrorCode err = ffxFsr2ContextDispatch(&m_impl->fsr2Context, &dispatchDesc);
    if (err != FFX_OK) {
        UZ_LOG(Renderer, Error, "FSR2: ffxFsr2ContextDispatch failed with error {}",
               static_cast<int32_t>(err));
        return false;
    }

    m_impl->frameIndex++;
    return true;
}

} // namespace uz

#else // !UZ_FSR2_ENABLED — stub implementation

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

struct FSR2Upscaler::Impl {};

FSR2Upscaler::FSR2Upscaler() = default;
FSR2Upscaler::~FSR2Upscaler() = default;

UpscalerType FSR2Upscaler::GetType() const noexcept { return UpscalerType::FSR2; }
const char* FSR2Upscaler::GetName() const noexcept { return "FSR2 (stub)"; }
bool FSR2Upscaler::IsAvailable() const noexcept { return false; }
float FSR2Upscaler::GetMinScreenPercentage() const noexcept { return 33.f; }
float FSR2Upscaler::GetMaxScreenPercentage() const noexcept { return 100.f; }

bool FSR2Upscaler::Init(IRHIDevice*, PipelineLibrary*, CBRingAllocator*) {
    UZ_LOG(Renderer, Warning,
           "FSR2 is not available — build with UZ_FSR2_ENABLED=1 and link ffx_fsr2.lib");
    return false;
}
void FSR2Upscaler::Shutdown() {}
void FSR2Upscaler::OnResize(uint32_t, uint32_t) {}
bool FSR2Upscaler::Execute(IRHICommandList*, const UpscalerInput&) { return false; }

} // namespace uz

#endif // UZ_FSR2_ENABLED
