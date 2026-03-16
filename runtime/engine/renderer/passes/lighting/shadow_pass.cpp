/// UzEngine Shadow Depth Pass — Implementation
///
/// Registers and executes a shadow depth pass that renders depth-only geometry
/// into a 2x2 cascade atlas for Cascaded Shadow Maps (CSM).

#include "shadow_pass.h"

#include "constant_buffers.h"
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
// ShadowCB — per-cascade constant buffer (local to this translation unit)
// ─────────────────────────────────────────────────────────────────────────────

/// Per-cascade constant buffer containing the light-space view-projection
/// matrix for the current cascade. Bound at root parameter slot 3.
struct ShadowCB {
    /// Light-space view-projection matrix (lightProj * lightView).
    ///
    /// Transforms world-space vertex positions into light clip space for
    /// depth comparison during shadow map sampling.
    Mat4 viewProj;
};

// ─────────────────────────────────────────────────────────────────────────────
// Atlas tile helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the top-left pixel offset of a cascade tile in the dynamic NxM atlas.
///
/// # Arguments
/// * `cascadeIndex`   — Cascade index (0 to totalCascades-1).
/// * `totalCascades`  — Total number of cascades (determines grid dimensions).
/// * `resolution`     — Per-cascade resolution in texels.
/// * `outX`           — Receives the horizontal pixel offset.
/// * `outY`           — Receives the vertical pixel offset.
///
/// # Layout
/// Grid columns determined by `GetAtlasColumns(totalCascades)`.
/// Cascades fill left-to-right, top-to-bottom.
static void GetCascadeTileOffset(uint32_t cascadeIndex, uint32_t totalCascades,
                                 uint32_t resolution,
                                 uint32_t& outX, uint32_t& outY)
{
    const uint32_t cols = GetAtlasColumns(totalCascades);
    outX = (cascadeIndex % cols) * resolution;
    outY = (cascadeIndex / cols) * resolution;
}

// ─────────────────────────────────────────────────────────────────────────────
// AddShadowPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddShadowPass(rg::RgBuilder& builder, const ShadowPassDesc& desc)
{
    UZ_CHECKF(desc.drawList != nullptr, "ShadowPass: drawList must not be null");
    UZ_CHECKF(desc.cbAllocator != nullptr, "ShadowPass: cbAllocator must not be null");
    UZ_CHECKF(desc.cascadeCount > 0 && desc.cascadeCount <= kMaxShadowCascades,
              "ShadowPass: cascadeCount must be 1..{}", kMaxShadowCascades);

    builder.AddPass("Shadow", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.shadowAtlas, RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("ShadowPass::Execute");

            // ── Clear the entire shadow atlas to max depth (standard-Z far=1.0) ──
            cmd->ClearDepthStencil(desc.shadowAtlasTexture, kShadowDepthClear, 0);

            const auto& commands = desc.drawList->GetCommands();
            const uint32_t resolution = desc.atlasResolution;
            uint32_t totalDrawCalls = 0;

            // ── Render each cascade into its atlas tile ───────────────────
            for (uint32_t cascade = 0; cascade < desc.cascadeCount; ++cascade) {
                uint32_t tileX = 0;
                uint32_t tileY = 0;
                GetCascadeTileOffset(cascade, desc.cascadeCount, resolution, tileX, tileY);

                // ── Viewport: cascade tile region ─────────────────────────
                RHIViewport vp{};
                vp.x = static_cast<float>(tileX);
                vp.y = static_cast<float>(tileY);
                vp.width    = static_cast<float>(resolution);
                vp.height   = static_cast<float>(resolution);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                // ── Scissor: match viewport exactly ───────────────────────
                RHIScissorRect sc{};
                sc.left   = static_cast<int32_t>(tileX);
                sc.top    = static_cast<int32_t>(tileY);
                sc.right  = static_cast<int32_t>(tileX + resolution);
                sc.bottom = static_cast<int32_t>(tileY + resolution);
                cmd->SetScissor(sc);

                // ── Depth-only render pass (0 color attachments) ──────────
                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 0;
                passDesc.hasDepth = true;
                passDesc.depthAttachment.texture    = desc.shadowAtlasTexture;
                passDesc.depthAttachment.clearDepth = kShadowDepthClear;
                passDesc.depthAttachment.clear      = false; // Already cleared above
                cmd->BeginRenderPass(passDesc);

                // ── Bind pipeline ─────────────────────────────────────────
                // For the instanced path, use mesh[0] as the shared geometry
                // (instanced batches are always homogeneous in mesh — future work
                // will store per-batch meshId and bind accordingly).
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
                    // Per-draw buffer binding handled inside the draw loop below.
                }

                // ── Bind per-cascade shadow CB at slot 3 ──────────────────
                ShadowCB shadowCb{};
                shadowCb.viewProj = desc.cascades[cascade].viewProj;

                CBAllocation shadowAlloc = desc.cbAllocator->Allocate(sizeof(ShadowCB));
                if (shadowAlloc.cpuPtr == nullptr) {
                    UZ_LOG(Renderer, Error,
                           "ShadowPass: CB ring exhausted for cascade {} shadow CB", cascade);
                    cmd->EndRenderPass();
                    break;
                }
                std::memcpy(shadowAlloc.cpuPtr, &shadowCb, sizeof(ShadowCB));
                cmd->SetRootCBV(kSlotShadow, shadowAlloc.bufferHandle, shadowAlloc.offsetBytes,
                                shadowAlloc.cpuPtr, sizeof(ShadowCB));

                // ── Draw loop ─────────────────────────────────────────────
                if (useInstanced) {
                    // Instanced path — one draw per batch, all using mesh[0].
                    // DrawIndexed with batch.instanceCount and firstInstance offset.
                    const GPUMesh& mesh0 = desc.meshes[0];
                    for (uint32_t bi = 0; bi < desc.batchCount; ++bi) {
                        const auto& batch = desc.batchData[bi];
                        // Shadow pass doesn't need material — just a dummy PerObjectCB
                        PerObjectCB perObject{};
                        CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
                        if (objAlloc.cpuPtr == nullptr) {
                            UZ_LOG(Renderer, Error,
                                   "ShadowPass(instanced): CB ring exhausted at cascade {} batch {}",
                                   cascade, totalDrawCalls);
                            break;
                        }
                        std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));
                        cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                        objAlloc.cpuPtr, sizeof(PerObjectCB));
                        cmd->DrawIndexed(mesh0.indexCount, batch.instanceCount,
                                         0, 0, batch.firstInstance);
                        ++totalDrawCalls;
                    }
                } else {
                    // Non-instanced path — bind the correct mesh per draw command.
                    // Each command carries a `meshId` that indexes into `desc.meshes`.
                    // Commands referencing an out-of-range or invalid mesh are skipped.
                    uint32_t lastBoundMeshId = UINT32_MAX; // Track to avoid redundant rebinds.
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

                        PerObjectCB perObject{};
                        perObject.model = drawCmd.worldMatrix;

                        CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
                        if (objAlloc.cpuPtr == nullptr) {
                            UZ_LOG(Renderer, Error,
                                   "ShadowPass: CB ring exhausted at cascade {} draw {}",
                                   cascade, totalDrawCalls);
                            break;
                        }
                        std::memcpy(objAlloc.cpuPtr, &perObject, sizeof(PerObjectCB));

                        cmd->SetRootCBV(kSlotPerObject, objAlloc.bufferHandle, objAlloc.offsetBytes,
                                        objAlloc.cpuPtr, sizeof(PerObjectCB));
                        cmd->DrawIndexed(mesh.indexCount);
                        ++totalDrawCalls;
                    }
                }

                cmd->EndRenderPass();
            }

            // DEBUG: log shadow draw calls once
            static bool loggedShadowDraws = false;
            if (!loggedShadowDraws) {
                UZ_LOG(Renderer, Info, "ShadowPass: {} cascades, {} total draw calls, {} commands in drawList",
                       desc.cascadeCount, totalDrawCalls, commands.size());
                loggedShadowDraws = true;
            }
        });
    });
}

} // namespace uz
