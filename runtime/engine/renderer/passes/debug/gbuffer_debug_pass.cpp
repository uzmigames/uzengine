/// UzEngine G-Buffer Debug Visualization Pass — Implementation
///
/// Registers and executes a fullscreen debug pass that reads the three
/// G-Buffer textures and writes a single diagnostic channel (selected at
/// runtime via `GBufferDebugMode`) to the backbuffer.

#include "gbuffer_debug_pass.h"

#include "constant_buffers.h"
#include "multi_light.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddGBufferDebugPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddGBufferDebugPass(rg::RgBuilder& builder, const GBufferDebugPassDesc& desc)
{
    UZ_CHECKF(static_cast<uint32_t>(desc.mode) < static_cast<uint32_t>(GBufferDebugMode::Count),
              "GBufferDebugPass: mode value out of range");

    builder.AddPass("GBufferDebug", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Read(desc.albedoGBuffer,     RHIResourceState::ShaderResource);
        pb.Read(desc.normalGBuffer,     RHIResourceState::ShaderResource);
        pb.Read(desc.depthGBuffer,      RHIResourceState::ShaderResource);
        pb.Read(desc.velocityGBuffer,   RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("GBufferDebug::Execute");

            auto bb = res.GetTexture(desc.backbuffer);

            // ── Begin render pass — 1 color attachment, no depth ─────────
            // Debug pass writes directly over the backbuffer; no clear is
            // issued because the fullscreen triangle covers every pixel.
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount          = 1;
            passDesc.colorAttachments[0].texture   = bb;
            passDesc.colorAttachments[0].clear     = false;
            passDesc.hasDepth                      = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind debug PSO ───────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind per-frame CB at b1 (root param 1) ───────────────────
            // Required for invViewProj when WorldPos mode is active.
            cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                            desc.perFrameAlloc.offsetBytes,
                            desc.perFrameAlloc.cpuPtr, 256);

            // ── Bind G-Buffer textures + debug mode ──────────────────────
            if (desc.bindless) {
                // SM6.6: push 4 SRV descriptor indices at root param 0 (b0).
                // Layout: [albedoSrv, normalSrv, depthSrv, velocitySrv]
                const uint32_t rootData[4] = {
                    desc.albedoSrvIndex,
                    desc.normalSrvIndex,
                    desc.depthSrvIndex,
                    desc.velocitySrvIndex,
                };
                cmd->SetRootConstants(kSlotRootConstants, rootData, 4);

                // Bind debugMode via root CBV at b2 (kSlotPerObject reused
                // for fullscreen passes that have no per-object data).
                cmd->SetRootCBV(kSlotPerObject, desc.debugAlloc.bufferHandle,
                                desc.debugAlloc.offsetBytes,
                                desc.debugAlloc.cpuPtr, 256);
            } else {
                // SM5: push debug mode as root constant at b0.
                const uint32_t modeValue = static_cast<uint32_t>(desc.mode);
                cmd->SetRootConstants(kSlotRootConstants, &modeValue, 1);

                // SM5: bind textures directly at fragment texture slots t0-t3.
                cmd->SetFragmentTexture(0, desc.albedoTexture);
                cmd->SetFragmentTexture(1, desc.normalTexture);
                cmd->SetFragmentTexture(2, desc.depthTextureForSrv);
                cmd->SetFragmentTexture(3, desc.velocityTexture);
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

            cmd->EndRenderPass();

            // First-frame diagnostic logging
            static bool loggedOnce = false;
            if (!loggedOnce) {
                UZ_LOG(Renderer, Info,
                       "GBufferDebugPass: first execution — mode={}, viewport={}x{}, bindless={}, "
                       "albedoSRV={}, normalSRV={}, depthSRV={}, velocitySRV={}",
                       static_cast<uint32_t>(desc.mode),
                       desc.viewportWidth, desc.viewportHeight, desc.bindless,
                       desc.albedoSrvIndex, desc.normalSrvIndex,
                       desc.depthSrvIndex, desc.velocitySrvIndex);

                if (desc.bindless) {
                    if (desc.albedoSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "GBufferDebugPass: albedoSrvIndex is UINT32_MAX");
                    if (desc.normalSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "GBufferDebugPass: normalSrvIndex is UINT32_MAX");
                    if (desc.depthSrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "GBufferDebugPass: depthSrvIndex is UINT32_MAX");
                    if (desc.velocitySrvIndex == UINT32_MAX)
                        UZ_LOG(Renderer, Error, "GBufferDebugPass: velocitySrvIndex is UINT32_MAX");
                }
                loggedOnce = true;
            }
        });
    });
}

} // namespace uz
