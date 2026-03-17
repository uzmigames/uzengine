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

#include <cmath>
#include <cstring>

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
        UZ_LOG(Renderer, Error, "GTAOPass: failed to compile gtao_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "gtao_cs";

    m_gtaoCS = m_device->CreateShader(shaderDesc);
    if (!m_gtaoCS.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create GTAO shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_gtaoCS;
    pipeDesc.debugName     = "GTAO_Compute";

    m_gtaoPipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_gtaoPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create GTAO pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: GTAO pipeline created");
    return true;
}

bool GTAOPass::EnsureDenoisePipeline() {
    if (m_denoisePipeline.IsValid()) {
        return true;
    }

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::GtaoDenoiseCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to compile gtao_denoise_cs.hlsl");
        return false;
    }

    RHIShaderDesc shaderDesc{};
    shaderDesc.bytecode     = bytecode.data();
    shaderDesc.bytecodeSize = bytecode.size();
    shaderDesc.stage        = RHIShaderStage::Compute;
    shaderDesc.entryPoint   = "main";
    shaderDesc.debugName    = "gtao_denoise_cs";

    m_denoiseCS = m_device->CreateShader(shaderDesc);
    if (!m_denoiseCS.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create denoise shader");
        return false;
    }

    RHIComputePipelineDesc pipeDesc{};
    pipeDesc.computeShader = m_denoiseCS;
    pipeDesc.debugName     = "GTAO_Denoise";

    m_denoisePipeline = m_device->CreateComputePipeline(pipeDesc);
    if (!m_denoisePipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "GTAOPass: failed to create denoise pipeline");
        return false;
    }

    UZ_LOG(Renderer, Info, "GTAOPass: denoise pipeline created");
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

    // Resize if needed
    if (width != m_width || height != m_height) {
        if (!Resize(width, height)) {
            return UINT32_MAX;
        }
    }

    if (!EnsureGTAOPipeline() || !EnsureDenoisePipeline()) {
        return UINT32_MAX;
    }

    // ── Compute temporal jitter rotation ──
    // Rotate the per-pixel noise pattern each frame to distribute samples
    // over time (works with TAA to converge).
    /// @source UE5 PostProcessAmbientOcclusion.usf:820-833
    const float temporalAngle = static_cast<float>(frameIndex % 6) * (3.14159265f / 3.0f);
    const float temporalCos = std::cos(temporalAngle);
    const float temporalSin = std::sin(temporalAngle);
    const float temporalOffset = static_cast<float>((frameIndex / 6) % 4) * 0.25f;

    // ── Compute distance fade parameters ──
    // fadeMul and fadeAdd are precomputed so the shader can do:
    //   fade = saturate(depth * fadeMul + fadeAdd)
    /// @source UE5 PostProcessAmbientOcclusion.usf:913-915
    const float fadeRange = settings.fadeEndDist - settings.fadeStartDist;
    const float fadeMul = (fadeRange > 0.0f) ? (1.0f / fadeRange) : 0.0f;
    const float fadeAdd = -settings.fadeStartDist * fadeMul;

    // ══════════════════════════════════════════════════════════════════════
    // STEP 1: GTAO Compute Pass
    // ══════════════════════════════════════════════════════════════════════

    {
        cmdList->SetPipeline(m_gtaoPipeline);

        // ── Root constants at b0 (slot 0, 16 DWORDs) ──
        // All GTAO params packed into root constants — compute root sig has no CBV slots.
        GTAORootConstants rc{};
        rc.depthSrvIndex  = depthSrvIndex;
        rc.normalSrvIndex = normalSrvIndex;
        rc.frameIndex     = frameIndex;
        rc.outputWidth    = width;
        rc.outputHeight   = static_cast<float>(height);
        rc.invWidth       = 1.0f / static_cast<float>(width);
        rc.invHeight      = 1.0f / static_cast<float>(height);
        rc.worldRadius    = settings.worldRadius;
        rc.invTanHalfFov  = invTanHalfFov;
        rc.nearPlane      = nearPlane;
        rc.farPlane       = farPlane;
        rc.aoIntensity    = settings.intensity;
        rc.temporalCos    = temporalCos;
        rc.temporalSin    = temporalSin;
        rc.temporalOffset = temporalOffset;
        rc.thicknessBlend = settings.thicknessBlend;
        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 16);

        // ── Bind UAV (slot 4) — raw AO output at u0 ──
        cmdList->SetDescriptorTable(4, m_rawAOUavIndex);

        // ── Bind SRV table (slot 5, SM5) — depth at t0, normal at t1 ──
        cmdList->SetDescriptorTable(5, depthSrvIndex);

        // ── Dispatch ──
        const uint32_t groupsX = (width  + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── UAV barrier: raw AO write → read ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_rawAOTexture;
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
        drc.texelSizeX     = 1.0f / static_cast<float>(width);
        drc.texelSizeY     = 1.0f / static_cast<float>(height);
        drc.nearPlane      = nearPlane;
        drc.farPlane       = farPlane;
        drc.rawAOSrvIndex  = m_rawAOSrvIndex;
        drc.depthSrvIndex  = depthSrvIndex;
        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&drc), 8);

        // ── Bind UAV (slot 4) — filtered AO output at u0 ──
        cmdList->SetDescriptorTable(4, m_filteredAOUavIndex);

        // ── Bind SRV table (slot 5, SM5) — raw AO at t0, depth at t1 ──
        cmdList->SetDescriptorTable(5, m_rawAOSrvIndex);

        // ── Dispatch ──
        const uint32_t groupsX = (width  + kThreadGroupSize - 1) / kThreadGroupSize;
        const uint32_t groupsY = (height + kThreadGroupSize - 1) / kThreadGroupSize;
        cmdList->Dispatch(groupsX, groupsY, 1);
    }

    // ── Transition filtered AO to SRV for lighting pass ──
    {
        RHIBarrierDesc barrier{};
        barrier.texture     = m_filteredAOTexture;
        barrier.stateBefore = RHIResourceState::UnorderedAccess;
        barrier.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&barrier, 1);
    }

    // Log once for diagnostics
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "GTAOPass: first execution ({}x{}, radius={:.0f}cm, rawUAV={}, filteredSRV={})",
               width, height, settings.worldRadius, m_rawAOUavIndex, m_filteredAOSrvIndex);
        loggedOnce = true;
    }

    return m_filteredAOSrvIndex;
}

} // namespace uz
