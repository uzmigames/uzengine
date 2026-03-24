/// UzEngine PBR Pass — Implementation
///
/// Registers and executes a forward PBR rendering pass that iterates a sorted
/// DrawList, binding per-object constant buffers and issuing indexed draw calls.

#include "pbr_pass.h"

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

UZ_DECLARE_LOG_CATEGORY(Renderer);

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
            cmd->ClearDepthStencil(desc.depthTexture, kDepthFar, 0);

            // ── Begin render pass ────────────────────────────────────────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = desc.depthTexture;
            passDesc.depthAttachment.clearDepth = kDepthFar;
            passDesc.depthAttachment.clear      = false;
            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline and shared per-frame state ─────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind per-frame CB ────────────────────────────────────────
            if (desc.perFrameParams != nullptr && desc.cbRingAllocator != nullptr) {
                // Binder path: upload PerFrameParams CB via metadata.
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

            // ── Draw loop — bind the correct mesh per draw command ────────
            // Each command carries a `meshId` that indexes into `desc.meshes`.
            // Commands referencing an out-of-range or invalid mesh are skipped.
            const auto& commands = desc.drawList->GetCommands();
            uint32_t drawCallCount = 0;

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

                    // Set texture handles for SM5 binding
                    perObjParams.albedoTexture    = drawCmd.albedoTexture;
                    perObjParams.normalTexture    = drawCmd.normalTexture;
                    perObjParams.roughnessTexture = drawCmd.roughnessTexture;
                    perObjParams.metallicTexture  = drawCmd.metallicTexture;
                    perObjParams.aoTexture        = drawCmd.aoTexture;

                    CBAllocation objAlloc = ShaderParameterBinder::Bind(
                        cmd, &perObjParams,
                        GBufferPerObjectParams::GetMetadata(),
                        *desc.cbRingAllocator, kSlotPerObject,
                        desc.bindless);
                    if (objAlloc.cpuPtr == nullptr) {
                        UZ_LOG(Renderer, Error,
                               "PBRPass: CB ring exhausted at draw command {}", drawCallCount);
                        break;
                    }
                } else {
                    // Legacy path: manual PerObjectCB + SetRootCBV.
                    PerObjectCB perObject{};
                    perObject.model        = drawCmd.worldMatrix;
                    perObject.normalMatrix = drawCmd.normalMatrix;
                    perObject.albedoIndex    = drawCmd.albedoSrvIndex;
                    perObject.normalIndex    = drawCmd.normalSrvIndex;
                    perObject.roughnessIndex = drawCmd.roughnessSrvIndex;
                    perObject.metallicIndex  = drawCmd.metallicSrvIndex;
                    perObject.aoIndex        = drawCmd.aoSrvIndex;
                    perObject.emissiveColorR    = drawCmd.emissiveColorR;
                    perObject.emissiveColorG    = drawCmd.emissiveColorG;
                    perObject.emissiveColorB    = drawCmd.emissiveColorB;
                    perObject.emissiveIntensity = drawCmd.emissiveIntensity;
                    perObject.baseColorTintR      = drawCmd.baseColorTintR;
                    perObject.baseColorTintG      = drawCmd.baseColorTintG;
                    perObject.baseColorTintB      = drawCmd.baseColorTintB;
                    perObject.roughnessMultiplier = drawCmd.roughnessMultiplier;
                    perObject.metallicMultiplier  = drawCmd.metallicMultiplier;
                    perObject.normalIntensity     = drawCmd.normalIntensity;
                    perObject.uvScaleX            = drawCmd.uvScaleX;
                    perObject.uvScaleY            = drawCmd.uvScaleY;
                    perObject.uvOffsetX           = drawCmd.uvOffsetX;
                    perObject.uvOffsetY           = drawCmd.uvOffsetY;
                    perObject.aoIntensity         = drawCmd.aoIntensity;
                    perObject.opacity             = drawCmd.opacity;
                    perObject.alphaClipThreshold  = drawCmd.alphaClipThreshold;
                    perObject.opacitySrvIndex     = drawCmd.opacitySrvIndex;
                    perObject.materialFlags       = drawCmd.materialFlags;

                    CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
                    if (objAlloc.cpuPtr == nullptr) {
                        UZ_LOG(Renderer, Error,
                               "PBRPass: CB ring exhausted at draw command {}", drawCallCount);
                        break;
                    }
                    std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

                    cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                    objAlloc.cpuPtr, sizeof(PerObjectCB));
                }
                cmd->DrawIndexed(mesh.indexCount);
                ++drawCallCount;
            }

            cmd->EndRenderPass();

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "PBRPass: first execution — {} draw calls, viewport={}x{}, "
                       "pipeline={}, bindless={}, meshCount={}",
                       drawCallCount, desc.viewportWidth, desc.viewportHeight,
                       desc.pipeline.index, desc.bindless, desc.meshCount);
                if (drawCallCount == 0) {
                    UZ_LOG(Renderer, Warning,
                           "PBRPass: zero draw calls on first frame — "
                           "drawList has {} commands", desc.drawList->GetCommands().size());
                }
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
