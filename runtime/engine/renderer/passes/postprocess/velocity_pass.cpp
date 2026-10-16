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
#include "draw_list.h"
#include "gpu_mesh.h"
#include "cb_ring_allocator.h"

#include <cstring>

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

void AddVelocityObjectPass(rg::RgBuilder& builder, const VelocityObjectPassDesc& desc)
{
    builder.AddPass("VelocityObjects", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.velocityTarget, RHIResourceState::RenderTarget);
        pb.Read(desc.depthBuffer, RHIResourceState::DepthStencilRead);
        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            RHIRenderPassDesc pass{};
            pass.colorAttachmentCount = 1;
            pass.colorAttachments[0].texture = res.GetTexture(desc.velocityTarget);
            pass.hasDepth = true;
            pass.depthAttachment.texture = res.GetTexture(desc.depthBuffer);
            cmd->BeginRenderPass(pass);
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                desc.perFrameAlloc.offsetBytes, desc.perFrameAlloc.cpuPtr,
                desc.perFrameAlloc.sizeBytes);
            RHIViewport viewport{};
            viewport.width = desc.viewportWidth;
            viewport.height = desc.viewportHeight;
            viewport.maxDepth = 1.f;
            cmd->SetViewport(viewport);
            RHIScissorRect scissor{};
            scissor.right = static_cast<int32_t>(desc.viewportWidth);
            scissor.bottom = static_cast<int32_t>(desc.viewportHeight);
            cmd->SetScissor(scissor);

            for (const auto& draw : desc.drawList->GetCommands()) {
                // Translucent/additive and mesh decals do not own the G-buffer depth.
                if (!draw.hasObjectMotion || !IsGIOccluder(draw)
                    || draw.meshId >= desc.meshCount) continue;
                const auto& mesh = desc.meshes[draw.meshId];
                if (!mesh.IsValid()) continue;
                const bool twoSided = draw.twoSided != 0 || (draw.materialFlags & 0x8u) != 0
                    || (!draw.materialGraphPipeline.IsValid() && (draw.materialFlags & 3u) == 1u);
                const auto pipeline = twoSided ? desc.twoSidedPipeline : desc.pipeline;
                if (!pipeline.IsValid()) continue;
                cmd->SetPipeline(pipeline);
                if (desc.bindless) {
                    const uint32_t push[4] = {mesh.vbSrvIndex,
                        mesh.vertexStride == 32 ? 1u : 0u, mesh.colorSrvIndex,
                        draw.prevVertexBufferSrvIndex};
                    cmd->SetRootConstants(kSlotRootConstants, push, 4);
                } else {
                    cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                }
                VelocityObjectCB constants{};
                constants.model = draw.worldMatrix;
                constants.prevModel = draw.prevWorldMatrix;
                constants.unjitteredViewProj = desc.unjitteredViewProj;
                constants.prevViewProj = desc.prevViewProj;
                constants.screenSize[0] = desc.viewportWidth;
                constants.screenSize[1] = desc.viewportHeight;
                const auto allocation = desc.cbAllocator->Allocate(sizeof(constants));
                if (!allocation.cpuPtr) break;
                std::memcpy(allocation.cpuPtr, &constants, sizeof(constants));
                cmd->SetRootCBV(kSlotPerObject, allocation.bufferHandle,
                    allocation.offsetBytes, allocation.cpuPtr, allocation.sizeBytes);
                if (mesh.IsIndexed()) {
                    cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat,
                        draw.lodIndexByteOffset);
                    cmd->DrawIndexed(draw.lodIndexCount ? draw.lodIndexCount : mesh.indexCount,
                        1, 0, 0, 0);
                } else {
                    cmd->Draw(mesh.vertexCount);
                }
            }
            cmd->EndRenderPass();
        });
    });
}

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

            // ── Bind PerFrameCB at b1 (provides invViewProj for depth reconstruction) ──
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            // ── Bind VelocityCB at b2 (prevViewProj + screen dimensions) ──
            cmd->SetRootCBV(kSlotPerObject, desc.velocityCBAlloc.bufferHandle,
                            desc.velocityCBAlloc.offsetBytes,
                            desc.velocityCBAlloc.cpuPtr, sizeof(VelocityCB));

            // ── Bind depth SRV (root constants for bindless, or t0 for SM5) ──
            if (desc.bindless) {
                const uint32_t rootData[4] = {
                    desc.depthSrvIndex, 0, 0, 0
                };
                cmd->SetRootConstants(kSlotRootConstants, rootData, 4);
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
