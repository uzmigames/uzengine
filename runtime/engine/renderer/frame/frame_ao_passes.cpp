/// UzEngine Scene Renderer — AO-related passes: DBuffer apply, GTAO, and
/// Distance Field Shadows (DFS cull + cone trace).
///
/// Extracted from frame_lighting.cpp. All functions are private methods of
/// SceneRenderer, called by RegisterLightingPasses().

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"

#include <cmath>
#include <cstring>

namespace uz {

// ── DBuffer Apply Pass ──────────────────────────────────────────────────────
// Merges the DBuffer decal targets into the GBuffer albedo and normal
// textures via a fullscreen compute pass. Only runs if the DBuffer was
// rendered (m_frameDecals is non-empty) and UAV indices are available.
// @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"

void SceneRenderer::RegisterDBufferApplyPass(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_frameDecals.empty()
        && ctx.dbufferARes.IsValid()
        && m_gbuffer.albedoUAV != UINT32_MAX
        && m_gbuffer.normalUAV != UINT32_MAX) {

        DecalApplyPassDesc applyDesc{};
        applyDesc.dbufferARes       = ctx.dbufferARes;
        applyDesc.dbufferBRes       = ctx.dbufferBRes;
        applyDesc.dbufferCRes       = ctx.dbufferCRes;
        applyDesc.dbufferASrvIndex  = m_dbufferASrv;
        applyDesc.dbufferBSrvIndex  = m_dbufferBSrv;
        applyDesc.dbufferCSrvIndex  = m_dbufferCSrv;
        applyDesc.albedoRes         = ctx.albedoRes;
        applyDesc.normalRes         = ctx.normalRes;
        applyDesc.albedoUavIndex    = m_gbuffer.albedoUAV;
        applyDesc.normalUavIndex    = m_gbuffer.normalUAV;
        applyDesc.pipeline          = ctx.pipelines->GetDecalApply();
        applyDesc.width             = m_renderWidth;
        applyDesc.height            = m_renderHeight;

        AddDecalApplyPass(rgBuilder, applyDesc);
    }
}

// ── GTAO Pass (after GBuffer, before lighting) ─────────────────────────────

void SceneRenderer::RegisterGTAOPass(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // IsEnabled() is a live CVar query — settings.enabled is only refreshed
    // inside Execute() and would be stale here, causing the graph to still
    // schedule the GTAO pass after r.GTAO=0 for at least one frame.
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->IsEnabled()
        && m_passInspector.IsPassEnabled("gtao")) {
        rgBuilder.AddPass("GTAO", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("GTAO::Execute");

                // Get view matrix from the camera.
                const auto& cam = m_cameraManager->GetActiveCamera();
                float viewMatrixData[16];
                const auto& viewMat = cam.GetView();
                std::memcpy(viewMatrixData, &viewMat, 16 * sizeof(float));

                float invTanHalfFov = 1.0f / std::tan(cam.GetFovY() * 0.5f);
                float aspectRatio = static_cast<float>(m_renderWidth) / static_cast<float>(m_renderHeight);

                // Log input SRV indices being passed to GTAO on first frame
                static bool loggedGtaoInputs = false;
                if (!loggedGtaoInputs) {
                    UZ_LOG(Renderer, Info,
                           "SceneRenderer: GTAO inputs — depthSRV={}, normalSRV={}, "
                           "depthHandle={}, normalHandle={}, size={}x{}, frame={}",
                           m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                           m_gbuffer.depth.IsValid(), m_gbuffer.normal.IsValid(),
                           m_renderWidth, m_renderHeight, ctx.frameIndex);
                    loggedGtaoInputs = true;
                }

                // Hi-Z mip 0 (half-res min-depth) for cache-friendly distant
                // horizon steps. Falls back gracefully when unavailable.
                const uint32_t hizSrv =
                    (m_hizMipCount > 0 && m_hizMipLevels[0].srvIndex != UINT32_MAX)
                        ? m_hizMipLevels[0].srvIndex : UINT32_MAX;

                const uint32_t gtaoResult = m_gtaoPass->Execute(
                    cmd, m_cbRing.get(),
                    m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                    m_gbuffer.depth, m_gbuffer.normal,
                    viewMatrixData,
                    invTanHalfFov, aspectRatio,
                    ctx.perFrame.nearPlane, ctx.perFrame.farPlane,
                    m_renderWidth, m_renderHeight,
                    ctx.frameIndex,
                    m_gbuffer.velocitySRV,
                    hizSrv);

                // Update perFrame with the actual AO SRV returned by Execute().
                // When temporal accumulation is active, the final AO is in a
                // history texture (not m_filteredAOTexture), so the SRV index
                // differs from what was pre-computed in SetupFrameData().
                // The deferred lighting pass reads perFrame via binder path
                // (pointer to ctx.perFrame), so this update takes effect.
                ctx.perFrame.gtaoSrvIndex = gtaoResult;

                if (gtaoResult == UINT32_MAX) {
                    UZ_LOG(Renderer, Warning,
                           "SceneRenderer: GTAO Execute() returned UINT32_MAX — "
                           "AO will not be applied this frame (depthSRV={}, normalSRV={})",
                           m_gbuffer.depthSRV, m_gbuffer.normalSRV);
                }
            });
        });
    }
}

// ── DFS Compute Passes (after GBuffer + GTAO, before lighting) ─────────────
// Runs only when DFS is enabled, an SDF atlas is bound and populated,
// and the DFS output texture exists. Two passes:
//   1. CullObjects: frustum-cull SDF objects into a compacted index buffer
//   2. ConeTrace: per-pixel ray march through culled SDFs → shadow texture

void SceneRenderer::RegisterDFSPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // Distance field shadow dispatch: cull objects first, then cone-trace.
    const bool dfsEnabled = m_dfsConfig.enabled
                          && m_sdfAtlas && m_sdfAtlas->IsInitialized()
                          && m_sdfAtlas->GetRegisteredObjectCount() > 0
                          && m_dfsOutputTexture.IsValid()
                          && m_dfsCulledIndicesBuffer.IsValid()
                          && m_dfsIndirectArgsBuffer.IsValid()
                          && m_passInspector.IsPassEnabled("dfs");
    if (!dfsEnabled) return;

    // Upload dirty SDF object transforms before dispatching cull/trace.
    m_sdfAtlas->UploadObjectBuffer();

    const uint32_t objectCount = m_sdfAtlas->GetRegisteredObjectCount();
    const uint32_t dfsWidth  = m_dfsConfig.fullResolution ? m_renderWidth  : m_renderWidth  / 2;
    const uint32_t dfsHeight = m_dfsConfig.fullResolution ? m_renderHeight : m_renderHeight / 2;
    const uint32_t downsampleFactor = m_dfsConfig.fullResolution ? 1 : 2;

    // ── Async compute note (§8.3) ─────────────────────────────────────
    // dispatches DFS on an async compute queue overlapping GBuffer
    // rendering (DistanceFieldShadowing.cpp:148 — FComputeFenceRHIRef).
    // Our render graph (RgBuilder) operates on a single IRHICommandList*
    // passed to Execute(), with no per-pass queue assignment. The RHI layer
    // supports RHIQueueType::Compute (CreateCommandList), but the render
    // graph would need:
    //   1. RgPassBuilder::SetQueue(RHIQueueType) to tag compute passes.
    //   2. RgBuilder::Execute() to manage multiple command lists + fences
    //      for cross-queue synchronization (UAV barriers across queues).
    //   3. Resource state tracking that understands concurrent queue access
    //      (D3D12 simultaneous-access / split barriers).
    // Until the render graph supports multi-queue execution, DFS dispatches
    // run on the direct (graphics) queue sequentially.

    // ── Pass 1: DFS Cull Objects (compute) ──────────────────────────
    /// DFS Pass 1: Frustum-cull SDF objects into a compacted index buffer.
    rgBuilder.AddPass("DFS_CullObjects", [&, objectCount](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx, objectCount](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("DFS::CullObjects");

            auto cullPipeline = ctx.pipelines->GetDfsCullObjects();
            if (!cullPipeline.IsValid()) return;

            cmd->SetPipeline(cullPipeline);

            // Build CullConstants cbuffer (56 DWORDs = 224 bytes).
            // Shadow convex hull planes from the primary cascade's view-projection.
            // For now, use the first cascade's viewProj to extract 6 frustum planes
            // expanded by the DFS max trace distance.
            struct CullConstantsCPU {
                float shadowConvexHull[12 * 4]; // float4 x 12 = 48 DW
                float shadowBoundingSphere[4];  // float4 = 4 DW
                uint32_t numShadowHullPlanes;   // 1 DW
                uint32_t totalObjectCount;      // 1 DW
                uint32_t pad[2];                // 2 DW
            };
            static_assert(sizeof(CullConstantsCPU) == 224, "CullConstants must be 224 bytes");

            CullConstantsCPU cullCB{};
            std::memset(&cullCB, 0, sizeof(cullCB));

            // Extract 6 frustum planes from the first cascade's VP matrix.
            // Each plane: float4(nx, ny, nz, d) where dot(n, p) - d < 0 = inside.
            // Extract 6 Gribb/Hartmann frustum planes from the VP matrix.
            if (ctx.totalCascadeCount > 0) {
                const Mat4& vp = ctx.shadowFrame.cascades[0].viewProj;
                // Left plane
                cullCB.shadowConvexHull[0]  = vp.m._14 + vp.m._11;
                cullCB.shadowConvexHull[1]  = vp.m._24 + vp.m._21;
                cullCB.shadowConvexHull[2]  = vp.m._34 + vp.m._31;
                cullCB.shadowConvexHull[3]  = vp.m._44 + vp.m._41;
                // Right plane
                cullCB.shadowConvexHull[4]  = vp.m._14 - vp.m._11;
                cullCB.shadowConvexHull[5]  = vp.m._24 - vp.m._21;
                cullCB.shadowConvexHull[6]  = vp.m._34 - vp.m._31;
                cullCB.shadowConvexHull[7]  = vp.m._44 - vp.m._41;
                // Bottom plane
                cullCB.shadowConvexHull[8]  = vp.m._14 + vp.m._12;
                cullCB.shadowConvexHull[9]  = vp.m._24 + vp.m._22;
                cullCB.shadowConvexHull[10] = vp.m._34 + vp.m._32;
                cullCB.shadowConvexHull[11] = vp.m._44 + vp.m._42;
                // Top plane
                cullCB.shadowConvexHull[12] = vp.m._14 - vp.m._12;
                cullCB.shadowConvexHull[13] = vp.m._24 - vp.m._22;
                cullCB.shadowConvexHull[14] = vp.m._34 - vp.m._32;
                cullCB.shadowConvexHull[15] = vp.m._44 - vp.m._42;
                // Near plane
                cullCB.shadowConvexHull[16] = vp.m._13;
                cullCB.shadowConvexHull[17] = vp.m._23;
                cullCB.shadowConvexHull[18] = vp.m._33;
                cullCB.shadowConvexHull[19] = vp.m._43;
                // Far plane
                cullCB.shadowConvexHull[20] = vp.m._14 - vp.m._13;
                cullCB.shadowConvexHull[21] = vp.m._24 - vp.m._23;
                cullCB.shadowConvexHull[22] = vp.m._34 - vp.m._33;
                cullCB.shadowConvexHull[23] = vp.m._44 - vp.m._43;

                // Normalize each plane
                for (uint32_t pi = 0; pi < 6; ++pi) {
                    float* p = &cullCB.shadowConvexHull[pi * 4];
                    float len = std::sqrt(p[0] * p[0] + p[1] * p[1] + p[2] * p[2]);
                    if (len > 0.0001f) {
                        float invLen = 1.0f / len;
                        p[0] *= invLen;
                        p[1] *= invLen;
                        p[2] *= invLen;
                        p[3] *= invLen;
                    }
                }

                cullCB.numShadowHullPlanes = 6;
            }

            // Directional light: no bounding sphere (w=0 disables sphere test)
            cullCB.shadowBoundingSphere[3] = 0.0f;
            cullCB.totalObjectCount = objectCount;

            // Upload cull constants via CB ring at slot 1 (b1 = per-frame CBV)
            CBAllocation cullAlloc = m_cbRing->Allocate(sizeof(CullConstantsCPU));
            if (cullAlloc.cpuPtr) {
                std::memcpy(cullAlloc.cpuPtr, &cullCB, sizeof(CullConstantsCPU));
                cmd->SetRootCBV(kSlotPerFrame, cullAlloc.bufferHandle,
                                cullAlloc.offsetBytes, cullAlloc.cpuPtr, sizeof(CullConstantsCPU));
            }

            // Clear indirect args buffer: reset culled object count to 0.
            // Allocate a tiny staging buffer from the CB ring (upload heap)
            // and CopyBuffer to the GPU-only indirect args buffer.
            {
                uint32_t clearZero[3] = {0, 1, 1}; // dispatch(0, 1, 1) — X filled by atomics
                CBAllocation clearAlloc = m_cbRing->Allocate(sizeof(clearZero));
                if (clearAlloc.cpuPtr) {
                    std::memcpy(clearAlloc.cpuPtr, clearZero, sizeof(clearZero));

                    // Transition indirect args to CopyDst for the clear
                    RHIBarrierDesc toCopy{};
                    toCopy.buffer      = m_dfsIndirectArgsBuffer;
                    toCopy.stateBefore = RHIResourceState::UnorderedAccess;
                    toCopy.stateAfter  = RHIResourceState::CopyDst;
                    cmd->ResourceBarrier(&toCopy, 1);

                    cmd->CopyBuffer(clearAlloc.bufferHandle, clearAlloc.offsetBytes,
                                    m_dfsIndirectArgsBuffer, 0, sizeof(clearZero));

                    // Transition back to UAV for atomic writes
                    RHIBarrierDesc toUav{};
                    toUav.buffer      = m_dfsIndirectArgsBuffer;
                    toUav.stateBefore = RHIResourceState::CopyDst;
                    toUav.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&toUav, 1);
                }
            }

            // Bind object data SRV at t0 (via descriptor table slot 5 for SM5,
            // or accessed via ResourceDescriptorHeap[] for ctx.bindless)
            if (!m_device->IsBindless()) {
                cmd->SetDescriptorTable(5, m_sdfAtlas->GetObjectBufferSrvIndex());
            }

            // Bind culled indices UAV (u0) and indirect args UAV (u1)
            cmd->SetDescriptorTable(4, m_dfsCulledIndicesUavIndex);

            // Dispatch: one thread per SDF object, 64 threads per group
            const uint32_t cullGroupsX = (objectCount + 63) / 64;
            cmd->Dispatch(cullGroupsX, 1, 1);

            // UAV barriers: synchronize cull writes before cone trace reads.
            // Both buffers stay in UnorderedAccess state — the barrier is a
            // UAV synchronization (same state before and after).
            RHIBarrierDesc postCullBarriers[2] = {};
            postCullBarriers[0].buffer      = m_dfsCulledIndicesBuffer;
            postCullBarriers[0].stateBefore = RHIResourceState::UnorderedAccess;
            postCullBarriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
            postCullBarriers[1].buffer      = m_dfsIndirectArgsBuffer;
            postCullBarriers[1].stateBefore = RHIResourceState::UnorderedAccess;
            postCullBarriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
            cmd->ResourceBarrier(postCullBarriers, 2);
        });
    });

    // ── Pass 2: DFS Cone Trace (compute) ────────────────────────────
    /// DFS Pass 2: Per-pixel cone trace through culled SDFs to produce shadow mask.
    rgBuilder.AddPass("DFS_ConeTrace", [&, objectCount, dfsWidth, dfsHeight, downsampleFactor](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx, objectCount, dfsWidth, dfsHeight, downsampleFactor](
                IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("DFS::ConeTrace");

            auto coneTracePipeline = ctx.pipelines->GetDfsConeTrace();
            if (!coneTracePipeline.IsValid()) return;

            cmd->SetPipeline(coneTracePipeline);

            // Build DfsShadowParams cbuffer.
            // The cone trace shader reads this at b0 as a cbuffer.
            // Since it's larger than 8 DWORDs, we bind it as a CBV via the ring allocator.
            // CPU-side mirror of cbuffer DfsShadowParams in dfs_cone_trace_cs.hlsl.
            // Must match HLSL 16-byte row packing exactly.
            struct DfsConeTraceCB {
                // Row 0-3: float4x4 (64 bytes)
                float invViewProj[16];
                // Row 4: float3 + float (16 bytes)
                float lightDirX;
                float lightDirY;
                float lightDirZ;
                float tanLightAngle;
                // Row 5: 4 floats (16 bytes)
                float maxTraceDistance;
                float selfShadowBias;
                float nearPlane;
                float farPlane;
                // Row 6: 4 uints (16 bytes)
                uint32_t objectCount;
                uint32_t outputWidth;
                uint32_t outputHeight;
                uint32_t downsampleFactor;
                // Row 7: uint3 + float (16 bytes)
                uint32_t atlasSizeInBricksX;
                uint32_t atlasSizeInBricksY;
                uint32_t atlasSizeInBricksZ;
                float atlasTexelSizeX;
                // Row 8: 4 floats (16 bytes)
                float atlasTexelSizeY;
                float atlasTexelSizeZ;
                float rayStartOffsetDepthScale;
                float minDepth;
                // Row 9: float + uint + 8 bytes pad (16 bytes)
                float maxDepth;
                uint32_t pad0;
                uint32_t pad1;
                uint32_t pad2;
            };
            static_assert(sizeof(DfsConeTraceCB) == 160,
                          "DfsConeTraceCB must be 160 bytes (10 x 16-byte rows)");

            DfsConeTraceCB traceCB{};

            // Compute inverse view-projection matrix
            const auto& camera = m_cameraManager->GetActiveCamera();
            const Mat4 viewProj = camera.GetView() * camera.GetProjection();
            Mat4 invViewProj = viewProj.Inverse();
            std::memcpy(traceCB.invViewProj, &invViewProj, 64);

            traceCB.lightDirX = ctx.sunDir.x;
            traceCB.lightDirY = ctx.sunDir.y;
            traceCB.lightDirZ = ctx.sunDir.z;

            // Default sun angular diameter: 0.5357 degrees (Earth sun).
            const float lightSourceAngleDeg = ctx.lightData.lightSourceAngleDeg > 0.001f
                                            ? ctx.lightData.lightSourceAngleDeg : 0.5357f;
            traceCB.tanLightAngle = std::tan(lightSourceAngleDeg * 3.14159265f / 180.0f);
            traceCB.maxTraceDistance = m_dfsConfig.distance;
            traceCB.selfShadowBias  = m_dfsConfig.selfShadowBias;
            traceCB.nearPlane  = ctx.perFrame.nearPlane;
            traceCB.farPlane   = ctx.perFrame.farPlane;
            traceCB.objectCount = objectCount;
            traceCB.outputWidth  = dfsWidth;
            traceCB.outputHeight = dfsHeight;
            traceCB.downsampleFactor = downsampleFactor;

            // Atlas dimensions: the atlas is a cube of atlasSize^3 texels,
            // divided into 8^3 bricks. BricksPerAxis = atlasSize / 8.
            // We compute from the atlas total capacity and brick size.
            const uint32_t totalBricks = m_sdfAtlas->GetTotalBrickCapacity();
            // Cube root approximation: atlas is cube, so bricksPerAxis = cbrt(totalBricks)
            // For 256^3 atlas with 8^3 bricks: 32 bricks per axis, totalBricks = 32768
            uint32_t bricksPerAxis = 1;
            while (bricksPerAxis * bricksPerAxis * bricksPerAxis < totalBricks) {
                ++bricksPerAxis;
            }
            traceCB.atlasSizeInBricksX = bricksPerAxis;
            traceCB.atlasSizeInBricksY = bricksPerAxis;
            traceCB.atlasSizeInBricksZ = bricksPerAxis;

            const float atlasTexelDim = static_cast<float>(bricksPerAxis * 8);
            traceCB.atlasTexelSizeX = 1.0f / atlasTexelDim;
            traceCB.atlasTexelSizeY = 1.0f / atlasTexelDim;
            traceCB.atlasTexelSizeZ = 1.0f / atlasTexelDim;

            // Ray start offset scaled by depth to avoid self-intersection artifacts.
            traceCB.rayStartOffsetDepthScale = 0.0005f;
            traceCB.minDepth = 0.0f;
            traceCB.maxDepth = m_dfsConfig.distance;
            traceCB.pad0 = 0;
            traceCB.pad1 = 0;
            traceCB.pad2 = 0;

            // Upload via CB ring at slot 1 (b1 in the shader's cbuffer register).
            // The shader declares the cbuffer at register(b1) to match the
            // compute root signature's kSlotPerFrame = slot 1 = b1.
            CBAllocation traceAlloc = m_cbRing->Allocate(sizeof(DfsConeTraceCB));
            if (traceAlloc.cpuPtr) {
                std::memcpy(traceAlloc.cpuPtr, &traceCB, sizeof(DfsConeTraceCB));
                cmd->SetRootCBV(kSlotPerFrame, traceAlloc.bufferHandle,
                                traceAlloc.offsetBytes, traceAlloc.cpuPtr, sizeof(DfsConeTraceCB));
            }

            // Bind SRVs:
            // t0 = depth buffer, t1 = object data, t2 = SDF atlas, t3 = indirection, t4 = culled indices
            if (!m_device->IsBindless()) {
                cmd->SetDescriptorTable(5, m_gbuffer.depthSRV);
            }

            // Bind output UAV (u0) — DFS result texture
            cmd->SetDescriptorTable(4, m_dfsOutputUavIndex);

            // Dispatch: 8x8 thread groups covering the output resolution
            const uint32_t groupsX = (dfsWidth  + 7) / 8;
            const uint32_t groupsY = (dfsHeight + 7) / 8;
            cmd->Dispatch(groupsX, groupsY, 1);

            // Transition DFS output: UAV write -> SRV read for lighting pass
            RHIBarrierDesc outputBarrier{};
            outputBarrier.texture     = m_dfsOutputTexture;
            outputBarrier.stateBefore = RHIResourceState::UnorderedAccess;
            outputBarrier.stateAfter  = RHIResourceState::ShaderResource;
            cmd->ResourceBarrier(&outputBarrier, 1);
        });
    });
}

} // namespace uz
