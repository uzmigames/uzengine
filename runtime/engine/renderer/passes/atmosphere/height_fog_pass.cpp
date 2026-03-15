/// UzEngine Height Fog Pass — Implementation.
///
/// Registers and executes a fullscreen fog compositing pass using alpha
/// blending. The shader computes fog opacity per pixel and outputs it as
/// alpha; the PSO's blend state handles the compositing.

#include "height_fog_pass.h"

#include "constant_buffers.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddHeightFogPass
// ─────────────────────────────────────────────────────────────────────────────

void AddHeightFogPass(rg::RgBuilder& builder, const HeightFogPassDesc& desc)
{
    builder.AddPass("HeightFog", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Read(desc.depthGBuffer, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("HeightFog::Execute");

            auto bb = res.GetTexture(desc.backbuffer);

            // ── Begin render pass — 1 color attachment, no depth ─────────
            // Do NOT clear — fog is alpha-blended over the existing scene.
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline (includes alpha blend state) ───────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind constant buffers ────────────────────────────────────
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            cmd->SetRootCBV(kSlotPerObject, desc.fogAlloc.bufferHandle,
                            desc.fogAlloc.offsetBytes,
                            desc.fogAlloc.cpuPtr, sizeof(HeightFogCB));

            // ── Bind depth texture ───────────────────────────────────────
            if (desc.bindless) {
                const uint32_t indices[4] = {
                    desc.depthSrvIndex, 0, 0, 0
                };
                cmd->SetRootConstants(kSlotRootConstants, indices, 4);
            } else {
                cmd->SetFragmentTexture(0, desc.depthTexture);
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

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
