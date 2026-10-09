/// VFX volumetric render passes — implementation (phase 9.10.0).

#include "vfx_volumetric_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Thread group size matching [numthreads(64, 1, 1)] in the splat CS.
static constexpr uint32_t kSplatThreadGroupSize = 64;

/// Root parameter slot for per-dispatch CB (b0).
static constexpr uint32_t kSlotCB = 0;

/// Root parameter slot for bindless indices (b1, b2 in bindless path).
static constexpr uint32_t kSlotBindless1 = 1;
static constexpr uint32_t kSlotBindless2 = 2;

/// Root parameter slot for UAV descriptor table (u0..u3).
static constexpr uint32_t kSlotUAVTable = 4;

/// Root parameter slot for SRV descriptor table (t0..tN).
static constexpr uint32_t kSlotSRVTable = 5;

static uint32_t DivRoundUp(uint32_t count, uint32_t groupSize) {
    if (count == 0) return 0;
    return (count + groupSize - 1) / groupSize;
}

// ─────────────────────────────────────────────────────────────────────────────
// AddVfxVolumetricSplatPass
// ─────────────────────────────────────────────────────────────────────────────

void AddVfxVolumetricSplatPass(rg::RgBuilder& builder,
                                const VfxVolumetricSplatPassDesc& desc) {
    UZ_CHECKF(desc.splatPipeline.IsValid(),
              "VfxVolumetricSplatPass: pipeline handle is invalid");
    UZ_CHECKF(desc.densityVolume.IsValid(),
              "VfxVolumetricSplatPass: density volume resource is invalid");
    UZ_CHECKF(desc.cbRing != nullptr,
              "VfxVolumetricSplatPass: CB ring allocator is null");

    auto passDesc = desc;

    builder.AddPass("VFX_VolumetricSplat", [passDesc](rg::RgPassBuilder& pb) {
        // Density volume: UAV write (splat accumulates into it).
        pb.Write(passDesc.densityVolume, RHIResourceState::UnorderedAccess);

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                            const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::VolumetricSplat");

            if (passDesc.emitters.empty()) return;

            cmd->SetPipeline(passDesc.splatPipeline);

            for (const auto& emitter : passDesc.emitters) {
                if (emitter.aliveCount == 0) continue;

                // Fill VolumetricSplatCB.
                VfxVolumetricSplatCB cb{};
                cb.gridCenterX       = emitter.config.gridCenter.x;
                cb.gridCenterY       = emitter.config.gridCenter.y;
                cb.gridCenterZ       = emitter.config.gridCenter.z;
                cb.halfExtent        = emitter.config.halfExtent;
                cb.gridResolution    = emitter.config.gridResolution;
                cb.densityPerParticle= emitter.config.densityPerParticle;
                cb.aliveCount        = emitter.aliveCount;
                cb._pad0             = 0;

                auto cbAlloc = passDesc.cbRing->Allocate(sizeof(cb));
                if (cbAlloc.cpuPtr) {
                    std::memcpy(cbAlloc.cpuPtr, &cb, sizeof(cb));
                }
                cmd->SetRootCBV(kSlotCB, cbAlloc.gpuAddr);

                // Bindless path: set UAV index (density volume) and SRV index
                // (particle positions) via root constants.
                const uint32_t uavIdx = passDesc.densityVolumeUavIndex;
                cmd->SetRootConstants(kSlotBindless1, &uavIdx, 1, 0);
                const uint32_t srvIdx = emitter.positionSrvIndex;
                cmd->SetRootConstants(kSlotBindless2, &srvIdx, 1, 0);

                const uint32_t groups = DivRoundUp(emitter.aliveCount,
                                                     kSplatThreadGroupSize);
                cmd->Dispatch(groups, 1, 1);
            }

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "VfxVolumetricSplatPass: first dispatch — {} emitters",
                       passDesc.emitters.size());
                loggedOnce = true;
            }
        });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// AddVfxVolumetricRaymarchPass
// ─────────────────────────────────────────────────────────────────────────────

void AddVfxVolumetricRaymarchPass(rg::RgBuilder& builder,
                                    const VfxVolumetricRaymarchPassDesc& desc) {
    UZ_CHECKF(desc.raymarchPipeline.IsValid(),
              "VfxVolumetricRaymarchPass: pipeline handle is invalid");
    UZ_CHECKF(desc.densityVolume.IsValid(),
              "VfxVolumetricRaymarchPass: density volume resource is invalid");
    UZ_CHECKF(desc.colorTarget.IsValid(),
              "VfxVolumetricRaymarchPass: color target is invalid");
    UZ_CHECKF(desc.cbRing != nullptr,
              "VfxVolumetricRaymarchPass: CB ring allocator is null");

    auto passDesc = desc;

    builder.AddPass("VFX_VolumetricRaymarch", [passDesc](rg::RgPassBuilder& pb) {
        // Density volume: SRV read (the splat pass wrote it).
        pb.Read(passDesc.densityVolume, RHIResourceState::ShaderResource);

        // Color target: read-modify-write (alpha blend over existing scene).
        pb.Read(passDesc.colorTarget, RHIResourceState::RenderTarget);
        pb.Write(passDesc.colorTarget, RHIResourceState::RenderTarget);

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                            const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::VolumetricRaymarch");

            // Fill VolumetricRaymarchCB.
            VfxVolumetricRaymarchCB cb{};
            for (int i = 0; i < 16; ++i) cb.invViewProj[i] = passDesc.invViewProj[i];
            cb.cameraPosX      = passDesc.cameraPosX;
            cb.cameraPosY      = passDesc.cameraPosY;
            cb.cameraPosZ      = passDesc.cameraPosZ;
            cb.halfExtent      = passDesc.config.halfExtent;
            cb.gridCenterX     = passDesc.config.gridCenter.x;
            cb.gridCenterY     = passDesc.config.gridCenter.y;
            cb.gridCenterZ     = passDesc.config.gridCenter.z;
            cb.gridResolution  = passDesc.config.gridResolution;
            cb.sunDirX         = passDesc.sunDirX;
            cb.sunDirY         = passDesc.sunDirY;
            cb.sunDirZ         = passDesc.sunDirZ;
            cb.extinctionCoeff = passDesc.config.extinctionCoeff;
            cb.scatteringColorR = passDesc.config.scatteringColor.x;
            cb.scatteringColorG = passDesc.config.scatteringColor.y;
            cb.scatteringColorB = passDesc.config.scatteringColor.z;
            cb.hgAnisotropy    = passDesc.config.hgAnisotropy;
            cb.raymarchSteps   = passDesc.config.raymarchSteps;
            cb._pad0 = cb._pad1 = cb._pad2 = 0;

            auto cbAlloc = passDesc.cbRing->Allocate(sizeof(cb));
            if (cbAlloc.cpuPtr) {
                std::memcpy(cbAlloc.cpuPtr, &cb, sizeof(cb));
            }

            // Bind render target for fullscreen draw.
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture = passDesc.colorTexture;
            rpDesc.colorAttachments[0].clear   = false;
            rpDesc.hasDepth = false;
            cmd->BeginRenderPass(rpDesc);

            // Viewport + scissor.
            {
                RHIViewport vp{};
                vp.width    = static_cast<float>(passDesc.renderWidth);
                vp.height   = static_cast<float>(passDesc.renderHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);
                RHIScissorRect sr{};
                sr.right  = static_cast<int32_t>(passDesc.renderWidth);
                sr.bottom = static_cast<int32_t>(passDesc.renderHeight);
                cmd->SetScissor(sr);
            }

            cmd->SetPipeline(passDesc.raymarchPipeline);
            cmd->SetRootCBV(kSlotCB, cbAlloc.gpuAddr);

            // Bindless: set density volume SRV index.
            const uint32_t srvIdx = passDesc.densityVolumeSrvIndex;
            cmd->SetRootConstants(kSlotBindless1, &srvIdx, 1, 0);

            // Fullscreen triangle (3 vertices, no VB/IB — UV generated in VS).
            cmd->Draw(3, 1, 0, 0);

            cmd->EndRenderPass();

            // Log first execution for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "VfxVolumetricRaymarchPass: first draw — {}x{}, grid={}³",
                       passDesc.renderWidth, passDesc.renderHeight,
                       passDesc.config.gridResolution);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
