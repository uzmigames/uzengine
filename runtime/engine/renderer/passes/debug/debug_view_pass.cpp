/// UzEngine Debug View Pass — Implementation.
///
/// Registers and executes a fullscreen pass that reads G-Buffer textures
/// and visualizes a selected debug channel into the backbuffer.

#include "debug_view_pass.h"

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
// AddDebugViewPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddDebugViewPass(rg::RgBuilder& builder, const DebugViewPassDesc& desc)
{
    UZ_CHECK(desc.mode != DebugViewMode::Lit);
    UZ_CHECK(desc.mode < DebugViewMode::Count);

    builder.AddPass("DebugView", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Read(desc.albedoGBuffer,   RHIResourceState::ShaderResource);
        pb.Read(desc.normalGBuffer,   RHIResourceState::ShaderResource);
        pb.Read(desc.depthGBuffer,    RHIResourceState::ShaderResource);
        pb.Read(desc.positionGBuffer, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("DebugView::Execute");

            auto bb = res.GetTexture(desc.backbuffer);

            // ── Clear backbuffer ─────────────────────────────────────────
            constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            cmd->ClearRenderTarget(bb, kClearColor);

            // ── Begin render pass — 1 color attachment, no depth ─────────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline ────────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind per-frame CB (near/far planes for depth linearization) ──
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            // ── Bind debug mode + G-Buffer SRV indices ───────────────────
            if (desc.bindless) {
                // SM6.6: pack mode + 4 SRV indices into 5 root constants.
                const uint32_t constants[5] = {
                    static_cast<uint32_t>(desc.mode),
                    desc.albedoSrvIndex,
                    desc.normalSrvIndex,
                    desc.depthSrvIndex,
                    desc.positionSrvIndex
                };
                cmd->SetRootConstants(kSlotRootConstants, constants, 5);
            } else {
                // SM5: push mode as a single root constant, bind textures explicitly.
                const uint32_t modeVal = static_cast<uint32_t>(desc.mode);
                cmd->SetRootConstants(kSlotRootConstants, &modeVal, 1);
                cmd->SetFragmentTexture(0, desc.albedoTexture);
                cmd->SetFragmentTexture(1, desc.normalTexture);
                cmd->SetFragmentTexture(2, desc.depthTexture);
                cmd->SetFragmentTexture(3, desc.positionTexture);
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

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "DebugViewPass: first execution — mode={}, viewport={}x{}, bindless={}, "
                       "albedoSRV={}, normalSRV={}, depthSRV={}, positionSRV={}",
                       static_cast<uint32_t>(desc.mode),
                       desc.viewportWidth, desc.viewportHeight, desc.bindless,
                       desc.albedoSrvIndex, desc.normalSrvIndex,
                       desc.depthSrvIndex, desc.positionSrvIndex);

                if (desc.bindless) {
                    if (desc.albedoSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "DebugViewPass: albedoSrvIndex is UINT32_MAX");
                    if (desc.normalSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "DebugViewPass: normalSrvIndex is UINT32_MAX");
                    if (desc.depthSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "DebugViewPass: depthSrvIndex is UINT32_MAX");
                    if (desc.positionSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "DebugViewPass: positionSrvIndex is UINT32_MAX");
                }
                loggedOnce = true;
            }

            // ── Fullscreen triangle — 3 vertices from SV_VertexID ────────
            cmd->Draw(3);

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
