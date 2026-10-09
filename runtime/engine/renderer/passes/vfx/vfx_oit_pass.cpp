/// VFX OIT accumulation + resolve passes — implementation (phase 9.11.0).

#include "vfx_oit_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root parameter slot for bindless indices (b0).
static constexpr uint32_t kSlotBindlessIndices = 0;

/// Root parameter slot for per-frame CB (b1).
static constexpr uint32_t kSlotPerFrame = 1;

/// Root parameter slot for per-emitter CB (b2).
static constexpr uint32_t kSlotPerEmitter = 2;

// ─────────────────────────────────────────────────────────────────────────────
// AddVfxOITAccumPass
// ─────────────────────────────────────────────────────────────────────────────

void AddVfxOITAccumPass(rg::RgBuilder& builder,
                         const VfxOITAccumPassDesc& desc) {
    if (desc.emitters.empty()) return;

    UZ_CHECKF(desc.oitAccumPipeline.IsValid(),
              "VfxOITAccumPass: pipeline handle is invalid");
    UZ_CHECKF(desc.accumTarget.IsValid(),
              "VfxOITAccumPass: accumulation target is invalid");
    UZ_CHECKF(desc.revealageTarget.IsValid(),
              "VfxOITAccumPass: revealage target is invalid");

    auto passDesc = desc;

    builder.AddPass("VFX_OITAccum", [passDesc](rg::RgPassBuilder& pb) {
        // Accumulation: write (additive blend clears are needed first frame).
        pb.Write(passDesc.accumTarget, RHIResourceState::RenderTarget);
        pb.Write(passDesc.revealageTarget, RHIResourceState::RenderTarget);

        // Depth: read-only for depth testing (no depth write).
        if (passDesc.depthTarget.IsValid()) {
            pb.Write(passDesc.depthTarget, RHIResourceState::DepthStencilWrite);
        }

        // Indirect draw args barriers.
        for (const auto& emitter : passDesc.emitters) {
            if (emitter.rgIndirectDrawArgs.IsValid()) {
                pb.Read(emitter.rgIndirectDrawArgs, RHIResourceState::IndirectArgument);
            }
        }

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                            const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::OITAccum");

            // Begin render pass with MRT: accum (clear to black) + revealage (clear to white).
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 2;
            rpDesc.colorAttachments[0].texture = passDesc.accumTexture;
            rpDesc.colorAttachments[0].clear = true;
            rpDesc.colorAttachments[0].clearColor[0] = 0.0f;
            rpDesc.colorAttachments[0].clearColor[1] = 0.0f;
            rpDesc.colorAttachments[0].clearColor[2] = 0.0f;
            rpDesc.colorAttachments[0].clearColor[3] = 0.0f;
            rpDesc.colorAttachments[1].texture = passDesc.revealageTexture;
            rpDesc.colorAttachments[1].clear = true;
            rpDesc.colorAttachments[1].clearColor[0] = 1.0f; // revealage starts at 1 (fully transparent)
            rpDesc.colorAttachments[1].clearColor[1] = 0.0f;
            rpDesc.colorAttachments[1].clearColor[2] = 0.0f;
            rpDesc.colorAttachments[1].clearColor[3] = 0.0f;
            rpDesc.hasDepth = passDesc.depthTexture.IsValid();
            if (rpDesc.hasDepth) {
                rpDesc.depthAttachment.texture = passDesc.depthTexture;
                rpDesc.depthAttachment.clear = false;
            }
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

            cmd->SetPipeline(passDesc.oitAccumPipeline);

            // Re-bind per-frame CB.
            if (passDesc.perFrameCB.gpuAddr != 0) {
                cmd->SetRootCBV(kSlotPerFrame, passDesc.perFrameCB.gpuAddr);
            }

            // Draw each OIT-tagged emitter.
            for (const auto& emitter : passDesc.emitters) {
                // Set per-emitter CB (billboard constants).
                if (emitter.renderCB.gpuAddr != 0) {
                    cmd->SetRootCBV(kSlotPerEmitter, emitter.renderCB.gpuAddr);
                }

                // Set bindless SRV indices for particle attributes.
                const uint32_t indices[4] = {
                    emitter.positionSrvIdx,
                    emitter.sizeSrvIdx,
                    emitter.colorSrvIdx,
                    emitter.rotationSrvIdx,
                };
                cmd->SetRootConstants(kSlotBindlessIndices, indices, 4, 0);

                // DrawIndexedIndirect — same indirect buffer as the regular pass.
                if (emitter.indirectDrawArgs.IsValid()) {
                    cmd->DrawIndexedIndirect(emitter.indirectDrawArgs, 0, 1, 0);
                }
            }

            cmd->EndRenderPass();

            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "VfxOITAccumPass: first draw — {} emitters, viewport={}x{}",
                       passDesc.emitters.size(), passDesc.renderWidth,
                       passDesc.renderHeight);
                loggedOnce = true;
            }
        });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// AddVfxOITResolvePass
// ─────────────────────────────────────────────────────────────────────────────

void AddVfxOITResolvePass(rg::RgBuilder& builder,
                            const VfxOITResolvePassDesc& desc) {
    if (!desc.accumTarget.IsValid()) return; // no OIT this frame

    UZ_CHECKF(desc.resolvePipeline.IsValid(),
              "VfxOITResolvePass: pipeline handle is invalid");
    UZ_CHECKF(desc.colorTarget.IsValid(),
              "VfxOITResolvePass: color target is invalid");

    auto passDesc = desc;

    builder.AddPass("VFX_OITResolve", [passDesc](rg::RgPassBuilder& pb) {
        // Read accum + revealage as SRVs.
        pb.Read(passDesc.accumTarget, RHIResourceState::ShaderResource);
        pb.Read(passDesc.revealageTarget, RHIResourceState::ShaderResource);

        // Composite onto the HDR scene colour target.
        pb.Read(passDesc.colorTarget, RHIResourceState::RenderTarget);
        pb.Write(passDesc.colorTarget, RHIResourceState::RenderTarget);

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                            const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::OITResolve");

            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 1;
            rpDesc.colorAttachments[0].texture = passDesc.colorTexture;
            rpDesc.colorAttachments[0].clear = false;
            rpDesc.hasDepth = false;
            cmd->BeginRenderPass(rpDesc);

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

            cmd->SetPipeline(passDesc.resolvePipeline);

            // Bindless SRV indices for accum + revealage textures.
            const uint32_t accumIdx = passDesc.accumSrvIndex;
            cmd->SetRootConstants(kSlotBindlessIndices, &accumIdx, 1, 0);
            const uint32_t revealIdx = passDesc.revealageSrvIndex;
            cmd->SetRootConstants(kSlotBindlessIndices + 1, &revealIdx, 1, 0);

            // Fullscreen triangle.
            cmd->Draw(3, 1, 0, 0);

            cmd->EndRenderPass();

            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "VfxOITResolvePass: first composite — viewport={}x{}",
                       passDesc.renderWidth, passDesc.renderHeight);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
