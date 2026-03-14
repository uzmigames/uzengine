/// UzEngine G-Buffer Pass — Implementation
///
/// Registers and executes a deferred G-Buffer pass that iterates a sorted
/// DrawList, binding per-object constant buffers and issuing indexed draw calls
/// into two MRT color targets plus depth.

#include "gbuffer_pass.h"

#include "constant_buffers.h"
#include "multi_light.h"
#include "draw_list.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddGBufferPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddGBufferPass(rg::RgBuilder& builder, const GBufferPassDesc& desc)
{
    UZ_CHECKF(desc.drawList != nullptr, "GBufferPass: drawList must not be null");
    UZ_CHECKF(desc.cbAllocator != nullptr, "GBufferPass: cbAllocator must not be null");

    builder.AddPass("GBuffer", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.albedoTarget, RHIResourceState::RenderTarget);
        pb.Write(desc.normalTarget, RHIResourceState::RenderTarget);
        pb.Write(desc.depthBuffer, RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("GBuffer::Execute");

            auto albedo = res.GetTexture(desc.albedoTarget);
            auto normal = res.GetTexture(desc.normalTarget);

            // ── Clear render targets ─────────────────────────────────────
            constexpr float kClearBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            cmd->ClearRenderTarget(albedo, kClearBlack);
            cmd->ClearRenderTarget(normal, kClearBlack);
            cmd->ClearDepthStencil(desc.depthTexture, 1.0f, 0);

            // ── Begin render pass with 2 color attachments + depth ───────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 2;

            passDesc.colorAttachments[0].texture = albedo;
            passDesc.colorAttachments[0].clear   = false;

            passDesc.colorAttachments[1].texture = normal;
            passDesc.colorAttachments[1].clear   = false;

            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = desc.depthTexture;
            passDesc.depthAttachment.clearDepth = 1.0f;
            passDesc.depthAttachment.clear      = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline and shared state ───────────────────────────
            cmd->SetPipeline(desc.pipeline);

            if (desc.bindless) {
                cmd->SetDescriptorIndex(kSlotRootConstants, desc.vertexBufferSrvIndex);
            } else {
                cmd->SetVertexBuffer(0, desc.vertexBuffer, 0);
            }

            cmd->SetIndexBuffer(desc.indexBuffer, desc.indexFormat, 0);

            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, sizeof(PerFrameMultiLightCB));

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

            // ── Draw loop — iterate sorted draw commands ─────────────────
            const auto& commands = desc.drawList->GetCommands();
            uint32_t drawCallCount = 0;

            for (const auto& drawCmd : commands) {
                PerObjectCB perObject{};
                perObject.model        = drawCmd.worldMatrix;
                perObject.normalMatrix = drawCmd.normalMatrix;
                perObject.albedoIndex    = drawCmd.albedoSrvIndex;
                perObject.normalIndex    = drawCmd.normalSrvIndex;
                perObject.roughnessIndex = drawCmd.roughnessSrvIndex;
                perObject.metallicIndex  = drawCmd.metallicSrvIndex;
                perObject.aoIndex        = drawCmd.aoSrvIndex;

                CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
                if (objAlloc.cpuPtr == nullptr) {
                    UZ_LOG(Renderer, Error,
                           "GBufferPass: CB ring exhausted at draw command {}", drawCallCount);
                    break;
                }
                std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

                cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                objAlloc.cpuPtr, sizeof(PerObjectCB));

                // SM5: bind material textures at t0-t4 for non-bindless path
                if (!desc.bindless) {
                    cmd->SetFragmentTexture(0, drawCmd.albedoTexture);
                    cmd->SetFragmentTexture(1, drawCmd.normalTexture);
                    cmd->SetFragmentTexture(2, drawCmd.roughnessTexture);
                    cmd->SetFragmentTexture(3, drawCmd.metallicTexture);
                    cmd->SetFragmentTexture(4, drawCmd.aoTexture);
                }

                cmd->DrawIndexed(desc.indexCount);
                ++drawCallCount;
            }

            cmd->EndRenderPass();

            // Draw call count available via stat counters — no per-frame log.
        });
    });
}

} // namespace uz
