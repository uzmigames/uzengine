/// UzEngine G-Buffer Pass — Implementation
///
/// Registers and executes a deferred G-Buffer pass that iterates a sorted
/// DrawList, binding per-object constant buffers and issuing indexed draw calls
/// into two MRT color targets plus depth.

#include "gbuffer_pass.h"

#include "constant_buffers.h"
#include "gbuffer_per_object_params.h"
#include "multi_light.h"
#include "draw_list.h"
#include "per_frame_params.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "shader_parameter_binder.h"
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
        pb.Write(desc.albedoTarget,   RHIResourceState::RenderTarget);
        pb.Write(desc.normalTarget,   RHIResourceState::RenderTarget);
        pb.Write(desc.positionTarget, RHIResourceState::RenderTarget);
        pb.Write(desc.depthBuffer,    RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("GBuffer::Execute");

            auto albedo    = res.GetTexture(desc.albedoTarget);
            auto normal    = res.GetTexture(desc.normalTarget);
            auto positionT = res.GetTexture(desc.positionTarget);

            // ── Clear render targets ─────────────────────────────────────
            constexpr float kClearBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            cmd->ClearRenderTarget(albedo,    kClearBlack);
            cmd->ClearRenderTarget(normal,    kClearBlack);
            cmd->ClearRenderTarget(positionT, kClearBlack);
            cmd->ClearDepthStencil(desc.depthTexture, kDepthFar, 0);

            // ── Begin render pass with 3 color attachments + depth ───────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 3;

            passDesc.colorAttachments[0].texture = albedo;
            passDesc.colorAttachments[0].clear   = false;

            passDesc.colorAttachments[1].texture = normal;
            passDesc.colorAttachments[1].clear   = false;

            passDesc.colorAttachments[2].texture = positionT;
            passDesc.colorAttachments[2].clear   = false;

            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = desc.depthTexture;
            passDesc.depthAttachment.clearDepth = kDepthFar;
            passDesc.depthAttachment.clear      = true;

            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline and shared per-frame state ─────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind per-frame CB ────────────────────────────────────────
            if (desc.perFrameParams != nullptr && desc.cbRingAllocator != nullptr) {
                // Binder path: upload PerFrameParams CB via metadata.
                // GBuffer VS only reads viewProj from b1 — no IBL textures
                // needed here, but the binder handles the full struct uniformly.
                ShaderParameterBinder::BindCB(
                    cmd, desc.perFrameParams,
                    PerFrameParams::GetMetadata(),
                    *desc.cbRingAllocator, kSlotPerFrame);
            } else {
                // Legacy path: use pre-allocated CB allocation.
                cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                                desc.perFrameAlloc.offsetBytes,
                                desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);
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

            // ── Draw loop ─────────────────────────────────────────────────
            uint32_t drawCallCount = 0;

            // Instanced path uses mesh[0] as the homogeneous batch geometry.
            // Per-batch meshId support is future work; for now all batches must
            // reference the same mesh, which the InstanceBatcher already enforces.
            const bool useInstanced = desc.instanced && desc.bindless
                                      && desc.batchData != nullptr
                                      && desc.batchCount > 0
                                      && desc.meshCount > 0
                                      && desc.meshes[0].IsValid();

            if (useInstanced) {
                // ── Instanced path — one DrawIndexedInstanced per batch ────
                const GPUMesh& mesh0 = desc.meshes[0];
                cmd->SetPipeline(desc.instancedPipeline);

                // Push both vertexBufferIndex and instanceBufferIndex as root constants.
                const uint32_t rootConstants[2] = {
                    mesh0.vbSrvIndex,
                    desc.instanceBufferSrvIndex
                };
                cmd->SetRootConstants(kSlotRootConstants, rootConstants, 2);
                cmd->SetIndexBuffer(mesh0.indexBuffer, mesh0.indexFormat, 0);

                // Select allocator: prefer cbRingAllocator (binder path), fall back to cbAllocator.
                CBRingAllocator& ringAlloc = desc.cbRingAllocator != nullptr
                                             ? *desc.cbRingAllocator : *desc.cbAllocator;

                for (uint32_t bi = 0; bi < desc.batchCount; ++bi) {
                    const auto& batch = desc.batchData[bi];
                    // Per-batch material in GBufferPerObjectParams (shared by all instances).
                    // Instanced path is bindless-only, so no SM5 texture handles needed.
                    GBufferPerObjectParams perObjParams{};
                    perObjParams.albedoIndex    = batch.albedoSrvIndex;
                    perObjParams.normalIndex    = batch.normalSrvIndex;
                    perObjParams.roughnessIndex = batch.roughnessSrvIndex;
                    perObjParams.metallicIndex  = batch.metallicSrvIndex;
                    perObjParams.aoIndex        = batch.aoSrvIndex;

                    CBAllocation objAlloc = ShaderParameterBinder::BindCB(
                        cmd, &perObjParams,
                        GBufferPerObjectParams::GetMetadata(),
                        ringAlloc, kSlotPerObject);
                    if (objAlloc.cpuPtr == nullptr) {
                        UZ_LOG(Renderer, Error,
                               "GBufferPass(instanced): CB ring exhausted at batch {}", drawCallCount);
                        break;
                    }

                    cmd->DrawIndexed(mesh0.indexCount, batch.instanceCount,
                                     0, 0, batch.firstInstance);
                    ++drawCallCount;
                }
            } else {
                // ── Non-instanced path — bind the correct mesh per draw command ──
                // Each command carries a `meshId` that indexes into `desc.meshes`.
                // Commands referencing an out-of-range or invalid mesh are skipped.
                const auto& commands = desc.drawList->GetCommands();

                // Track last bound mesh to avoid redundant VB/IB rebinds for
                // commands sorted by mesh ID (the sort key places mesh ID in LSBs).
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
                    // The DrawList is sorted so same-mesh commands are consecutive.
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
                        // Binder path: fill GBufferPerObjectParams and bind CB + textures via metadata.
                        GBufferPerObjectParams perObjParams{};
                        perObjParams.model          = drawCmd.worldMatrix;
                        perObjParams.normalMatrix   = drawCmd.normalMatrix;
                        perObjParams.albedoIndex    = drawCmd.albedoSrvIndex;
                        perObjParams.normalIndex    = drawCmd.normalSrvIndex;
                        perObjParams.roughnessIndex = drawCmd.roughnessSrvIndex;
                        perObjParams.metallicIndex  = drawCmd.metallicSrvIndex;
                        perObjParams.aoIndex        = drawCmd.aoSrvIndex;

                        // Set texture handles with fallbacks for SM5 binding
                        perObjParams.albedoTexture    = drawCmd.albedoTexture.IsValid()    ? drawCmd.albedoTexture    : desc.fallbackWhite;
                        perObjParams.normalTexture    = drawCmd.normalTexture.IsValid()    ? drawCmd.normalTexture    : desc.fallbackNormal;
                        perObjParams.roughnessTexture = drawCmd.roughnessTexture.IsValid() ? drawCmd.roughnessTexture : desc.fallbackWhite;
                        perObjParams.metallicTexture  = drawCmd.metallicTexture.IsValid()  ? drawCmd.metallicTexture  : desc.fallbackBlack;
                        perObjParams.aoTexture        = drawCmd.aoTexture.IsValid()        ? drawCmd.aoTexture        : desc.fallbackWhite;

                        CBAllocation objAlloc = ShaderParameterBinder::Bind(
                            cmd, &perObjParams,
                            GBufferPerObjectParams::GetMetadata(),
                            *desc.cbRingAllocator, kSlotPerObject,
                            desc.bindless);
                        if (objAlloc.cpuPtr == nullptr) {
                            UZ_LOG(Renderer, Error,
                                   "GBufferPass: CB ring exhausted at draw command {}", drawCallCount);
                            break;
                        }
                    } else {
                        // Legacy path: manual PerObjectCB + SetFragmentTexture.
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

                        if (!desc.bindless) {
                            cmd->SetFragmentTexture(0, drawCmd.albedoTexture.IsValid()    ? drawCmd.albedoTexture    : desc.fallbackWhite);
                            cmd->SetFragmentTexture(1, drawCmd.normalTexture.IsValid()    ? drawCmd.normalTexture    : desc.fallbackNormal);
                            cmd->SetFragmentTexture(2, drawCmd.roughnessTexture.IsValid() ? drawCmd.roughnessTexture : desc.fallbackWhite);
                            cmd->SetFragmentTexture(3, drawCmd.metallicTexture.IsValid()  ? drawCmd.metallicTexture  : desc.fallbackBlack);
                            cmd->SetFragmentTexture(4, drawCmd.aoTexture.IsValid()        ? drawCmd.aoTexture        : desc.fallbackWhite);
                        }
                    }

                    cmd->DrawIndexed(mesh.indexCount);
                    ++drawCallCount;
                }
            }

            cmd->EndRenderPass();
        });
    });
}

} // namespace uz
