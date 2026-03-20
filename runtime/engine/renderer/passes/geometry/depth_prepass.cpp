/// UzEngine Depth Prepass — Implementation
///
/// Registers and executes a depth-only prepass that populates the GBuffer depth
/// buffer before the main GBuffer pass. This enables Equal depth testing in the
/// GBuffer pass to eliminate overdraw.
///
/// @ref FDepthPassMeshProcessor / RenderPrePass

#include "depth_prepass.h"

#include "constant_buffers.h"
#include "draw_list.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "shader_parameter_binder.h"
#include "shadow_pass_params.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddDepthPrepass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddDepthPrepass(rg::RgBuilder& builder, const DepthPrepassDesc& desc)
{
    UZ_CHECKF(desc.drawList != nullptr, "DepthPrepass: drawList must not be null");
    UZ_CHECKF(desc.cbAllocator != nullptr, "DepthPrepass: cbAllocator must not be null");

    builder.AddPass("DepthPrepass", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.depthBuffer, RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("DepthPrepass::Execute");

            // ── Clear depth to reversed-Z far plane ───────────────────────
            cmd->ClearDepthStencil(desc.depthTexture, kDepthFar, 0);

            // ── Depth-only render pass (0 color attachments) ──────────────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 0;
            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = desc.depthTexture;
            passDesc.depthAttachment.clearDepth = kDepthFar;
            passDesc.depthAttachment.clear      = false; // Already cleared above
            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline ─────────────────────────────────────────────
            // For the instanced path, use mesh[0] as the shared geometry.
            const bool useInstanced = desc.instanced && desc.bindless
                                      && desc.batchData != nullptr
                                      && desc.batchCount > 0
                                      && desc.meshCount > 0
                                      && desc.meshes[0].IsValid();

            if (useInstanced) {
                const GPUMesh& mesh0 = desc.meshes[0];
                cmd->SetPipeline(desc.instancedPipeline);
                const uint32_t rootConstants[2] = {
                    mesh0.vbSrvIndex,
                    desc.instanceBufferSrvIndex
                };
                cmd->SetRootConstants(kSlotRootConstants, rootConstants, 2);
                cmd->SetIndexBuffer(mesh0.indexBuffer, mesh0.indexFormat, 0);
            } else {
                cmd->SetPipeline(desc.pipeline);
            }

            // ── Bind per-frame CB at slot 1 ───────────────────────────────
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

            // ── Viewport and scissor ──────────────────────────────────────
            RHIViewport vp{};
            vp.width    = desc.viewportWidth;
            vp.height   = desc.viewportHeight;
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(desc.viewportWidth);
            sc.bottom = static_cast<int32_t>(desc.viewportHeight);
            cmd->SetScissor(sc);

            // ── Draw loop ─────────────────────────────────────────────────
            uint32_t drawCallCount = 0;

            if (useInstanced) {
                // ── Instanced path — one draw per batch ───────────────────
                const GPUMesh& mesh0 = desc.meshes[0];
                CBRingAllocator& ringAlloc = desc.cbRingAllocator != nullptr
                                             ? *desc.cbRingAllocator : *desc.cbAllocator;

                for (uint32_t bi = 0; bi < desc.batchCount; ++bi) {
                    const auto& batch = desc.batchData[bi];
                    // Depth prepass doesn't need material — zeroed per-object params.
                    ShadowPerObjectParams perObjParams{};

                    CBAllocation objAlloc = ShaderParameterBinder::BindCB(
                        cmd, &perObjParams,
                        ShadowPerObjectParams::GetMetadata(),
                        ringAlloc, kSlotPerObject);
                    if (objAlloc.cpuPtr == nullptr) {
                        UZ_LOG(Renderer, Error,
                               "DepthPrepass(instanced): CB ring exhausted at batch {}", drawCallCount);
                        break;
                    }
                    cmd->DrawIndexed(mesh0.indexCount, batch.instanceCount,
                                     0, 0, batch.firstInstance);
                    ++drawCallCount;
                }
            } else {
                // ── Non-instanced path — per-draw mesh binding ────────────
                const auto& commands = desc.drawList->GetCommands();
                uint32_t lastBoundMeshId = UINT32_MAX;

                for (const auto& drawCmd : commands) {
                    if (drawCmd.meshId >= desc.meshCount) {
                        continue; // Guard: meshId out of range.
                    }
                    const GPUMesh& mesh = desc.meshes[drawCmd.meshId];
                    if (!mesh.IsValid()) {
                        continue; // Guard: mesh not yet uploaded.
                    }

                    // Rebind vertex/index buffers only when the mesh changes.
                    if (drawCmd.meshId != lastBoundMeshId) {
                        if (desc.bindless) {
                            cmd->SetDescriptorIndex(kSlotRootConstants, mesh.vbSrvIndex);
                        } else {
                            cmd->SetVertexBuffer(0, mesh.vertexBuffer, 0);
                        }
                        cmd->SetIndexBuffer(mesh.indexBuffer, mesh.indexFormat, 0);
                        lastBoundMeshId = drawCmd.meshId;
                    }

                    if (desc.cbRingAllocator != nullptr) {
                        // Binder path: upload per-object CB via metadata.
                        ShadowPerObjectParams perObjParams{};
                        perObjParams.model = drawCmd.worldMatrix;

                        CBAllocation objAlloc = ShaderParameterBinder::BindCB(
                            cmd, &perObjParams,
                            ShadowPerObjectParams::GetMetadata(),
                            *desc.cbRingAllocator, kSlotPerObject);
                        if (objAlloc.cpuPtr == nullptr) {
                            UZ_LOG(Renderer, Error,
                                   "DepthPrepass: CB ring exhausted at draw command {}", drawCallCount);
                            break;
                        }
                    } else {
                        // Legacy path: manual PerObjectCB + SetRootCBV.
                        PerObjectCB perObject{};
                        perObject.model = drawCmd.worldMatrix;

                        CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
                        if (objAlloc.cpuPtr == nullptr) {
                            UZ_LOG(Renderer, Error,
                                   "DepthPrepass: CB ring exhausted at draw command {}", drawCallCount);
                            break;
                        }
                        std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

                        cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                        objAlloc.cpuPtr, sizeof(PerObjectCB));
                    }

                    cmd->DrawIndexed(mesh.indexCount);
                    ++drawCallCount;
                }
            }

            cmd->EndRenderPass();

            // Log draw count once for diagnostics.
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "DepthPrepass: first execution — {} draw calls, viewport={}x{}, "
                       "pipeline={}, instanced={}, bindless={}, meshCount={}",
                       drawCallCount, desc.viewportWidth, desc.viewportHeight,
                       desc.pipeline.index, useInstanced, desc.bindless, desc.meshCount);
                if (drawCallCount == 0) {
                    UZ_LOG(Renderer, Warning,
                           "DepthPrepass: zero draw calls on first frame — "
                           "drawList has {} commands", desc.drawList->GetCommands().size());
                }
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
