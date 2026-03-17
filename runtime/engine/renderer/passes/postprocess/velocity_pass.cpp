/// UzEngine Velocity Buffer Generation Pass -- Implementation
///
/// Registers and executes a fullscreen pass that reads the depth buffer,
/// reconstructs world positions, and computes per-pixel screen-space motion
/// vectors by reprojecting through the previous frame's view-projection.

#include "velocity_pass.h"

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
// AddVelocityPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddVelocityPass(rg::RgBuilder& builder, const VelocityPassDesc& desc)
{
    builder.AddPass("VelocityBuffer", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.velocityTarget, RHIResourceState::RenderTarget);
        pb.Read(desc.depthBuffer, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VelocityBuffer::Execute");

            auto velocityRT = res.GetTexture(desc.velocityTarget);

            // ── Begin render pass — 1 color attachment, no depth ─────────
            // Velocity is written for every pixel by the fullscreen triangle,
            // so no clear is needed.
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = velocityRT;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth                    = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind velocity PSO ─────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind VelocityCB at b2 (root param 2) ─────────────────────
            cmd->SetRootCBV(kSlotPerObject, desc.velocityCBAlloc.bufferHandle,
                            desc.velocityCBAlloc.offsetBytes,
                            desc.velocityCBAlloc.cpuPtr, sizeof(VelocityCB));

            // ── Bind depth SRV + screen dimensions ───────────────────────
            if (desc.bindless) {
                // SM6.6: push [depthSrvIndex, screenWidth, screenHeight]
                const uint32_t rootData[3] = {
                    desc.depthSrvIndex,
                    static_cast<uint32_t>(desc.viewportWidth),
                    static_cast<uint32_t>(desc.viewportHeight),
                };
                cmd->SetRootConstants(kSlotRootConstants, rootData, 3);
            } else {
                // SM5: push screen dimensions as root constants, bind depth at t0
                const uint32_t rootData[3] = {
                    0, // unused in SM5 path
                    static_cast<uint32_t>(desc.viewportWidth),
                    static_cast<uint32_t>(desc.viewportHeight),
                };
                cmd->SetRootConstants(kSlotRootConstants, rootData, 3);
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

            // ── Fullscreen triangle — 3 vertices from SV_VertexID ────────
            cmd->Draw(3);

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "VelocityPass: first execution — viewport={}x{}, bindless={}, "
                       "pipeline={}, depthSRV={}",
                       desc.viewportWidth, desc.viewportHeight, desc.bindless,
                       desc.pipeline.index, desc.depthSrvIndex);

                UZ_LOG(Renderer, Info,
                       "VelocityPass: velocityCBAlloc valid={}, size={}, offset={}",
                       desc.velocityCBAlloc.cpuPtr != nullptr,
                       desc.velocityCBAlloc.sizeBytes,
                       desc.velocityCBAlloc.offsetBytes);

                if (desc.bindless && desc.depthSrvIndex == UINT32_MAX) {
                    UZ_LOG(Renderer, Error,
                           "VelocityPass: depthSrvIndex is UINT32_MAX in bindless mode");
                }
                loggedOnce = true;
            }

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
