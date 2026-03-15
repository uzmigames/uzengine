/// UzEngine Sky Atmosphere Pass — Implementation.
///
/// Registers and executes a fullscreen pass that ray-marches through the
/// atmosphere to produce physical sky color for background pixels.

#include "sky_atmosphere_pass.h"

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
// AddSkyAtmospherePass
// ─────────────────────────────────────────────────────────────────────────────

void AddSkyAtmospherePass(rg::RgBuilder& builder, const SkyAtmospherePassDesc& desc)
{
    builder.AddPass("SkyAtmosphere", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Read(desc.depthGBuffer, RHIResourceState::ShaderResource);
        // Declare LUT as read so the render graph emits the UAV→SRV barrier after the compute pass.
        if (desc.transmittanceLutRes.IsValid())
            pb.Read(desc.transmittanceLutRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("SkyAtmosphere::Execute");

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

            // ── Bind constant buffers ────────────────────────────────────
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            cmd->SetRootCBV(kSlotPerObject, desc.skyAtmosphereAlloc.bufferHandle,
                            desc.skyAtmosphereAlloc.offsetBytes,
                            desc.skyAtmosphereAlloc.cpuPtr, sizeof(SkyAtmosphereCB));

            // ── Bind depth texture + transmittance LUT ──────────────────
            if (desc.bindless) {
                const uint32_t indices[4] = {
                    desc.depthSrvIndex,
                    desc.transmittanceLutSrvIndex,
                    0, 0
                };
                cmd->SetRootConstants(kSlotRootConstants, indices, 4);
            } else {
                cmd->SetFragmentTexture(0, desc.depthTexture);
                // t1: transmittance LUT (non-bindless, SM5 path)
                // Note: SM5 sky shader uses USE_TRANSMITTANCE_LUT define — if LUT not
                // available (UINT32_MAX) the shader falls back to the ray-march path.
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
