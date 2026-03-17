/// UzEngine Deferred Lighting Pass — Implementation
///
/// Registers and executes a fullscreen lighting pass that reads G-Buffer
/// textures, evaluates point lights, and writes the composited result to
/// the backbuffer.
///
/// Supports two binding modes:
/// 1. **Binder path** (perFrameParams != nullptr): Uses ShaderParameterBinder
///    to upload per-frame CB and bind IBL textures automatically from metadata.
/// 2. **Legacy path** (perFrameParams == nullptr): Uses pre-allocated CBAllocations
///    and manual SetFragmentTexture calls for SM5 texture binding.

#include "lighting_pass.h"

#include "constant_buffers.h"
#include "multi_light.h"
#include "render_graph_builder.h"
#include "render_graph_pass.h"
#include "root_slots.h"
#include "shader_parameter_binder.h"
#include "engine/rhi/rhi.h"

#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddLightingPass — registers the pass in the render graph
// ─────────────────────────────────────────────────────────────────────────────

void AddLightingPass(rg::RgBuilder& builder, const LightingPassDesc& desc)
{
    builder.AddPass("DeferredLighting", [desc](rg::RgPassBuilder& pb) {
        pb.Write(desc.backbuffer, RHIResourceState::RenderTarget);
        pb.Read(desc.albedoGBuffer,   RHIResourceState::ShaderResource);
        pb.Read(desc.normalGBuffer,   RHIResourceState::ShaderResource);
        pb.Read(desc.depthGBuffer,    RHIResourceState::ShaderResource);
        pb.Read(desc.positionGBuffer, RHIResourceState::ShaderResource);
        if (desc.shadowAtlasResource.IsValid()) {
            pb.Read(desc.shadowAtlasResource, RHIResourceState::ShaderResource);
        }

        pb.SetExecutionCallback([desc](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("DeferredLighting::Execute");

            auto bb = res.GetTexture(desc.backbuffer);

            // ── Clear backbuffer ─────────────────────────────────────────
            constexpr float kClearColor[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            cmd->ClearRenderTarget(bb, kClearColor);

            // ── Begin render pass — 1 color attachment, no depth ─────────
            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = false;

            cmd->BeginRenderPass(passDesc);

            // ── Bind pipeline ────────────────────────────────────────────
            cmd->SetPipeline(desc.pipeline);

            // ── Bind per-frame CB + IBL resources ────────────────────────
            if (desc.perFrameParams != nullptr && desc.cbRingAllocator != nullptr) {
                // Binder path: upload PerFrameParams CB + bind IBL textures via metadata.
                ShaderParameterBinder::Bind(
                    cmd, desc.perFrameParams,
                    PerFrameParams::GetMetadata(),
                    *desc.cbRingAllocator, kSlotPerFrame,
                    desc.bindless);
            } else {
                // Legacy path: use pre-allocated CB allocation.
                cmd->SetRootCBV(kSlotPerFrame, desc.perFrameAlloc.bufferHandle,
                                desc.perFrameAlloc.offsetBytes,
                                desc.perFrameAlloc.cpuPtr, desc.perFrameAlloc.sizeBytes);

                // SM5 IBL textures — manual binding when not using the binder.
                if (!desc.bindless) {
                    if (desc.iblIrradiance.IsValid()) {
                        cmd->SetFragmentTexture(5, desc.iblIrradiance);
                    }
                    if (desc.iblPrefiltered.IsValid()) {
                        cmd->SetFragmentTexture(6, desc.iblPrefiltered);
                    }
                    if (desc.iblBrdfLut.IsValid()) {
                        cmd->SetFragmentTexture(7, desc.iblBrdfLut);
                    }
                }
            }

            // ── Bind lighting CB ─────────────────────────────────────────
            cmd->SetRootCBV(kSlotPerObject, desc.lightingAlloc.bufferHandle,
                            desc.lightingAlloc.offsetBytes,
                            desc.lightingAlloc.cpuPtr, sizeof(LightingCB));

            // ── Bind shadow data at b4 (root param 3) ───────────────────
            if (desc.shadowDataParams != nullptr && desc.cbRingAllocator != nullptr) {
                // Binder path: upload ShadowDataParams CB + bind shadow textures via metadata.
                ShaderParameterBinder::Bind(
                    cmd, desc.shadowDataParams,
                    ShadowDataParams::GetMetadata(),
                    *desc.cbRingAllocator, kSlotShadow,
                    desc.bindless);
            } else if (desc.shadowAlloc.cpuPtr != nullptr) {
                // Legacy path: use pre-allocated shadow CB allocation.
                cmd->SetRootCBV(kSlotShadow, desc.shadowAlloc.bufferHandle,
                                desc.shadowAlloc.offsetBytes,
                                desc.shadowAlloc.cpuPtr, sizeof(ShadowDataCB));
            }

            // ── Bind G-Buffer textures ───────────────────────────────────
            if (desc.bindless) {
                // SM6.6: push descriptor indices for G-Buffer SRVs.
                const uint32_t indices[4] = {
                    desc.albedoSrvIndex,
                    desc.normalSrvIndex,
                    desc.depthSrvIndex,
                    desc.positionSrvIndex
                };
                cmd->SetRootConstants(kSlotRootConstants, indices, 4);
            } else {
                // SM5: bind G-Buffer textures directly at fragment texture slots t0-t4.
                cmd->SetFragmentTexture(0, desc.albedoTexture);
                cmd->SetFragmentTexture(1, desc.normalTexture);
                cmd->SetFragmentTexture(2, desc.depthTextureForSrv);
                cmd->SetFragmentTexture(3, desc.shadowAtlasTexture);
                cmd->SetFragmentTexture(4, desc.positionTexture);
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
        });
    });
}

} // namespace uz
