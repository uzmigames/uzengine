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

    // Transition write target to UAV
    m_states->Transition(cmdList, m_history[writeIdx], RHIResourceState::UnorderedAccess);

    cmdList->SetPipeline(m_pipeline);

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
    } rc;

    rc.albedoIdx         = params.albedoSrvIdx;
    rc.normalIdx         = params.normalSrvIdx;
    rc.depthIdx          = params.depthSrvIdx;
    rc.positionIdx       = params.positionSrvIdx;
    rc.width             = m_width;
    rc.height            = m_height;
    rc.historyReadSrvIdx = m_historyValid ? m_historySrv[readIdx] : UINT32_MAX;
    rc.outputUavIdx      = m_historyUav[writeIdx];
    rc.envCubemapSrvIdx  = params.envCubemapSrvIdx;
    rc.aliasSrvIdx       = sampler.GetAliasSrv();
    rc.lumMip0SrvIdx     = sampler.GetLuminanceMip0Srv();
    rc.lumWidth          = sampler.GetLuminanceWidth();
    rc.lumHeight         = sampler.GetLuminanceHeight();
    rc.frameIndex        = params.frameIndex;
    rc.totalLuminance    = sampler.GetTotalLuminance();
    rc.temporalAlpha     = params.temporalAlpha;

    cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 16);
    cmdList->SetDescriptorTable(4, m_historyUav[writeIdx]);

    cmdList->Dispatch(
        (m_width  + kThreadGroupSize - 1) / kThreadGroupSize,
        (m_height + kThreadGroupSize - 1) / kThreadGroupSize,
        1);

    // Transition write target to SRV (for deferred lighting to read)
    m_states->Transition(cmdList, m_history[writeIdx], RHIResourceState::ShaderResource);

    // Flip ping-pong
    m_writeIdx = 1 - m_writeIdx;
    m_historyValid = true;
}

uint32_t EnvMISPass::GetAccumulatedSrv() const noexcept {
    if (!m_historyValid) return UINT32_MAX;
    // After Execute(), writeIdx was flipped, so the PREVIOUS write target
    // (now at 1 - m_writeIdx) is the one with the latest accumulated data.
    uint32_t lastWritten = 1 - m_writeIdx;
    return m_historySrv[lastWritten];
}

} // namespace uz
