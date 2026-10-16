/// UzEngine Volumetric Fog Pass — Implementation.
///
/// Creates 3D froxel textures and registers inject + scatter + composite
/// passes in the render graph.
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

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool VolumetricFogPass::Init(IRHIDevice* device)
{
    if (m_initialized) return true;
    if (!device) return false;

    // ── Inscattering + extinction (inject output) ─────────────────────
    // RGBA16F: RGB = inscattering, A = extinction (density × extinctionScale)
    {
        RHITextureDesc desc{};
        desc.width          = kFroxelGridX;
        desc.height         = kFroxelGridY;
        desc.depthOrArraySize = kFroxelGridZ;
        desc.is3D           = true;
        desc.format         = RHIFormat::RGBA16Float;
        desc.usage          = RHITextureUsage::ShaderResource
                            | RHITextureUsage::UnorderedAccess;
        desc.debugName      = "FroxelFog_Inscattering";

        m_inscattering = device->CreateTexture(desc);
        if (!m_inscattering.IsValid()) {
            UZ_LOG(Renderer, Error, "VolumetricFogPass: failed to create inscattering 3D texture");
            return false;
        }
        m_inscatteringSrv = device->GetTextureSRVIndex(m_inscattering);
        m_inscatteringUav = device->GetTextureUAVIndex(m_inscattering);
    }

    // ── Integrated inscattering + transmittance (scatter output) ──────
    // RGBA16F: RGB = accumulated inscattering, A = accumulated transmittance
    {
        RHITextureDesc desc{};
        desc.width          = kFroxelGridX;
        desc.height         = kFroxelGridY;
        desc.depthOrArraySize = kFroxelGridZ;
        desc.is3D           = true;
        desc.format         = RHIFormat::RGBA16Float;
        desc.usage          = RHITextureUsage::ShaderResource
                            | RHITextureUsage::UnorderedAccess;
        desc.debugName      = "FroxelFog_Integrated";

        m_integrated = device->CreateTexture(desc);
        if (!m_integrated.IsValid()) {
            UZ_LOG(Renderer, Error, "VolumetricFogPass: failed to create integrated 3D texture");
            device->DestroyTexture(m_inscattering);
            m_inscattering = {};
            return false;
        }
        m_integratedSrv = device->GetTextureSRVIndex(m_integrated);
        m_integratedUav = device->GetTextureUAVIndex(m_integrated);
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
    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// RegisterPasses — inject + scatter + composite
// ─────────────────────────────────────────────────────────────────────────────

void VolumetricFogPass::RegisterPasses(
    rg::RgBuilder& rgBuilder,
    rg::RgResourceId hdrRes,
    rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc,
    const VolumetricFogCB& params,
    CBRingAllocator& cbRing,
    PipelineLibrary* pipelines,
    bool bindless,
    float vpWidth, float vpHeight,
    rg::RgResourceId shadowAtlasRes,
    const CBAllocation& shadowAlloc)
{
    if (!m_initialized || !bindless) return;

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
    std::memcpy(fogAlloc.cpuPtr, &fogCB, sizeof(VolumetricFogCB));

    // Import 3D textures into render graph
    rg::RgResourceId inscatterRes = rgBuilder.ImportTrackedTexture("FroxelFog_Inscatter",
        m_inscattering);
    rg::RgResourceId integratedRes = rgBuilder.ImportTrackedTexture("FroxelFog_Integrated",
        m_integrated);

    // ── Phase 1: Inject — evaluate density + light per froxel ─────────
    auto injectPipeline = pipelines->GetVolumetricFogInject();
    if (injectPipeline.IsValid()) {
        rgBuilder.AddPass("FroxelFog_Inject", [=](rg::RgPassBuilder& pb) {
            pb.Write(inscatterRes, RHIResourceState::UnorderedAccess);
            // The sun in-scatter samples the cascaded shadow atlas: declare the read so the graph
            // orders this pass after the shadow pass (block B.4).
            if (shadowAtlasRes.IsValid()) pb.Read(shadowAtlasRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("FroxelFog::Inject");
                cmd->SetPipeline(injectPipeline);
                cmd->SetRootCBV(2, fogAlloc.gpuAddr);
                if (shadowAlloc.gpuAddr != 0) cmd->SetRootCBV(kSlotShadow, shadowAlloc.gpuAddr);   // ShadowDataCB at b4
                // Dispatch: ceil(160/8) × ceil(90/8) × ceil(64/4): one thread per froxel
                cmd->Dispatch(
                    (kFroxelGridX + 7) / 8,
                    (kFroxelGridY + 7) / 8,
                    (kFroxelGridZ + 3) / 4);
            });
        });
    }

    // ── Phase 2: Scatter — front-to-back integration per column ───────
    auto scatterPipeline = pipelines->GetVolumetricFogScatter();
    if (scatterPipeline.IsValid()) {
        rgBuilder.AddPass("FroxelFog_Scatter", [=](rg::RgPassBuilder& pb) {
            pb.Read(inscatterRes, RHIResourceState::ShaderResource);
            pb.Write(integratedRes, RHIResourceState::UnorderedAccess);

            pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("FroxelFog::Scatter");
                cmd->SetPipeline(scatterPipeline);
                cmd->SetRootCBV(2, fogAlloc.gpuAddr);
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
        rgBuilder.AddPass("FroxelFog_Composite", [=, &perFrameAlloc](rg::RgPassBuilder& pb) {
            pb.Write(hdrRes, RHIResourceState::RenderTarget);
            pb.Read(depthRes, RHIResourceState::ShaderResource);
            pb.Read(integratedRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([=, &perFrameAlloc](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("FroxelFog::Composite");

                auto bb = res.GetTexture(hdrRes);

                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments[0].texture = bb;
                passDesc.colorAttachments[0].clear = false;
                passDesc.hasDepth = false;

                cmd->BeginRenderPass(passDesc);
                cmd->SetPipeline(compositePipeline);

                // Push root constants at b0: depth SRV + integrated volume SRV
                uint32_t compositeRC[2] = { fogCB.depthSrvIdx, fogCB.integratedSrvIdx };
                cmd->SetRootConstants(0, compositeRC, 2);

                cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.gpuAddr);
                cmd->SetRootCBV(2, fogAlloc.gpuAddr);

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

    // First-frame diagnostic
    static bool loggedOnce = false;
    if (!loggedOnce) {
        UZ_LOG(Renderer, Info,
               "VolumetricFogPass: first registration — "
               "inject={}, scatter={}, composite={}, grid={}x{}x{}",
               injectPipeline.IsValid(), scatterPipeline.IsValid(),
               compositePipeline.IsValid(),
               kFroxelGridX, kFroxelGridY, kFroxelGridZ);
        loggedOnce = true;
    }
}

} // namespace uz
