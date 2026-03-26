/// UzEngine HDRI Skybox Pass — Implementation.
///
/// Registers and executes a fullscreen pass that samples an HDRI cubemap
/// to produce sky color for background pixels (where depth == 0).

#include "hdri_skybox_pass.h"

#include "constant_buffers.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddHdriSkyboxPass
// ─────────────────────────────────────────────────────────────────────────────

void AddHdriSkyboxPass(rg::RgBuilder& builder, const HdriSkyboxPassDesc& desc)
{
    builder.AddPass("HdriSkybox", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Read(desc.depthGBuffer, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("HdriSkybox::Execute");

            auto bb = res.GetTexture(desc.backbuffer);

            // ── Begin render pass — 1 color attachment, no depth ─────────
            // Do NOT clear — the lighting pass already wrote the lit scene.
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline ────────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind PerFrameCB at b1 ────────────────────────────────────
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            // ── Bind depth texture + HDRI cubemap ────────────────────────
            if (desc.bindless) {
                const uint32_t indices[2] = {
                    desc.depthSrvIndex,
                    desc.cubemapSrvIndex
                };
                cmd->SetRootConstants(kSlotRootConstants, indices, 2);
            } else {
                cmd->SetFragmentTexture(0, desc.depthTexture);
                cmd->SetFragmentTexture(1, desc.cubemapTexture);
            }

            // ── Viewport and scissor ─────────────────────────────────────
            RHIViewport vp{};
            vp.width    = desc.viewportWidth;
            vp.height   = desc.viewportHeight;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(desc.viewportWidth);
            sc.bottom = static_cast<int32_t>(desc.viewportHeight);
            cmd->SetScissor(sc);

            // ── Fullscreen triangle ──────────────────────────────────────
            cmd->Draw(3);

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "HdriSkyboxPass: first execution — viewport={}x{}, bindless={}, "
                       "pipeline={}, depthSRV={}, cubemapSRV={}",
                       desc.viewportWidth, desc.viewportHeight, desc.bindless,
                       desc.pipeline.index,
                       desc.depthSrvIndex, desc.cubemapSrvIndex);
                loggedOnce = true;
            }

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
