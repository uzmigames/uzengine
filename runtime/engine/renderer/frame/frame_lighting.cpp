/// UzEngine Scene Renderer — Lighting-related passes: GTAO, DFS cone-trace,
/// SSR trace+resolve, and deferred lighting (or GBuffer debug / debug view).

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
#include "atmosphere_renderer.h"
#include "fog_renderer.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"

#include <cmath>
#include <cstring>

namespace uz {

void SceneRenderer::RegisterLightingPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // Only deferred path has lighting passes — forward PBR handles lighting inline.
    if (m_renderPath != RenderPath::Deferred) return;

    const auto& cascades = ctx.shadowFrame.cascades;

    // ── GTAO Pass (after GBuffer, before lighting) ─────────────────
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
        rgBuilder.AddPass("GTAO", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("GTAO::Execute");

                // Get view matrix from the camera.
                const auto& cam = *m_camera;
                float viewMatrixData[16];
                const auto& viewMat = cam.GetView();
                std::memcpy(viewMatrixData, &viewMat, 16 * sizeof(float));

                float invTanHalfFov = 1.0f / std::tan(cam.GetFovY() * 0.5f);
                float aspectRatio = static_cast<float>(m_width) / static_cast<float>(m_height);

                // Log input SRV indices being passed to GTAO on first frame
                static bool loggedGtaoInputs = false;
                if (!loggedGtaoInputs) {
                    UZ_LOG(Renderer, Info,
                           "SceneRenderer: GTAO inputs — depthSRV={}, normalSRV={}, "
                           "depthHandle={}, normalHandle={}, size={}x{}, frame={}",
                           m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                           m_gbuffer.depth.IsValid(), m_gbuffer.normal.IsValid(),
                           m_width, m_height, ctx.frameIndex);
                    loggedGtaoInputs = true;
                }

                const uint32_t gtaoResult = m_gtaoPass->Execute(
                    cmd, m_cbRing.get(),
                    m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                    m_gbuffer.depth, m_gbuffer.normal,
                    viewMatrixData,
                    invTanHalfFov, aspectRatio,
                    ctx.perFrame.nearPlane, ctx.perFrame.farPlane,
                    m_width, m_height,
                    ctx.frameIndex);

                if (gtaoResult == UINT32_MAX) {
                    UZ_LOG(Renderer, Warning,
                           "SceneRenderer: GTAO Execute() returned UINT32_MAX — "
                           "AO will not be applied this frame (depthSRV={}, normalSRV={})",
                           m_gbuffer.depthSRV, m_gbuffer.normalSRV);
                }
            });
        });
    }

    // ── DFS Compute Passes (after GBuffer + GTAO, before lighting) ──────
    // Runs only when DFS is enabled, an SDF atlas is bound and populated,
    // and the DFS output texture exists. Two passes:
    //   1. CullObjects: frustum-cull SDF objects into a compacted index buffer
    //   2. ConeTrace: per-pixel ray march through culled SDFs → shadow texture
    //
    // @source UE5 DistanceFieldShadowing.cpp — dispatch order
    const bool dfsEnabled = m_dfsConfig.enabled
                          && m_sdfAtlas && m_sdfAtlas->IsInitialized()
                          && m_sdfAtlas->GetRegisteredObjectCount() > 0
                          && m_dfsOutputTexture.IsValid()
                          && m_dfsCulledIndicesBuffer.IsValid()
                          && m_dfsIndirectArgsBuffer.IsValid();
    if (dfsEnabled) {
        // Upload dirty SDF object transforms before dispatching cull/trace.
        m_sdfAtlas->UploadObjectBuffer();

        const uint32_t objectCount = m_sdfAtlas->GetRegisteredObjectCount();
        const uint32_t dfsWidth  = m_dfsConfig.fullResolution ? m_width  : m_width  / 2;
        const uint32_t dfsHeight = m_dfsConfig.fullResolution ? m_height : m_height / 2;
        const uint32_t downsampleFactor = m_dfsConfig.fullResolution ? 1 : 2;

        // ── Async compute note (§8.3) ─────────────────────────────────────
        // UE5 dispatches DFS on an async compute queue overlapping GBuffer
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
        /// @source UE5 DistanceFieldShadowing.cpp:196-210 — CullObjectsForShadow dispatch
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
                // @source UE5 DistanceFieldShadowing.cpp:230-260 — FrustumPlaneExtraction
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
        /// @source UE5 DistanceFieldShadowing.cpp:380-420 — DistanceFieldShadowing dispatch
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
                const auto& camera = *m_camera;
                const Mat4 viewProj = camera.GetView() * camera.GetProjection();
                Mat4 invViewProj = viewProj.Inverse();
                std::memcpy(traceCB.invViewProj, &invViewProj, 64);

                traceCB.lightDirX = ctx.sunDir.x;
                traceCB.lightDirY = ctx.sunDir.y;
                traceCB.lightDirZ = ctx.sunDir.z;

                // @source UE5 DirectionalLightComponent.h — default LightSourceAngle = 0.5357 degrees
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

                // @source UE5 DistanceFieldShadowing.usf:696 — RayStartOffsetDepthScale
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

    // ── SSR Compute Passes (after GBuffer + depth prepass + HiZ, before lighting) ──
    // Two compute passes:
    //   1. SSR_Trace:   Hi-Z ray march → trace output (rgb=hit color, a=confidence)
    //   2. SSR_Resolve: Fresnel weight + roughness fade → resolved SSR for lighting
    //
    // The lighting pass blends SSR with IBL specular via ssrSrvIndex in PerFrameCB.
    //
    // @source UE5 ScreenSpaceRayTracing.cpp:279-318 — SSR pass setup
    // @source UE5 SSRTReflections.usf:441-462 — ScreenSpaceReflectionsCS
    if (ctx.ssrActive) {
        // ── Pass 1: SSR Trace (compute) ──────────────────────────────────
        /// @source UE5 ScreenSpaceRayTracing.cpp:279-318 — ComputeSSRParams
        rgBuilder.AddPass("SSR_Trace", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("SSR::Trace");

                auto ssrTracePSO = ctx.pipelines->GetSsrTrace();
                if (!ssrTracePSO.IsValid()) return;

                // Transition SSR trace output to UAV for writing
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrTraceOutput;
                    barrier.buffer      = RHIBufferHandle::Invalid();
                    barrier.stateBefore = RHIResourceState::ShaderResource;
                    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&barrier, 1);
                }

                cmd->SetPipeline(ssrTracePSO);

                // Root constants: 8 DWORDs (matches SSRRootCB in ssr_trace_cs.hlsl)
                /// @source UE5 ScreenSpaceRayTracing.cpp:291-318 — ComputeSSRParams
                struct SsrTraceConstants {
                    uint32_t depthSrvIdx;       // DWORD 0
                    uint32_t normalSrvIdx;      // DWORD 1
                    uint32_t hizSrvIdx;         // DWORD 2
                    uint32_t sceneColorSrvIdx;  // DWORD 3
                    uint32_t outputWidth;       // DWORD 4
                    uint32_t outputHeight;      // DWORD 5
                    float    intensity;         // DWORD 6
                    float    roughnessMaskScale;// DWORD 7
                };

                SsrTraceConstants rc{};
                rc.depthSrvIdx      = m_gbuffer.depthSRV;
                rc.normalSrvIdx     = m_gbuffer.normalSRV;
                // Use depth buffer as HiZ source (single mip — no hierarchical
                // acceleration, but functionally correct). Proper HiZ with mip
                // chain is a follow-up when the RHI supports per-mip SRVs on
                // a single texture.
                rc.hizSrvIdx        = m_gbuffer.depthSRV;
                rc.sceneColorSrvIdx = m_hdrSceneColorSrv;
                rc.outputWidth      = m_width;
                rc.outputHeight     = m_height;
                /// @source UE5 ScreenSpaceRayTracing.cpp:314 — Clamp(Intensity * 0.01, 0, 1)
                rc.intensity        = 1.0f;
                /// @source UE5 ScreenSpaceRayTracing.cpp:287 — -2.0 / MaxRoughness
                rc.roughnessMaskScale = -2.0f / 0.4f;  // MaxRoughness = 0.4

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

                // PerFrameCB at b1 (has viewProj, invViewProj, cameraPos, nearPlane, farPlane)
                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                sizeof(PerFrameCB));

                // UAV at descriptor table slot 4 (u0 in shader)
                cmd->SetDescriptorTable(4, m_ssrTraceOutputUav);

                // Dispatch: 8x8 thread groups
                const uint32_t groupsX = (m_width  + 7) / 8;
                const uint32_t groupsY = (m_height + 7) / 8;
                cmd->Dispatch(groupsX, groupsY, 1);

                // Transition trace output: UAV → SRV for resolve pass to read
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrTraceOutput;
                    barrier.buffer      = RHIBufferHandle::Invalid();
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }
            });
        });

        // ── Pass 2: SSR Resolve (compute) ────────────────────────────────
        /// @source UE5 SSRTReflections.usf:105-416 — ScreenSpaceReflections resolve
        rgBuilder.AddPass("SSR_Resolve", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("SSR::Resolve");

                auto ssrResolvePSO = ctx.pipelines->GetSsrResolve();
                if (!ssrResolvePSO.IsValid()) return;

                // Transition SSR resolved output to UAV for writing
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrResolvedOutput;
                    barrier.buffer      = RHIBufferHandle::Invalid();
                    barrier.stateBefore = RHIResourceState::ShaderResource;
                    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&barrier, 1);
                }

                cmd->SetPipeline(ssrResolvePSO);

                // Root constants: 8 DWORDs (matches SSRResolveCB in ssr_resolve_cs.hlsl)
                struct SsrResolveConstants {
                    uint32_t ssrTraceSrvIdx;    // DWORD 0
                    uint32_t normalSrvIdx;      // DWORD 1
                    uint32_t depthSrvIdx;       // DWORD 2
                    uint32_t reserved;          // DWORD 3
                    uint32_t outputWidth;       // DWORD 4
                    uint32_t outputHeight;      // DWORD 5
                    float    intensity;         // DWORD 6
                    float    roughnessMaskScale;// DWORD 7
                };

                SsrResolveConstants rc{};
                rc.ssrTraceSrvIdx   = m_ssrTraceOutputSrv;
                rc.normalSrvIdx     = m_gbuffer.normalSRV;
                rc.depthSrvIdx      = m_gbuffer.depthSRV;
                rc.reserved         = 0;
                rc.outputWidth      = m_width;
                rc.outputHeight     = m_height;
                /// @source UE5 ScreenSpaceRayTracing.cpp:314
                rc.intensity        = 1.0f;
                /// @source UE5 ScreenSpaceRayTracing.cpp:287
                rc.roughnessMaskScale = -2.0f / 0.4f;

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

                // PerFrameCB at b1
                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                sizeof(PerFrameCB));

                // UAV at descriptor table slot 4 (u0 in shader)
                cmd->SetDescriptorTable(4, m_ssrResolvedOutputUav);

                // Dispatch: 8x8 thread groups
                const uint32_t groupsX = (m_width  + 7) / 8;
                const uint32_t groupsY = (m_height + 7) / 8;
                cmd->Dispatch(groupsX, groupsY, 1);

                // Transition resolved output: UAV → SRV for lighting pass to read
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrResolvedOutput;
                    barrier.buffer      = RHIBufferHandle::Invalid();
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }
            });
        });
    }

    // ── Deferred Lighting / Debug View / GBuffer Debug ──────────────────
    const bool debugActive = (m_debugMode != GBufferDebugMode::Count);
    const bool debugViewActive = (m_debugViewMode != DebugViewMode::Lit);

    if (debugViewActive) {
        // ── Debug View Mode — replaces lighting with channel visualization ──
        DebugViewPassDesc dvDesc{};
        dvDesc.backbuffer        = ctx.hdrRes;
        dvDesc.albedoGBuffer     = ctx.albedoRes;
        dvDesc.normalGBuffer     = ctx.normalRes;
        dvDesc.depthGBuffer      = ctx.depthRes;
        dvDesc.positionGBuffer   = ctx.positionRes;
        dvDesc.perFrameAlloc     = ctx.perFrameAlloc;
        dvDesc.pipeline          = ctx.pipelines->GetDebugView();
        dvDesc.mode              = m_debugViewMode;
        dvDesc.albedoSrvIndex    = m_gbuffer.albedoSRV;
        dvDesc.normalSrvIndex    = m_gbuffer.normalSRV;
        dvDesc.depthSrvIndex     = m_gbuffer.depthSRV;
        dvDesc.positionSrvIndex  = m_gbuffer.positionWSSRV;
        dvDesc.albedoTexture     = m_gbuffer.albedo;
        dvDesc.normalTexture     = m_gbuffer.normal;
        dvDesc.depthTexture      = m_gbuffer.depth;
        dvDesc.positionTexture   = m_gbuffer.positionWS;
        dvDesc.bindless          = ctx.bindless;
        dvDesc.viewportWidth     = ctx.vpWidth;
        dvDesc.viewportHeight    = ctx.vpHeight;
        AddDebugViewPass(rgBuilder, dvDesc);
    } else if (!debugActive) {
        LightingPassDesc lightDesc{};
        lightDesc.backbuffer          = ctx.hdrRes;
        lightDesc.albedoGBuffer       = ctx.albedoRes;
        lightDesc.normalGBuffer       = ctx.normalRes;
        lightDesc.depthGBuffer        = ctx.depthRes;
        lightDesc.positionGBuffer     = ctx.positionRes;
        lightDesc.emissiveGBuffer     = ctx.emissiveRes;
        lightDesc.perFrameAlloc       = ctx.perFrameAlloc;
        lightDesc.lightingAlloc       = ctx.lightingAlloc;
        lightDesc.pipeline            = ctx.pipelines->GetLighting();
        lightDesc.albedoSrvIndex      = m_gbuffer.albedoSRV;
        lightDesc.normalSrvIndex      = m_gbuffer.normalSRV;
        lightDesc.depthSrvIndex       = m_gbuffer.depthSRV;
        lightDesc.positionSrvIndex    = m_gbuffer.positionWSSRV;
        lightDesc.emissiveSrvIndex    = m_gbuffer.emissiveSRV;
        lightDesc.albedoTexture       = m_gbuffer.albedo;
        lightDesc.normalTexture       = m_gbuffer.normal;
        lightDesc.depthTextureForSrv  = m_gbuffer.depth;
        lightDesc.positionTexture     = m_gbuffer.positionWS;
        lightDesc.emissiveTexture     = m_gbuffer.emissive;
        lightDesc.shadowAlloc         = ctx.shadowAlloc;
        lightDesc.shadowAtlasResource = ctx.shadowAtlasRes;
        lightDesc.shadowAtlasTexture  = m_shadowAtlas.GetTexture();
        // IBL texture handles for SM5 fallback path (t5-t7) — kept for
        // backward compat; the binder path reads from perFrameParams instead.
        if (m_iblBaker) {
            lightDesc.iblIrradiance   = m_iblBaker->GetIrradianceTexture();
            lightDesc.iblPrefiltered  = m_iblBaker->GetPrefilteredTexture();
            lightDesc.iblBrdfLut      = m_iblBaker->GetBRDFLutTexture();
        }
        lightDesc.bindless            = ctx.bindless;
        lightDesc.viewportWidth       = ctx.vpWidth;
        lightDesc.viewportHeight      = ctx.vpHeight;
        // Binder path — ShaderParameterBinder uploads per-frame CB and
        // binds IBL textures via metadata. On SM5, BindResources calls
        // SetFragmentTexture for each texture member automatically.
        lightDesc.perFrameParams    = &ctx.perFrame;
        lightDesc.shadowDataParams  = &ctx.shadowDataParams;
        lightDesc.cbRingAllocator   = m_cbRing.get();
        AddLightingPass(rgBuilder, lightDesc);
    } else {
        GBufferDebugPassDesc dbgDesc{};
        dbgDesc.backbuffer         = ctx.hdrRes;
        dbgDesc.albedoGBuffer      = ctx.albedoRes;
        dbgDesc.normalGBuffer      = ctx.normalRes;
        dbgDesc.depthGBuffer       = ctx.depthRes;
        dbgDesc.perFrameAlloc      = ctx.perFrameAlloc;

        struct DebugCBData { uint32_t mode; uint32_t pad[3]; };
        CBAllocation debugAlloc = m_cbRing->Allocate(sizeof(DebugCBData));
        DebugCBData debugCBData{static_cast<uint32_t>(m_debugMode), {0, 0, 0}};
        std::memcpy(debugAlloc.cpuPtr, &debugCBData, sizeof(DebugCBData));
        dbgDesc.debugAlloc         = debugAlloc;

        dbgDesc.pipeline           = ctx.pipelines->GetGBufferDebug();
        dbgDesc.mode               = m_debugMode;
        dbgDesc.albedoSrvIndex     = m_gbuffer.albedoSRV;
        dbgDesc.normalSrvIndex     = m_gbuffer.normalSRV;
        dbgDesc.depthSrvIndex      = m_gbuffer.depthSRV;
        dbgDesc.albedoTexture      = m_gbuffer.albedo;
        dbgDesc.normalTexture      = m_gbuffer.normal;
        dbgDesc.depthTextureForSrv = m_gbuffer.depth;
        dbgDesc.bindless           = ctx.bindless;
        dbgDesc.viewportWidth      = ctx.vpWidth;
        dbgDesc.viewportHeight     = ctx.vpHeight;
        AddGBufferDebugPass(rgBuilder, dbgDesc);
    }

    // ── Sky Atmosphere + Height Fog (HDR, before postprocess/tonemap) ────
    // Must run after deferred lighting but before bloom/TAA/tonemap so the
    // sky is included in the HDR scene color that postprocess operates on.
    // @source UE5 SceneRendering.cpp — atmosphere passes after lighting, before post-process
    {
        AtmosphereFrameContext atmoCtx{};
        atmoCtx.device              = m_device;
        atmoCtx.world               = ctx.world;
        atmoCtx.cbRing              = m_cbRing.get();
        atmoCtx.pipelines           = ctx.pipelines;
        atmoCtx.sunDir              = &ctx.sunDir;
        atmoCtx.sunColor            = &ctx.sunColor;
        atmoCtx.bindless            = ctx.bindless;
        atmoCtx.vpWidth             = ctx.vpWidth;
        atmoCtx.vpHeight            = ctx.vpHeight;
        atmoCtx.transmittanceLut    = m_transmittanceLut;
        atmoCtx.transmittanceLutSrv = m_transmittanceLutSrv;
        atmoCtx.transmittanceLutUav = m_transmittanceLutUav;
        atmoCtx.transmittanceLutDirty  = &m_transmittanceLutDirty;
        atmoCtx.transmittanceLutFilled = &m_transmittanceLutFilled;
        atmoCtx.multiScatterLut     = m_multiScatterLut;
        atmoCtx.multiScatterLutSrv  = m_multiScatterLutSrv;
        atmoCtx.multiScatterLutUav  = m_multiScatterLutUav;
        atmoCtx.multiScatterLutDirty  = &m_multiScatterLutDirty;
        atmoCtx.multiScatterLutFilled = &m_multiScatterLutFilled;
        atmoCtx.skyCubemap          = m_skyCubemap;
        atmoCtx.skyCubemapSrv       = m_skyCubemapSrv;
        atmoCtx.skyCubemapUav       = m_skyCubemapUav;
        atmoCtx.skyIBLDirty         = &m_skyIBLDirty;
        atmoCtx.iblBaker            = m_iblBaker.get();
        atmoCtx.iblResult           = &m_iblResult;
        atmoCtx.brdfLutSrvIndex     = m_brdfLutSrvIndex;
        atmoCtx.skyIrradianceSH     = &m_skyIrradianceSH;
        atmoCtx.depthSrvIndex       = m_gbuffer.depthSRV;
        atmoCtx.depthTexture        = m_gbuffer.depth;

        if (m_shadowDebugMode == 0) {
            AddAtmospherePasses(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc, atmoCtx);

            /// @source UE5 SkyAtmosphereCommon.ush:343-352 — GetViewDistanceSkyLightColor
            /// UE5 uses DistantSkyLightLutBufferSRV (a precomputed 1-pixel LUT of sky
            /// luminance at camera position). UzEngine doesn't have this LUT, so we use
            /// the SkyLight component's color × intensity as an equivalent approximation.
            /// The previous code used SH L0 coefficients (coeffs[N].w) which are scaled
            /// by the diffuse convolution constant (~0.28), producing values too dim.
            float skyFogR = ctx.perFrame.skyLightColorR * ctx.perFrame.skyLightIntensity;
            float skyFogG = ctx.perFrame.skyLightColorG * ctx.perFrame.skyLightIntensity;
            float skyFogB = ctx.perFrame.skyLightColorB * ctx.perFrame.skyLightIntensity;

            AddHeightFogFromECS(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc,
                                ctx.world, *m_camera, ctx.pipelines, *m_cbRing,
                                m_gbuffer.depthSRV, m_gbuffer.depth,
                                ctx.bindless, ctx.vpWidth, ctx.vpHeight,
                                skyFogR, skyFogG, skyFogB);
        }
    }
}

} // namespace uz
