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

        // Optional extended resources — only declared when the mode needs them.
        if (desc.hdrGBuffer.IsValid()) {
            pb.Read(desc.hdrGBuffer, RHIResourceState::ShaderResource);
        }
        if (desc.ssrGBuffer.IsValid()) {
            pb.Read(desc.ssrGBuffer, RHIResourceState::ShaderResource);
        }
        if (desc.directGBuffer.IsValid()) {
            pb.Read(desc.directGBuffer,   RHIResourceState::ShaderResource);
            pb.Read(desc.indirectGBuffer, RHIResourceState::ShaderResource);
            pb.Read(desc.shadowGBuffer,   RHIResourceState::ShaderResource);
        }

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
                // SM6.6: mode + 4 G-Buffer SRVs + hdr/ssr + the three lighting split targets +
                // GTAO. Eleven of the sixteen 32-bit root constants the signature provides.
                // Shader cbuffer layout must match exactly.
                const uint32_t constants[11] = {
                    static_cast<uint32_t>(desc.mode),
                    desc.albedoSrvIndex,
                    desc.normalSrvIndex,
                    desc.depthSrvIndex,
                    desc.positionSrvIndex,
                    desc.hdrSrvIndex,   // UINT32_MAX when unused — shader guards before load
                    desc.ssrSrvIndex,   // UINT32_MAX when unused
                    desc.directSrvIndex,
                    desc.indirectSrvIndex,
                    desc.shadowSrvIndex,
                    desc.gtaoSrvIndex,
                };
                cmd->SetRootConstants(kSlotRootConstants, constants, 11);
            } else {
                // SM5: push mode as a single root constant, bind textures explicitly.
                const uint32_t modeVal = static_cast<uint32_t>(desc.mode);
                cmd->SetRootConstants(kSlotRootConstants, &modeVal, 1);
                cmd->SetFragmentTexture(0, desc.albedoTexture);
                cmd->SetFragmentTexture(1, desc.normalTexture);
                cmd->SetFragmentTexture(2, desc.depthTexture);
                cmd->SetFragmentTexture(3, desc.positionTexture);
                // t4/t5 are optional — only bound when the mode needs them.
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
                       "DebugViewPass: first execution — mode={}, viewport={}x{}, bindless={}, "
                       "pipeline={}, albedoSRV={}, normalSRV={}, depthSRV={}, positionSRV={}",
                       static_cast<uint32_t>(desc.mode),
                       desc.viewportWidth, desc.viewportHeight, desc.bindless,
                       desc.pipeline.index,
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

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
