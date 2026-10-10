/// VFX CustomDepth + stencil render pass — implementation (phase 9.12.0).

#include "vfx_customdepth_pass.h"

#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Root parameter slot for bindless indices (b0).
static constexpr uint32_t kSlotBindlessIndices = 0;

/// Root parameter slot for per-frame CB (b1).
static constexpr uint32_t kSlotPerFrame = 1;

/// Root parameter slot for per-emitter CB (b2).
static constexpr uint32_t kSlotPerEmitter = 2;

// ─────────────────────────────────────────────────────────────────────────────
// AddVfxCustomDepthPass
// ─────────────────────────────────────────────────────────────────────────────

void AddVfxCustomDepthPass(rg::RgBuilder& builder,
                            const VfxCustomDepthPassDesc& desc) {
    if (desc.emitters.empty()) return;

    UZ_CHECKF(desc.customDepthTarget.IsValid(),
              "VfxCustomDepthPass: customDepth target is invalid");
    // At least the Disabled PSO (index 0) must be valid — it's the depth-only fallback.
    UZ_CHECKF(desc.pipelines[0].IsValid(),
              "VfxCustomDepthPass: depth-only pipeline (mode=Disabled) is invalid");

    auto passDesc = desc;

    builder.AddPass("VFX_CustomDepth", [passDesc](rg::RgPassBuilder& pb) {
        // CustomDepth RT: depth write.
        pb.Write(passDesc.customDepthTarget, RHIResourceState::DepthStencilWrite);

        // Indirect draw args barriers.
        for (const auto& emitter : passDesc.emitters) {
            if (emitter.rgIndirectDrawArgs.IsValid()) {
                pb.Read(emitter.rgIndirectDrawArgs, RHIResourceState::IndirectArgument);
            }
        }

        pb.SetExecutionCallback([passDesc](IRHICommandList* cmd,
                                            const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("VFX::CustomDepth");

            // Begin depth-only render pass (no colour attachments).
            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachmentCount = 0;
            rpDesc.hasDepth = true;
            rpDesc.depthAttachment.texture = passDesc.customDepthTexture;
            rpDesc.depthAttachment.clear = true;
            rpDesc.depthAttachment.clearDepth = 0.0f; // reversed-Z: clear to 0 (far)
            cmd->BeginRenderPass(rpDesc);

            // Viewport + scissor.
            {
                RHIViewport vp{};
                vp.width    = static_cast<float>(passDesc.renderWidth);
                vp.height   = static_cast<float>(passDesc.renderHeight);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);
                RHIScissorRect sr{};
                sr.right  = static_cast<int32_t>(passDesc.renderWidth);
                sr.bottom = static_cast<int32_t>(passDesc.renderHeight);
                cmd->SetScissor(sr);
            }

            // Re-bind per-frame CB.
            if (passDesc.perFrameCB.gpuAddr != 0) {
                cmd->SetRootCBV(kSlotPerFrame, passDesc.perFrameCB.gpuAddr);
            }

            RHIPipelineHandle currentPipeline{};

            for (const auto& emitter : passDesc.emitters) {
                // Select PSO variant by stencil write mode.
                const auto modeIdx = static_cast<uint8_t>(emitter.stencilConfig.writeMode);
                UZ_CHECK(modeIdx < 3);
                const auto pipeline = passDesc.pipelines[modeIdx];
                if (!pipeline.IsValid()) continue; // PSO variant not created

                // Only re-bind pipeline when mode changes.
                if (pipeline != currentPipeline) {
                    cmd->SetPipeline(pipeline);
                    currentPipeline = pipeline;

                    // Re-bind per-frame CB after SetPipeline resets root state.
                    if (passDesc.perFrameCB.gpuAddr != 0) {
                        cmd->SetRootCBV(kSlotPerFrame, passDesc.perFrameCB.gpuAddr);
                    }
                }

                // Dynamic stencil ref for Replace mode.
                if (emitter.stencilConfig.writeMode == VfxStencilWriteMode::Replace) {
                    cmd->SetStencilRef(emitter.stencilConfig.stencilRef);
                }

                // Per-emitter CB.
                if (emitter.renderCB.gpuAddr != 0) {
                    cmd->SetRootCBV(kSlotPerEmitter, emitter.renderCB.gpuAddr);
                }

                // Bindless: sprite texture SRV index at b0 for the customdepth PS.
                const uint32_t spriteIdx = emitter.spriteSrvIdx;
                cmd->SetRootConstants(kSlotBindlessIndices, &spriteIdx, 1, 0);

                // DrawIndexedIndirect — same buffer as the billboard pass.
                if (emitter.indirectDrawArgs.IsValid()) {
                    cmd->DrawIndexedIndirect(emitter.indirectDrawArgs, 0, 1, 0);
                }
            }

            cmd->EndRenderPass();

            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(VFX, Info,
                       "VfxCustomDepthPass: first draw — {} emitters, viewport={}x{}",
                       passDesc.emitters.size(), passDesc.renderWidth,
                       passDesc.renderHeight);
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
