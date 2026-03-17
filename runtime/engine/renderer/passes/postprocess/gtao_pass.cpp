/// UzEngine GTAO Pass — Implementation.
///
/// Creates R8_UNORM AO textures, compiles GTAO + denoise compute shaders,
/// and dispatches GPU work each frame. The filtered AO result is consumed
/// by the deferred lighting pass to modulate ambient occlusion.
///
/// @source UE5 PostProcessAmbientOcclusion.usf — GTAO algorithm

#include "gtao_pass.h"

#include "engine/rhi/rhi.h"
#include "engine/renderer/core/shader_manager.h"
#include "engine/renderer/core/cb_ring_allocator.h"
#include "engine_shaders.h"
#include "log.h"
#include "profiler.h"


UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

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

    // ── Filtered AO texture (R8_UNORM, UAV+SRV) ──
    {
        RHITextureDesc desc{};
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::R8Unorm;
        desc.usage     = RHITextureUsage::ShaderResource
                       | RHITextureUsage::UnorderedAccess;
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

    return true;
}

void GTAOPass::DestroyTextures() {
    if (m_rawAOTexture.IsValid()) {
        m_device->DestroyTexture(m_rawAOTexture);
        m_rawAOTexture = RHITextureHandle::Invalid();
    }
    if (m_filteredAOTexture.IsValid()) {
        m_device->DestroyTexture(m_filteredAOTexture);
        m_filteredAOTexture = RHITextureHandle::Invalid();
    }
    m_rawAOSrvIndex      = UINT32_MAX;
    m_rawAOUavIndex      = UINT32_MAX;
    m_filteredAOSrvIndex = UINT32_MAX;
    m_filteredAOUavIndex = UINT32_MAX;
    m_width  = 0;
    m_height = 0;
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
    if (m_gtaoCS.IsValid()) {
        m_device->DestroyShader(m_gtaoCS);
        m_gtaoCS = RHIShaderHandle::Invalid();
    }
    if (m_denoiseCS.IsValid()) {
        m_device->DestroyShader(m_denoiseCS);
        m_denoiseCS = RHIShaderHandle::Invalid();
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
                           uint32_t frameIndex)
{
    UZ_PROFILE_SCOPE("GTAOPass::Execute");

    if (!m_initialized) {
        UZ_LOG(Renderer, Error, "GTAOPass::Execute called before Init()");
        return UINT32_MAX;
    }

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

    // ══════════════════════════════════════════════════════════════════════
    // STEP 1: GTAO Compute Pass
    // ══════════════════════════════════════════════════════════════════════

    {
        cmdList->SetPipeline(m_gtaoPipeline);

        // ── Root constants at b0 (slot 0, 8 DWORDs) ──
        // Dynamic per-frame values only — shader hardcodes radius/intensity/etc.
        GTAORootConstants rc{};
        rc.depthSrvIndex  = depthSrvIndex;
        rc.normalSrvIndex = normalSrvIndex;
        rc.outputWidth    = width;
        rc.outputHeight   = height;
        rc.invTanHalfFov  = invTanHalfFov;
        rc.nearPlane      = nearPlane;
        rc.farPlane       = farPlane;
        rc.frameIndex     = frameIndex;

        // Dump all root constants on first frame to diagnose bad shader inputs
        static bool loggedGtaoConstants = false;
        if (!loggedGtaoConstants) {
            UZ_LOG(Renderer, Info,
                   "GTAOPass: GTAO root constants (first frame) — "
                   "depthSRV={}, normalSRV={}, size={}x{}, "
                   "invTanHalfFov={:.4f}, near={:.2f}, far={:.1f}, frame={}",
                   rc.depthSrvIndex, rc.normalSrvIndex,
                   rc.outputWidth, rc.outputHeight,
                   rc.invTanHalfFov, rc.nearPlane, rc.farPlane, rc.frameIndex);
            loggedGtaoConstants = true;
        }

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

        // ── Bind UAV (slot 4) — raw AO output at u0 ──
        cmdList->SetDescriptorTable(4, m_rawAOUavIndex);

        // ── Bind SRV table (slot 5, SM5) — depth at t0, normal at t1 ──
        cmdList->SetDescriptorTable(5, depthSrvIndex);

        // ── Dispatch ──
        const uint32_t groupsX = (width  + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;

        UZ_LOG(Renderer, Debug,
               "GTAOPass: GTAO dispatch — groups=({}, {}, 1), threadGroup={}, UAV={}, SRV={}",
               groupsX, groupsY, kThreadGroupSize, m_rawAOUavIndex, depthSrvIndex);

        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── UAV barrier: raw AO write → read ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_rawAOTexture;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);

        UZ_LOG(Renderer, Debug,
               "GTAOPass: barrier rawAO — UnorderedAccess -> ShaderResource");
    }

    // ══════════════════════════════════════════════════════════════════════
    // STEP 2: Spatial Denoise Pass
    // ══════════════════════════════════════════════════════════════════════

    {
        cmdList->SetPipeline(m_denoisePipeline);

        // ── Root constants at b0 (slot 0, 8 DWORDs) ──
        GTAODenoiseRootConstants drc{};
        drc.texelSizeX     = 1.0f / static_cast<float>(width);
        drc.texelSizeY     = 1.0f / static_cast<float>(height);
        drc.nearPlane      = nearPlane;
        drc.farPlane       = farPlane;
        drc.rawAOSrvIndex  = m_rawAOSrvIndex;
        drc.depthSrvIndex  = depthSrvIndex;

        // Dump denoise constants on first frame
        static bool loggedDenoiseConstants = false;
        if (!loggedDenoiseConstants) {
            UZ_LOG(Renderer, Info,
                   "GTAOPass: denoise root constants (first frame) — "
                   "texelSize=({:.6f}, {:.6f}), near={:.2f}, far={:.1f}, "
                   "rawAOSRV={}, depthSRV={}",
                   drc.texelSizeX, drc.texelSizeY,
                   drc.nearPlane, drc.farPlane,
                   drc.rawAOSrvIndex, drc.depthSrvIndex);
            loggedDenoiseConstants = true;
        }

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&drc), 8);

        // ── Bind UAV (slot 4) — filtered AO output at u0 ──
        cmdList->SetDescriptorTable(4, m_filteredAOUavIndex);

        // ── Bind SRV table (slot 5, SM5) — raw AO at t0, depth at t1 ──
        cmdList->SetDescriptorTable(5, m_rawAOSrvIndex);

        // ── Dispatch ──
        const uint32_t groupsX = (width  + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;

        UZ_LOG(Renderer, Debug,
               "GTAOPass: denoise dispatch — groups=({}, {}, 1), UAV={}, SRV={}",
               groupsX, groupsY, m_filteredAOUavIndex, m_rawAOSrvIndex);

        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Transition filtered AO to SRV for lighting pass ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_filteredAOTexture;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);

        UZ_LOG(Renderer, Debug,
               "GTAOPass: barrier filteredAO — UnorderedAccess -> ShaderResource");
    }

    // Log once for diagnostics — comprehensive first-frame summary
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "GTAOPass: first execution complete — "
               "{}x{}, radius={:.0f}cm, intensity={:.2f}, thickness={:.2f}, "
               "rawUAV={}, rawSRV={}, filteredUAV={}, filteredSRV={}, "
               "gtaoPipeline={}, denoisePipeline={}, "
               "inputDepthSRV={}, inputNormalSRV={}",
               width, height, settings.worldRadius, settings.intensity, settings.thicknessBlend,
               m_rawAOUavIndex, m_rawAOSrvIndex, m_filteredAOUavIndex, m_filteredAOSrvIndex,
               m_gtaoPipeline.index, m_denoisePipeline.index,
               depthSrvIndex, normalSrvIndex);
        loggedOnce = true;
    }

    return m_filteredAOSrvIndex;
}

} // namespace uz
