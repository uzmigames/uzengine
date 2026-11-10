/// UzEngine Volumetric Fog Pass — Implementation.
///
/// Creates the 3D froxel textures and registers the inject + scatter + composite passes in the
/// render graph. The temporal history (two ping-pong volumes) is allocated on the first frame the
/// temporal filter runs, so the OFF path costs nothing.
///
/// @ref Hillaire 2015 "Physically Based & Unified Volumetric Rendering in Frostbite"

#include "volumetric_fog_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "pipeline_library.h"
#include "per_frame_params.h"
#include "engine/rhi/rhi.h"

#include "log.h"
#include "profiler.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

namespace {

/// One 160x90x64 RGBA16F volume with an SRV and a UAV.
bool CreateFroxelVolume(IRHIDevice* device, const char* debugName,
                        RHITextureHandle& tex, uint32_t& srv, uint32_t& uav)
{
    RHITextureDesc desc{};
    desc.width            = kFroxelGridX;
    desc.height           = kFroxelGridY;
    desc.depthOrArraySize = kFroxelGridZ;
    desc.is3D             = true;
    desc.format           = RHIFormat::RGBA16Float;
    desc.usage            = RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;
    desc.debugName        = debugName;

    tex = device->CreateTexture(desc);
    if (!tex.IsValid()) {
        UZ_LOG(Renderer, Error, "VolumetricFogPass: failed to create 3D texture '{}'", debugName);
        return false;
    }
    srv = device->GetTextureSRVIndex(tex);
    uav = device->GetTextureUAVIndex(tex);
    return true;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool VolumetricFogPass::Init(IRHIDevice* device)
{
    if (m_initialized) return true;
    if (!device) return false;
    m_device = device;

    // Inscattering + extinction (inject output): RGB = in-scatter, A = extinction (density x extinctionScale)
    if (!CreateFroxelVolume(device, "FroxelFog_Inscattering", m_inscattering, m_inscatteringSrv, m_inscatteringUav))
        return false;

    // Integrated inscattering + transmittance (scatter output): RGB = accumulated in-scatter, A = transmittance
    if (!CreateFroxelVolume(device, "FroxelFog_Integrated", m_integrated, m_integratedSrv, m_integratedUav)) {
        device->DestroyTexture(m_inscattering);
        m_inscattering = {};
        return false;
    }

    UZ_LOG(Renderer, Info,
           "VolumetricFogPass: initialized ({}x{}x{} RGBA16F, "
           "inscatter SRV={}/UAV={}, integrated SRV={}/UAV={})",
           kFroxelGridX, kFroxelGridY, kFroxelGridZ,
           m_inscatteringSrv, m_inscatteringUav,
           m_integratedSrv, m_integratedUav);

    m_initialized = true;
    return true;
}

bool VolumetricFogPass::EnsureHistory()
{
    if (m_history[0].IsValid() && m_history[1].IsValid()) return true;
    if (!m_device) return false;
    if (!CreateFroxelVolume(m_device, "FroxelFog_History0", m_history[0], m_historySrv[0], m_historyUav[0]))
        return false;
    if (!CreateFroxelVolume(m_device, "FroxelFog_History1", m_history[1], m_historySrv[1], m_historyUav[1])) {
        m_device->DestroyTexture(m_history[0]);
        m_history[0] = {};
        m_historySrv[0] = m_historyUav[0] = UINT32_MAX;
        return false;
    }
    UZ_LOG(Renderer, Info, "VolumetricFogPass: temporal history allocated (2x {}x{}x{} RGBA16F)",
           kFroxelGridX, kFroxelGridY, kFroxelGridZ);
    return true;
}

void VolumetricFogPass::Shutdown(IRHIDevice* device)
{
    if (!device) return;
    if (m_inscattering.IsValid()) {
        device->DestroyTexture(m_inscattering);
        m_inscattering = {};
    }
    if (m_integrated.IsValid()) {
        device->DestroyTexture(m_integrated);
        m_integrated = {};
    }
    for (uint32_t i = 0; i < 2; ++i) {
        if (m_history[i].IsValid()) {
            device->DestroyTexture(m_history[i]);
            m_history[i] = {};
        }
        m_historySrv[i] = m_historyUav[i] = UINT32_MAX;
    }
    m_historyValid = false;
    m_initialized = false;
    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// RegisterPasses — inject + scatter + composite
// ─────────────────────────────────────────────────────────────────────────────

void VolumetricFogPass::RegisterPasses(
    rg::RgBuilder& rgBuilder,
    const VolumetricFogFrameInputs& in,
    const VolumetricFogCB& params,
    CBRingAllocator& cbRing,
    PipelineLibrary* pipelines)
{
    if (!m_initialized || !in.bindless) return;

    // ── Temporal state ────────────────────────────────────────────────
    // The history is reused only when the previous frame wrote it AND the camera matrix is
    // continuous (TemporalDecision); otherwise this frame's filtered volume restarts it.
    const bool temporal = in.temporalWanted && EnsureHistory();
    const bool blendHistory = temporal && m_historyValid && in.historyReusable;
    const uint32_t historyRead  = 1u - m_historyWrite;
    const uint32_t historyWrite = m_historyWrite;

    // ── Upload CB ─────────────────────────────────────────────────────
    CBAllocation fogAlloc = cbRing.Allocate(sizeof(VolumetricFogCB));
    VolumetricFogCB fogCB = params;
    fogCB.inscatteringUavIdx = m_inscatteringUav;
    fogCB.inscatteringSrvIdx = m_inscatteringSrv;
    fogCB.integratedUavIdx   = m_integratedUav;
    fogCB.integratedSrvIdx   = m_integratedSrv;
    fogCB.gridX = kFroxelGridX;
    fogCB.gridY = kFroxelGridY;
    fogCB.gridZ = kFroxelGridZ;
    fogCB.historySrvIdx   = temporal ? m_historySrv[historyRead] : UINT32_MAX;
    fogCB.historyUavIdx   = temporal ? m_historyUav[historyWrite] : UINT32_MAX;
    fogCB.temporalEnabled = blendHistory ? 1u : 0u;
    fogCB.jitterEnabled   = (temporal && params.jitterEnabled != 0u) ? 1u : 0u;
    const bool localLights = params.localLightsEnabled != 0u && in.lightingAlloc.gpuAddr != 0;
    fogCB.localLightsEnabled  = localLights ? 1u : 0u;
    fogCB.localShadowEnabled  = (localLights && params.localShadowEnabled != 0u) ? 1u : 0u;
    std::memcpy(fogAlloc.cpuPtr, &fogCB, sizeof(VolumetricFogCB));

    // Import 3D textures into the render graph
    rg::RgResourceId inscatterRes  = rgBuilder.ImportTrackedTexture("FroxelFog_Inscatter", m_inscattering);
    rg::RgResourceId integratedRes = rgBuilder.ImportTrackedTexture("FroxelFog_Integrated", m_integrated);
    rg::RgResourceId historyReadRes{};
    rg::RgResourceId historyWriteRes{};
    if (temporal) {
        historyReadRes  = rgBuilder.ImportTrackedTexture(historyRead == 0 ? "FroxelFog_History0" : "FroxelFog_History1",
                                                         m_history[historyRead]);
        historyWriteRes = rgBuilder.ImportTrackedTexture(historyWrite == 0 ? "FroxelFog_History0" : "FroxelFog_History1",
                                                         m_history[historyWrite]);
    }

    const rg::RgResourceId hdrRes = in.hdrRes;
    const rg::RgResourceId depthRes = in.depthRes;
    const rg::RgResourceId shadowAtlasRes = in.shadowAtlasRes;
    const rg::RgResourceId localShadowAtlasRes = in.localShadowAtlasRes;
    const CBAllocation perFrameAlloc = in.perFrameAlloc;
    const CBAllocation shadowAlloc = in.shadowAlloc;
    const CBAllocation lightingAlloc = in.lightingAlloc;
    const float vpWidth = in.vpWidth;
    const float vpHeight = in.vpHeight;

    // ── Phase 1: Inject — density + light per froxel ─────────────────
    auto injectPipeline = pipelines->GetVolumetricFogInject();
    if (injectPipeline.IsValid()) {
        rgBuilder.AddPass("FroxelFog_Inject", [=](rg::RgPassBuilder& pb) {
            pb.Write(inscatterRes, RHIResourceState::UnorderedAccess);
            // The sun in-scatter samples the cascaded shadow atlas and the local lights sample the
            // local atlas: declare the reads so the graph orders this pass after the shadow passes.
            if (shadowAtlasRes.IsValid()) pb.Read(shadowAtlasRes, RHIResourceState::ShaderResource);
            if (localLights && localShadowAtlasRes.IsValid()) pb.Read(localShadowAtlasRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("FroxelFog::Inject");
                cmd->SetPipeline(injectPipeline);
                // LightingCB (point + spot lights) at b1: the per-frame slot, which the fog shaders do
                // not otherwise bind (b3 is pixel-visible only in the shared root signature).
                if (localLights) cmd->SetRootCBV(kSlotPerFrame, lightingAlloc.gpuAddr);
                cmd->SetRootCBV(kSlotPerObject, fogAlloc.gpuAddr);
                if (shadowAlloc.gpuAddr != 0) cmd->SetRootCBV(kSlotShadow, shadowAlloc.gpuAddr);   // ShadowDataCB at b4
                // Dispatch: ceil(160/8) x ceil(90/8) x ceil(64/4): one thread per froxel
                cmd->Dispatch(
                    (kFroxelGridX + 7) / 8,
                    (kFroxelGridY + 7) / 8,
                    (kFroxelGridZ + 3) / 4);
            });
        });
    }

    // ── Phase 2: Scatter — temporal filter + front-to-back integration per column ───
    auto scatterPipeline = pipelines->GetVolumetricFogScatter();
    if (scatterPipeline.IsValid()) {
        rgBuilder.AddPass("FroxelFog_Scatter", [=](rg::RgPassBuilder& pb) {
            pb.Read(inscatterRes, RHIResourceState::ShaderResource);
            pb.Write(integratedRes, RHIResourceState::UnorderedAccess);
            if (temporal) {
                pb.Read(historyReadRes, RHIResourceState::ShaderResource);
                pb.Write(historyWriteRes, RHIResourceState::UnorderedAccess);
            }

            pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("FroxelFog::Scatter");
                cmd->SetPipeline(scatterPipeline);
                cmd->SetRootCBV(kSlotPerObject, fogAlloc.gpuAddr);
                cmd->Dispatch(
                    (kFroxelGridX + 7) / 8,
                    (kFroxelGridY + 7) / 8,
                    1);
            });
        });
    }

    // ── Phase 3: Composite — sample integrated volume at scene depth ──
    auto compositePipeline = pipelines->GetVolumetricFogComposite();
    if (compositePipeline.IsValid()) {
        rgBuilder.AddPass("FroxelFog_Composite", [=](rg::RgPassBuilder& pb) {
            pb.Write(hdrRes, RHIResourceState::RenderTarget);
            pb.Read(depthRes, RHIResourceState::ShaderResource);
            pb.Read(integratedRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("FroxelFog::Composite");

                auto bb = res.GetTexture(hdrRes);

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear = false;
                passDesc.hasDepth = false;

                cmd->BeginRenderPass(passDesc);
                cmd->SetPipeline(compositePipeline);

                // Push root constants at b0: depth SRV + integrated volume SRV (2 of the 20 DWORDs)
                uint32_t compositeRC[2] = { fogCB.depthSrvIdx, fogCB.integratedSrvIdx };
                cmd->SetRootConstants(kSlotRootConstants, compositeRC, 2);

                cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.gpuAddr);
                cmd->SetRootCBV(kSlotPerObject, fogAlloc.gpuAddr);

                RHIViewport vp{};
                vp.width = vpWidth; vp.height = vpHeight; vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);
                RHIScissorRect sc{};
                sc.right = static_cast<int32_t>(vpWidth);
                sc.bottom = static_cast<int32_t>(vpHeight);
                cmd->SetScissor(sc);

                cmd->Draw(3);
                cmd->EndRenderPass();
            });
        });
    }

    // The scatter wrote m_history[historyWrite] this frame (when temporal); it is next frame's read.
    if (temporal && scatterPipeline.IsValid()) {
        m_historyValid = true;
        m_historyWrite = historyRead;
    } else {
        m_historyValid = false;
    }

    // First-frame diagnostic
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "VolumetricFogPass: first registration — "
               "inject={}, scatter={}, composite={}, grid={}x{}x{}, temporal={}, localLights={}",
               injectPipeline.IsValid(), scatterPipeline.IsValid(),
               compositePipeline.IsValid(),
               kFroxelGridX, kFroxelGridY, kFroxelGridZ, temporal, localLights);
        loggedOnce = true;
    }
}

} // namespace uz
