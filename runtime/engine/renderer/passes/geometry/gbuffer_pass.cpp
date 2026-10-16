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

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddGBufferPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddGBufferPass(rg::RgBuilder& builder, const GBufferPassDesc& desc)
{
    UZ_CHECKF(desc.drawList != nullptr, "GBufferPass: drawList must not be null");
    UZ_CHECKF(desc.cbAllocator != nullptr, "GBufferPass: cbAllocator must not be null");

    // Block B.4c: in depth-only mode this pass is the depth prepass (graph name "DepthPrepass");
    // with `keepDepth` the colour pass reuses the prepass's depth instead of clearing it.
    builder.AddPass(desc.depthOnly ? "DepthPrepass" : "GBuffer", [desc](rg::RgPassBuilder& pb) {
        if (!desc.depthOnly) {
            pb.Write(desc.albedoTarget,   RHIResourceState::RenderTarget);
            pb.Write(desc.normalTarget,   RHIResourceState::RenderTarget);
            pb.Write(desc.positionTarget, RHIResourceState::RenderTarget);
            pb.Write(desc.emissiveTarget, RHIResourceState::RenderTarget);
        }
        pb.Write(desc.depthBuffer,    RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("GBuffer::Execute");

            RHIRenderPassDesc passDesc{};
            if (!desc.depthOnly) {
                auto albedo    = res.GetTexture(desc.albedoTarget);
                auto normal    = res.GetTexture(desc.normalTarget);
                auto positionT = res.GetTexture(desc.positionTarget);
                auto emissiveT = res.GetTexture(desc.emissiveTarget);

                // ── Clear render targets ─────────────────────────────────
                constexpr float kClearBlack[4] = {0.0f, 0.0f, 0.0f, 0.0f};
                cmd->ClearRenderTarget(albedo,    kClearBlack);
                cmd->ClearRenderTarget(normal,    kClearBlack);
                cmd->ClearRenderTarget(positionT, kClearBlack);
                cmd->ClearRenderTarget(emissiveT, kClearBlack);

                // ── 4 color attachments + depth ──────────────────────────
                passDesc.colorAttachmentCount = 4;
                passDesc.colorAttachments[0].texture = albedo;
                passDesc.colorAttachments[0].clear   = false;
                passDesc.colorAttachments[1].texture = normal;
                passDesc.colorAttachments[1].clear   = false;
                passDesc.colorAttachments[2].texture = positionT;
                passDesc.colorAttachments[2].clear   = false;
                passDesc.colorAttachments[3].texture = emissiveT;
                passDesc.colorAttachments[3].clear   = false;
            } else {
                passDesc.colorAttachmentCount = 0;   // depth prepass
            }
            // The depth is cleared unless the prepass wrote it this frame (block B.4c); until then
            // the colour pass always cleared it, which is why the prepass bought nothing.
            if (!desc.keepDepth) {
                cmd->ClearDepthStencil(desc.depthTexture, kDepthFar, 0);
            }

            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = desc.depthTexture;
            passDesc.depthAttachment.clearDepth = kDepthFar;
            passDesc.depthAttachment.clear      = !desc.keepDepth;

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
                const uint32_t rootConstants[4] = {
                    mesh0.vbSrvIndex,
                    desc.instanceBufferSrvIndex,
                    mesh0.colorSrvIndex, 0u
                };
                cmd->SetRootConstants(kSlotRootConstants, rootConstants, 4);
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
                    perObjParams.emissiveIndex  = batch.emissiveSrvIndex;
                    perObjParams.emissiveColorR    = batch.emissiveColorR;
                    perObjParams.emissiveColorG    = batch.emissiveColorG;
                    perObjParams.emissiveColorB    = batch.emissiveColorB;
                    perObjParams.emissiveIntensity = batch.emissiveIntensity;

                    CBAllocation objAlloc = ShaderParameterBinder::BindCB(
                        cmd, &perObjParams,
                        GBufferPerObjectParams::GetMetadata(),
                        ringAlloc, kSlotPerObject);
                    if (objAlloc.cpuPtr == nullptr) {
                        UZ_LOG(Renderer, Error,
                               "GBufferPass(instanced): CB ring exhausted at batch {}", drawCallCount);
                        break;
                    }

                    if (desc.useIndirectDraw && desc.indirectArgsBuffer.IsValid()
                        && desc.batchCount == 1) {
                        // Phase 1 indirect draw: the GPU cull pass has written the visible
                        // instance count into the indirect args buffer at offset 4.
                        // The index count, start index, base vertex, and start
                        // instance fields were pre-filled before the cull dispatch.
                        cmd->DrawIndexedIndirect(desc.indirectArgsBuffer, 0, 1,
                                                 sizeof(RHIDrawIndexedIndirectArgs));
                        ++drawCallCount;

                        // Phase 2 indirect draw: newly-visible instances from the
                        // second cull pass (re-tested against updated HiZ).
                        if (desc.usePhase2IndirectDraw && desc.indirectArgsBuffer2.IsValid()) {
                            cmd->DrawIndexedIndirect(desc.indirectArgsBuffer2, 0, 1,
                                                     sizeof(RHIDrawIndexedIndirectArgs));
                            ++drawCallCount;
                        }
                    } else {
                        cmd->DrawIndexed(mesh0.indexCount, batch.instanceCount,
                                         0, 0, batch.firstInstance);
                        ++drawCallCount;
                    }
                }
            } else {
                // ── Non-instanced path — bind the correct mesh per draw command ──
                // Each command carries a `meshId` that indexes into `desc.meshes`.
                // Commands referencing an out-of-range or invalid mesh are skipped.
                const auto& commands = desc.drawList->GetCommands();

                // Track last bound mesh to avoid redundant VB/IB rebinds for
                // commands sorted by mesh ID (the sort key places mesh ID in LSBs).
                uint32_t lastBoundMeshId = UINT32_MAX;

                // Mesh decals (materialFlags bit 4) blend over the finished opaque G-buffer: the
                // commands run twice, the opaque and masked draws first and the decals second (their
                // PSO blends the albedo and writes no depth); the depth-only pass has no decals.
                for (int drawPhase = 0; drawPhase < 2; ++drawPhase)
                for (const auto& drawCmd : commands) {
                    const bool meshDecal = (drawCmd.materialFlags & 0x10u) != 0u;
                    if (meshDecal != (drawPhase == 1)) {
                        continue;
                    }
                    if (meshDecal && desc.depthOnly) {
                        continue;
                    }
                    // Skip translucent and additive draws — they are rendered in
                    // a separate forward pass after deferred lighting.
                    const uint32_t blendMode = drawCmd.materialFlags & 0x3u;
                    if (blendMode >= 2) {
                        continue;
                    }

                    if (drawCmd.meshId >= desc.meshCount) {
                        continue; // Guard: meshId out of range.
                    }
                    const GPUMesh& mesh = desc.meshes[drawCmd.meshId];
                    if (!mesh.IsValid()) {
                        continue; // Guard: mesh not yet uploaded.
                    }

                    // Block B.4c: the depth prepass writes standard opaque geometry only; masked and
                    // material-graph draws are not in it and test Greater (with writes) in the colour
                    // pass, which is what their own pipelines do.
                    if (desc.depthOnly && (blendMode == 1 || drawCmd.materialGraphPipeline.IsValid())) {
                        continue;
                    }

                    // Select PSO based on blend mode: Masked materials use the
                    // two-sided (no cull) pipeline; alpha test is done in the
                    // pixel shader via the materialFlags CB field.
                    const bool isMasked = (blendMode == 1) && desc.maskedPipeline.IsValid();
                    if (isMasked) {
                        cmd->SetPipeline(desc.maskedPipeline);
                    }

                    // Rebind vertex/index buffers only when the mesh changes.
                    // The DrawList is sorted so same-mesh commands are consecutive.
                    if (drawCmd.meshId != lastBoundMeshId) {
                        if (desc.bindless) {
                            // Shared mesh root layout: vb, packed, color, previous vb.
                            const uint32_t pushData[4] = {
                                mesh.vbSrvIndex,
                                (mesh.vertexStride == 32) ? 1u : 0u,
                                mesh.colorSrvIndex,
                                0u
                            };
                            cmd->SetRootConstants(kSlotRootConstants, pushData, 4);
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
                        perObjParams.emissiveIndex  = drawCmd.emissiveSrvIndex;
                        perObjParams.emissiveColorR    = drawCmd.emissiveColorR;
                        perObjParams.emissiveColorG    = drawCmd.emissiveColorG;
                        perObjParams.emissiveColorB    = drawCmd.emissiveColorB;
                        perObjParams.emissiveIntensity = drawCmd.emissiveIntensity;
                        perObjParams.baseColorTintR      = drawCmd.baseColorTintR;
                        perObjParams.baseColorTintG      = drawCmd.baseColorTintG;
                        perObjParams.baseColorTintB      = drawCmd.baseColorTintB;
                        perObjParams.roughnessMultiplier = drawCmd.roughnessMultiplier;
                        perObjParams.metallicMultiplier  = drawCmd.metallicMultiplier;
                        perObjParams.normalIntensity     = drawCmd.normalIntensity;
                        perObjParams.uvScaleX            = drawCmd.uvScaleX;
                        perObjParams.uvScaleY            = drawCmd.uvScaleY;
                        perObjParams.uvOffsetX           = drawCmd.uvOffsetX;
                        perObjParams.uvOffsetY           = drawCmd.uvOffsetY;
                        perObjParams.aoIntensity         = drawCmd.aoIntensity;
                        perObjParams.opacity             = drawCmd.opacity;
                        perObjParams.alphaClipThreshold   = drawCmd.alphaClipThreshold;
                        perObjParams.opacitySrvIndex     = drawCmd.opacitySrvIndex;
                        perObjParams.materialFlags       = drawCmd.materialFlags;

                        // Set texture handles with fallbacks for SM5 binding
                        perObjParams.albedoTexture    = drawCmd.albedoTexture.IsValid()    ? drawCmd.albedoTexture    : desc.fallbackWhite;
                        perObjParams.normalTexture    = drawCmd.normalTexture.IsValid()    ? drawCmd.normalTexture    : desc.fallbackNormal;
                        perObjParams.roughnessTexture = drawCmd.roughnessTexture.IsValid() ? drawCmd.roughnessTexture : desc.fallbackWhite;
                        perObjParams.metallicTexture  = drawCmd.metallicTexture.IsValid()  ? drawCmd.metallicTexture  : desc.fallbackBlack;
                        perObjParams.aoTexture        = drawCmd.aoTexture.IsValid()        ? drawCmd.aoTexture        : desc.fallbackWhite;
                        perObjParams.emissiveTexture  = drawCmd.emissiveTexture.IsValid()  ? drawCmd.emissiveTexture  : desc.fallbackBlack;

                        // Force fresh descriptor block per draw — each draw binds
                        // different material textures to the same t0-t5 slots.
                        cmd->InvalidateDescriptorBlock();

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
                        perObject.emissiveIndex  = drawCmd.emissiveSrvIndex;
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
                        perObject.displacementScale   = drawCmd.displacementScale;
                        perObject.heightmapSrvIndex   = drawCmd.heightmapSrvIndex;
                        perObject.pomSelfShadow       = drawCmd.pomSelfShadow;
                        perObject.alphaClipThreshold  = drawCmd.alphaClipThreshold;
                        perObject.opacitySrvIndex     = drawCmd.opacitySrvIndex;
                        perObject.materialFlags       = drawCmd.materialFlags;

                        CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
                        if (objAlloc.cpuPtr == nullptr) {
                            UZ_LOG(Renderer, Error,
                                   "GBufferPass: CB ring exhausted at draw command {}", drawCallCount);
                            break;
                        }
                        std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

                        cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                        objAlloc.cpuPtr, sizeof(PerObjectCB));

                        if (!desc.bindless && !desc.depthOnly) {
                            cmd->InvalidateDescriptorBlock();
                            cmd->SetFragmentTexture(0, drawCmd.albedoTexture.IsValid()    ? drawCmd.albedoTexture    : desc.fallbackWhite);
                            cmd->SetFragmentTexture(1, drawCmd.normalTexture.IsValid()    ? drawCmd.normalTexture    : desc.fallbackNormal);
                            cmd->SetFragmentTexture(2, drawCmd.roughnessTexture.IsValid() ? drawCmd.roughnessTexture : desc.fallbackWhite);
                            cmd->SetFragmentTexture(3, drawCmd.metallicTexture.IsValid()  ? drawCmd.metallicTexture  : desc.fallbackBlack);
                            cmd->SetFragmentTexture(4, drawCmd.aoTexture.IsValid()        ? drawCmd.aoTexture        : desc.fallbackWhite);
                            cmd->SetFragmentTexture(5, drawCmd.emissiveTexture.IsValid()  ? drawCmd.emissiveTexture  : desc.fallbackBlack);
                            // t6 (opacityTexture) — not bound because DrawCommand has no
                            // RHITextureHandle for opacity. The shader defaults to sampling
                            // a null SRV (returns 0) and uses the scalar opacity field instead.
                            // Opacity via texture is bindless-only (opacitySrvIndex in CB).
                        }
                    }

                    // ── Material Graph PSO switch ─────────────────────────
                    // If this draw command has a material-graph-compiled PSO,
                    // switch to it and bind material params at b3.
                    if (drawCmd.materialGraphPipeline.IsValid()) {
                        cmd->SetPipeline(drawCmd.materialGraphPipeline);

                        // Upload materialParams CB at b3 — contains:
                        // [0..3] = texture SRV indices (as float-encoded uint via asuint)
                        // [4..15] = user-defined material parameters
                        {
                            constexpr uint32_t kMaterialCBSize = 256; // 16 float4 = 256 bytes
                            auto matAlloc = desc.cbAllocator->Allocate(kMaterialCBSize);
                            if (matAlloc.cpuPtr) {
                                std::memset(matAlloc.cpuPtr, 0, kMaterialCBSize);

                                // Pack texture SRV indices into first 4 float4 (16 uint slots).
                                // Use memcpy to reinterpret uint → float bits (matching asuint in shader).
                                auto* dst = static_cast<uint32_t*>(matAlloc.cpuPtr);
                                for (uint32_t ti = 0; ti < drawCmd.materialTexCount && ti < 16; ++ti) {
                                    dst[ti] = drawCmd.materialTexIndices[ti];
                                }

                                // User params go at offset 64 bytes (slot 4+)
                                if (drawCmd.materialParamCount > 0) {
                                    const uint32_t paramOffset = 16 * sizeof(uint32_t); // 64 bytes
                                    const uint32_t paramBytes = std::min(drawCmd.materialParamCount * 16u,
                                                                         kMaterialCBSize - paramOffset);
                                    std::memcpy(static_cast<uint8_t*>(matAlloc.cpuPtr) + paramOffset,
                                                drawCmd.materialParams, paramBytes);
                                }

                                cmd->SetRootCBV(kSlotMaterialParams, matAlloc.bufferHandle, matAlloc.offsetBytes,
                                                matAlloc.cpuPtr, kMaterialCBSize);
                            }
                        }
                    }

                    // Use LOD-specific index range if available (UZMH v2 multi-LOD).
                    if (drawCmd.lodIndexCount > 0) {
                        cmd->DrawIndexed(drawCmd.lodIndexCount, 1, drawCmd.lodIndexByteOffset / 4);
                    } else {
                        cmd->DrawIndexed(mesh.indexCount);
                    }

                    // Restore default GBuffer PSO if we switched for material graph or masked
                    if (drawCmd.materialGraphPipeline.IsValid() || isMasked) {
                        cmd->SetPipeline(desc.pipeline);
                    }

                    ++drawCallCount;
                }
            }

            cmd->EndRenderPass();

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "GBufferPass: first execution — {} draw calls, viewport={}x{}, "
                       "pipeline={}, instanced={}, bindless={}, meshCount={}",
                       drawCallCount, desc.viewportWidth, desc.viewportHeight,
                       desc.pipeline.index, useInstanced, desc.bindless, desc.meshCount);
                if (drawCallCount == 0) {
                    UZ_LOG(Renderer, Warning,
                           "GBufferPass: zero draw calls on first frame — "
                           "drawList has {} commands, meshCount={}",
                           desc.drawList->GetCommands().size(), desc.meshCount);
                }
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
