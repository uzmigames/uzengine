/// UzEngine DBuffer Decal Pass — Implementation
///
/// Registers and executes two render graph passes:
/// 1. DecalRender — renders decal OBBs into the three DBuffer render targets.
/// 2. DecalApply — fullscreen compute merge of DBuffer into GBuffer.
///
/// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
/// @ref Pesce 2014 "Rendering Decals in Screen Space"

#include "decal_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Decal unit cube: 36 vertices (12 triangles)
// ─────────────────────────────────────────────────────────────────────────────

/// Number of vertices for a unit cube rendered without an index buffer.
static constexpr uint32_t kDecalCubeVertexCount = 36;

// ─────────────────────────────────────────────────────────────────────────────
// AddDecalRenderPass
// ─────────────────────────────────────────────────────────────────────────────

void AddDecalRenderPass(rg::RgBuilder& builder, const DecalRenderPassDesc& desc) {
    UZ_CHECKF(desc.cbAllocator != nullptr, "DecalRenderPass: cbAllocator must not be null");
    UZ_CHECKF(desc.decalCount == 0 || desc.decals != nullptr,
              "DecalRenderPass: decals must not be null when decalCount > 0");

    // Skip registration if there are no decals — the DBuffer will remain
    // cleared to zero (no modification).
    if (desc.decalCount == 0) return;

    builder.AddPass("DecalRender", [desc](rg::RgPassBuilder& pb) {
        // Write to all three DBuffer targets.
        pb.Write(desc.dbufferARes, RHIResourceState::RenderTarget);
        pb.Write(desc.dbufferBRes, RHIResourceState::RenderTarget);
        pb.Write(desc.dbufferCRes, RHIResourceState::RenderTarget);

        // Read the depth buffer for world position reconstruction.
        pb.Read(desc.depthRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("Decal::Render");

            auto dbufA = res.GetTexture(desc.dbufferARes);
            auto dbufB = res.GetTexture(desc.dbufferBRes);
            auto dbufC = res.GetTexture(desc.dbufferCRes);

            // ── Begin render pass with 3 color attachments, no depth ────
            // Decals use the depth buffer as a read-only SRV, not as a
            // depth target. Front-face culling ensures we see the decal
            // from inside the OBB (camera is typically outside the box;
            // back faces are rasterized).
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 3;

            passDesc.colorAttachments[0].texture = dbufA;
            passDesc.colorAttachments[0].clear   = false; // Already cleared above.

            passDesc.colorAttachments[1].texture = dbufB;
            passDesc.colorAttachments[1].clear   = false;

            passDesc.colorAttachments[2].texture = dbufC;
            passDesc.colorAttachments[2].clear   = false;

            passDesc.hasDepth = false; // No depth write — depth is read as SRV.
            cmd->BeginRenderPass(passDesc);

            // ── Set viewport and scissor ─────────────────────────────────
            RHIViewport vp{};
            vp.x         = 0.0f;
            vp.y         = 0.0f;
            vp.width     = desc.viewportWidth;
            vp.height    = desc.viewportHeight;
            vp.minDepth  = 0.0f;
            vp.maxDepth  = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect scissor{};
            scissor.left   = 0;
            scissor.top    = 0;
            scissor.right  = static_cast<int32_t>(desc.viewportWidth);
            scissor.bottom = static_cast<int32_t>(desc.viewportHeight);
            cmd->SetScissor(scissor);

            // ── Bind pipeline ────────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Draw each decal ──────────────────────────────────────────
            for (uint32_t i = 0; i < desc.decalCount; ++i) {
                const auto& decal = desc.decals[i];

                // Upload per-decal CB.
                CBAllocation objAlloc = desc.cbAllocator->Allocate(sizeof(DecalObjectCB));
                if (!objAlloc.cpuPtr) {
                    UZ_LOG(Renderer, Warning,
                           "DecalRenderPass: CB ring full, skipping decal {}",
                           i);
                    continue;
                }
                std::memcpy(objAlloc.cpuPtr, &decal.cb, sizeof(DecalObjectCB));

                // Bind per-decal CB at slot 2 (b2 = PerObjectCB slot).
                cmd->SetRootCBV(kSlotPerObject,
                                objAlloc.bufferHandle,
                                objAlloc.offsetBytes,
                                reinterpret_cast<const uint8_t*>(objAlloc.cpuPtr),
                                objAlloc.sizeBytes);

                // Draw unit cube — 36 vertices, 1 instance, no VB/IB.
                cmd->Draw(kDecalCubeVertexCount, 1, 0, 0);
            }

            cmd->EndRenderPass();
        });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// AddDecalApplyPass
// ─────────────────────────────────────────────────────────────────────────────

void AddDecalApplyPass(rg::RgBuilder& builder, const DecalApplyPassDesc& desc) {
    UZ_CHECKF(desc.pipeline.IsValid(), "DecalApplyPass: pipeline handle is invalid");

    builder.AddPass("DecalApply", [desc](rg::RgPassBuilder& pb) {
        // Read DBuffer textures.
        pb.Read(desc.dbufferARes, RHIResourceState::ShaderResource);
        pb.Read(desc.dbufferBRes, RHIResourceState::ShaderResource);
        pb.Read(desc.dbufferCRes, RHIResourceState::ShaderResource);

        // Write GBuffer textures (UAV for in-place modification).
        pb.Write(desc.albedoRes, RHIResourceState::UnorderedAccess);
        pb.Write(desc.normalRes, RHIResourceState::UnorderedAccess);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("Decal::Apply");

            // ── Bind compute pipeline ────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Set root constants with DBuffer/GBuffer SRV/UAV indices ──
            DecalApplyConstants constants{};
            constants.dbufferASrvIndex      = desc.dbufferASrvIndex;
            constants.dbufferBSrvIndex      = desc.dbufferBSrvIndex;
            constants.dbufferCSrvIndex      = desc.dbufferCSrvIndex;
            constants.gbufferAlbedoUavIndex = desc.albedoUavIndex;
            constants.gbufferNormalUavIndex = desc.normalUavIndex;
            constants.texWidth              = desc.width;
            constants.texHeight             = desc.height;

            // Root constants at slot 0 (b0) — 10 uint32 values.
            static_assert(sizeof(DecalApplyConstants) == 40);
            cmd->SetRootConstants(kSlotRootConstants,
                                  reinterpret_cast<const uint32_t*>(&constants),
                                  sizeof(DecalApplyConstants) / sizeof(uint32_t));

            // ── Dispatch ─────────────────────────────────────────────────
            // Thread group size is 8x8x1 (defined in decal_apply_cs.hlsl).
            constexpr uint32_t kGroupSize = 8;
            const uint32_t groupsX = (desc.width  + kGroupSize - 1) / kGroupSize;
            const uint32_t groupsY = (desc.height + kGroupSize - 1) / kGroupSize;
            cmd->Dispatch(groupsX, groupsY, 1);
        });
    });
}

} // namespace uz
