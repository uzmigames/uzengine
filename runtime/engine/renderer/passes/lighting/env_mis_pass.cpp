/// Environment MIS Temporal Accumulation Pass — Implementation.

#include "env_mis_pass.h"
#include "engine/renderer/render_graph/render_graph_states.h"

#include "log.h"
#include "rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"
#include "env_map_sampler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

EnvMISPass::~EnvMISPass() {
    Shutdown();
}

// ─── Pipeline ────────────────────────────────────────────────────────────

bool EnvMISPass::EnsurePipeline() {
    if (m_pipeline.IsValid()) return true;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::EnvMisTemporalCS);
    if (bytecode.empty()) {
        UZ_LOG(Renderer, Error, "EnvMISPass: failed to compile env_mis_temporal_cs.hlsl");
        return false;
    }

    RHIShaderDesc sd;
    sd.bytecode     = bytecode.data();
    sd.bytecodeSize = bytecode.size();
    sd.stage        = RHIShaderStage::Compute;
    sd.entryPoint   = "main";
    sd.debugName    = "env_mis_temporal_cs";
    m_shaderCS = m_device->CreateShader(sd);
    if (!m_shaderCS.IsValid()) return false;

    RHIComputePipelineDesc pd;
    pd.computeShader = m_shaderCS;
    pd.debugName     = "EnvMIS_Temporal";
    m_pipeline = m_device->CreateComputePipeline(pd);
    if (!m_pipeline.IsValid()) return false;

    UZ_LOG(Renderer, Info, "EnvMISPass: pipeline created");
    return true;
}

// r.EnvMIS.Reproject: the UZ_ENVMIS_REPROJECT=1 permutation of the same shader (the TAA_Resolve_SilhouetteReject
// pattern); the default pipeline above is untouched, so the CVar off renders what it rendered before.
bool EnvMISPass::EnsureReprojectPipeline() {
    if (m_reprojectPipeline.IsValid()) return true;
    if (m_reprojectPipelineFailed) return false;

    m_reprojectShaderCS = m_shaderMgr->CreateShaderWithDefines(
        EngineShader::EnvMisTemporalCS, RHIShaderStage::Compute, "UZ_ENVMIS_REPROJECT=1");
    if (m_reprojectShaderCS.IsValid()) {
        RHIComputePipelineDesc pd;
        pd.computeShader = m_reprojectShaderCS;
        pd.debugName     = "EnvMIS_Temporal_Reproject";
        m_reprojectPipeline = m_device->CreateComputePipeline(pd);
    }
    if (!m_reprojectPipeline.IsValid()) {
        m_reprojectPipelineFailed = true;
        UZ_LOG(Renderer, Warning, "EnvMISPass: the UZ_ENVMIS_REPROJECT permutation failed to compile; r.EnvMIS.Reproject runs the default EnvMIS_Temporal pipeline");
        return false;
    }
    UZ_LOG(Renderer, Info, "EnvMISPass: created EnvMIS_Temporal_Reproject pipeline");
    return true;
}

// r.EnvMIS.Denoise: env_mis_resolve_cs.hlsl, the spatial resolve of the raw estimate followed by the same
// reprojected accumulation; a failed compile logs once and leaves the accumulation permutation in charge.
bool EnvMISPass::EnsureResolvePipeline() {
    if (m_resolvePipeline.IsValid()) return true;
    if (m_resolvePipelineFailed) return false;

    auto bytecode = m_shaderMgr->GetBytecode(EngineShader::EnvMisResolveCS);
    if (!bytecode.empty()) {
        RHIShaderDesc sd;
        sd.bytecode     = bytecode.data();
        sd.bytecodeSize = bytecode.size();
        sd.stage        = RHIShaderStage::Compute;
        sd.entryPoint   = "main";
        sd.debugName    = "env_mis_resolve_cs";
        m_resolveShaderCS = m_device->CreateShader(sd);
        if (m_resolveShaderCS.IsValid()) {
            RHIComputePipelineDesc pd;
            pd.computeShader = m_resolveShaderCS;
            pd.debugName     = "EnvMIS_Resolve";
            m_resolvePipeline = m_device->CreateComputePipeline(pd);
        }
    }
    if (!m_resolvePipeline.IsValid()) {
        m_resolvePipelineFailed = true;
        UZ_LOG(Renderer, Warning, "EnvMISPass: env_mis_resolve_cs.hlsl failed to compile; r.EnvMIS.Denoise runs the EnvMIS_Temporal_Reproject pipeline");
        return false;
    }
    UZ_LOG(Renderer, Info, "EnvMISPass: created EnvMIS_Resolve pipeline");
    return true;
}

// ─── Textures ────────────────────────────────────────────────────────────

bool EnvMISPass::CreateTextures(uint32_t width, uint32_t height) {
    DestroyTextures();

    for (int i = 0; i < 2; ++i) {
        RHITextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::UnorderedAccess
                       | RHITextureUsage::ShaderResource;
        desc.debugName = (i == 0) ? "EnvMIS_History0" : "EnvMIS_History1";

        m_history[i] = m_device->CreateTexture(desc);
        if (!m_history[i].IsValid()) {
            UZ_LOG(Renderer, Error, "EnvMISPass: failed to create history texture {}", i);
            return false;
        }
        m_historySrv[i] = m_device->GetTextureSRVIndex(m_history[i]);
        m_historyUav[i] = m_device->GetTextureUAVIndex(m_history[i]);
    }

    m_width  = width;
    m_height = height;
    m_writeIdx = 0;
    m_historyValid = false;

    UZ_LOG(Renderer, Info, "EnvMISPass: created {}x{} ping-pong history textures", width, height);
    return true;
}

void EnvMISPass::DestroyTextures() {
    for (int i = 0; i < 2; ++i) {
        if (m_history[i].IsValid()) {
            m_device->DestroyTexture(m_history[i]);
            m_history[i] = RHITextureHandle::Invalid();
        }
        m_historySrv[i] = UINT32_MAX;
        m_historyUav[i] = UINT32_MAX;
    }
    m_historyValid = false;
    DestroyGeometryTextures();
    DestroyRawTexture();
}

/// r.EnvMIS.Denoise: this frame's estimate before the resolve (RGBA16F: rgb the estimate, a the coverage), the
/// size of the history; created the first time the resolve runs, freed with the history.
bool EnvMISPass::EnsureRawTexture() {
    if (m_raw.IsValid()) return true;
    RHITextureDesc desc;
    desc.width     = m_width;
    desc.height    = m_height;
    desc.format    = RHIFormat::RGBA16Float;
    desc.usage     = RHITextureUsage::UnorderedAccess
                   | RHITextureUsage::ShaderResource;
    desc.debugName = "EnvMIS_Raw";
    m_raw = m_device->CreateTexture(desc);
    if (!m_raw.IsValid()) {
        UZ_LOG(Renderer, Error, "EnvMISPass: failed to create the raw estimate texture");
        return false;
    }
    m_rawSrv = m_device->GetTextureSRVIndex(m_raw);
    m_rawUav = m_device->GetTextureUAVIndex(m_raw);
    UZ_LOG(Renderer, Info, "EnvMISPass: created {}x{} raw estimate texture for r.EnvMIS.Denoise", m_width, m_height);
    return true;
}

void EnvMISPass::DestroyRawTexture() {
    if (m_raw.IsValid()) {
        m_device->DestroyTexture(m_raw);
        m_raw = RHITextureHandle::Invalid();
    }
    m_rawSrv = UINT32_MAX;
    m_rawUav = UINT32_MAX;
}

/// r.EnvMIS.Reproject: the geometry history pair (RGBA16F: octahedral normal, view distance in metres, sample
/// count), the size of the colour history; created the first time the permutation runs, freed with the history.
bool EnvMISPass::EnsureGeometryTextures() {
    if (m_geometry[0].IsValid() && m_geometry[1].IsValid()) return true;
    DestroyGeometryTextures();
    for (int i = 0; i < 2; ++i) {
        RHITextureDesc desc;
        desc.width     = m_width;
        desc.height    = m_height;
        desc.format    = RHIFormat::RGBA16Float;
        desc.usage     = RHITextureUsage::UnorderedAccess
                       | RHITextureUsage::ShaderResource;
        desc.debugName = (i == 0) ? "EnvMIS_Geometry0" : "EnvMIS_Geometry1";
        m_geometry[i] = m_device->CreateTexture(desc);
        if (!m_geometry[i].IsValid()) {
            UZ_LOG(Renderer, Error, "EnvMISPass: failed to create geometry history texture {}", i);
            DestroyGeometryTextures();
            return false;
        }
        m_geometrySrv[i] = m_device->GetTextureSRVIndex(m_geometry[i]);
        m_geometryUav[i] = m_device->GetTextureUAVIndex(m_geometry[i]);
    }
    m_reprojectHistoryValid = false;
    UZ_LOG(Renderer, Info, "EnvMISPass: created {}x{} geometry history textures for r.EnvMIS.Reproject", m_width, m_height);
    return true;
}

void EnvMISPass::DestroyGeometryTextures() {
    for (int i = 0; i < 2; ++i) {
        if (m_geometry[i].IsValid()) {
            m_device->DestroyTexture(m_geometry[i]);
            m_geometry[i] = RHITextureHandle::Invalid();
        }
        m_geometrySrv[i] = UINT32_MAX;
        m_geometryUav[i] = UINT32_MAX;
    }
    m_reprojectHistoryValid = false;
}

// ─── Init / Shutdown / Resize ────────────────────────────────────────────

bool EnvMISPass::Init(IRHIDevice* device, ShaderManager* shaderMgr,
                      uint32_t width, uint32_t height)
{
    if (!device || !shaderMgr) return false;
    m_device    = device;
    m_shaderMgr = shaderMgr;

    if (!CreateTextures(width, height)) return false;

    m_initialized = true;
    return true;
}

void EnvMISPass::Shutdown() {
    DestroyTextures();
    m_initialized = false;
}

void EnvMISPass::OnResize(uint32_t width, uint32_t height) {
    if (width == m_width && height == m_height) return;
    CreateTextures(width, height);
}

// ─── Execute ─────────────────────────────────────────────────────────────

void EnvMISPass::Execute(IRHICommandList* cmdList, const EnvMapSampler& sampler,
                         const DispatchParams& params)
{
    if (!m_initialized) return;
    if (!sampler.IsReady()) return;
    if (!EnsurePipeline()) return;

    uint32_t writeIdx = m_writeIdx;
    uint32_t readIdx  = 1 - writeIdx;

    // r.EnvMIS.Reproject: the permutation runs when its pipeline and geometry history exist; otherwise the
    // default path below, exactly as without the CVar.
    const bool reproject = params.reproject && EnsureReprojectPipeline() && EnsureGeometryTextures();
    // The geometry history is reusable only when this permutation wrote it on the previous frame, under a
    // continuous camera (the frame's temporal decision); anything else restarts the accumulation.
    const bool reusable = reproject && m_reprojectHistoryValid && params.historyReusable
                       && m_reprojectFrame + 1u == params.frameIndex;
    // r.EnvMIS.Denoise builds on the reprojected accumulation: the estimate goes to the raw texture first (the
    // default pipeline with no history writes exactly its estimate), then the resolve accumulates it.
    const bool denoise = reproject && params.denoise && EnsureResolvePipeline() && EnsureRawTexture();

    // Transition write target to UAV
    m_states->Transition(cmdList, m_history[writeIdx], RHIResourceState::UnorderedAccess);
    if (reproject) m_states->Transition(cmdList, m_geometry[writeIdx], RHIResourceState::UnorderedAccess);
    if (denoise) m_states->Transition(cmdList, m_raw, RHIResourceState::UnorderedAccess);

    cmdList->SetPipeline(denoise ? m_pipeline : (reproject ? m_reprojectPipeline : m_pipeline));

    // PerFrameCB at b1 (compute root slot 1): camera position, sky light tint.
    if (params.perFrameBuffer.IsValid()) {
        cmdList->SetRootCBV(1, params.perFrameBuffer, params.perFrameOffset,
                            params.perFrameCpu, params.perFrameSize);
    }

    // Pack root constants (16 uint/float values)
    struct {
        uint32_t albedoIdx;
        uint32_t normalIdx;
        uint32_t depthIdx;
        uint32_t positionIdx;
        uint32_t width;
        uint32_t height;
        uint32_t historyReadSrvIdx;
        uint32_t outputUavIdx;
        uint32_t envCubemapSrvIdx;
        uint32_t aliasSrvIdx;
        uint32_t lumMip0SrvIdx;
        uint32_t lumWidth;
        uint32_t lumHeight;
        uint32_t frameIndex;
        float    totalLuminance;
        float    temporalAlpha;
        // r.EnvMIS.Reproject (UZ_ENVMIS_REPROJECT=1 only; the default pipeline is pushed the 16 above)
        uint32_t velocitySrvIdx;
        uint32_t historyGeometrySrvIdx;
        uint32_t geometryUavIdx;
        uint32_t historyReusable;
        float    prevCameraPos[3];
    } rc;
    static_assert(sizeof(rc) == 23 * sizeof(uint32_t), "EnvMIS root constants: 16 + 7 (the root signature holds 24)");

    rc.albedoIdx         = params.albedoSrvIdx;
    rc.normalIdx         = params.normalSrvIdx;
    rc.depthIdx          = params.depthSrvIdx;
    rc.positionIdx       = params.positionSrvIdx;
    rc.width             = m_width;
    rc.height            = m_height;
    rc.historyReadSrvIdx = (m_historyValid && !denoise) ? m_historySrv[readIdx] : UINT32_MAX;
    rc.outputUavIdx      = denoise ? m_rawUav : m_historyUav[writeIdx];
    rc.envCubemapSrvIdx  = params.envCubemapSrvIdx;
    rc.aliasSrvIdx       = sampler.GetAliasSrv();
    rc.lumMip0SrvIdx     = sampler.GetLuminanceMip0Srv();
    rc.lumWidth          = sampler.GetLuminanceWidth();
    rc.lumHeight         = sampler.GetLuminanceHeight();
    rc.frameIndex        = params.frameIndex;
    rc.totalLuminance    = sampler.GetTotalLuminance();
    rc.temporalAlpha     = params.temporalAlpha;
    rc.velocitySrvIdx        = params.velocitySrvIdx;
    rc.historyGeometrySrvIdx = reusable ? m_geometrySrv[readIdx] : UINT32_MAX;
    rc.geometryUavIdx        = reproject ? m_geometryUav[writeIdx] : UINT32_MAX;
    rc.historyReusable       = reusable ? 1u : 0u;
    for (int i = 0; i < 3; ++i) rc.prevCameraPos[i] = m_prevCameraPos[i];

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), (reproject && !denoise) ? 23 : 16);
    cmdList->SetDescriptorTable(4, denoise ? m_rawUav : m_historyUav[writeIdx]);

    cmdList->Dispatch(
        (m_width  + kThreadGroupSize - 1) / kThreadGroupSize,
        (m_height + kThreadGroupSize - 1) / kThreadGroupSize,
        1);

    if (denoise) {
        // The resolve: this frame's raw estimate, resolved over the neighbouring pixels of the same lobe and
        // material, accumulated into the history where the surface point was last frame (env_mis_resolve_cs.hlsl).
        m_states->Transition(cmdList, m_raw, RHIResourceState::ShaderResource);
        cmdList->SetPipeline(m_resolvePipeline);

        struct {
            uint32_t albedoIdx;
            uint32_t normalIdx;
            uint32_t positionIdx;
            uint32_t rawSrvIdx;
            uint32_t width;
            uint32_t height;
            uint32_t historyReadSrvIdx;
            uint32_t outputUavIdx;
            uint32_t velocitySrvIdx;
            uint32_t historyGeometrySrvIdx;
            uint32_t geometryUavIdx;
            uint32_t historyReusable;
            float    prevCameraPos[3];
            float    cameraPos[3];
            float    temporalAlpha;
            uint32_t radius;
            float    lobeFraction;
        } rr;
        static_assert(sizeof(rr) == 21 * sizeof(uint32_t), "EnvMIS resolve root constants: 21 (the root signature holds 24)");

        rr.albedoIdx             = params.albedoSrvIdx;
        rr.normalIdx             = params.normalSrvIdx;
        rr.positionIdx           = params.positionSrvIdx;
        rr.rawSrvIdx             = m_rawSrv;
        rr.width                 = m_width;
        rr.height                = m_height;
        rr.historyReadSrvIdx     = m_historyValid ? m_historySrv[readIdx] : UINT32_MAX;
        rr.outputUavIdx          = m_historyUav[writeIdx];
        rr.velocitySrvIdx        = params.velocitySrvIdx;
        rr.historyGeometrySrvIdx = reusable ? m_geometrySrv[readIdx] : UINT32_MAX;
        rr.geometryUavIdx        = m_geometryUav[writeIdx];
        rr.historyReusable       = reusable ? 1u : 0u;
        for (int i = 0; i < 3; ++i) rr.prevCameraPos[i] = m_prevCameraPos[i];
        for (int i = 0; i < 3; ++i) rr.cameraPos[i] = params.cameraPos[i];
        rr.temporalAlpha         = params.temporalAlpha;
        rr.radius                = params.denoiseRadius;
        rr.lobeFraction          = params.denoiseLobeFraction;

        cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rr), 21);
        cmdList->SetDescriptorTable(4, m_historyUav[writeIdx]);

        cmdList->Dispatch(
            (m_width  + kThreadGroupSize - 1) / kThreadGroupSize,
            (m_height + kThreadGroupSize - 1) / kThreadGroupSize,
            1);
    }

    // Transition write target to SRV (for deferred lighting to read)
    m_states->Transition(cmdList, m_history[writeIdx], RHIResourceState::ShaderResource);
    if (reproject) m_states->Transition(cmdList, m_geometry[writeIdx], RHIResourceState::ShaderResource);

    // Flip ping-pong
    m_writeIdx = 1 - m_writeIdx;
    m_historyValid = true;
    m_reprojectHistoryValid = reproject;
    m_reprojectFrame = params.frameIndex;
    for (int i = 0; i < 3; ++i) m_prevCameraPos[i] = params.cameraPos[i];
}

uint32_t EnvMISPass::GetAccumulatedSrv() const noexcept {
    if (!m_historyValid) return UINT32_MAX;
    // After Execute(), writeIdx was flipped, so the PREVIOUS write target
    // (now at 1 - m_writeIdx) is the one with the latest accumulated data.
    uint32_t lastWritten = 1 - m_writeIdx;
    return m_historySrv[lastWritten];
}

} // namespace uz
