/// UzEngine Scene Renderer — Geometry passes: depth prepass, Hi-Z pyramid,
/// two-phase GPU frustum+HiZ occlusion culling, GBuffer, velocity buffer,
/// and forward PBR fallback.
///
/// # Two-Phase Occlusion Culling Flow
/// @ref Wihlidal 2015 "Optimizing the Graphics Pipeline with Compute"
///
/// 1. **Phase 1 cull** (previous frame's HiZ): identifies "definitely visible"
///    instances. On the first frame (no prev HiZ), runs frustum-only.
/// 2. **Depth prepass**: draws Phase 1 visible via indirect draw. When two-phase
///    is inactive (first frame or GPU cull off), draws all instances normally.
/// 3. **HiZ pyramid build**: generates current frame's hierarchical depth buffer.
/// 4. **Phase 2 cull** (current frame's HiZ): re-tests ALL instances. Objects
///    that pass Phase 2 but were rejected by Phase 1 are "newly visible."
/// 5. **GBuffer**: draws Phase 1 visible + Phase 2 newly visible (two indirect
///    draws). Falls back to single indirect when two-phase is inactive.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "quaternion.h"
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"

#include <algorithm>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: register a copy pass that initializes an indirect args buffer
// ─────────────────────────────────────────────────────────────────────────────

/// Registers a render graph pass that copies zeroed DrawIndexedIndirectArgs
/// from the CB ring (upload heap) to a default-heap indirect args buffer.
/// The indexCountPerInstance field is pre-filled from mesh0; instanceCount
/// is zeroed so the GPU cull shader can atomically increment it.
///
/// # Arguments
/// * `rgBuilder` — Render graph builder.
/// * `passName` — Debug name for the copy pass.
/// * `cbRing` — CB ring allocator for staging memory.
/// * `mesh0` — First mesh (provides indexCount for indirect args).
/// * `dstBuffer` — Default-heap indirect args buffer to initialize.
static void RegisterInitIndirectArgsPass(
    rg::RgBuilder& rgBuilder,
    const char* passName,
    CBRingAllocator& cbRing,
    const GPUMesh& mesh0,
    RHIBufferHandle dstBuffer)
{
    CBAllocation staging = cbRing.Allocate(sizeof(RHIDrawIndexedIndirectArgs));
    if (!staging.cpuPtr) return;

    auto* args = reinterpret_cast<RHIDrawIndexedIndirectArgs*>(staging.cpuPtr);
    args->indexCountPerInstance  = mesh0.indexCount;
    args->instanceCount          = 0;  // GPU cull shader will atomically increment
    args->startIndexLocation     = 0;
    args->baseVertexLocation     = 0;
    args->startInstanceLocation  = 0;

    const RHIBufferHandle srcBuf = staging.bufferHandle;
    const uint64_t srcOff = staging.offsetBytes;
    rgBuilder.AddPass(passName, [srcBuf, srcOff, dstBuffer](rg::RgPassBuilder& pb) {
        pb.SetExecutionCallback([srcBuf, srcOff, dstBuffer](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("GpuCull::InitIndirectArgs");
            // Transition indirect args buffer to copy dest.
            RHIBarrierDesc barrier{};
            barrier.buffer      = dstBuffer;
            barrier.stateBefore = RHIResourceState::IndirectArgument;
            barrier.stateAfter  = RHIResourceState::CopyDst;
            cmd->ResourceBarrier(&barrier, 1);

            cmd->CopyBuffer(srcBuf, srcOff, dstBuffer, 0,
                            sizeof(RHIDrawIndexedIndirectArgs));

            // Transition to UAV for the cull pass to write.
            barrier.stateBefore = RHIResourceState::CopyDst;
            barrier.stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(&barrier, 1);
        });
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// RegisterGeometryPasses
// ─────────────────────────────────────────────────────────────────────────────

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
        // ── Deferred: Two-Phase Occlusion + DepthPrepass + Hi-Z + GBuffer ──
        if (!m_meshes.empty()) {

            // Determine if GPU culling conditions are met for this frame.
            const bool gpuCullReady = ctx.gpuCullingActive
                && ctx.gpuCullCBAlloc.cpuPtr != nullptr
                && ctx.boundingSphereSrvIndex != UINT32_MAX
                && m_meshes.size() > 0 && m_meshes[0].IsValid();

            const uint32_t instCount = gpuCullReady
                ? static_cast<uint32_t>(ctx.instanceDataVec.size()) : 0;

            // ── Phase 1: GPU Cull against previous frame's HiZ ───────────
            // Runs BEFORE the depth prepass. Phase 1 identifies "definitely
            // visible" instances using the previous frame's depth information.
            // On the first frame (prevHizMipCount == 0), Phase 1 still runs
            // but with HiZ disabled (frustum-only), which is correct — all
            // frustum-visible instances pass and get drawn in the depth prepass.
            if (gpuCullReady && m_depthPrepassEnabled) {
                // Initialize Phase 1 indirect args buffer.
                RegisterInitIndirectArgsPass(
                    rgBuilder, "GpuCull_InitIndirectArgs_Phase1",
                    *m_cbRing, m_meshes[0], ctx.indirectArgsBuffer);

                // Phase 1 cull dispatch.
                GpuCullPassDesc phase1Desc{};
                phase1Desc.pipeline              = pipelines->GetGpuCull();
                phase1Desc.cullCBAlloc           = ctx.gpuCullCBAlloc;  // prev-frame HiZ
                phase1Desc.boundingSphereSrvIndex = ctx.boundingSphereSrvIndex;
                phase1Desc.visibleIndexUavIndex  = ctx.visibleIndexUavIndex;
                phase1Desc.indirectArgsUavIndex  = ctx.indirectArgsUavIndex;
                phase1Desc.visibleIndexBuffer    = ctx.visibleIndexBuffer;
                phase1Desc.indirectArgsBuffer    = ctx.indirectArgsBuffer;
                phase1Desc.instanceCount         = instCount;
                phase1Desc.bindless              = bindless;
                AddGpuCullPass(rgBuilder, phase1Desc);
            }

            // ── Depth Prepass ─────────────────────────────────────────────
            // When two-phase culling is active, the depth prepass draws only
            // Phase 1 visible instances via indirect draw. This builds a
            // partial depth buffer that the HiZ pyramid uses to catch newly
            // revealed objects in Phase 2.
            // When GPU culling is inactive or on the first frame, the prepass
            // draws all instances normally.
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

                    // Use indirect draw when Phase 1 cull has run (GPU cull active
                    // with depth prepass). This draws only Phase 1 visible instances.
                    if (gpuCullReady && ctx.indirectArgsBuffer.IsValid()) {
                        zpDesc.useIndirectDraw  = true;
                        zpDesc.indirectArgsBuffer = ctx.indirectArgsBuffer;
                    }
                }
                AddDepthPrepass(rgBuilder, zpDesc);

                // ── Hi-Z Pyramid (runs after depth prepass) ───────────────
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

                // ── Phase 2: GPU Cull against CURRENT frame's HiZ ────────
                // Re-tests ALL instances against the updated HiZ pyramid.
                // Objects that pass Phase 2 but were rejected by Phase 1
                // are "newly visible" (e.g., camera moved to reveal them).
                // Phase 2 only runs when two-phase occlusion is active
                // (previous frame's HiZ was available for Phase 1).
                if (gpuCullReady && ctx.twoPhaseOcclusion
                    && ctx.gpuCullCBAlloc2.cpuPtr != nullptr)
                {
                    // Initialize Phase 2 indirect args buffer.
                    RegisterInitIndirectArgsPass(
                        rgBuilder, "GpuCull_InitIndirectArgs_Phase2",
                        *m_cbRing, m_meshes[0], ctx.indirectArgsBuffer2);

                    // Phase 2 cull dispatch — uses current frame's HiZ.
                    GpuCullPassDesc phase2Desc{};
                    phase2Desc.pipeline              = pipelines->GetGpuCull();
                    phase2Desc.cullCBAlloc           = ctx.gpuCullCBAlloc2;  // current HiZ
                    phase2Desc.boundingSphereSrvIndex = ctx.boundingSphereSrvIndex;
                    phase2Desc.visibleIndexUavIndex  = ctx.visibleIndexUavIndex2;
                    phase2Desc.indirectArgsUavIndex  = ctx.indirectArgsUavIndex2;
                    phase2Desc.visibleIndexBuffer    = ctx.visibleIndexBuffer2;
                    phase2Desc.indirectArgsBuffer    = ctx.indirectArgsBuffer2;
                    phase2Desc.instanceCount         = instCount;
                    phase2Desc.bindless              = bindless;
                    AddGpuCullPass(rgBuilder, phase2Desc);
                }
            }

            // ── DBuffer Decal Render Pass (after depth prepass, before GBuffer) ──
            // @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
            // Gathers all DecalComponent entities, sorts by priority, and renders
            // into three DBuffer targets. Requires depth for world pos reconstruction.
            if (ctx.dbufferARes.IsValid() && ctx.world) {
                // Gather decals from ECS
                std::vector<DecalInfo> decalInfos;
                auto decalView = ctx.world->View<DecalComponent, SceneComponent>();
                for (auto [enttEntity, decal, scene] : decalView.each()) {
                    if (!decal.enabled) continue;

                    const Mat4& worldMat = scene.GetWorldMatrix();

                    // Build decal world matrix incorporating extents as scale.
                    // The unit cube [-0.5, 0.5]^3 is scaled by 2*extents to form
                    // the decal OBB. We bake the extents into the world matrix
                    // by pre-multiplying a scale matrix.
                    Vec3 extentScale(decal.extentsX * 2.0f,
                                     decal.extentsY * 2.0f,
                                     decal.extentsZ * 2.0f);
                    Mat4 scaleMat = Mat4::TRS(Vec3(0.0f, 0.0f, 0.0f),
                                              Quat::Identity(),
                                              extentScale);
                    Mat4 decalWorldMat = scaleMat * worldMat;

                    DecalInfo info{};
                    info.cb.world         = decalWorldMat;
                    info.cb.invWorld      = decalWorldMat.Inverse();
                    info.cb.opacity       = decal.opacity;
                    info.cb.normalBlend   = decal.normalBlend;
                    info.cb.roughness     = decal.roughness;
                    info.cb.metallic      = decal.metallic;
                    info.cb.fadeDistance   = decal.fadeDistance;
                    info.cb.angleFade     = decal.angleFadeStart;
                    info.cb.albedoSrv     = decal.albedoTextureId;
                    info.cb.normalSrv     = decal.normalTextureId;
                    info.cb.materialSrv   = decal.materialTextureId;
                    info.cb.depthSrv      = m_gbuffer.depthSRV;
                    info.cb.normalGbufSrv = m_gbuffer.normalSRV;
                    info.priority         = decal.priority;

                    decalInfos.push_back(info);
                }

                // Sort decals by priority (ascending — higher priority renders last).
                std::sort(decalInfos.begin(), decalInfos.end(),
                    [](const DecalInfo& a, const DecalInfo& b) {
                        return a.priority < b.priority;
                    });

                if (!decalInfos.empty()) {
                    // Store sorted decals in a member to keep them alive until Execute.
                    m_frameDecals = std::move(decalInfos);

                    DecalRenderPassDesc decalDesc{};
                    decalDesc.dbufferARes    = ctx.dbufferARes;
                    decalDesc.dbufferBRes    = ctx.dbufferBRes;
                    decalDesc.dbufferCRes    = ctx.dbufferCRes;
                    decalDesc.dbufferATex    = m_dbufferA;
                    decalDesc.dbufferBTex    = m_dbufferB;
                    decalDesc.dbufferCTex    = m_dbufferC;
                    decalDesc.depthRes       = ctx.depthRes;
                    decalDesc.normalRes      = ctx.normalRes;
                    decalDesc.pipeline       = pipelines->GetDecalRender();
                    decalDesc.cbAllocator    = m_cbRing.get();
                    decalDesc.decals         = m_frameDecals.data();
                    decalDesc.decalCount     = static_cast<uint32_t>(m_frameDecals.size());
                    decalDesc.viewportWidth  = vpWidth;
                    decalDesc.viewportHeight = vpHeight;

                    AddDecalRenderPass(rgBuilder, decalDesc);
                }
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

                // When GPU culling is active, use indirect draw so the GPU-written
                // instance count controls how many instances are drawn.
                if (gpuCullReady && ctx.indirectArgsBuffer.IsValid()) {
                    gbDesc.useIndirectDraw    = true;
                    gbDesc.indirectArgsBuffer = ctx.indirectArgsBuffer;

                    // When two-phase occlusion is active, issue a second indirect
                    // draw for Phase 2 newly-visible instances.
                    if (ctx.twoPhaseOcclusion && ctx.indirectArgsBuffer2.IsValid()) {
                        gbDesc.usePhase2IndirectDraw = true;
                        gbDesc.indirectArgsBuffer2   = ctx.indirectArgsBuffer2;
                    }
                }
            }
            AddGBufferPass(rgBuilder, gbDesc);
        }

        // ── Velocity Buffer Pass (after GBuffer, before GTAO) ─────────
        // Generates per-pixel screen-space motion vectors by reprojecting
        // depth-reconstructed world positions through prevViewProj.
        // @ref camera-only velocity pass
        if (m_prevViewProjValid && m_gbuffer.velocity.IsValid()) {
            VelocityCB velCB{};
            velCB.prevViewProj          = m_prevViewProj;
            velCB.unjitteredInvViewProj = ctx.unjitteredViewProj.Inverse();
            velCB.screenWidth           = vpWidth;
            velCB.screenHeight          = vpHeight;

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
