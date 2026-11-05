/// Sun-shadow denoiser (r.Shadows.SoftPCSS.Denoise) — Implementation.

#include "sun_shadow_denoise_pass.h"
#include "engine/renderer/render_graph/render_graph_states.h"
#include "engine/renderer/core/pipeline/root_slots.h"

#include "log.h"
#include "rhi.h"
#include "shader_manager.h"
#include "engine_shaders.h"

#include <cmath>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

SunShadowDenoisePass::~SunShadowDenoisePass() {
    Shutdown();
}

bool SunShadowDenoisePass::Init(IRHIDevice* device, ShaderManager* shaderMgr) {
    if (!device || !shaderMgr) return false;
    m_device    = device;
    m_shaderMgr = shaderMgr;
    m_initialized = true;
    return true;
}

void SunShadowDenoisePass::Shutdown() {
    if (!m_device) return;
    DestroyTextures();
    if (m_rawPipeline.IsValid())     m_device->DestroyPipeline(m_rawPipeline);
    if (m_denoisePipeline.IsValid()) m_device->DestroyPipeline(m_denoisePipeline);
    if (m_rawShaderCS.IsValid())     m_device->DestroyShader(m_rawShaderCS);
    if (m_denoiseShaderCS.IsValid()) m_device->DestroyShader(m_denoiseShaderCS);
    m_rawPipeline     = RHIPipelineHandle::Invalid();
    m_denoisePipeline = RHIPipelineHandle::Invalid();
    m_rawShaderCS     = RHIShaderHandle::Invalid();
    m_denoiseShaderCS = RHIShaderHandle::Invalid();
    m_initialized = false;
}

// ─── Pipelines ───────────────────────────────────────────────────────────

bool SunShadowDenoisePass::EnsurePipelines() {
    if (m_rawPipeline.IsValid() && m_denoisePipeline.IsValid()) return true;
    if (m_pipelinesFailed) return false;

    const auto build = [this](EngineShader shader, const char* name, RHIShaderHandle& cs, RHIPipelineHandle& pso) {
        auto bytecode = m_shaderMgr->GetBytecode(shader);
        if (bytecode.empty()) return false;
        RHIShaderDesc sd;
        sd.bytecode     = bytecode.data();
        sd.bytecodeSize = bytecode.size();
        sd.stage        = RHIShaderStage::Compute;
        sd.entryPoint   = "main";
        sd.debugName    = name;
        cs = m_device->CreateShader(sd);
        if (!cs.IsValid()) return false;
        RHIComputePipelineDesc pd;
        pd.computeShader = cs;
        pd.debugName     = name;
        pso = m_device->CreateComputePipeline(pd);
        return pso.IsValid();
    };
    if (!build(EngineShader::SunShadowRawCS, "SunShadow_Raw", m_rawShaderCS, m_rawPipeline)
        || !build(EngineShader::SunShadowDenoiseCS, "SunShadow_Denoise", m_denoiseShaderCS, m_denoisePipeline)) {
        m_pipelinesFailed = true;
        UZ_LOG(Renderer, Warning, "SunShadowDenoisePass: sun_shadow_raw_cs / sun_shadow_denoise_cs failed to compile; "
               "r.Shadows.SoftPCSS.Denoise keeps the stock lighting");
        return false;
    }
    UZ_LOG(Renderer, Info, "SunShadowDenoisePass: created SunShadow_Raw and SunShadow_Denoise pipelines");
    return true;
}

// ─── Textures ────────────────────────────────────────────────────────────

bool SunShadowDenoisePass::EnsureTextures(uint32_t width, uint32_t height) {
    if (m_raw.IsValid() && width == m_width && height == m_height) return true;
    DestroyTextures();
    if (width == 0 || height == 0) return false;

    const auto create = [&](RHIFormat format, const char* name, RHITextureHandle& tex, uint32_t& srv, uint32_t& uav) {
        RHITextureDesc desc;
        desc.width     = width;
        desc.height    = height;
        desc.format    = format;
        desc.usage     = RHITextureUsage::UnorderedAccess | RHITextureUsage::ShaderResource;
        desc.debugName = name;
        tex = m_device->CreateTexture(desc);
        if (!tex.IsValid()) return false;
        srv = m_device->GetTextureSRVIndex(tex);
        uav = m_device->GetTextureUAVIndex(tex);
        return true;
    };
    const bool ok = create(RHIFormat::RG16Float, "SunShadow_Raw", m_raw, m_rawSrv, m_rawUav)
        && create(RHIFormat::R32Float, "SunShadow_Visibility0", m_visibility[0], m_visibilitySrv[0], m_visibilityUav[0])
        && create(RHIFormat::R32Float, "SunShadow_Visibility1", m_visibility[1], m_visibilitySrv[1], m_visibilityUav[1])
        && create(RHIFormat::RGBA16Float, "SunShadow_Geometry0", m_geometry[0], m_geometrySrv[0], m_geometryUav[0])
        && create(RHIFormat::RGBA16Float, "SunShadow_Geometry1", m_geometry[1], m_geometrySrv[1], m_geometryUav[1]);
    if (!ok) {
        UZ_LOG(Renderer, Error, "SunShadowDenoisePass: failed to create the {}x{} textures", width, height);
        DestroyTextures();
        return false;
    }
    m_width  = width;
    m_height = height;
    m_writeIdx = 0;
    m_historyValid = false;
    UZ_LOG(Renderer, Info, "SunShadowDenoisePass: created {}x{} textures ({:.1f} MB)", width, height,
           static_cast<double>(GetTextureBytes()) / (1024.0 * 1024.0));
    return true;
}

void SunShadowDenoisePass::DestroyTextures() {
    const auto destroy = [this](RHITextureHandle& tex, uint32_t& srv, uint32_t& uav) {
        if (tex.IsValid()) m_device->DestroyTexture(tex);
        tex = RHITextureHandle::Invalid();
        srv = UINT32_MAX;
        uav = UINT32_MAX;
    };
    destroy(m_raw, m_rawSrv, m_rawUav);
    for (int i = 0; i < 2; ++i) {
        destroy(m_visibility[i], m_visibilitySrv[i], m_visibilityUav[i]);
        destroy(m_geometry[i], m_geometrySrv[i], m_geometryUav[i]);
    }
    m_width = 0;
    m_height = 0;
    m_historyValid = false;
}

uint64_t SunShadowDenoisePass::GetTextureBytes() const noexcept {
    if (!m_raw.IsValid()) return 0;
    // RG16F raw (4 B) + 2 x R32F visibility (4 B) + 2 x RGBA16F geometry (8 B) per pixel.
    return static_cast<uint64_t>(m_width) * m_height * (4u + 2u * 4u + 2u * 8u);
}

uint32_t SunShadowDenoisePass::Prepare(uint32_t width, uint32_t height) {
    if (!m_initialized || !EnsurePipelines() || !EnsureTextures(width, height)) return UINT32_MAX;
    return m_visibilitySrv[m_writeIdx];
}

// ─── Execute ─────────────────────────────────────────────────────────────

void SunShadowDenoisePass::Execute(IRHICommandList* cmdList, const DispatchParams& params) {
    if (!m_initialized || !m_raw.IsValid() || !m_rawPipeline.IsValid() || !m_denoisePipeline.IsValid()) return;

    const uint32_t writeIdx = m_writeIdx;
    const uint32_t readIdx  = 1u - writeIdx;
    // The history is reusable only when it was written on the previous frame under a continuous camera.
    const bool reusable = m_historyValid && params.historyReusable && m_lastFrame + 1u == params.frameIndex;
    const uint32_t groupsX = (m_width  + kThreadGroupSize - 1) / kThreadGroupSize;
    const uint32_t groupsY = (m_height + kThreadGroupSize - 1) / kThreadGroupSize;

    // ── 1. Raw estimate: PCSS with the per-frame kernel rotation ────────
    m_states->Transition(cmdList, m_raw, RHIResourceState::UnorderedAccess);
    cmdList->SetPipeline(m_rawPipeline);
    cmdList->SetRootCBV(kSlotPerFrame, params.perFrameBuffer, params.perFrameOffset, params.perFrameCpu, params.perFrameSize);
    cmdList->SetRootCBV(kSlotShadow, params.shadowBuffer, params.shadowOffset, params.shadowCpu, params.shadowSize);
    struct {
        uint32_t normalIdx;
        uint32_t depthIdx;
        uint32_t positionIdx;
        uint32_t rawUavIdx;
        uint32_t width;
        uint32_t height;
        float    rotationOffset;
        uint32_t prevGeometrySrvIdx;
    } rc;
    static_assert(sizeof(rc) == 8 * sizeof(uint32_t), "SunShadow_Raw root constants: 8");
    rc.normalIdx   = params.normalSrvIdx;
    rc.depthIdx    = params.depthSrvIdx;
    rc.positionIdx = params.positionSrvIdx;
    rc.rawUavIdx   = m_rawUav;
    rc.width       = m_width;
    rc.height      = m_height;
    // The golden-ratio sequence of rotations (in turns): consecutive frames fall far apart on the circle and any run
    // of frames covers it evenly. Computed in double so the fraction stays exact at large frame indices.
    /// @constants 0.6180339887498949 = (sqrt(5) - 1) / 2
    rc.rotationOffset = params.rotate
        ? static_cast<float>(std::fmod(static_cast<double>(params.frameIndex) * 0.6180339887498949, 1.0)) : 0.0f;
    // The previous geometry history tells which pixels restarted their accumulation recently; they keep the
    // lighting's rotation (sun_shadow_raw_cs.hlsl). It was left in ShaderResource by the last Execute.
    rc.prevGeometrySrvIdx = reusable ? m_geometrySrv[readIdx] : UINT32_MAX;
    cmdList->SetRootConstants(kSlotRootConstants, reinterpret_cast<const uint32_t*>(&rc), 8);
    cmdList->SetDescriptorTable(4, m_rawUav);
    cmdList->Dispatch(groupsX, groupsY, 1);

    // ── 2. Denoise: reproject + validate, pre-filter, clip, accumulate ──
    m_states->Transition(cmdList, m_raw, RHIResourceState::ShaderResource);
    m_states->Transition(cmdList, m_visibility[writeIdx], RHIResourceState::UnorderedAccess);
    m_states->Transition(cmdList, m_geometry[writeIdx], RHIResourceState::UnorderedAccess);
    cmdList->SetPipeline(m_denoisePipeline);
    struct {
        uint32_t rawSrvIdx;
        uint32_t normalIdx;
        uint32_t positionIdx;
        uint32_t velocitySrvIdx;
        uint32_t historySrvIdx;
        uint32_t historyGeometrySrvIdx;
        uint32_t outputUavIdx;
        uint32_t geometryUavIdx;
        uint32_t width;
        uint32_t height;
        uint32_t historyReusable;
        float    prevCameraPos[3];
        float    cameraPos[3];
        float    pixelAngle;
        uint32_t maxCount;
        float    spatialScale;
        float    clampSigma;
    } rd;
    static_assert(sizeof(rd) == 21 * sizeof(uint32_t), "SunShadow_Denoise root constants: 21 (the root signature holds 24)");
    rd.rawSrvIdx             = m_rawSrv;
    rd.normalIdx             = params.normalSrvIdx;
    rd.positionIdx           = params.positionSrvIdx;
    rd.velocitySrvIdx        = params.velocitySrvIdx;
    rd.historySrvIdx         = reusable ? m_visibilitySrv[readIdx] : UINT32_MAX;
    rd.historyGeometrySrvIdx = reusable ? m_geometrySrv[readIdx] : UINT32_MAX;
    rd.outputUavIdx          = m_visibilityUav[writeIdx];
    rd.geometryUavIdx        = m_geometryUav[writeIdx];
    rd.width                 = m_width;
    rd.height                = m_height;
    rd.historyReusable       = reusable ? 1u : 0u;
    for (int i = 0; i < 3; ++i) rd.prevCameraPos[i] = m_prevCameraPos[i];
    for (int i = 0; i < 3; ++i) rd.cameraPos[i] = params.cameraPos[i];
    rd.pixelAngle            = params.pixelAngle;
    rd.maxCount              = params.maxCount < 2u ? 2u : params.maxCount;
    rd.spatialScale          = params.spatialScale;
    rd.clampSigma            = params.clampSigma;
    cmdList->SetRootConstants(kSlotRootConstants, reinterpret_cast<const uint32_t*>(&rd), 21);
    cmdList->SetDescriptorTable(4, m_visibilityUav[writeIdx]);
    cmdList->Dispatch(groupsX, groupsY, 1);

    // The lighting reads the visibility; the next frame reads both as history.
    m_states->Transition(cmdList, m_visibility[writeIdx], RHIResourceState::ShaderResource);
    m_states->Transition(cmdList, m_geometry[writeIdx], RHIResourceState::ShaderResource);

    m_writeIdx     = readIdx;
    m_historyValid = true;
    m_lastFrame    = params.frameIndex;
    for (int i = 0; i < 3; ++i) m_prevCameraPos[i] = params.cameraPos[i];
}

} // namespace uz
