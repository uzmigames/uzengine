/// UzEngine Scene Renderer — Geometry passes: depth prepass, Hi-Z pyramid,
/// GPU frustum+HiZ occlusion culling, GBuffer, velocity buffer, and forward PBR fallback.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"

#include <cstring>

namespace uz {

void SceneRenderer::RegisterGeometryPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    const bool bindless = ctx.bindless;
    auto* pipelines = ctx.pipelines;
    // Geometry passes (GBuffer, depth prepass, velocity) render at internal
    // render resolution, not display resolution. The TAA resolve pass upscales
    // to display resolution later in the pipeline.
    const float vpWidth = ctx.renderWidth;
    const float vpHeight = ctx.renderHeight;

    if (m_renderPath == RenderPath::Deferred) {
        // ── Deferred: optional DepthPrepass + Hi-Z + GBuffer ──────────
        if (!m_meshes.empty()) {
            // ── Depth Prepass (optional, enabled by default) ─────────────
            if (m_depthPrepassEnabled) {
                DepthPrepassDesc zpDesc{};
                zpDesc.depthBuffer      = ctx.depthRes;
                zpDesc.depthTexture     = m_gbuffer.depth;
                zpDesc.drawList         = &ctx.drawList;
                zpDesc.cbAllocator      = m_cbRing.get();
                zpDesc.perFrameAlloc    = ctx.perFrameAlloc;
                zpDesc.pipeline         = pipelines->GetDepthPrepass(bindless);
                zpDesc.meshes           = m_meshes.data();
                zpDesc.meshCount        = static_cast<uint32_t>(m_meshes.size());
                zpDesc.bindless         = bindless;
                zpDesc.viewportWidth    = vpWidth;
                zpDesc.viewportHeight   = vpHeight;
                zpDesc.cbRingAllocator  = m_cbRing.get();
                if (ctx.useInstancing) {
                    zpDesc.instanced              = true;
                    zpDesc.instancedPipeline      = pipelines->GetDepthPrepassInstanced();
                    zpDesc.instanceBufferSrvIndex = ctx.instanceBufferSrvIndex;
                    zpDesc.batchData              = ctx.instanceBatches.data();
                    zpDesc.batchCount             = static_cast<uint32_t>(ctx.instanceBatches.size());
                }
                AddDepthPrepass(rgBuilder, zpDesc);

                // ── Hi-Z Pyramid (runs after depth prepass) ─────────────
                if (m_hizMipCount > 0) {
                    HiZPyramidPassDesc hizDesc{};
                    hizDesc.depthSource          = ctx.depthRes;
                    hizDesc.depthSourceTexture   = m_gbuffer.depth;
                    hizDesc.depthSourceSrvIndex  = m_gbuffer.depthSRV;
                    hizDesc.mipCount             = m_hizMipCount;
                    hizDesc.pipeline             = pipelines->GetHiZPyramid();
                    hizDesc.sourceWidth          = m_renderWidth;
                    hizDesc.sourceHeight         = m_renderHeight;
                    hizDesc.bindless             = bindless;
                    for (uint32_t i = 0; i < m_hizMipCount; ++i) {
                        hizDesc.mipLevels[i] = m_hizMipLevels[i];
                    }
                    AddHiZPyramidPass(rgBuilder, hizDesc);
                }

                // ── GPU Frustum + HiZ Occlusion Culling (after Hi-Z, before GBuffer) ──
                // @ref HZBTestPS
                //
                // The GPU cull pass is registered when occlusion culling is enabled
                // and bounding sphere data has been uploaded. The pass writes to a
                // visibility buffer that downstream passes can consume for indirect
                // draw or CPU readback filtering.
                //
                // Currently gated behind m_gpuOcclusionCullingEnabled (default: false).
                // Bounding sphere upload and indirect draw integration are future work
                // tracked in the depth-prepass-hiz-pyramid tasks.
            }

            // ── GBuffer Pass ─────────────────────────────────────────────
            GBufferPassDesc gbDesc{};
            gbDesc.albedoTarget         = ctx.albedoRes;
            gbDesc.normalTarget         = ctx.normalRes;
            gbDesc.positionTarget       = ctx.positionRes;
            gbDesc.emissiveTarget       = ctx.emissiveRes;
            gbDesc.depthBuffer          = ctx.depthRes;
            gbDesc.depthTexture         = m_gbuffer.depth;
            gbDesc.drawList             = &ctx.drawList;
            gbDesc.cbAllocator          = m_cbRing.get();
            gbDesc.cbRingAllocator      = m_cbRing.get();
            gbDesc.perFrameAlloc        = ctx.perFrameAlloc;
            gbDesc.perFrameParams       = &ctx.perFrame;
            gbDesc.pipeline             = pipelines->GetGBuffer(bindless);
            gbDesc.maskedPipeline       = pipelines->GetGBufferMasked(bindless);
            gbDesc.meshes               = m_meshes.data();
            gbDesc.meshCount            = static_cast<uint32_t>(m_meshes.size());
            gbDesc.fallbackWhite        = m_fallbackWhite;
            gbDesc.fallbackBlack        = m_fallbackBlack;
            gbDesc.fallbackNormal       = m_fallbackNormal;
            gbDesc.bindless             = bindless;
            gbDesc.viewportWidth        = vpWidth;
            gbDesc.viewportHeight       = vpHeight;
            if (ctx.useInstancing) {
                gbDesc.instanced              = true;
                gbDesc.instancedPipeline      = pipelines->GetGBufferInstanced();
                gbDesc.instanceBufferSrvIndex = ctx.instanceBufferSrvIndex;
                gbDesc.batchData              = ctx.instanceBatches.data();
                gbDesc.batchCount             = static_cast<uint32_t>(ctx.instanceBatches.size());
            }
            AddGBufferPass(rgBuilder, gbDesc);
        }

        // ── Velocity Buffer Pass (after GBuffer, before GTAO) ─────────
        // Generates per-pixel screen-space motion vectors by reprojecting
        // depth-reconstructed world positions through prevViewProj.
        // @ref camera-only velocity pass
        if (m_prevViewProjValid && m_gbuffer.velocity.IsValid()) {
            VelocityCB velCB{};
            velCB.prevViewProj = m_prevViewProj;
            velCB.screenWidth  = vpWidth;
            velCB.screenHeight = vpHeight;

            CBAllocation velCBAlloc = m_cbRing->Allocate(sizeof(VelocityCB));
            std::memcpy(velCBAlloc.cpuPtr, &velCB, sizeof(VelocityCB));

            VelocityPassDesc velDesc{};
            velDesc.velocityTarget  = ctx.velocityRes;
            velDesc.depthBuffer     = ctx.depthRes;
            velDesc.velocityTexture = m_gbuffer.velocity;
            velDesc.depthTexture    = m_gbuffer.depth;
            velDesc.depthSrvIndex   = m_gbuffer.depthSRV;
            velDesc.perFrameAlloc   = ctx.perFrameAlloc;
            velDesc.velocityCBAlloc = velCBAlloc;
            velDesc.pipeline        = pipelines->GetVelocityPass();
            velDesc.viewportWidth   = vpWidth;
            velDesc.viewportHeight  = vpHeight;
            velDesc.bindless        = bindless;

            AddVelocityPass(rgBuilder, velDesc);
        }
    } else {
        // ── Forward: single-pass PBR ────────────────────────────────────
        if (!m_meshes.empty()) {
            PBRPassDesc fwdDesc{};
            fwdDesc.backbuffer           = ctx.hdrRes;
            fwdDesc.depthBuffer          = ctx.depthRes;
            fwdDesc.depthTexture         = m_gbuffer.depth;
            fwdDesc.drawList             = &ctx.drawList;
            fwdDesc.cbAllocator          = m_cbRing.get();
            fwdDesc.perFrameAlloc        = ctx.perFrameAlloc;
            fwdDesc.pipeline             = pipelines->GetForwardPBR(bindless);
            fwdDesc.meshes               = m_meshes.data();
            fwdDesc.meshCount            = static_cast<uint32_t>(m_meshes.size());
            fwdDesc.bindless             = bindless;
            fwdDesc.viewportWidth        = vpWidth;
            fwdDesc.viewportHeight       = vpHeight;
            AddPBRPass(rgBuilder, fwdDesc);
        }
    }
}

} // namespace uz
