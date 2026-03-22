/// UzEngine Scene Renderer — Lighting-related passes: GTAO, DFS cone-trace,
/// SSR trace+resolve, and deferred lighting (or GBuffer debug / debug view).

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "gi/ssgi_renderer.h"
#include "gi/mesh_sdf_baker.h"
#include "gpu_mesh.h"
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "atmosphere_renderer.h"
#include "fog_renderer.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

#include <cmath>
#include <cstring>

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_MotionBlurAmount = UZ_CVAR_FLOAT("r.MotionBlur.Amount", 0.5f, "Motion blur intensity", 0.f, 5.f);
static auto* cv_MotionBlurMax    = UZ_CVAR_FLOAT("r.MotionBlur.Max", 5.f, "Max blur radius (%)", 0.f, 50.f);
static auto* cv_CASSharpness     = UZ_CVAR_FLOAT("r.CAS.Sharpness", 0.15f, "CAS sharpening strength", 0.f, 1.f);
static auto* cv_LODBias          = UZ_CVAR_FLOAT("r.LOD.Bias", 1.f, "Global LOD bias multiplier", 0.1f, 10.f);
static auto* cv_ContactShadows   = UZ_CVAR_BOOL("r.ContactShadows", true, "Contact shadows");

namespace uz {

void SceneRenderer::RegisterLightingPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // Only deferred path has lighting passes — forward PBR handles lighting inline.
    if (m_renderPath != RenderPath::Deferred) return;

    const auto& cascades = ctx.shadowFrame.cascades;

    // ── GTAO Pass (after GBuffer, before lighting) ─────────────────
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled
        && m_passInspector.IsPassEnabled("gtao")) {
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

                const uint32_t gtaoResult = m_gtaoPass->Execute(
                    cmd, m_cbRing.get(),
                    m_gbuffer.depthSRV, m_gbuffer.normalSRV,
                    m_gbuffer.depth, m_gbuffer.normal,
                    viewMatrixData,
                    invTanHalfFov, aspectRatio,
                    ctx.perFrame.nearPlane, ctx.perFrame.farPlane,
                    m_renderWidth, m_renderHeight,
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
    // Distance field shadow dispatch: cull objects first, then cone-trace.
    const bool dfsEnabled = m_dfsConfig.enabled
                          && m_sdfAtlas && m_sdfAtlas->IsInitialized()
                          && m_sdfAtlas->GetRegisteredObjectCount() > 0
                          && m_dfsOutputTexture.IsValid()
                          && m_dfsCulledIndicesBuffer.IsValid()
                          && m_dfsIndirectArgsBuffer.IsValid()
                          && m_passInspector.IsPassEnabled("dfs");
    if (dfsEnabled) {
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
                const auto& camera = *m_camera;
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

    // ── SSGI Compute Passes (after GTAO, before SSR and lighting) ────────────
    // Dispatches 5 compute passes: PrevColorReduce, Trace, SpatialDenoise,
    // TemporalAccum, BilateralUpsample. The result SRV indices are written
    // to PerFrameCB in frame_setup.cpp for the deferred lighting shader.
    //
    // Screen-space global illumination — traces against the depth buffer for indirect light.
    const bool ssgiActive = m_ssgiRenderer.IsReady()
                          && m_device->IsBindless()
                          && m_passInspector.IsPassEnabled("ssgi");
    if (ssgiActive) {
        rgBuilder.AddPass("SSGI", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.velocityRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("SSGI::Execute");

                SSGIRenderParams ssgiParams{};
                ssgiParams.cbRing          = m_cbRing.get();
                ssgiParams.pipelines       = ctx.pipelines;
                ssgiParams.depthSrvIndex   = m_gbuffer.depthSRV;
                ssgiParams.normalSrvIndex  = m_gbuffer.normalSRV;
                ssgiParams.velocitySrvIndex = m_gbuffer.velocitySRV;
                ssgiParams.depthTexture    = m_gbuffer.depth;
                ssgiParams.normalTexture   = m_gbuffer.normal;
                ssgiParams.velocityTexture = m_gbuffer.velocity;
                ssgiParams.hdrSceneColor   = m_hdrSceneColor;
                ssgiParams.hdrSceneColorSrv = m_hdrSceneColorSrv;
                ssgiParams.perFrameBuffer  = ctx.perFrameAlloc.bufferHandle;
                ssgiParams.perFrameOffset  = ctx.perFrameAlloc.offsetBytes;
                ssgiParams.perFrameCpuPtr  = ctx.perFrameAlloc.cpuPtr;
                ssgiParams.perFrameSize    = sizeof(PerFrameCB);
                ssgiParams.frameIndex      = ctx.frameIndex;
                ssgiParams.renderWidth     = static_cast<uint32_t>(ctx.renderWidth);
                ssgiParams.renderHeight    = static_cast<uint32_t>(ctx.renderHeight);

                m_ssgiRenderer.Render(cmd, ssgiParams);
            });
        });
    }

    // ── SDF GI: GDF Composite + Radiance Cache Trace (after SSGI, before lighting) ──
    // Updates the global distance field from mesh SDFs, then traces radiance
    // probes. The probe SH buffer SRV is written to PerFrameCB for the
    // deferred lighting pass.
    //
    // GDF composite + radiance cache probe trace for software-raytraced GI.
    const bool sdfGiActive = m_sdfGiEnabled
                           && m_globalDistanceField.IsInitialized()
                           && m_radianceCache.IsInitialized()
                           && m_device->IsBindless()
                           && m_passInspector.IsPassEnabled("sdf_gi");
    if (sdfGiActive) {
        // ── Pass: GDF Composite ──────────────────────────────────────────
        // Collect mesh SDFs and world transforms from all StaticMeshComponent
        // entities, then composite them into the global distance field clipmaps.
        // Composite all per-mesh SDFs into the cascaded GDF clipmaps.
        rgBuilder.AddPass("GDF_Composite", [&](rg::RgPassBuilder& pb) {
            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("GDF::Composite");

                auto gdfPipeline = ctx.pipelines->GetGdfComposite();
                if (!gdfPipeline.IsValid()) return;

                // Update clipmap centers to track camera
                Vec3 camPos = ctx.perFrame.cameraPos.xyz();
                m_globalDistanceField.Update(camPos);

                // Collect mesh SDFs from the ECS, precompute inverse transforms.
                std::vector<MeshSDF> meshSDFs;
                std::vector<Mat4> meshTransforms;

                if (ctx.world) {
                    auto meshView = ctx.world->GetRegistry().view<StaticMeshComponent, SceneComponent>();
                    for (auto [enttEntity, meshComp, sceneComp] : meshView.each()) {
                        if (meshComp.meshId >= m_meshes.size()) continue;
                        const GPUMesh& gpuMesh = m_meshes[meshComp.meshId];
                        if (!gpuMesh.HasSDF()) continue;

                        MeshSDF sdfCopy = gpuMesh.sdf;
                        // Precompute world-to-local inverse transform (cached in MeshSDF)
                        Mat4 worldMat = sceneComp.GetWorldMatrix();
                        Mat4 inv = worldMat.Inverse();
                        sdfCopy.invTransform[0]  = inv.m._11;
                        sdfCopy.invTransform[1]  = inv.m._12;
                        sdfCopy.invTransform[2]  = inv.m._13;
                        sdfCopy.invTransform[3]  = inv.m._14;
                        sdfCopy.invTransform[4]  = inv.m._21;
                        sdfCopy.invTransform[5]  = inv.m._22;
                        sdfCopy.invTransform[6]  = inv.m._23;
                        sdfCopy.invTransform[7]  = inv.m._24;
                        sdfCopy.invTransform[8]  = inv.m._31;
                        sdfCopy.invTransform[9]  = inv.m._32;
                        sdfCopy.invTransform[10] = inv.m._33;
                        sdfCopy.invTransform[11] = inv.m._34;

                        meshSDFs.push_back(sdfCopy);
                        meshTransforms.push_back(worldMat);
                    }
                }

                const auto sdfCount = static_cast<uint32_t>(meshSDFs.size());

                m_globalDistanceField.CompositeFromMeshSDFs(
                    cmd,
                    sdfCount > 0 ? meshSDFs.data() : nullptr,
                    sdfCount > 0 ? meshTransforms.data() : nullptr,
                    sdfCount,
                    gdfPipeline,
                    m_cbRing.get(),
                    ctx.perFrameAlloc.bufferHandle,
                    ctx.perFrameAlloc.offsetBytes,
                    ctx.perFrameAlloc.cpuPtr,
                    sizeof(PerFrameCB));
            });
        });

        // ── Pass: Radiance Cache Trace ─────────────────────────────────
        rgBuilder.AddPass("RadianceCache_Trace", [&](rg::RgPassBuilder& pb) {
            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("RadianceCache::Trace");

                auto rcPipeline = ctx.pipelines->GetRadianceCacheTrace();
                if (!rcPipeline.IsValid()) return;

                // Update probe grid center to track camera
                Vec3 camPos = ctx.perFrame.cameraPos.xyz();
                m_radianceCache.Update(camPos);

                m_radianceCache.UpdateProbes(
                    cmd, m_globalDistanceField, rcPipeline,
                    m_cbRing.get(),
                    ctx.perFrameAlloc.bufferHandle,
                    ctx.perFrameAlloc.offsetBytes,
                    ctx.perFrameAlloc.cpuPtr,
                    sizeof(PerFrameCB),
                    ctx.frameIndex);
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
    // Screen-space reflections: Hi-Z ray march + Fresnel-weighted resolve.
    if (ctx.ssrActive) {
        // ── Pass 1: SSR Trace (compute) ──────────────────────────────────
        /// SSR Trace: Hi-Z ray march producing (hitColor, confidence) per pixel.
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
                /// SSR trace root constants: texture indices, dimensions, quality params.
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
                rc.outputWidth      = m_renderWidth;
                rc.outputHeight     = m_renderHeight;
                /// Reflection intensity (1.0 = full strength).
                rc.intensity        = 1.0f;
                /// Roughness fade: surfaces above MaxRoughness get no SSR.
                rc.roughnessMaskScale = -2.0f / 0.4f;  // MaxRoughness = 0.4

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

                // PerFrameCB at b1 (has viewProj, invViewProj, cameraPos, nearPlane, farPlane)
                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                sizeof(PerFrameCB));

                // UAV at descriptor table slot 4 (u0 in shader)
                cmd->SetDescriptorTable(4, m_ssrTraceOutputUav);

                // Dispatch: 8x8 thread groups
                const uint32_t groupsX = (m_renderWidth  + 7) / 8;
                const uint32_t groupsY = (m_renderHeight + 7) / 8;
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
        /// SSR Resolve: Fresnel weight + roughness fade → final SSR contribution.
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
                rc.outputWidth      = m_renderWidth;
                rc.outputHeight     = m_renderHeight;
                /// Reflection intensity (1.0 = full strength).
                rc.intensity        = 1.0f;
                /// Roughness fade: surfaces above MaxRoughness get no SSR.
                rc.roughnessMaskScale = -2.0f / 0.4f;

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

                // PerFrameCB at b1
                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                sizeof(PerFrameCB));

                // UAV at descriptor table slot 4 (u0 in shader)
                cmd->SetDescriptorTable(4, m_ssrResolvedOutputUav);

                // Dispatch: 8x8 thread groups
                const uint32_t groupsX = (m_renderWidth  + 7) / 8;
                const uint32_t groupsY = (m_renderHeight + 7) / 8;
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
        dvDesc.viewportWidth     = ctx.renderWidth;
        dvDesc.viewportHeight    = ctx.renderHeight;
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
        lightDesc.viewportWidth       = ctx.renderWidth;
        lightDesc.viewportHeight      = ctx.renderHeight;
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
        dbgDesc.viewportWidth      = ctx.renderWidth;
        dbgDesc.viewportHeight     = ctx.renderHeight;
        AddGBufferDebugPass(rgBuilder, dbgDesc);
    }

    // ── Sky Atmosphere + Height Fog (HDR, before postprocess/tonemap) ────
    // Must run after deferred lighting but before bloom/TAA/tonemap so the
    // sky is included in the HDR scene color that postprocess operates on.
    // Atmosphere + fog render after deferred lighting, before postprocess/tonemap.
    {
        AtmosphereFrameContext atmoCtx{};
        atmoCtx.device              = m_device;
        atmoCtx.world               = ctx.world;
        atmoCtx.cbRing              = m_cbRing.get();
        atmoCtx.pipelines           = ctx.pipelines;
        atmoCtx.sunDir              = &ctx.sunDir;
        atmoCtx.sunColor            = &ctx.sunColor;
        atmoCtx.bindless            = ctx.bindless;
        atmoCtx.vpWidth             = ctx.renderWidth;
        atmoCtx.vpHeight            = ctx.renderHeight;
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
        atmoCtx.apVolume            = m_apVolume;
        atmoCtx.apVolumeSrv         = m_apVolumeSrv;
        atmoCtx.apVolumeUav         = m_apVolumeUav;
        atmoCtx.invViewProjData     = reinterpret_cast<const float*>(&ctx.perFrame.invViewProj);
        atmoCtx.cameraPos           = reinterpret_cast<const Vec3*>(&ctx.perFrame.cameraPos);
        atmoCtx.depthSrvIndex       = m_gbuffer.depthSRV;
        atmoCtx.depthTexture        = m_gbuffer.depth;

        if (m_shadowDebugMode == 0) {
            AddAtmospherePasses(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc, atmoCtx);

            /// Sky light color for fog inscattering: uses the SH L0 coefficient from
            /// the IBL irradiance projection as the average sky color. Our equivalent of
            /// a precomputed 1-pixel distant-sky luminance LUT. The SH L0 from
            /// the IBL irradiance projection, which is the average color of the sky cubemap.
            /// SH L0 in reference engines packed format: coeffs[0].w=R, coeffs[1].w=G, coeffs[2].w=B.
            /// These are already the sky's average irradiance (blue for a daytime sky).
            /// Scale SH L0 sky color by SkyLight intensity:
            /// SkyLightColor = Intensity * LightColor (standard sky light convention).
            float skyFogR = m_skyIrradianceSH.coeffs[0].w * ctx.perFrame.skyLightIntensity;
            float skyFogG = m_skyIrradianceSH.coeffs[1].w * ctx.perFrame.skyLightIntensity;
            float skyFogB = m_skyIrradianceSH.coeffs[2].w * ctx.perFrame.skyLightIntensity;


            if (m_passInspector.IsPassEnabled("fog")) {
                AddHeightFogFromECS(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc,
                                    ctx.world, *m_camera, ctx.pipelines, *m_cbRing,
                                    m_gbuffer.depthSRV, m_gbuffer.depth,
                                    ctx.bindless, ctx.renderWidth, ctx.renderHeight,
                                    skyFogR, skyFogG, skyFogB);
            }

            // ── Volumetric Clouds (compute, composited over HDR) ─────────────
            if (m_cloudPass && m_cloudPass->IsInitialized()
                && m_passInspector.IsPassEnabled("clouds") && ctx.world) {

                auto cloudView = ctx.world->GetRegistry().view<VolumetricCloudsComponent>();
                for (auto enttEntity : cloudView) {
                    auto& cloudComp = cloudView.get<VolumetricCloudsComponent>(enttEntity);
                    if (!cloudComp.enabled) continue;

                    // Fill CloudPassInput from component + frame data.
                    // Use UNJITTERED invViewProj — jittered matrix causes cloud noise
                    // to shift every frame (TSR subpixel jitter), creating false movement.
                    CloudPassInput cloudInput{};
                    Mat4 unjitteredInvVP = ctx.unjitteredViewProj.Inverse();
                    std::memcpy(cloudInput.invViewProj, &unjitteredInvVP, 64);
                    // Use previous frame's unjittered view-projection for temporal reprojection.
                    // On the first frame, m_prevViewProj is identity — the temporal filter
                    // converges after a few frames regardless.
                    std::memcpy(cloudInput.prevViewProj, &m_prevViewProj, 64);

                    // Camera position: convert from cm (engine units) to km (cloud units)
                    cloudInput.cameraPos[0] = ctx.perFrame.cameraPos.x * 0.00001f;
                    cloudInput.cameraPos[1] = ctx.perFrame.cameraPos.y * 0.00001f;
                    cloudInput.cameraPos[2] = ctx.perFrame.cameraPos.z * 0.00001f;

                    // Sun direction and color from the scene's directional light
                    cloudInput.sunDir[0] = ctx.sunDir.x;
                    cloudInput.sunDir[1] = ctx.sunDir.y;
                    cloudInput.sunDir[2] = ctx.sunDir.z;
                    cloudInput.sunColor[0] = ctx.sunColor.x;
                    cloudInput.sunColor[1] = ctx.sunColor.y;
                    cloudInput.sunColor[2] = ctx.sunColor.z;

                    // Sky ambient for multi-scatter approximation (SH L0 from IBL)
                    cloudInput.ambientColor[0] = skyFogR;
                    cloudInput.ambientColor[1] = skyFogG;
                    cloudInput.ambientColor[2] = skyFogB;

                    cloudInput.time              = m_totalTime;
                    cloudInput.coverage          = cloudComp.coverage;
                    cloudInput.density           = cloudComp.density;
                    cloudInput.extinction        = cloudComp.extinction;
                    cloudInput.scatteringAlbedo  = cloudComp.scatteringAlbedo;
                    cloudInput.phaseG            = cloudComp.phaseG;
                    cloudInput.phaseG2           = cloudComp.phaseG2;
                    cloudInput.phaseBlend        = cloudComp.phaseBlend;
                    cloudInput.layerBottomKm     = cloudComp.layerBottomAltitudeKm;
                    cloudInput.layerHeightKm     = cloudComp.layerHeightKm;
                    cloudInput.windVelocity[0]   = cloudComp.windVelocity.x;
                    cloudInput.windVelocity[1]   = cloudComp.windVelocity.y;
                    cloudInput.windVelocity[2]   = cloudComp.windVelocity.z;
                    cloudInput.detailWindScale   = cloudComp.detailWindScale;
                    cloudInput.ambientMultiplier = cloudComp.ambientMultiplier;
                    cloudInput.primarySteps      = cloudComp.primarySteps;
                    cloudInput.lightSteps        = cloudComp.lightSteps;
                    cloudInput.depthSrv          = m_gbuffer.depthSRV;
                    cloudInput.globalCoverage    = cloudComp.coverage;
                    cloudInput.temporalBlendFactor = 1.0f; // DEBUG: 100% current, skip temporal history

                    // Store cloud input for deferred execution after render graph.
                    // CloudPass::Execute() manages its own barriers on internally-owned
                    // textures (noise, weather, cloud output). Running it inside a render
                    // graph callback conflicts with the graph's barrier tracking and causes
                    // "closed command list" errors. Instead, we execute it as a standalone
                    // compute dispatch after rgBuilder.Execute(), similar to the IBL bake.
                    m_pendingCloudInput = std::make_unique<CloudPassInput>(cloudInput);
                    m_pendingCloudDispatch = true;

                    // Cloud composite pass — fullscreen triangle that alpha-blends
                    // the half-res cloud output over the HDR scene color using
                    // premultiplied-alpha: final.rgb = cloud.rgb + scene.rgb * transmittance
                    if (m_cloudOutputSrv != UINT32_MAX)
                    rgBuilder.AddPass("CloudComposite", [this, &ctx](rg::RgPassBuilder& pb) {
                        pb.Write(ctx.hdrRes, RHIResourceState::RenderTarget);

                        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& res) {
                            UZ_PROFILE_SCOPE("CloudPass::Composite");

                            if (m_cloudOutputSrv == UINT32_MAX) {
                                UZ_LOG(Renderer, Warning, "CloudComposite: skipped — m_cloudOutputSrv is UINT32_MAX");
                                return;
                            }

                            static bool loggedOnce = false;
                            if (!loggedOnce) {
                                UZ_LOG(Renderer, Info, "CloudComposite: executing — cloudSRV={}, depthSRV={}",
                                       m_cloudOutputSrv, m_gbuffer.depthSRV);
                                loggedOnce = true;
                            }

                            auto compositePSO = ctx.pipelines->GetCloudComposite();
                            if (!compositePSO.IsValid()) return;

                            // Begin render pass — blend over existing HDR scene (no clear)
                            auto hdrTex = res.GetTexture(ctx.hdrRes);
                            RHIRenderPassDesc passDesc{};
                            passDesc.colorAttachmentCount = 1;
                            passDesc.colorAttachments[0].texture = hdrTex;
                            passDesc.colorAttachments[0].clear   = false;
                            passDesc.hasDepth = false;
                            cmd->BeginRenderPass(passDesc);

                            cmd->SetPipeline(compositePSO);

                            // Root constants: cloud SRV index + depth SRV index
                            const uint32_t rootConst[4] = { m_cloudOutputSrv, m_gbuffer.depthSRV, 0, 0 };
                            cmd->SetRootConstants(kSlotRootConstants, rootConst, 4);
                            cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                            ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                            sizeof(PerFrameCB));

                            RHIViewport vp{};
                            vp.width    = ctx.renderWidth;
                            vp.height   = ctx.renderHeight;
                            vp.maxDepth = 1.0f;
                            cmd->SetViewport(vp);

                            RHIScissorRect sc{};
                            sc.right  = static_cast<int32_t>(ctx.renderWidth);
                            sc.bottom = static_cast<int32_t>(ctx.renderHeight);
                            cmd->SetScissor(sc);

                            cmd->Draw(3);  // Fullscreen triangle

                            cmd->EndRenderPass();
                        });
                    });

                    break;  // First VolumetricCloudsComponent only (singleton semantics)
                }
            }
        }
    }

    // ── SSGI Previous Frame Color Copy ──────────────────────────────────────
    // Copy the current HDR scene color (after lighting + atmosphere) to the
    // prev-frame buffer for SSGI to use next frame. This must happen AFTER
    // all scene color contributions (lighting, atmosphere, fog) and BEFORE
    // post-process modifies the HDR buffer (bloom, tonemap, etc.).
    if (ssgiActive) {
        rgBuilder.AddPass("SSGI_CopyPrevColor", [&](rg::RgPassBuilder& pb) {
            pb.Read(ctx.hdrRes, RHIResourceState::RenderTarget);

            pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
                UZ_PROFILE_SCOPE("SSGI::CopyPrevColor");

                m_ssgiRenderer.CopyPrevSceneColor(
                    cmd, m_hdrSceneColor,
                    static_cast<uint32_t>(ctx.renderWidth),
                    static_cast<uint32_t>(ctx.renderHeight));
            });
        });
    }

    // ── PassInspector: register intermediate targets for visualization ───
    {
        const uint32_t rw = static_cast<uint32_t>(ctx.renderWidth);
        const uint32_t rh = static_cast<uint32_t>(ctx.renderHeight);

        // GBuffer targets
        m_passInspector.RegisterTarget("gbuffer.albedo",   m_gbuffer.albedo,   m_gbuffer.albedoSRV,   VisDisplayMode::Color,     rw, rh);
        m_passInspector.RegisterTarget("gbuffer.normal",   m_gbuffer.normal,   m_gbuffer.normalSRV,   VisDisplayMode::Normal,    rw, rh);
        // GBuffer depth ends the frame in DepthStencilWrite (the render graph
        // does not emit restore barriers for imported depth resources).
        m_passInspector.RegisterTarget("gbuffer.depth",    m_gbuffer.depth,    m_gbuffer.depthSRV,    VisDisplayMode::Depth,     rw, rh,
                                        RHIResourceState::DepthStencilWrite);
        m_passInspector.RegisterTarget("gbuffer.velocity", m_gbuffer.velocity, m_gbuffer.velocitySRV, VisDisplayMode::Velocity,  rw, rh);
        m_passInspector.RegisterTarget("gbuffer.emissive", m_gbuffer.emissive, m_gbuffer.emissiveSRV, VisDisplayMode::Color,     rw, rh);

        // Shadow atlas (tile resolution used as approximate display dimensions)
        if (m_shadowAtlas.GetTexture().IsValid() && m_shadowAtlas.GetSrvIndex() != UINT32_MAX) {
            // Shadow atlas ends the frame in DepthStencilWrite (the render graph
            // does not emit restore barriers for imported depth resources).
            m_passInspector.RegisterTarget("shadow.atlas", m_shadowAtlas.GetTexture(), m_shadowAtlas.GetSrvIndex(),
                                            VisDisplayMode::Grayscale, kShadowResolution, kShadowResolution,
                                            RHIResourceState::DepthStencilWrite);
        }

        // GTAO
        if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
            const uint32_t gtaoSrv = m_gtaoPass->GetFilteredAOSrvIndex();
            if (gtaoSrv != UINT32_MAX) {
                m_passInspector.RegisterTarget("gtao.result", m_gtaoPass->GetFilteredAOTexture(), gtaoSrv,
                                                VisDisplayMode::Grayscale, rw, rh);
            }
        }

        // SSGI intermediate textures
        if (ssgiActive) {
            const uint32_t hw = m_ssgiRenderer.GetHalfWidth();
            const uint32_t hh = m_ssgiRenderer.GetHalfHeight();

            m_passInspector.RegisterTarget("ssgi.prev_color", m_ssgiRenderer.GetPrevColorTexture(),
                                            m_ssgiRenderer.GetPrevColorSrvIndex(), VisDisplayMode::Color, rw, rh);
            m_passInspector.RegisterTarget("ssgi.trace", m_ssgiRenderer.GetTraceTexture(),
                                            m_ssgiRenderer.GetTraceSrvIndex(), VisDisplayMode::HDR, hw, hh);
            m_passInspector.RegisterTarget("ssgi.denoised", m_ssgiRenderer.GetDenoisedTexture(),
                                            m_ssgiRenderer.GetDenoisedSrvIndex(), VisDisplayMode::HDR, hw, hh);
            m_passInspector.RegisterTarget("ssgi.result", m_ssgiRenderer.GetResultTexture(),
                                            m_ssgiRenderer.GetResultSrvIndex(), VisDisplayMode::HDR, rw, rh);
            m_passInspector.RegisterTarget("ssgi.ao", m_ssgiRenderer.GetAOResultTexture(),
                                            m_ssgiRenderer.GetAOResultSrvForInspector(), VisDisplayMode::Grayscale, rw, rh);
        }

        // SSR
        if (ctx.ssrActive) {
            m_passInspector.RegisterTarget("ssr.trace", m_ssrTraceOutput, m_ssrTraceOutputSrv,
                                            VisDisplayMode::Color, rw, rh);
            m_passInspector.RegisterTarget("ssr.result", m_ssrResolvedOutput, m_ssrResolvedOutputSrv,
                                            VisDisplayMode::Color, rw, rh);
        }

        // HDR scene color (after lighting + atmosphere, before postprocess)
        m_passInspector.RegisterTarget("hdr", m_hdrSceneColor, m_hdrSceneColorSrv,
                                        VisDisplayMode::HDR, rw, rh);

        // DFS output
        if (dfsEnabled && m_dfsOutputSrvIndex != UINT32_MAX) {
            const uint32_t dfsW = m_dfsConfig.fullResolution ? rw : rw / 2;
            const uint32_t dfsH = m_dfsConfig.fullResolution ? rh : rh / 2;
            m_passInspector.RegisterTarget("dfs.result", m_dfsOutputTexture, m_dfsOutputSrvIndex,
                                            VisDisplayMode::Grayscale, dfsW, dfsH);
        }

        // Atmosphere LUTs
        if (m_transmittanceLut.IsValid() && m_transmittanceLutSrv != UINT32_MAX && m_transmittanceLutFilled) {
            m_passInspector.RegisterTarget("atmo.transmittance", m_transmittanceLut, m_transmittanceLutSrv,
                                            VisDisplayMode::Color, 256, 64);
        }
        if (m_multiScatterLut.IsValid() && m_multiScatterLutSrv != UINT32_MAX && m_multiScatterLutFilled) {
            m_passInspector.RegisterTarget("atmo.multiscatter", m_multiScatterLut, m_multiScatterLutSrv,
                                            VisDisplayMode::Color, 32, 32);
        }
        if (m_apVolume.IsValid() && m_apVolumeSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("atmo.ap_volume", m_apVolume, m_apVolumeSrv,
                                            VisDisplayMode::HDR, 32, 32);
        }
        if (m_skyCubemap.IsValid() && m_skyCubemapSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("atmo.sky_cubemap", m_skyCubemap, m_skyCubemapSrv,
                                            VisDisplayMode::HDR, 64, 64);
        }

        // Cloud output (half-res RGBA16F) — register both ping-pong buffers for debugging
        if (m_cloudPass && m_cloudPass->IsInitialized() && m_cloudOutputSrv != UINT32_MAX) {
            m_passInspector.RegisterTarget("cloud.output", m_cloudPass->GetCloudOutputTexture(),
                                            m_cloudOutputSrv, VisDisplayMode::HDR,
                                            rw / 2, rh / 2);
            // DEBUG: also register both raw ping-pong textures
            m_passInspector.RegisterTarget("cloud.raw0", m_cloudPass->GetDebugRaymarchOutput(0),
                                            m_cloudPass->GetDebugRaymarchOutputSrv(0), VisDisplayMode::HDR,
                                            rw / 2, rh / 2);
            m_passInspector.RegisterTarget("cloud.raw1", m_cloudPass->GetDebugRaymarchOutput(1),
                                            m_cloudPass->GetDebugRaymarchOutputSrv(1), VisDisplayMode::HDR,
                                            rw / 2, rh / 2);
        }

        // HiZ pyramid mip 0 (half-res R32F depth min)
        if (m_hizMipCount > 0 && m_hizMipLevels[0].srvIndex != UINT32_MAX) {
            m_passInspector.RegisterTarget("hiz.mip0", m_hizMipLevels[0].texture, m_hizMipLevels[0].srvIndex,
                                            VisDisplayMode::Grayscale, m_hizMipLevels[0].width, m_hizMipLevels[0].height);
        }

        // TAA history (current write target)
        if (m_taaHistory[m_taaHistoryIndex].IsValid() && m_taaHistorySrv[m_taaHistoryIndex] != UINT32_MAX) {
            m_passInspector.RegisterTarget("taa.history", m_taaHistory[m_taaHistoryIndex],
                                            m_taaHistorySrv[m_taaHistoryIndex],
                                            VisDisplayMode::Color, rw, rh);
        }

        // Global Distance Field cascade 0
        if (m_sdfGiEnabled) {
            const auto& clip = m_globalDistanceField.GetClipmap(0);
            if (clip.texture.IsValid() && clip.srvIndex != UINT32_MAX) {
                m_passInspector.RegisterTarget("gi.gdf_cascade0", clip.texture, clip.srvIndex,
                                                VisDisplayMode::Grayscale, 64, 64);
            }
        }
    }
}

} // namespace uz
