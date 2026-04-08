/// UzEngine GTAO Pass — Implementation.
///
/// Creates R8_UNORM AO textures, compiles GTAO + denoise compute shaders,
/// and dispatches GPU work each frame. The filtered AO result is consumed
/// by the deferred lighting pass to modulate ambient occlusion.
///
/// BUILD MARKER: rebuild trigger for shader gen.h dependency tracking.

#include "gtao_pass.h"

#include "engine/rhi/rhi.h"
#include "shader_manager.h"
#include "cb_ring_allocator.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"
#include "cvar.h"

#include <algorithm>


UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_GTAOEnabled        = UZ_CVAR_BOOL("r.GTAO", false, "Ground truth ambient occlusion");
static auto* cv_GTAORadius         = UZ_CVAR_FLOAT("r.GTAO.Radius", 200.f, "AO search radius (cm)", 1.f, 10000.f);
static auto* cv_GTAOIntensity      = UZ_CVAR_FLOAT("r.GTAO.Intensity", 1.f, "AO strength multiplier", 0.f, 5.f);
static auto* cv_GTAOPower          = UZ_CVAR_FLOAT("r.GTAO.Power", 1.f, "AO power curve exponent", 0.1f, 10.f);
static auto* cv_GTAOThicknessBlend = UZ_CVAR_FLOAT("r.GTAO.ThicknessBlend", 0.5f, "Thickness heuristic", 0.f, 1.f);
static auto* cv_GTAONumAngles      = UZ_CVAR_INT("r.GTAO.NumAngles", 2, "GTAO direction search count", 1, 16);
static auto* cv_GTAONumTaps        = UZ_CVAR_INT("r.GTAO.NumTaps", 8, "GTAO steps per direction", 4, 16);
static auto* cv_GTAOMaxScreenRadius = UZ_CVAR_FLOAT("r.GTAO.MaxScreenRadius", 256.f, "GTAO max screen-space radius (px)", 64.f, 512.f);
static auto* cv_GTAOFadeStart      = UZ_CVAR_FLOAT("r.GTAO.FadeStart", 50000.f, "GTAO distance fade start (cm)", 100.f, 1000000.f);
static auto* cv_GTAOFadeRange      = UZ_CVAR_FLOAT("r.GTAO.FadeRange", 50000.f, "GTAO distance fade range (cm)", 100.f, 1000000.f);
static auto* cv_GTAOQuality        = UZ_CVAR_INT("r.GTAO.Quality", 2, "GTAO quality (0=Low, 1=Medium, 2=High, 3=Ultra)", 0, 3);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Quality Preset Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the number of direction search angles for a quality preset.
/// Low=1, Medium=2, High=2, Ultra=3.
/// @ref Jimenez 2016 "GTAO" — NumAngles clamped [1,16]
static uint32_t GetAnglesForQuality(GTAOQuality q) noexcept {
    switch (q) {
        case GTAOQuality::Low:    return 1;
        case GTAOQuality::Medium: return 2;
        case GTAOQuality::High:   return 2;
        case GTAOQuality::Ultra:  return 3;
    }
    return 2; // default: High
}

/// Returns the number of ray march steps per direction for a quality preset.
/// Low=4, Medium=6, High=8, Ultra=12.
/// @ref Jimenez 2016 "GTAO" — GTAO_NUMTAPS per SHADER_QUALITY level
static uint32_t GetTapsForQuality(GTAOQuality q) noexcept {
    switch (q) {
        case GTAOQuality::Low:    return 4;
        case GTAOQuality::Medium: return 6;
        case GTAOQuality::High:   return 8;
        case GTAOQuality::Ultra:  return 12;
    }
    return 8; // default: High
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

GTAOPass::~GTAOPass() {
    if (m_initialized) {
        Shutdown();
    }
}

bool GTAOPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                    uint32_t width, uint32_t height) {
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "GTAOPass::Init called while already initialized");
        return true;
    }

    if (!device || !shaderMgr) {
        UZ_LOG(Renderer, Error, "GTAOPass::Init: null device or shaderMgr");
        return false;
    }

    m_device    = device;
    m_shaderMgr = shaderMgr;

    if (!CreateTextures(width, height)) {
        return false;
    }

    m_initialized = true;
    UZ_LOG(Renderer, Info, "GTAOPass initialized ({}x{}, rawSRV={}, filteredSRV={})",
           width, height, m_rawAOSrvIndex, m_filteredAOSrvIndex);
    return true;
}

bool GTAOPass::CreateTextures(uint32_t width, uint32_t height) {
    m_width  = width;
    m_height = height;

    // ── Raw AO texture (R8_UNORM, UAV+SRV) ──
    {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "GTAO_RawAO";

        m_rawAOTexture = m_device->CreateTexture(desc);
        if (!m_rawAOTexture.IsValid()) {
            UZ_LOG(Renderer, Error, "GTAOPass: failed to create raw AO texture");
            return false;
        }

        m_rawAOSrvIndex = m_device->GetTextureSRVIndex(m_rawAOTexture);
        m_rawAOUavIndex = m_device->GetTextureUAVIndex(m_rawAOTexture);

        UZ_LOG(Renderer, Debug, "GTAOPass: raw AO texture created — SRV={}, UAV={}",
               m_rawAOSrvIndex, m_rawAOUavIndex);

        if (m_rawAOSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: raw AO texture has no SRV descriptor");
            DestroyTextures();
            return false;
        }

        if (m_rawAOUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: raw AO texture has no UAV descriptor");
            DestroyTextures();
            return false;
        }
    }

    // ── Filtered AO texture (R8_UNORM, UAV+SRV+RenderTarget) ──
    // RenderTarget usage is needed because the bilateral upsample pixel shader
    // renders into this texture when quality is half-res (Low/Medium).
    {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess
                       | RHITextureUsage::RenderTarget;
        desc.debugName = "GTAO_FilteredAO";

        m_filteredAOTexture = m_device->CreateTexture(desc);
        if (!m_filteredAOTexture.IsValid()) {
            UZ_LOG(Renderer, Error, "GTAOPass: failed to create filtered AO texture");
            DestroyTextures();
            return false;
        }

        m_filteredAOSrvIndex = m_device->GetTextureSRVIndex(m_filteredAOTexture);
        m_filteredAOUavIndex = m_device->GetTextureUAVIndex(m_filteredAOTexture);

        UZ_LOG(Renderer, Debug, "GTAOPass: filtered AO texture created — SRV={}, UAV={}",
               m_filteredAOSrvIndex, m_filteredAOUavIndex);

        if (m_filteredAOSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: filtered AO texture has no SRV descriptor");
            DestroyTextures();
            return false;
        }

        if (m_filteredAOUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: filtered AO texture has no UAV descriptor");
            DestroyTextures();
            return false;
        }
    }

    // ── Temporal history textures (ping-pong, R8_UNORM, full-res) ──
    // Always full-res because the temporal pass runs after upsample (if half-res)
    // or after denoise (if full-res). Two textures for ping-pong double-buffering.
    for (int i = 0; i < 2; ++i) {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = (i == 0) ? "GTAO_History0" : "GTAO_History1";

        m_aoHistory[i] = m_device->CreateTexture(desc);
        if (!m_aoHistory[i].IsValid()) {
            UZ_LOG(Renderer, Error, "GTAOPass: failed to create history texture {}", i);
            DestroyTextures();
            return false;
        }

        m_aoHistorySrvIndex[i] = m_device->GetTextureSRVIndex(m_aoHistory[i]);
        m_aoHistoryUavIndex[i] = m_device->GetTextureUAVIndex(m_aoHistory[i]);

        if (m_aoHistorySrvIndex[i] == UINT32_MAX || m_aoHistoryUavIndex[i] == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "GTAOPass: history texture {} has invalid descriptors (SRV={}, UAV={})",
                   i, m_aoHistorySrvIndex[i], m_aoHistoryUavIndex[i]);
            DestroyTextures();
            return false;
        }

        UZ_LOG(Renderer, Debug,
               "GTAOPass: history texture {} created ({}x{}) — SRV={}, UAV={}",
               i, width, height, m_aoHistorySrvIndex[i], m_aoHistoryUavIndex[i]);
    }
    m_historyIndex = 0;
    m_historyValid = false;

    // ── Half-resolution textures for Low/Medium quality presets ──
    if (!CreateHalfResTextures(width / 2, height / 2)) {
        DestroyTextures();
        return false;
    }

    return true;
}

bool GTAOPass::CreateHalfResTextures(uint32_t halfWidth, uint32_t halfHeight) {
    // Ensure minimum 1x1 dimensions
    halfWidth  = (halfWidth  > 0) ? halfWidth  : 1;
    halfHeight = (halfHeight > 0) ? halfHeight : 1;

    m_halfWidth  = halfWidth;
    m_halfHeight = halfHeight;

    // ── Raw AO half-res (R8_UNORM, UAV+SRV) ──
    {
        RHITextureDesc desc{};
        desc.width     = halfWidth;
        desc.height    = halfHeight;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "GTAO_RawAO_Half";

        m_rawAOHalf = m_device->CreateTexture(desc);
        if (!m_rawAOHalf.IsValid()) {
            UZ_LOG(Renderer, Error, "GTAOPass: failed to create half-res raw AO texture");
            return false;
        }

        m_rawAOHalfSrvIndex = m_device->GetTextureSRVIndex(m_rawAOHalf);
        m_rawAOHalfUavIndex = m_device->GetTextureUAVIndex(m_rawAOHalf);

        UZ_LOG(Renderer, Debug, "GTAOPass: half-res raw AO texture created ({}x{}) — SRV={}, UAV={}",
               halfWidth, halfHeight, m_rawAOHalfSrvIndex, m_rawAOHalfUavIndex);

        if (m_rawAOHalfSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: half-res raw AO texture has no SRV descriptor");
            DestroyHalfResTextures();
            return false;
        }

        if (m_rawAOHalfUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: half-res raw AO texture has no UAV descriptor");
            DestroyHalfResTextures();
            return false;
        }
    }

    // ── Filtered AO half-res (R8_UNORM, UAV+SRV) ──
    {
        RHITextureDesc desc{};
        desc.width     = halfWidth;
        desc.height    = halfHeight;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
        desc.debugName = "GTAO_FilteredAO_Half";

        m_filteredAOHalf = m_device->CreateTexture(desc);
        if (!m_filteredAOHalf.IsValid()) {
            UZ_LOG(Renderer, Error, "GTAOPass: failed to create half-res filtered AO texture");
            DestroyHalfResTextures();
            return false;
        }

        m_filteredAOHalfSrvIndex = m_device->GetTextureSRVIndex(m_filteredAOHalf);
        m_filteredAOHalfUavIndex = m_device->GetTextureUAVIndex(m_filteredAOHalf);

        UZ_LOG(Renderer, Debug, "GTAOPass: half-res filtered AO texture created ({}x{}) — SRV={}, UAV={}",
               halfWidth, halfHeight, m_filteredAOHalfSrvIndex, m_filteredAOHalfUavIndex);

        if (m_filteredAOHalfSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: half-res filtered AO texture has no SRV descriptor");
            DestroyHalfResTextures();
            return false;
        }

        if (m_filteredAOHalfUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GTAOPass: half-res filtered AO texture has no UAV descriptor");
            DestroyHalfResTextures();
            return false;
        }
    }

    return true;
}

void GTAOPass::DestroyHalfResTextures() {
    if (m_rawAOHalf.IsValid()) {
        m_device->DestroyTexture(m_rawAOHalf);
        m_rawAOHalf = RHITextureHandle::Invalid();
    }
    if (m_filteredAOHalf.IsValid()) {
        m_device->DestroyTexture(m_filteredAOHalf);
        m_filteredAOHalf = RHITextureHandle::Invalid();
    }
    m_rawAOHalfSrvIndex      = UINT32_MAX;
    m_rawAOHalfUavIndex      = UINT32_MAX;
    m_filteredAOHalfSrvIndex = UINT32_MAX;
    m_filteredAOHalfUavIndex = UINT32_MAX;
    m_halfWidth  = 0;
    m_halfHeight = 0;
}

void GTAOPass::DestroyTextures() {
    DestroyHalfResTextures();

    if (m_rawAOTexture.IsValid()) {
        m_device->DestroyTexture(m_rawAOTexture);
        m_rawAOTexture = RHITextureHandle::Invalid();
    }
    if (m_filteredAOTexture.IsValid()) {
        m_device->DestroyTexture(m_filteredAOTexture);
        m_filteredAOTexture = RHITextureHandle::Invalid();
    }

    // ── Destroy temporal history textures ──
    for (int i = 0; i < 2; ++i) {
        if (m_aoHistory[i].IsValid()) {
            m_device->DestroyTexture(m_aoHistory[i]);
            m_aoHistory[i] = RHITextureHandle::Invalid();
        }
        m_aoHistorySrvIndex[i] = UINT32_MAX;
        m_aoHistoryUavIndex[i] = UINT32_MAX;
        m_aoHistoryState[i]    = RHIResourceState::Common;
    }
    m_historyIndex = 0;
    m_historyValid = false;

    m_rawAOSrvIndex      = UINT32_MAX;
    m_rawAOUavIndex      = UINT32_MAX;
    m_filteredAOSrvIndex = UINT32_MAX;
    m_filteredAOUavIndex = UINT32_MAX;
    m_width  = 0;
    m_height = 0;
    m_initialBarriersDone = false;
    m_filteredAOCurrentState = RHIResourceState::Common;
}

bool GTAOPass::Resize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) {
        return true;
    }
    DestroyTextures();
    return CreateTextures(width, height);
}

void GTAOPass::Shutdown() {
    if (!m_initialized) {
        return;
    }

    DestroyTextures();

    if (m_gtaoPipeline.IsValid()) {
        m_device->DestroyPipeline(m_gtaoPipeline);
        m_gtaoPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_denoisePipeline.IsValid()) {
        m_device->DestroyPipeline(m_denoisePipeline);
        m_denoisePipeline = RHIPipelineHandle::Invalid();
    }
    if (m_upsamplePipeline.IsValid()) {
        m_device->DestroyPipeline(m_upsamplePipeline);
        m_upsamplePipeline = RHIPipelineHandle::Invalid();
    }
    if (m_temporalPipeline.IsValid()) {
        m_device->DestroyPipeline(m_temporalPipeline);
        m_temporalPipeline = RHIPipelineHandle::Invalid();
    }
    if (m_gtaoCS.IsValid()) {
        m_device->DestroyShader(m_gtaoCS);
        m_gtaoCS = RHIShaderHandle::Invalid();
    }
    if (m_denoiseCS.IsValid()) {
        m_device->DestroyShader(m_denoiseCS);
        m_denoiseCS = RHIShaderHandle::Invalid();
    }
    if (m_upsamplePS.IsValid()) {
        m_device->DestroyShader(m_upsamplePS);
        m_upsamplePS = RHIShaderHandle::Invalid();
    }
    if (m_temporalCS.IsValid()) {
        m_device->DestroyShader(m_temporalCS);
        m_temporalCS = RHIShaderHandle::Invalid();
    }

    m_device    = nullptr;
    m_shaderMgr = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "GTAOPass shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Pipeline Creation
// ─────────────────────────────────────────────────────────────────────────────

bool GTAOPass::EnsureGTAOPipeline() {
    if (m_gtaoPipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GtaoCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to compile gtao_cs.hlsl — bytecode is empty");
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: GTAO shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "gtao_cs";

    m_gtaoCS = m_device->CreateShader(shaderDesc);
    if (!m_gtaoCS.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create GTAO shader (bytecode={} bytes)",
               bytecode.size());
        return false;
    }

    UZ_LOG(Renderer, Debug, "GTAOPass: GTAO shader created (handle={})", m_gtaoCS.index);

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_gtaoCS;
    pipeDesc.debugName     = "GTAO_Compute";

    m_gtaoPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_gtaoPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create GTAO pipeline (shader={})",
               m_gtaoCS.index);
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: GTAO pipeline created (handle={})", m_gtaoPipeline.index);
    return true;
}

bool GTAOPass::EnsureDenoisePipeline() {
    if (m_denoisePipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GtaoDenoiseCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to compile gtao_denoise_cs.hlsl — bytecode is empty");
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: denoise shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "gtao_denoise_cs";

    m_denoiseCS = m_device->CreateShader(shaderDesc);
    if (!m_denoiseCS.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create denoise shader (bytecode={} bytes)",
               bytecode.size());
        return false;
    }

    UZ_LOG(Renderer, Debug, "GTAOPass: denoise shader created (handle={})", m_denoiseCS.index);

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_denoiseCS;
    pipeDesc.debugName     = "GTAO_Denoise";

    m_denoisePipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_denoisePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create denoise pipeline (shader={})",
               m_denoiseCS.index);
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: denoise pipeline created (handle={})", m_denoisePipeline.index);
    return true;
}

bool GTAOPass::EnsureUpsamplePipeline() {
    if (m_upsamplePipeline.IsValid()) {
        return true;
    }

    // ── Compile fullscreen vertex shader ──
    auto vsBytecode = m_shaderMgr->GetBytecode(EngineShader::FullscreenVS);
    if (vsBytecode.empty()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to get FullscreenVS bytecode — upsample pipeline aborted");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode     = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage        = RHIShaderStage::Vertex;
    vsDesc.entryPoint   = "main";
    vsDesc.debugName    = "gtao_fullscreen_vs";

    RHIShaderHandle fullscreenVS = m_device->CreateShader(vsDesc);
    if (!fullscreenVS.IsValid()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to create fullscreen VS (bytecode={} bytes)",
               vsBytecode.size());
        return false;
    }

    // ── Compile bilateral upsample pixel shader ──
    auto psBytecode = m_shaderMgr->GetBytecode(EngineShader::GtaoUpsamplePS);
    if (psBytecode.empty()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to get GtaoUpsamplePS bytecode — upsample pipeline aborted");
        m_device->DestroyShader(fullscreenVS);
        return false;
    }

    RHIShaderDesc psDesc{};
    psDesc.bytecode     = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage        = RHIShaderStage::Pixel;
    psDesc.entryPoint   = "main";
    psDesc.debugName    = "gtao_upsample_ps";

    m_upsamplePS = m_device->CreateShader(psDesc);
    if (!m_upsamplePS.IsValid()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to create upsample PS (bytecode={} bytes)",
               psBytecode.size());
        m_device->DestroyShader(fullscreenVS);
        return false;
    }

    // ── Create graphics pipeline (fullscreen triangle, no depth, no blend) ──
    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader          = fullscreenVS;
    pipeDesc.pixelShader           = m_upsamplePS;
    pipeDesc.renderTargetCount     = 1;
    pipeDesc.renderTargetFormats[0] = RHIFormat::R8Unorm;
    pipeDesc.depthStencilFormat    = RHIFormat::Unknown;
    pipeDesc.depthStencil.depthEnable = false;
    pipeDesc.depthStencil.depthWrite  = false;
    pipeDesc.debugName             = "GTAO_Upsample";

    m_upsamplePipeline = m_device->CreateGraphicsPipeline(pipeDesc);

    // DX12/Vulkan PSOs capture shader bytecode internally — the VS handle is
    // no longer needed after pipeline creation. Destroy it to avoid leaking.
    m_device->DestroyShader(fullscreenVS);

    if (!m_upsamplePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create upsample pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: upsample pipeline created (handle={})",
           m_upsamplePipeline.index);
    return true;
}

bool GTAOPass::EnsureTemporalPipeline() {
    if (m_temporalPipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GtaoTemporalCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to compile gtao_temporal_cs.hlsl — bytecode is empty");
        return false;
    }

    UZ_LOG(Renderer, Info,
           "GTAOPass: temporal shader bytecode size = {} bytes", bytecode.size());

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "gtao_temporal_cs";

    m_temporalCS = m_device->CreateShader(shaderDesc);
    if (!m_temporalCS.IsValid()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to create temporal shader (bytecode={} bytes)",
               bytecode.size());
        return false;
    }

    UZ_LOG(Renderer, Debug,
           "GTAOPass: temporal shader created (handle={})", m_temporalCS.index);

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_temporalCS;
    pipeDesc.debugName     = "GTAO_Temporal";

    m_temporalPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_temporalPipeline.IsValid()) {
        UZ_LOG(Renderer, Error,
               "GTAOPass: failed to create temporal pipeline (shader={})",
               m_temporalCS.index);
        return false;
    }

    UZ_LOG(Renderer, Info,
           "GTAOPass: temporal pipeline created (handle={})", m_temporalPipeline.index);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

uint32_t GTAOPass::Execute(IRHICommandList* cmdList, CBRingAllocator* cbRing,
                           uint32_t depthSrvIndex, uint32_t normalSrvIndex,
                           RHITextureHandle depthTexture, RHITextureHandle normalTexture,
                           const float* viewMatrix,
                           float invTanHalfFov, float aspectRatio,
                           float nearPlane, float farPlane,
                           uint32_t width, uint32_t height,
                           uint32_t frameIndex,
                           uint32_t velocitySrvIndex,
                           uint32_t hizSrvIndex)
{
    UZ_PROFILE_SCOPE("GTAOPass::Execute");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "GTAOPass::Execute called before Init()");
        return UINT32_MAX;
    }

    // Apply CVar overrides to settings
    settings.enabled        = cv_GTAOEnabled->value >= 0.5f;
    settings.worldRadius    = cv_GTAORadius->value;
    settings.intensity      = cv_GTAOIntensity->value;
    settings.aoPower        = cv_GTAOPower->value;
    settings.thicknessBlend = cv_GTAOThicknessBlend->value;
    settings.fadeStartDist  = cv_GTAOFadeStart->value;
    settings.fadeEndDist    = cv_GTAOFadeRange->value + cv_GTAOFadeStart->value;

    // Quality preset CVar: 0=Low, 1=Medium, 2=High, 3=Ultra
    const int qualityInt = static_cast<int>(cv_GTAOQuality->value);
    settings.quality = static_cast<GTAOQuality>(
        std::clamp(qualityInt, 0, static_cast<int>(GTAOQuality::Ultra)));

    if (!settings.enabled) {
        return UINT32_MAX;
    }

    // ── Validate inputs before touching the GPU ──
    if (width == 0 || height == 0) {
        UZ_LOG(Renderer, Error,
               "GTAOPass::Execute: zero-size dispatch (width={}, height={}) — aborting",
               width, height);
        return UINT32_MAX;
    }

    if (!cmdList) {
        UZ_LOG(Renderer, Error, "GTAOPass::Execute: null command list");
        return UINT32_MAX;
    }

    if (depthSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "GTAOPass::Execute: depthSrvIndex is UINT32_MAX — depth SRV not allocated");
        return UINT32_MAX;
    }

    if (normalSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "GTAOPass::Execute: normalSrvIndex is UINT32_MAX — normal SRV not allocated");
        return UINT32_MAX;
    }

    if (!depthTexture.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass::Execute: depthTexture handle is invalid");
        return UINT32_MAX;
    }

    if (!normalTexture.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass::Execute: normalTexture handle is invalid");
        return UINT32_MAX;
    }

    if (m_rawAOUavIndex == UINT32_MAX || m_filteredAOUavIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "GTAOPass::Execute: internal UAV indices invalid (rawUAV={}, filteredUAV={}) "
               "— CreateTextures() may have failed silently",
               m_rawAOUavIndex, m_filteredAOUavIndex);
        return UINT32_MAX;
    }

    if (m_rawAOSrvIndex == UINT32_MAX || m_filteredAOSrvIndex == UINT32_MAX) {
        UZ_LOG(Renderer, Error,
               "GTAOPass::Execute: internal SRV indices invalid (rawSRV={}, filteredSRV={}) "
               "— CreateTextures() may have failed silently",
               m_rawAOSrvIndex, m_filteredAOSrvIndex);
        return UINT32_MAX;
    }

    // Resize if needed
    if (width != m_width || height != m_height) {
        if (!Resize(width, height)) {
            return UINT32_MAX;
        }
    }

    if (!EnsureGTAOPipeline() || !EnsureDenoisePipeline()) {
        return UINT32_MAX;
    }

    const bool halfRes = GTAOQualityIsHalfRes(settings.quality);

    // Half-res path requires valid half-res textures
    if (halfRes) {
        if (m_rawAOHalfUavIndex == UINT32_MAX || m_filteredAOHalfUavIndex == UINT32_MAX ||
            m_rawAOHalfSrvIndex == UINT32_MAX || m_filteredAOHalfSrvIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error,
                   "GTAOPass::Execute: half-res descriptor indices invalid — "
                   "rawHalfUAV={}, filteredHalfUAV={}, rawHalfSRV={}, filteredHalfSRV={}",
                   m_rawAOHalfUavIndex, m_filteredAOHalfUavIndex,
                   m_rawAOHalfSrvIndex, m_filteredAOHalfSrvIndex);
            return UINT32_MAX;
        }
    }

    // ── Resolve dispatch dimensions based on quality ──
    const uint32_t dispatchW = halfRes ? m_halfWidth  : width;
    const uint32_t dispatchH = halfRes ? m_halfHeight : height;

    // ── Select target textures/indices based on quality ──
    const RHITextureHandle rawAOTarget      = halfRes ? m_rawAOHalf      : m_rawAOTexture;
    const uint32_t         rawAOUav         = halfRes ? m_rawAOHalfUavIndex : m_rawAOUavIndex;
    const uint32_t         rawAOSrv         = halfRes ? m_rawAOHalfSrvIndex : m_rawAOSrvIndex;
    const RHITextureHandle filteredAOTarget = halfRes ? m_filteredAOHalf : m_filteredAOTexture;
    const uint32_t         filteredAOUav    = halfRes ? m_filteredAOHalfUavIndex : m_filteredAOUavIndex;
    const uint32_t         filteredAOSrv    = halfRes ? m_filteredAOHalfSrvIndex : m_filteredAOSrvIndex;

    // ── Initial barriers: Common → UAV for first use after texture creation ──
    // DX12 textures are created in Common state. Unlike buffers, textures do NOT
    // support implicit Common→UAV promotion. Without this barrier, the first
    // compute dispatch writes to a texture in the wrong state → GPU hang/TDR.
    if (!m_initialBarriersDone) {
        // Full-res textures always need the initial barrier (used directly or as
        // upsample render target). History textures also need Common→UAV.
        RHIBarrierDesc initBarriers[6] = {};
        uint32_t barrierCount = 0;

        initBarriers[barrierCount].texture     = m_rawAOTexture;
        initBarriers[barrierCount].stateBefore = RHIResourceState::Common;
        initBarriers[barrierCount].stateAfter  = RHIResourceState::UnorderedAccess;
        barrierCount++;

        initBarriers[barrierCount].texture     = m_filteredAOTexture;
        initBarriers[barrierCount].stateBefore = RHIResourceState::Common;
        initBarriers[barrierCount].stateAfter  = RHIResourceState::UnorderedAccess;
        barrierCount++;

        // Half-res textures also need initial barriers if they exist
        if (m_rawAOHalf.IsValid()) {
            initBarriers[barrierCount].texture     = m_rawAOHalf;
            initBarriers[barrierCount].stateBefore = RHIResourceState::Common;
            initBarriers[barrierCount].stateAfter  = RHIResourceState::UnorderedAccess;
            barrierCount++;
        }
        if (m_filteredAOHalf.IsValid()) {
            initBarriers[barrierCount].texture     = m_filteredAOHalf;
            initBarriers[barrierCount].stateBefore = RHIResourceState::Common;
            initBarriers[barrierCount].stateAfter  = RHIResourceState::UnorderedAccess;
            barrierCount++;
        }

        // History textures: both start in Common, transition to UAV.
        // On first frame we will copy into history[cur] and leave history[prev] in UAV
        // (it will be transitioned to SRV when actually used as history input).
        for (int i = 0; i < 2; ++i) {
            if (m_aoHistory[i].IsValid()) {
                initBarriers[barrierCount].texture     = m_aoHistory[i];
                initBarriers[barrierCount].stateBefore = RHIResourceState::Common;
                initBarriers[barrierCount].stateAfter  = RHIResourceState::UnorderedAccess;
                barrierCount++;
                m_aoHistoryState[i] = RHIResourceState::UnorderedAccess;
            }
        }

        cmdList->ResourceBarrier(initBarriers, barrierCount);

        m_initialBarriersDone = true;
        m_filteredAOCurrentState = RHIResourceState::UnorderedAccess;
        UZ_LOG(Renderer, Info,
               "GTAOPass: initial barriers — Common -> UAV for {} textures (halfRes={})",
               barrierCount, halfRes);
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 1: GTAO Compute Pass
    // ══════════════════════════════════════════════════════════════════════

    {
        cmdList->SetPipeline(m_gtaoPipeline);

        // ── Root constants at b0 (slot 0, 16 DWORDs) ──
        // DWORDs 0-7: dynamic per-frame values (SRV indices, camera, frame).
        // DWORDs 8-15: algorithm parameters from GTAOSettings / CVars.
        // When half-res, the shader operates at half dimensions.
        GTAORootConstants rc{};
        rc.depthSrvIndex  = depthSrvIndex;
        rc.normalSrvIndex = normalSrvIndex;
        rc.outputWidth    = dispatchW;
        rc.outputHeight   = dispatchH;
        rc.invTanHalfFov  = invTanHalfFov;
        rc.nearPlane      = nearPlane;
        rc.farPlane       = farPlane;
        // Pack frameIndex (lower 16 bits) and hizSrvIndex (upper 16 bits) into DWORD 7.
        // hizSrvIndex == UINT32_MAX maps to 0xFFFF which the shader interprets as "disabled".
        const uint32_t packedHiz = (hizSrvIndex == UINT32_MAX) ? 0xFFFFu
                                 : (hizSrvIndex & 0xFFFFu);
        rc.frameAndHiz = (frameIndex & 0xFFFFu) | (packedHiz << 16u);

        // DWORDs 8-15: algorithm parameters driven by quality preset + CVars.
        // CVar overrides take precedence over quality preset defaults when
        // the user has explicitly set r.GTAO.NumAngles or r.GTAO.NumTaps.
        rc.numAngles      = static_cast<uint32_t>(cv_GTAONumAngles->value);
        rc.numTaps        = static_cast<uint32_t>(cv_GTAONumTaps->value);
        rc.worldRadius    = settings.worldRadius;
        rc.aoIntensity    = settings.intensity;
        // @ref Jimenez et al. 2016 "Practical Real-Time Strategies for GTAO"
        // Transform: ThicknessBlend = clamp(1 - t*t, 0, 0.99)
        { float t = settings.thicknessBlend; rc.thicknessBlend = std::clamp(1.0f - t * t, 0.0f, 0.99f); }
        rc.fadeStart      = settings.fadeStartDist;
        rc.fadeEnd        = settings.fadeEndDist;
        rc.aoPower        = settings.aoPower;

        // Dump all root constants on first frame to diagnose bad shader inputs
        static bool loggedGtaoConstants = false;
        if (!loggedGtaoConstants) {
            UZ_LOG(Renderer, Info,
                   "GTAOPass: GTAO root constants (first frame) — "
                   "depthSRV={}, normalSRV={}, size={}x{}, "
                   "invTanHalfFov={:.4f}, near={:.2f}, far={:.1f}, frame={}, hizSrv={}, halfRes={}, "
                   "angles={}, taps={}, radius={:.1f}, intensity={:.2f}, power={:.2f}",
                   rc.depthSrvIndex, rc.normalSrvIndex,
                   rc.outputWidth, rc.outputHeight,
                   rc.invTanHalfFov, rc.nearPlane, rc.farPlane,
                   frameIndex, (hizSrvIndex == UINT32_MAX ? -1 : static_cast<int>(hizSrvIndex)),
                   halfRes, rc.numAngles, rc.numTaps, rc.worldRadius, rc.aoIntensity, rc.aoPower);
            loggedGtaoConstants = true;
        }

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 16);

        // ── Bind UAV (slot 4) — raw AO output at u0 ──
        cmdList->SetDescriptorTable(4, rawAOUav);

        // ── Bind SRV table (slot 5, SM5 only) — depth at t0, normal at t1 ──
        // In bindless (SM6.6+), root signature has only 5 params (0-4).
        // Slot 5 only exists in SM5 mode. Bindless reads via ResourceDescriptorHeap[].
        if (!m_device->IsBindless()) {
            cmdList->SetDescriptorTable(5, depthSrvIndex);
        }

        // ── Dispatch ──
        const uint32_t groupsX = (dispatchW + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t groupsY = (dispatchH + kThreadGroupSize - 1) / kThreadGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── UAV barrier: raw AO write → read ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = rawAOTarget;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 2: Spatial Denoise Pass
    // ══════════════════════════════════════════════════════════════════════

    {
        cmdList->SetPipeline(m_denoisePipeline);

        // ── Root constants at b0 (slot 0, 8 DWORDs) ──
        GTAODenoiseRootConstants drc{};
        drc.texelSizeX     = 1.0f / static_cast<float>(dispatchW);
        drc.texelSizeY     = 1.0f / static_cast<float>(dispatchH);
        drc.nearPlane      = nearPlane;
        drc.farPlane       = farPlane;
        drc.rawAOSrvIndex  = rawAOSrv;
        drc.depthSrvIndex  = depthSrvIndex;

        // Dump denoise constants on first frame
        static bool loggedDenoiseConstants = false;
        if (!loggedDenoiseConstants) {
            UZ_LOG(Renderer, Info,
                   "GTAOPass: denoise root constants (first frame) — "
                   "texelSize=({:.6f}, {:.6f}), near={:.2f}, far={:.1f}, "
                   "rawAOSRV={}, depthSRV={}, halfRes={}",
                   drc.texelSizeX, drc.texelSizeY,
                   drc.nearPlane, drc.farPlane,
                   drc.rawAOSrvIndex, drc.depthSrvIndex, halfRes);
            loggedDenoiseConstants = true;
        }

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&drc), 8);

        // ── Bind UAV (slot 4) — filtered AO output at u0 ──
        cmdList->SetDescriptorTable(4, filteredAOUav);

        // ── Bind SRV table (slot 5, SM5 only) — raw AO at t0, depth at t1 ──
        if (!m_device->IsBindless()) {
            cmdList->SetDescriptorTable(5, rawAOSrv);
        }

        // ── Dispatch ──
        const uint32_t groupsX = (dispatchW + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t groupsY = (dispatchH + kThreadGroupSize - 1) / kThreadGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 3: Upsample (half-res only) or transition to SRV (full-res)
    // ══════════════════════════════════════════════════════════════════════

    if (halfRes) {
        // ── Barrier: half-res filtered AO UAV → SRV (for upsample PS to read) ──
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_filteredAOHalf;
            barrier.stateBefore = RHIResourceState::UnorderedAccess;
            barrier.stateAfter  = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&barrier, 1);
        }

        // ── Barrier: full-res filtered AO → RenderTarget (upsample writes here) ──
        // On the first frame, the initial barriers put this texture in UAV state.
        // On subsequent frames, the end-of-upsample barriers leave it in SRV state.
        // Track the correct source state to avoid D3D12 validation errors.
        {
            RHIBarrierDesc barrier{};
            barrier.texture     = m_filteredAOTexture;
            barrier.stateBefore = m_filteredAOCurrentState;
            barrier.stateAfter  = RHIResourceState::RenderTarget;
            cmdList->ResourceBarrier(&barrier, 1);
        }

        if (!EnsureUpsamplePipeline()) {
            return UINT32_MAX;
        }

        // ── Begin render pass: render into full-res filtered AO ──
        RHIRenderPassDesc passDesc{};
        passDesc.colorAttachmentCount            = 1;
        passDesc.colorAttachments[0].texture     = m_filteredAOTexture;
        passDesc.colorAttachments[0].clear       = false;
        passDesc.hasDepth                        = false;

        cmdList->BeginRenderPass(passDesc);

        cmdList->SetPipeline(m_upsamplePipeline);

        // ── Root constants for bilateral upsample (8 DWORDs at b0) ──
        GTAOUpsampleRootConstants urc{};
        urc.halfResAOSrvIndex    = m_filteredAOHalfSrvIndex;
        urc.halfResDepthSrvIndex = depthSrvIndex;
        urc.fullResDepthSrvIndex = depthSrvIndex;
        urc._pad0                = 0;
        urc.halfResTexelSizeX    = 1.0f / static_cast<float>(m_halfWidth);
        urc.halfResTexelSizeY    = 1.0f / static_cast<float>(m_halfHeight);
        urc.fullResTexelSizeX    = 1.0f / static_cast<float>(width);
        urc.fullResTexelSizeY    = 1.0f / static_cast<float>(height);

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&urc), 8);

        // ── Bind half-res filtered AO SRV for SM5 pixel shaders ──
        if (!m_device->IsBindless()) {
            cmdList->SetFragmentTexture(0, m_filteredAOHalf);
        }

        // ── Viewport and scissor at FULL resolution ──
        RHIViewport vp{};
        vp.width    = static_cast<float>(width);
        vp.height   = static_cast<float>(height);
        vp.maxDepth = 1.0f;
        cmdList->SetViewport(vp);

        RHIScissorRect sc{};
        sc.right  = static_cast<int32_t>(width);
        sc.bottom = static_cast<int32_t>(height);
        cmdList->SetScissor(sc);

        // ── Fullscreen triangle (vertex positions generated in VS via SV_VertexID) ──
        cmdList->Draw(3);

        cmdList->EndRenderPass();

        // ── End-of-upsample barriers ──
        // Batch transitions:
        //   1. Full-res filtered AO: RenderTarget → SRV (temporal pass reads it)
        //   2. Half-res raw AO: SRV → UAV (next frame's GTAO compute writes)
        //   3. Half-res filtered AO: SRV → UAV (next frame's denoise writes)
        {
            RHIBarrierDesc endBarriers[3] = {};

            endBarriers[0].texture     = m_filteredAOTexture;
            endBarriers[0].stateBefore = RHIResourceState::RenderTarget;
            endBarriers[0].stateAfter  = RHIResourceState::ShaderResource;

            endBarriers[1].texture     = m_rawAOHalf;
            endBarriers[1].stateBefore = RHIResourceState::ShaderResource;
            endBarriers[1].stateAfter  = RHIResourceState::UnorderedAccess;

            endBarriers[2].texture     = m_filteredAOHalf;
            endBarriers[2].stateBefore = RHIResourceState::ShaderResource;
            endBarriers[2].stateAfter  = RHIResourceState::UnorderedAccess;

            cmdList->ResourceBarrier(endBarriers, 3);
        }

        // After upsample, full-res filtered AO is in SRV state.
        m_filteredAOCurrentState = RHIResourceState::ShaderResource;
    } else {
        // ── Full-res path: transition filtered AO to SRV for temporal/lighting pass ──
        RHIBarrierDesc barrier{};
        barrier.texture     = m_filteredAOTexture;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);

        m_filteredAOCurrentState = RHIResourceState::ShaderResource;
    }

    // At this point, m_filteredAOTexture is in SRV state and contains the
    // spatially-denoised AO (either full-res from denoise or upsampled from half-res).
    // The SRV index for the current frame's denoised AO is always m_filteredAOSrvIndex.

    // ══════════════════════════════════════════════════════════════════════
    // STEP 4: Temporal Accumulation Pass
    // ══════════════════════════════════════════════════════════════════════
    //
    // Blends the current frame's denoised AO with the previous frame's
    // accumulated history using velocity-based reprojection and variance
    // clipping. Outputs to history[cur] which becomes the final AO.
    //
    // Skip temporal pass when:
    // - Velocity buffer is not available (e.g., first frame, velocity pass disabled)
    // - Temporal pipeline fails to create
    // In these cases, fall back to spatial-only AO.
    //
    // @ref Jimenez 2016 "GTAO" — temporal accumulation
    // @ref Salvi 2016 "An Excursion in Temporal Supersampling"

    // Determine the final AO SRV to return. Defaults to spatial-only.
    uint32_t finalAOSrvIndex = m_filteredAOSrvIndex;

    const bool canDoTemporal = (velocitySrvIndex != UINT32_MAX) && EnsureTemporalPipeline();

    if (canDoTemporal) {
        UZ_PROFILE_SCOPE("GTAO::Temporal");

        const uint32_t prevIdx = m_historyIndex;
        const uint32_t curIdx  = 1 - m_historyIndex;

        if (!m_historyValid) {
            // ── First frame: no valid history — copy denoised AO to history[cur] ──
            // This seeds the temporal accumulation with the spatial-only result.
            {
                RHIBarrierDesc copyBarriers[2] = {};
                copyBarriers[0].texture     = m_filteredAOTexture;
                copyBarriers[0].stateBefore = RHIResourceState::ShaderResource;
                copyBarriers[0].stateAfter  = RHIResourceState::CopySrc;

                copyBarriers[1].texture     = m_aoHistory[curIdx];
                copyBarriers[1].stateBefore = m_aoHistoryState[curIdx];
                copyBarriers[1].stateAfter  = RHIResourceState::CopyDst;

                cmdList->ResourceBarrier(copyBarriers, 2);
            }

            cmdList->CopyTexture(m_filteredAOTexture, m_aoHistory[curIdx]);

            // Transition history[cur] → SRV (final output), filteredAO → SRV (restore)
            {
                RHIBarrierDesc postCopyBarriers[2] = {};
                postCopyBarriers[0].texture     = m_aoHistory[curIdx];
                postCopyBarriers[0].stateBefore = RHIResourceState::CopyDst;
                postCopyBarriers[0].stateAfter  = RHIResourceState::ShaderResource;

                postCopyBarriers[1].texture     = m_filteredAOTexture;
                postCopyBarriers[1].stateBefore = RHIResourceState::CopySrc;
                postCopyBarriers[1].stateAfter  = RHIResourceState::ShaderResource;

                cmdList->ResourceBarrier(postCopyBarriers, 2);
            }

            m_aoHistoryState[curIdx] = RHIResourceState::ShaderResource;
            m_historyValid = true;
            finalAOSrvIndex = m_aoHistorySrvIndex[curIdx];

            static bool loggedFirstFrame = false;
            if (!loggedFirstFrame) {
                UZ_LOG(Renderer, Info,
                       "GTAOPass: temporal first frame — copied denoised AO to history[{}], SRV={}",
                       curIdx, m_aoHistorySrvIndex[curIdx]);
                loggedFirstFrame = true;
            }
        } else {
            // ── Normal frame: run temporal resolve compute pass ──
            // Inputs: filteredAO (SRV), history[prev] (SRV), velocity (SRV), depth (SRV)
            // Output: history[cur] (UAV)
            //
            // history[prev]: always in SRV from the previous frame's post-temporal transition.
            // history[cur]: tracked via m_aoHistoryState — may be UAV (after init/first frame
            //               didn't touch it) or SRV (from two frames ago when it was the output).

            // Transition history[cur] to UAV for the compute write.
            if (m_aoHistoryState[curIdx] != RHIResourceState::UnorderedAccess) {
                RHIBarrierDesc barrier{};
                barrier.texture     = m_aoHistory[curIdx];
                barrier.stateBefore = m_aoHistoryState[curIdx];
                barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                cmdList->ResourceBarrier(&barrier, 1);
                m_aoHistoryState[curIdx] = RHIResourceState::UnorderedAccess;
            }

            cmdList->SetPipeline(m_temporalPipeline);

            GTAOTemporalRootConstants trc{};
            trc.currentAOSrvIndex = m_filteredAOSrvIndex;
            trc.historyAOSrvIndex = m_aoHistorySrvIndex[prevIdx];
            trc.velocitySrvIndex  = velocitySrvIndex;
            trc.depthSrvIndex     = depthSrvIndex;
            trc.outputWidth       = width;
            trc.outputHeight      = height;
            trc.blendWeight       = 0.1f;  // Jimenez 2016 default
            trc._pad0             = 0;

            static bool loggedTemporalConstants = false;
            if (!loggedTemporalConstants) {
                UZ_LOG(Renderer, Info,
                       "GTAOPass: temporal root constants (first run) — "
                       "currentAOSrv={}, historySrv={}, velocitySrv={}, depthSrv={}, "
                       "size={}x{}, blend={:.2f}, prevIdx={}, curIdx={}",
                       trc.currentAOSrvIndex, trc.historyAOSrvIndex,
                       trc.velocitySrvIndex, trc.depthSrvIndex,
                       trc.outputWidth, trc.outputHeight, trc.blendWeight,
                       prevIdx, curIdx);
                loggedTemporalConstants = true;
            }

            cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&trc), 8);

            // Bind UAV (slot 4) — output AO at u0
            cmdList->SetDescriptorTable(4, m_aoHistoryUavIndex[curIdx]);

            // Bind SRV table (slot 5, SM5 only) — currentAO at t0, historyAO at t1,
            // velocity at t2, depth at t3
            if (!m_device->IsBindless()) {
                cmdList->SetDescriptorTable(5, m_filteredAOSrvIndex);
            }

            const uint32_t groupsX = (width  + kThreadGroupSize - 1) / kThreadGroupSize;
            const uint32_t groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;
            cmdList->Dispatch(groupsX, groupsY, 1);

            // ── Barrier: history[cur] UAV → SRV (lighting pass reads it) ──
            {
                RHIBarrierDesc barrier{};
                barrier.texture     = m_aoHistory[curIdx];
                barrier.stateBefore = RHIResourceState::UnorderedAccess;
                barrier.stateAfter  = RHIResourceState::ShaderResource;
                cmdList->ResourceBarrier(&barrier, 1);
                m_aoHistoryState[curIdx] = RHIResourceState::ShaderResource;
            }

            finalAOSrvIndex = m_aoHistorySrvIndex[curIdx];
        }

        // Flip history index for next frame
        m_historyIndex = curIdx;
    }

    // Log once for diagnostics — comprehensive first-frame summary
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "GTAOPass: first execution complete — "
               "{}x{} (dispatch={}x{}), quality={}, halfRes={}, temporal={}, "
               "radius={:.0f}cm, intensity={:.2f}, thickness={:.2f}, "
               "rawUAV={}, rawSRV={}, filteredUAV={}, filteredSRV={}, "
               "gtaoPipeline={}, denoisePipeline={}, temporalPipeline={}, "
               "inputDepthSRV={}, inputNormalSRV={}, velocitySRV={}, "
               "finalAOSRV={}",
               width, height, dispatchW, dispatchH,
               static_cast<int>(settings.quality), halfRes, canDoTemporal,
               settings.worldRadius, settings.intensity, settings.thicknessBlend,
               m_rawAOUavIndex, m_rawAOSrvIndex, m_filteredAOUavIndex, m_filteredAOSrvIndex,
               m_gtaoPipeline.index, m_denoisePipeline.index,
               m_temporalPipeline.IsValid() ? m_temporalPipeline.index : UINT32_MAX,
               depthSrvIndex, normalSrvIndex, velocitySrvIndex,
               finalAOSrvIndex);
        loggedOnce = true;
    }

    return finalAOSrvIndex;
}

} // namespace uz
