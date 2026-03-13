/// UzEngine PBR Pass — Implementation
///
/// Registers and executes a forward PBR rendering pass that iterates a sorted
/// DrawList, binding per-object constant buffers and issuing indexed draw calls.

#include "pbr_pass.h"

#include "constant_buffers.h"
#include "draw_list.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddPBRPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddPBRPass(rg::RgBuilder& builder, const PBRPassDesc& desc)
{
    UZ_CHECKF(desc.drawList != nullptr, "PBRPass: drawList must not be null");
    UZ_CHECKF(desc.cbAllocator != nullptr, "PBRPass: cbAllocator must not be null");

    builder.AddPass("ForwardPBR", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Write(desc.depthBuffer, RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("ForwardPBR::Execute");

            auto bb = res.GetTexture(desc.backbuffer);

            // ── Clear render targets ─────────────────────────────────────
            constexpr float kClearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
            cmd->ClearRenderTarget(bb, kClearColor);
            cmd->ClearDepthStencil(desc.depthTexture, 1.0f, 0);

            // ── Begin render pass ────────────────────────────────────────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = desc.depthTexture;
            passDesc.depthAttachment.clearDepth = 1.0f;
            passDesc.depthAttachment.clear      = false;
            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline and shared state ───────────────────────────
            cmd->SetPipeline(desc.pipeline);

            if (desc.bindless) {
                cmd->SetDescriptorIndex(0, desc.vertexBufferSrvIndex);
            } else {
                cmd->SetVertexBuffer(0, desc.vertexBuffer, 0);
            }

            cmd->SetIndexBuffer(desc.indexBuffer, desc.indexFormat, 0);

            cmd->SetRootCBV(1, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, sizeof(PerFrameCB));

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
                           "PBRPass: CB ring exhausted at draw command {}", drawCallCount);
                    break;
                }
                std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

                cmd->SetRootCBV(2, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                objAlloc.cpuPtr, sizeof(PerObjectCB));
                cmd->DrawIndexed(desc.indexCount);
                ++drawCallCount;
            }

            cmd->EndRenderPass();

            // Draw call count available via stat counters — no per-frame log.
        });
    });
}

} // namespace uz
