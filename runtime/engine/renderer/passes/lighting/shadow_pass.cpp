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

/// Computes the top-left pixel offset of a cascade tile in the 2x2 atlas.
///
/// # Arguments
/// * `cascadeIndex` — Cascade index (0–3).
/// * `resolution`   — Per-cascade resolution in texels.
/// * `outX`         — Receives the horizontal pixel offset.
/// * `outY`         — Receives the vertical pixel offset.
///
/// # Layout
/// Index 0 = top-left, 1 = top-right, 2 = bottom-left, 3 = bottom-right.
static void GetCascadeTileOffset(uint32_t cascadeIndex, uint32_t resolution,
                                 uint32_t& outX, uint32_t& outY)
{
    // Column = index % 2, Row = index / 2
    outX = (cascadeIndex % 2) * resolution;
    outY = (cascadeIndex / 2) * resolution;
}

// ─────────────────────────────────────────────────────────────────────────────
// AddShadowPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddShadowPass(rg::RgBuilder& builder, const ShadowPassDesc& desc)
{
    UZ_CHECKF(desc.drawList != nullptr, "ShadowPass: drawList must not be null");
    UZ_CHECKF(desc.cbAllocator != nullptr, "ShadowPass: cbAllocator must not be null");
    UZ_CHECKF(desc.cascadeCount > 0 && desc.cascadeCount <= kMaxShadowCascades,
              "ShadowPass: cascadeCount must be 1..4");

    builder.AddPass("Shadow", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.shadowAtlas, RHIResourceState::DepthStencilWrite);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("ShadowPass::Execute");

            // ── Clear the entire shadow atlas to max depth ────────────────
            cmd->ClearDepthStencil(desc.shadowAtlasTexture, 1.0f, 0);

            const auto& commands = desc.drawList->GetCommands();
            const uint32_t resolution = desc.atlasResolution;
            uint32_t totalDrawCalls = 0;

            // ── Render each cascade into its atlas tile ───────────────────
            for (uint32_t cascade = 0; cascade < desc.cascadeCount; ++cascade) {
                uint32_t tileX = 0;
                uint32_t tileY = 0;
                GetCascadeTileOffset(cascade, resolution, tileX, tileY);

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
                passDesc.depthAttachment.clearDepth = 1.0f;
                passDesc.depthAttachment.clear      = false; // Already cleared above
                cmd->BeginRenderPass(passDesc);

                // ── Bind pipeline and vertex/index state ──────────────────
                const bool useInstanced = desc.instanced && desc.bindless && desc.batchData != nullptr && desc.batchCount > 0;

                if (useInstanced) {
                    cmd->SetPipeline(desc.instancedPipeline);
                    const uint32_t rootConstants[2] = {
                        desc.vertexBufferSrvIndex,
                        desc.instanceBufferSrvIndex
                    };
                    cmd->SetRootConstants(kSlotRootConstants, rootConstants, 2);
                } else {
                    cmd->SetPipeline(desc.pipeline);
                    if (desc.bindless) {
                        cmd->SetDescriptorIndex(kSlotRootConstants, desc.vertexBufferSrvIndex);
                    } else {
                        cmd->SetVertexBuffer(0, desc.vertexBuffer, 0);
                    }
                }

                cmd->SetIndexBuffer(desc.indexBuffer, desc.indexFormat, 0);

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
                    // Instanced path — one draw per batch
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
                        cmd->DrawIndexed(desc.indexCount, batch.instanceCount,
                                         0, 0, batch.firstInstance);
                        ++totalDrawCalls;
                    }
                } else {
                    // Non-instanced path — one draw per command
                    for (const auto& drawCmd : commands) {
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
                        cmd->DrawIndexed(desc.indexCount);
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
