/// UzEngine SSGI Renderer — Implementation.
///
/// SSGI method selection + quality presets (ray count per quality level)

#include "ssgi_renderer.h"

#include "pipeline_library.h"
#include "cb_ring_allocator.h"
#include "root_slots.h"
#include "constant_buffers.h"
#include "rhi.h"
#include "log.h"
#include "engine_assert.h"
#include "profiler.h"
#include "cvar.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_SSGIEnabled   = UZ_CVAR_BOOL("r.SSGI", false, "Screen-space global illumination");
static auto* cv_SSGIQuality   = UZ_CVAR_INT("r.SSGI.Quality", 2, "SSGI quality (1=4rays, 2=8, 3=16, 4=32)", 1, 4);
static auto* cv_SSGIIntensity = UZ_CVAR_FLOAT("r.SSGI.Intensity", 0.3f, "SSGI indirect diffuse intensity", 0.f, 10.f);
static auto* cv_SSGIRayCount     = UZ_CVAR_INT("r.SSGI.RayCount", 8, "Rays per pixel (overrides quality preset)", 1, 32);
static auto* cv_SSGIMaxSteps     = UZ_CVAR_INT("r.SSGI.MaxMarchSteps", 32, "Max linear march steps per ray", 8, 128);
static auto* cv_SSGIStepSize     = UZ_CVAR_FLOAT("r.SSGI.StepSize", 0.02f, "UV-space step size per march step", 0.005f, 0.1f);
static auto* cv_SSGIThickness    = UZ_CVAR_FLOAT("r.SSGI.Thickness", 0.5f, "Depth comparison thickness (cm)", 0.01f, 2.0f);
static auto* cv_SSGITemporalAlpha = UZ_CVAR_FLOAT("r.SSGI.TemporalAlpha", 0.05f, "Temporal accumulation alpha (0=full history, 1=no history)", 0.01f, 0.5f);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

SSGIRenderer::~SSGIRenderer() {
    DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource Management
// ─────────────────────────────────────────────────────────────────────────────

bool SSGIRenderer::CreateResources(IRHIDevice* device, uint32_t width, uint32_t height) {
    if (!device || width == 0 || height == 0) {
        UZ_LOG(Renderer, Error, "SSGIRenderer::CreateResources: invalid params");
        return false;
    }

    m_device = device;
    m_fullWidth  = width;
    m_fullHeight = height;
    m_halfWidth  = width / 2;
    m_halfHeight = height / 2;

    if (m_halfWidth == 0) m_halfWidth = 1;
    if (m_halfHeight == 0) m_halfHeight = 1;

    if (!CreateTextures(width, height)) {
        UZ_LOG(Renderer, Error, "SSGIRenderer: failed to create textures");
        DestroyTextures();
        return false;
    }

    m_initialized = true;
    m_hasPrevColor = false;
    m_historyIndex = 0;

    UZ_LOG(Renderer, Info,
           "SSGIRenderer: initialized ({}x{} full, {}x{} half, result SRV={}, AO SRV={})",
           m_fullWidth, m_fullHeight, m_halfWidth, m_halfHeight,
           m_ssgiResultSrv, m_ssgiResultAOSrv);

    return true;
}

void SSGIRenderer::DestroyResources() {
    DestroyTextures();
    m_initialized = false;
    m_hasPrevColor = false;
    m_device = nullptr;
}

void SSGIRenderer::OnResize(uint32_t width, uint32_t height) {
    if (!m_device || width == 0 || height == 0) return;

    DestroyTextures();

    m_fullWidth  = width;
    m_fullHeight = height;
    m_halfWidth  = (width > 1) ? (width / 2) : 1;
    m_halfHeight = (height > 1) ? (height / 2) : 1;

    if (!CreateTextures(width, height)) {
        UZ_LOG(Renderer, Error, "SSGIRenderer::OnResize: failed to recreate textures");
        m_initialized = false;
        return;
    }

    m_hasPrevColor = false;
    m_historyIndex = 0;

    UZ_LOG(Renderer, Info,
           "SSGIRenderer: resized ({}x{} full, {}x{} half)",
           m_fullWidth, m_fullHeight, m_halfWidth, m_halfHeight);
}

// ─────────────────────────────────────────────────────────────────────────────
// Texture Creation / Destruction
// ─────────────────────────────────────────────────────────────────────────────

bool SSGIRenderer::CreateTextures(uint32_t width, uint32_t height) {
    const uint32_t halfW = m_halfWidth;
    const uint32_t halfH = m_halfHeight;

    // Helper lambda for creating SRV+UAV textures
    auto createTexSrvUav = [this](uint32_t w, uint32_t h, RHIFormat fmt,
                                   const char* name,
                                   RHITextureHandle& outTex,
                                   uint32_t& outSrv, uint32_t& outUav) -> bool {
        RHITextureDesc desc{};
        desc.width     = w;
        desc.height    = h;
        desc.format    = fmt;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = name;

        outTex = m_device->CreateTexture(desc);
        if (!outTex.IsValid()) {
            UZ_LOG(Renderer, Error, "SSGIRenderer: failed to create texture '{}'", name);
            return false;
        }
        outSrv = m_device->GetTextureSRVIndex(outTex);
        outUav = m_device->GetTextureUAVIndex(outTex);
        return true;
    };

    // Helper for SRV-only textures (prev scene color copy source)
    auto createTexSrvCopy = [this](uint32_t w, uint32_t h, RHIFormat fmt,
                                    const char* name,
                                    RHITextureHandle& outTex,
                                    uint32_t& outSrv) -> bool {
        RHITextureDesc desc{};
        desc.width     = w;
        desc.height    = h;
        desc.format    = fmt;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::CopyDst;
        desc.debugName = name;

        outTex = m_device->CreateTexture(desc);
        if (!outTex.IsValid()) {
            UZ_LOG(Renderer, Error, "SSGIRenderer: failed to create texture '{}'", name);
            return false;
        }
        outSrv = m_device->GetTextureSRVIndex(outTex);
        return true;
    };

    // ── Full-res textures ──────────────────────────────────────────────

    // Previous frame scene color copy (full-res, CopyDst + SRV)
    if (!createTexSrvCopy(width, height, RHIFormat::RGBA16Float,
                          "SSGI_PrevSceneColor", m_prevSceneColor, m_prevSceneColorSrv))
        return false;

    // Final upsampled SSGI result (full-res RGBA16F)
    if (!createTexSrvUav(width, height, RHIFormat::RGBA16Float,
                         "SSGI_Result", m_ssgiResult, m_ssgiResultSrv, m_ssgiResultUav))
        return false;

    // Final upsampled SSGI AO result (full-res R16F)
    if (!createTexSrvUav(width, height, RHIFormat::R16Float,
                         "SSGI_ResultAO", m_ssgiResultAO, m_ssgiResultAOSrv, m_ssgiResultAOUav))
        return false;

    // ── Half-res textures ──────────────────────────────────────────────

    // Reduced prev color (half-res RGBA16F)
    if (!createTexSrvUav(halfW, halfH, RHIFormat::RGBA16Float,
                         "SSGI_PrevColorReduced", m_prevColorReduced,
                         m_prevColorReducedSrv, m_prevColorReducedUav))
        return false;

    // Trace output: indirect diffuse (half-res RGBA16F)
    if (!createTexSrvUav(halfW, halfH, RHIFormat::RGBA16Float,
                         "SSGI_Trace", m_ssgiTrace, m_ssgiTraceSrv, m_ssgiTraceUav))
        return false;

    // Trace output: AO (half-res R16F)
    if (!createTexSrvUav(halfW, halfH, RHIFormat::R16Float,
                         "SSGI_TraceAO", m_ssgiTraceAO, m_ssgiTraceAOSrv, m_ssgiTraceAOUav))
        return false;

    // Denoised: indirect diffuse (half-res RGBA16F)
    if (!createTexSrvUav(halfW, halfH, RHIFormat::RGBA16Float,
                         "SSGI_Denoised", m_ssgiDenoised, m_ssgiDenoisedSrv, m_ssgiDenoisedUav))
        return false;

    // Denoised: AO (half-res R16F)
    if (!createTexSrvUav(halfW, halfH, RHIFormat::R16Float,
                         "SSGI_DenoisedAO", m_ssgiDenoisedAO, m_ssgiDenoisedAOSrv, m_ssgiDenoisedAOUav))
        return false;

    // Temporal history ping-pong: color x2
    for (int i = 0; i < 2; ++i) {
        const char* names[2] = {"SSGI_History_0", "SSGI_History_1"};
        if (!createTexSrvUav(halfW, halfH, RHIFormat::RGBA16Float,
                             names[i], m_ssgiHistory[i],
                             m_ssgiHistorySrv[i], m_ssgiHistoryUav[i]))
            return false;
    }

    // Temporal history ping-pong: AO x2
    for (int i = 0; i < 2; ++i) {
        const char* names[2] = {"SSGI_HistoryAO_0", "SSGI_HistoryAO_1"};
        if (!createTexSrvUav(halfW, halfH, RHIFormat::R16Float,
                             names[i], m_ssgiHistoryAO[i],
                             m_ssgiHistoryAOSrv[i], m_ssgiHistoryAOUav[i]))
            return false;
    }

    return true;
}

void SSGIRenderer::DestroyTextures() {
    if (!m_device) return;

    auto destroyTex = [this](RHITextureHandle& tex) {
        if (tex.IsValid()) {
            m_device->DestroyTexture(tex);
            tex = RHITextureHandle{};
        }
    };

    destroyTex(m_prevSceneColor);
    m_prevSceneColorSrv = UINT32_MAX;

    destroyTex(m_prevColorReduced);
    m_prevColorReducedSrv = UINT32_MAX;
    m_prevColorReducedUav = UINT32_MAX;

    destroyTex(m_ssgiTrace);
    m_ssgiTraceSrv = UINT32_MAX;
    m_ssgiTraceUav = UINT32_MAX;

    destroyTex(m_ssgiTraceAO);
    m_ssgiTraceAOSrv = UINT32_MAX;
    m_ssgiTraceAOUav = UINT32_MAX;

    destroyTex(m_ssgiDenoised);
    m_ssgiDenoisedSrv = UINT32_MAX;
    m_ssgiDenoisedUav = UINT32_MAX;

    destroyTex(m_ssgiDenoisedAO);
    m_ssgiDenoisedAOSrv = UINT32_MAX;
    m_ssgiDenoisedAOUav = UINT32_MAX;

    for (int i = 0; i < 2; ++i) {
        destroyTex(m_ssgiHistory[i]);
        m_ssgiHistorySrv[i] = UINT32_MAX;
        m_ssgiHistoryUav[i] = UINT32_MAX;
    }

    for (int i = 0; i < 2; ++i) {
        destroyTex(m_ssgiHistoryAO[i]);
        m_ssgiHistoryAOSrv[i] = UINT32_MAX;
        m_ssgiHistoryAOUav[i] = UINT32_MAX;
    }

    destroyTex(m_ssgiResult);
    m_ssgiResultSrv = UINT32_MAX;
    m_ssgiResultUav = UINT32_MAX;

    destroyTex(m_ssgiResultAO);
    m_ssgiResultAOSrv = UINT32_MAX;
    m_ssgiResultAOUav = UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render — dispatches all 5 SSGI passes
// ─────────────────────────────────────────────────────────────────────────────

void SSGIRenderer::Render(IRHICommandList* cmdList, const SSGIRenderParams& params) {
    UZ_PROFILE_FUNCTION();

    // Apply CVar overrides to settings
    settings.enabled   = cv_SSGIEnabled->value >= 0.5f;
    settings.quality   = static_cast<int>(cv_SSGIQuality->value);
    settings.intensity = cv_SSGIIntensity->value;

    if (!m_initialized || !settings.enabled) return;

    // Skip if no previous frame color is available yet (first frame).
    if (!m_hasPrevColor) return;

    // Pass 1: Downsample prev scene color to half-res
    DispatchPrevColorReduce(cmdList, params);

    // Pass 2: SSGI trace (half-res)
    DispatchTrace(cmdList, params);

    // Pass 3: Spatial denoise (half-res)
    DispatchSpatialDenoise(cmdList, params);

    // Pass 4: Temporal accumulation (half-res)
    DispatchTemporalAccum(cmdList, params);

    // Pass 5: Bilateral upsample (half-res -> full-res)
    DispatchUpsample(cmdList, params);

    // Flip history ping-pong for next frame
    m_historyIndex = 1 - m_historyIndex;
}

// ─────────────────────────────────────────────────────────────────────────────
// CopyPrevSceneColor — copy HDR output for next frame
// ─────────────────────────────────────────────────────────────────────────────

void SSGIRenderer::CopyPrevSceneColor(IRHICommandList* cmdList,
                                       RHITextureHandle hdrSceneColor,
                                       uint32_t width, uint32_t height) {
    if (!m_initialized || !settings.enabled) return;
    if (!hdrSceneColor.IsValid() || !m_prevSceneColor.IsValid()) return;

    UZ_PROFILE_SCOPE("SSGI::CopyPrevSceneColor");

    // Transition: HDR scene color RTV/SRV -> CopySrc
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = hdrSceneColor;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter  = RHIResourceState::CopySrc;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // Transition: prev scene color -> CopyDst.
    // On the first frame the texture is in Common state (from creation);
    // on subsequent frames it is in ShaderResource (from the restore barrier below).
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_prevSceneColor;
        barrier.stateBefore = m_hasPrevColor ? RHIResourceState::ShaderResource
                                             : RHIResourceState::Common;
        barrier.stateAfter  = RHIResourceState::CopyDst;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    cmdList->CopyTexture(hdrSceneColor, m_prevSceneColor);

    // Transition back: HDR scene color CopySrc -> RenderTarget
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = hdrSceneColor;
        barrier.stateBefore = RHIResourceState::CopySrc;
        barrier.stateAfter  = RHIResourceState::RenderTarget;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // Transition back: prev scene color CopyDst -> SRV
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_prevSceneColor;
        barrier.stateBefore = RHIResourceState::CopyDst;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    m_hasPrevColor = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 1: Previous Frame Color Reduction
// ─────────────────────────────────────────────────────────────────────────────

/// SetupPrevSceneColorReduction — mip chain of previous frame color for ray hits
void SSGIRenderer::DispatchPrevColorReduce(IRHICommandList* cmdList, const SSGIRenderParams& params) {
    UZ_PROFILE_SCOPE("SSGI::PrevColorReduce");

    auto pso = params.pipelines->GetSsgiPrevColorReduce();
    if (!pso.IsValid()) return;

    // Transition reduced output to UAV
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_prevColorReduced;
        barrier.stateBefore = RHIResourceState::ShaderResource;
        barrier.stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    cmdList->SetPipeline(pso);

    // Root constants: 8 DWORDs matching PrevColorReduceRootCB
    struct PrevColorReduceRC {
        uint32_t prevColorSrvIndex;
        uint32_t outputMip0UavIndex;
        uint32_t outputMip1UavIndex;
        uint32_t outputMip2UavIndex;
        uint32_t outputMip3UavIndex;
        uint32_t outputMip4UavIndex;
        uint32_t fullWidth;
        uint32_t fullHeight;
    };
    static_assert(sizeof(PrevColorReduceRC) == 32, "Must be 8 DWORDs");

    PrevColorReduceRC rc{};
    rc.prevColorSrvIndex  = m_prevSceneColorSrv;
    rc.outputMip0UavIndex = m_prevColorReducedUav;
    // Mip 1-4 reserved for future multi-mip generation
    rc.outputMip1UavIndex = 0;
    rc.outputMip2UavIndex = 0;
    rc.outputMip3UavIndex = 0;
    rc.outputMip4UavIndex = 0;
    rc.fullWidth          = m_fullWidth;
    rc.fullHeight         = m_fullHeight;

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

    // PerFrameCB at b1
    cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                        params.perFrameOffset, params.perFrameCpuPtr,
                        params.perFrameSize);

    // UAV at descriptor table slot 4 (u0)
    cmdList->SetDescriptorTable(4, m_prevColorReducedUav);

    // Dispatch: 8x8 thread groups covering half-res output
    const uint32_t groupsX = (m_halfWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_halfHeight + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Transition reduced output: UAV -> SRV for trace pass
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_prevColorReduced;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 2: SSGI Trace
// ─────────────────────────────────────────────────────────────────────────────

/// Main SSGI trace — screen-space ray tracing for diffuse indirect
void SSGIRenderer::DispatchTrace(IRHICommandList* cmdList, const SSGIRenderParams& params) {
    UZ_PROFILE_SCOPE("SSGI::Trace");

    auto pso = params.pipelines->GetSsgiTrace();
    if (!pso.IsValid()) return;

    // Transition trace outputs to UAV
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiTrace;
        barriers[0].stateBefore = RHIResourceState::ShaderResource;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_ssgiTraceAO;
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(barriers, 2);
    }

    cmdList->SetPipeline(pso);

    // Root constants: 13 DWORDs matching ssgi_trace_cs.hlsl RootConstants
    struct SSGITraceRC {
        uint32_t depthSrvIndex;
        uint32_t normalSrvIndex;
        uint32_t prevColorSrvIndex;
        uint32_t outputUavIndex;
        uint32_t aoOutputUavIndex;
        uint32_t packedDimensions;  // low16 = width, high16 = height
        uint32_t frameIndex;
        uint32_t hizSrvIndex;      // 0xFFFF = disabled
        uint32_t rayCount;
        uint32_t maxSteps;
        uint32_t stepSizeBits;     // float as uint bits
        uint32_t thicknessBits;    // float as uint bits
        uint32_t maxRayDistBits;   // float as uint bits (0.5 default)
    };
    static_assert(sizeof(SSGITraceRC) == 52, "Must be 13 DWORDs");

    SSGITraceRC rc{};
    rc.depthSrvIndex     = params.depthSrvIndex;
    rc.normalSrvIndex    = params.normalSrvIndex;
    rc.prevColorSrvIndex = m_prevColorReducedSrv;
    rc.outputUavIndex    = m_ssgiTraceUav;
    rc.aoOutputUavIndex  = m_ssgiTraceAOUav;
    rc.packedDimensions  = (m_halfWidth & 0xFFFF) | ((m_halfHeight & 0xFFFF) << 16);
    rc.frameIndex        = params.frameIndex;
    rc.hizSrvIndex       = 0xFFFF; // Hi-Z disabled for now (linear march)
    rc.rayCount          = static_cast<uint32_t>(cv_SSGIRayCount->value);
    rc.maxSteps          = static_cast<uint32_t>(cv_SSGIMaxSteps->value);
    float stepSize = cv_SSGIStepSize->value;
    float thickness = cv_SSGIThickness->value;
    float maxRayDist = 0.5f; // Could be another CVar in future
    std::memcpy(&rc.stepSizeBits, &stepSize, 4);
    std::memcpy(&rc.thicknessBits, &thickness, 4);
    std::memcpy(&rc.maxRayDistBits, &maxRayDist, 4);

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 13);

    // PerFrameCB at b1
    cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                        params.perFrameOffset, params.perFrameCpuPtr,
                        params.perFrameSize);

    // UAV at descriptor table slot 4 (u0 = trace output)
    cmdList->SetDescriptorTable(4, m_ssgiTraceUav);

    const uint32_t groupsX = (m_halfWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_halfHeight + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Transition trace outputs: UAV -> SRV for denoise pass
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiTrace;
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        barriers[1].texture     = m_ssgiTraceAO;
        barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[1].stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(barriers, 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 3: Spatial Denoise
// ─────────────────────────────────────────────────────────────────────────────

/// Spatial denoise dispatch — edge-preserving bilateral filter
void SSGIRenderer::DispatchSpatialDenoise(IRHICommandList* cmdList, const SSGIRenderParams& params) {
    UZ_PROFILE_SCOPE("SSGI::SpatialDenoise");

    auto pso = params.pipelines->GetSsgiSpatialDenoise();
    if (!pso.IsValid()) return;

    // Transition denoised outputs to UAV
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiDenoised;
        barriers[0].stateBefore = RHIResourceState::ShaderResource;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_ssgiDenoisedAO;
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(barriers, 2);
    }

    cmdList->SetPipeline(pso);

    // Root constants: 11 DWORDs matching ssgi_spatial_denoise_cs.hlsl
    struct SpatialDenoiseRC {
        uint32_t inputSrvIndex;
        uint32_t aoInputSrvIndex;
        uint32_t depthSrvIndex;
        uint32_t normalSrvIndex;
        uint32_t outputUavIndex;
        uint32_t aoOutputUavIndex;
        uint32_t texWidth;
        uint32_t texHeight;
        uint32_t sigmaDepthBits;
        uint32_t sigmaNormalBits;
        uint32_t sigmaLuminanceBits;
    };
    static_assert(sizeof(SpatialDenoiseRC) == 44, "Must be 11 DWORDs");

    static auto* cv_SigmaDepth = UZ_CVAR_FLOAT("r.SSGI.SpatialSigmaDepth", 1.0f, "Spatial denoise depth sigma", 0.1f, 10.0f);
    static auto* cv_SigmaNormal = UZ_CVAR_FLOAT("r.SSGI.SpatialSigmaNormal", 128.0f, "Spatial denoise normal sigma", 8.0f, 512.0f);
    static auto* cv_SigmaLum = UZ_CVAR_FLOAT("r.SSGI.SpatialSigmaLuminance", 4.0f, "Spatial denoise luminance sigma", 0.5f, 16.0f);

    SpatialDenoiseRC rc{};
    rc.inputSrvIndex    = m_ssgiTraceSrv;
    rc.aoInputSrvIndex  = m_ssgiTraceAOSrv;
    rc.depthSrvIndex    = params.depthSrvIndex;
    rc.normalSrvIndex   = params.normalSrvIndex;
    rc.outputUavIndex   = m_ssgiDenoisedUav;
    rc.aoOutputUavIndex = m_ssgiDenoisedAOUav;
    rc.texWidth         = m_halfWidth;
    rc.texHeight        = m_halfHeight;
    float sd = cv_SigmaDepth->value, sn = cv_SigmaNormal->value, sl = cv_SigmaLum->value;
    std::memcpy(&rc.sigmaDepthBits, &sd, 4);
    std::memcpy(&rc.sigmaNormalBits, &sn, 4);
    std::memcpy(&rc.sigmaLuminanceBits, &sl, 4);

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 11);

    cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                        params.perFrameOffset, params.perFrameCpuPtr,
                        params.perFrameSize);

    cmdList->SetDescriptorTable(4, m_ssgiDenoisedUav);

    const uint32_t groupsX = (m_halfWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_halfHeight + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Transition denoised outputs: UAV -> SRV for temporal pass
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiDenoised;
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        barriers[1].texture     = m_ssgiDenoisedAO;
        barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[1].stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(barriers, 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 4: Temporal Accumulation
// ─────────────────────────────────────────────────────────────────────────────

/// Temporal accumulation dispatch — history reprojection + blend
void SSGIRenderer::DispatchTemporalAccum(IRHICommandList* cmdList, const SSGIRenderParams& params) {
    UZ_PROFILE_SCOPE("SSGI::TemporalAccum");

    auto pso = params.pipelines->GetSsgiTemporalAccum();
    if (!pso.IsValid()) return;

    // Write index = current m_historyIndex, read index = 1 - m_historyIndex
    const int writeIdx = m_historyIndex;
    const int readIdx  = 1 - m_historyIndex;

    // Transition write targets to UAV, read targets to SRV
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiHistory[writeIdx];
        barriers[0].stateBefore = RHIResourceState::ShaderResource;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_ssgiHistoryAO[writeIdx];
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(barriers, 2);
    }

    cmdList->SetPipeline(pso);

    // Root constants: 10 DWORDs matching ssgi_temporal_accum_cs.hlsl RootConstants
    struct TemporalAccumRC {
        uint32_t currentSrvIndex;
        uint32_t currentAOSrvIndex;
        uint32_t historySrvIndex;
        uint32_t historyAOSrvIndex;
        uint32_t velocitySrvIndex;
        uint32_t outputUavIndex;
        uint32_t aoOutputUavIndex;
        uint32_t packedDimensions;  // low 16 = width, high 16 = height
        uint32_t temporalAlphaBits;
        uint32_t varianceClipGammaBits;
    };
    static_assert(sizeof(TemporalAccumRC) == 40, "Must be 10 DWORDs");

    TemporalAccumRC rc{};
    rc.currentSrvIndex   = m_ssgiDenoisedSrv;
    rc.currentAOSrvIndex = m_ssgiDenoisedAOSrv;
    rc.historySrvIndex   = m_ssgiHistorySrv[readIdx];
    rc.historyAOSrvIndex = m_ssgiHistoryAOSrv[readIdx];
    rc.velocitySrvIndex  = params.velocitySrvIndex;
    rc.outputUavIndex    = m_ssgiHistoryUav[writeIdx];
    rc.aoOutputUavIndex  = m_ssgiHistoryAOUav[writeIdx];
    rc.packedDimensions  = (m_halfWidth & 0xFFFF) | ((m_halfHeight & 0xFFFF) << 16);
    float temporalAlpha = cv_SSGITemporalAlpha->value;
    float varClipGamma = 1.0f; // Could be CVar in Phase 2
    std::memcpy(&rc.temporalAlphaBits, &temporalAlpha, 4);
    std::memcpy(&rc.varianceClipGammaBits, &varClipGamma, 4);

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 10);

    cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                        params.perFrameOffset, params.perFrameCpuPtr,
                        params.perFrameSize);

    cmdList->SetDescriptorTable(4, m_ssgiHistoryUav[writeIdx]);

    const uint32_t groupsX = (m_halfWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_halfHeight + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Transition write targets: UAV -> SRV for upsample pass
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiHistory[writeIdx];
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        barriers[1].texture     = m_ssgiHistoryAO[writeIdx];
        barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[1].stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(barriers, 2);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Pass 5: Bilateral Upsample
// ─────────────────────────────────────────────────────────────────────────────

/// DIM_UPSCALE_DIFFUSE_INDIRECT — bilateral upsample from half-res to full-res
void SSGIRenderer::DispatchUpsample(IRHICommandList* cmdList, const SSGIRenderParams& params) {
    UZ_PROFILE_SCOPE("SSGI::BilateralUpsample");

    auto pso = params.pipelines->GetSsgiBilateralUpsample();
    if (!pso.IsValid()) return;

    const int readIdx = m_historyIndex; // Just written by temporal pass

    // Transition full-res outputs to UAV
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiResult;
        barriers[0].stateBefore = RHIResourceState::ShaderResource;
        barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
        barriers[1].texture     = m_ssgiResultAO;
        barriers[1].stateBefore = RHIResourceState::ShaderResource;
        barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
        cmdList->ResourceBarrier(barriers, 2);
    }

    cmdList->SetPipeline(pso);

    // Root constants: 10 DWORDs matching ssgi_upsample_cs.hlsl
    struct UpsampleRC {
        uint32_t ssgiHalfSrvIndex;
        uint32_t ssgiAOHalfSrvIndex;
        uint32_t depthFullSrvIndex;
        uint32_t normalFullSrvIndex;
        uint32_t outputUavIndex;
        uint32_t aoOutputUavIndex;
        uint32_t fullWidth;
        uint32_t fullHeight;
        uint32_t depthSigmaBits;
        uint32_t normalPowerBits;
    };
    static_assert(sizeof(UpsampleRC) == 40, "Must be 10 DWORDs");

    static auto* cv_UpsDepthSigma = UZ_CVAR_FLOAT("r.SSGI.UpsampleDepthSigma", 0.5f, "Upsample depth weight sigma", 0.1f, 2.0f);
    static auto* cv_UpsNormalPow  = UZ_CVAR_FLOAT("r.SSGI.UpsampleNormalSigma", 32.0f, "Upsample normal cosine power", 4.0f, 128.0f);

    UpsampleRC rc{};
    rc.ssgiHalfSrvIndex   = m_ssgiHistorySrv[readIdx];
    rc.ssgiAOHalfSrvIndex = m_ssgiHistoryAOSrv[readIdx];
    rc.depthFullSrvIndex  = params.depthSrvIndex;
    rc.normalFullSrvIndex = params.normalSrvIndex;
    rc.outputUavIndex     = m_ssgiResultUav;
    rc.aoOutputUavIndex   = m_ssgiResultAOUav;
    rc.fullWidth          = m_fullWidth;
    rc.fullHeight         = m_fullHeight;
    float ds = cv_UpsDepthSigma->value, np = cv_UpsNormalPow->value;
    std::memcpy(&rc.depthSigmaBits, &ds, 4);
    std::memcpy(&rc.normalPowerBits, &np, 4);

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 10);

    cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer,
                        params.perFrameOffset, params.perFrameCpuPtr,
                        params.perFrameSize);

    cmdList->SetDescriptorTable(4, m_ssgiResultUav);

    const uint32_t groupsX = (m_fullWidth  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_fullHeight + kThreadGroupSize - 1) / kThreadGroupSize;
    cmdList->Dispatch(groupsX, groupsY, 1);

    // Transition full-res outputs: UAV -> SRV for deferred lighting
    {
        RHIBarrierDesc barriers[2] = {};
        barriers[0].texture     = m_ssgiResult;
        barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[0].stateAfter  = RHIResourceState::ShaderResource;
        barriers[1].texture     = m_ssgiResultAO;
        barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
        barriers[1].stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(barriers, 2);
    }
}

} // namespace uz
