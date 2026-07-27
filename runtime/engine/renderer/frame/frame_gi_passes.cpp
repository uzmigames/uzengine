/// UzEngine Scene Renderer — GI-related passes: SSGI, SDF GI (GDF composite +
/// radiance cache trace), SSR (trace + resolve + temporal), and SSGI prev-frame
/// color copy.
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
#include "gi/ssgi_renderer.h"
#include "gi/mesh_sdf_baker.h"
#include "raytracing/rt_manager.h"
#include "gpu_mesh.h"
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

// SSR per-pass CVars (idempotent registration — same CVar pointers as frame_lighting.cpp)
static auto* cv_SSRMaxRoughness    = UZ_CVAR_FLOAT("r.SSR.MaxRoughness", 0.35f, "SSR max roughness cutoff", 0.05f, 1.0f);
static auto* cv_SSRIntensity       = UZ_CVAR_FLOAT("r.SSR.Intensity", 1.0f, "SSR reflection intensity", 0.0f, 5.0f);
static auto* cv_SSRTrace           = UZ_CVAR_FLOAT("r.SSR.Trace", 1.0f, "Enable SSR trace pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRResolve         = UZ_CVAR_FLOAT("r.SSR.Resolve", 1.0f, "Enable SSR resolve pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRTemporal        = UZ_CVAR_FLOAT("r.SSR.Temporal", 1.0f, "Enable SSR temporal pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRSpatialDenoise  = UZ_CVAR_FLOAT("r.SSR.SpatialDenoise", 1.0f, "Enable SSR spatial denoise pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRComposite       = UZ_CVAR_FLOAT("r.SSR.Composite", 1.0f, "Enable SSR post-TAA composite (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRCopy            = UZ_CVAR_FLOAT("r.SSR.CopyPrev", 1.0f, "Enable SSR prev-frame color copy (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRNumSteps        = UZ_CVAR_INT("r.SSR.NumSteps", 16, "SSR ray march step count", 8, 64);
static auto* cv_SSRStartMip        = UZ_CVAR_FLOAT("r.SSR.StartMipLevel", 1.0f, "SSR initial HZB mip level", 0.f, 3.f);
static auto* cv_SSRSlopeTolerance  = UZ_CVAR_FLOAT("r.SSR.SlopeToleranceScale", 4.0f, "SSR slope tolerance scale", 1.f, 16.f);

/// Selects DXR ray-traced probe update over the SDF sphere-trace path.
/// Requires DXR 1.1 (`RayQuery<>`) and a built TLAS. When the device does not
/// support DXR or the TLAS is not yet ready, the runtime falls back to the
/// SDF radiance cache shader transparently. @ref Majercik 2019 "DDGI"
static auto* cv_RayTracingGI       = UZ_CVAR_BOOL("r.RayTracing.GI", false, "Use DXR ray-traced probe update (DDGI) instead of SDF sphere-tracing");

/// Average scene diffuse albedo used as the hit-surface albedo by the probe
/// tracers' surface-cache-lite shading (gi_hit_lighting.hlsli). The GDF has
/// no per-hit material identity, so a scene-wide average is the honest input;
/// 0.35 matches the measured Sponza GBuffer albedo average. A per-instance
/// material table (DXR path) supersedes this for RayQuery hits.
static auto* cv_GISceneAlbedo      = UZ_CVAR_FLOAT("r.GI.SceneAlbedo", 0.35f, "Average scene diffuse albedo for probe hit shading", 0.f, 1.f);

#include <cstring>

namespace uz {

// ── SSGI Compute Passes (after GTAO, before SSR and lighting) ───────────────
// Dispatches 5 compute passes: PrevColorReduce, Trace, SpatialDenoise,
// TemporalAccum, BilateralUpsample. The result SRV indices are written
// to PerFrameCB in frame_setup.cpp for the deferred lighting shader.

void SceneRenderer::RegisterSSGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_showFlags.Get(ShowFlag::SSGI)) return;
    // Screen-space global illumination — traces against the depth buffer for indirect light.
    const bool ssgiActive = m_ssgiRenderer.IsReady()
                          && m_device->IsBindless()
                          && m_passInspector.IsPassEnabled("ssgi");
    if (!ssgiActive) return;

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

// ── SDF GI: GDF Composite + Radiance Cache Trace ───────────────────────────
// Updates the global distance field from mesh SDFs, then traces radiance
// probes. The probe SH buffer SRV is written to PerFrameCB for the
// deferred lighting pass.

void SceneRenderer::RegisterSDFGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // GDF composite + radiance cache probe trace for software-raytraced GI.
    const bool sdfGiActive = m_sdfGiEnabled
                           && m_globalDistanceField.IsInitialized()
                           && m_radianceCache.IsInitialized()
                           && m_device->IsBindless()
                           && m_passInspector.IsPassEnabled("sdf_gi");
    if (!sdfGiActive) return;

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

    // ── Pass: Radiance Cache Trace (SDF) and DDGI Probe Update (DXR) ───
    // The SDF path always runs (writes to the L2 SH probe buffer sampled
    // by the existing `_rcEnabled` block in deferred lighting). When
    // `r.RayTracing.GI` is enabled and a TLAS is ready, the DDGI atlas
    // writer also runs and updates the octahedral irradiance + depth
    // atlases used by the Chebyshev sampler in deferred lighting.
    // @ref Majercik 2019 "DDGI" — atlases + Chebyshev visibility
    rgBuilder.AddPass("RadianceCache_Trace", [&](rg::RgPassBuilder& pb) {
        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("RadianceCache::Trace");

            // Always run the SDF SH path so the existing deferred lighting
            // SH sample stays populated regardless of DXR availability.
            auto rcPipeline = ctx.pipelines->GetRadianceCacheTrace();
            if (rcPipeline.IsValid()) {
                Vec3 camPos = ctx.perFrame.cameraPos.xyz();
                m_radianceCache.Update(camPos);

                // Surface-cache-lite hit inputs: scene-average albedo (the
                // GDF carries no per-hit material identity) + the atmosphere
                // sky cubemap for directional miss radiance. The cubemap
                // holds last frame's capture — sky changes slowly, and the
                // EMA-blended probes absorb the 1-frame lag.
                const float avgAlbedo = cv_GISceneAlbedo->value;
                const Vec3 sceneAlbedo{avgAlbedo, avgAlbedo, avgAlbedo};
                const uint32_t skySrv =
                    (m_skyCubemap.IsValid() && m_skyCubemapSrv != UINT32_MAX)
                        ? m_skyCubemapSrv : UINT32_MAX;

                m_radianceCache.UpdateProbes(
                    cmd, m_globalDistanceField, rcPipeline,
                    m_cbRing.get(),
                    ctx.perFrameAlloc.bufferHandle,
                    ctx.perFrameAlloc.offsetBytes,
                    ctx.perFrameAlloc.cpuPtr,
                    sizeof(PerFrameCB),
                    ctx.frameIndex,
                    sceneAlbedo,
                    skySrv);
            }

            // DDGI atlas writer — only when DXR + TLAS + CVar are ready.
            // Read the CVar through the registry (file-static pointers go
            // stale across module init — see the r.GTAO gating incident).
            const bool wantDdgi =
                CVarRegistry::Get().GetBool("r.RayTracing.GI", false)
                && m_rtManager && m_rtManager->IsReady()
                && m_ddgiSystem.IsInitialized();
            if (wantDdgi) {
                auto ddgiPipeline = ctx.pipelines->GetDdgiProbeUpdate();
                if (ddgiPipeline.IsValid()) {
                    // ── Per-mesh material table (GIMeshMaterialGPU) ─────
                    // Indexed by TLAS InstanceID (= meshId). Refreshed every
                    // frame from the draw list — a 2 KB Upload-heap memcpy.
                    // The albedo TEXTURE average is sampled in-shader at the
                    // mip tail; here we only record tint + SRV.
                    const uint32_t meshCap =
                        static_cast<uint32_t>(m_meshes.size());
                    if (meshCap > 0
                        && (!m_giMaterialTable.IsValid()
                            || m_giMaterialTableCapacity < meshCap)) {
                        if (m_giMaterialTable.IsValid()) {
                            m_device->DestroyBuffer(m_giMaterialTable);
                            m_giMaterialTable = {};
                            m_giMaterialTableSrv = UINT32_MAX;
                        }
                        RHIBufferDesc desc{};
                        desc.sizeBytes = static_cast<uint64_t>(meshCap)
                                       * sizeof(GIMeshMaterialGPU);
                        desc.stride    = sizeof(GIMeshMaterialGPU);
                        desc.usage     = RHIBufferUsage::StructuredBuffer;
                        desc.memory    = RHIMemoryType::Upload;
                        desc.debugName = "GI_MaterialTable";
                        m_giMaterialTable = m_device->CreateBuffer(desc);
                        m_giMaterialTableSrv = m_giMaterialTable.IsValid()
                            ? m_device->GetBufferSRVIndex(m_giMaterialTable)
                            : UINT32_MAX;
                        m_giMaterialTableCapacity = meshCap;
                    }
                    if (m_giMaterialTable.IsValid()) {
                        auto* entries = static_cast<GIMeshMaterialGPU*>(
                            m_device->MapBuffer(m_giMaterialTable));
                        if (entries) {
                            for (uint32_t e = 0; e < meshCap; ++e) {
                                entries[e] = {1.f, 1.f, 1.f, UINT32_MAX};
                            }
                            for (const auto& dc : ctx.drawList.GetCommands()) {
                                if (dc.meshId >= meshCap) continue;
                                auto& entry = entries[dc.meshId];
                                entry.tintR = dc.baseColorTintR;
                                entry.tintG = dc.baseColorTintG;
                                entry.tintB = dc.baseColorTintB;
                                // Slot 0 doubles as "unset" in DrawCommand.
                                entry.albedoSrv =
                                    (dc.albedoSrvIndex != 0
                                     && dc.albedoSrvIndex != UINT32_MAX)
                                        ? dc.albedoSrvIndex : UINT32_MAX;
                            }
                            m_device->UnmapBuffer(m_giMaterialTable);
                        }
                    }

                    const float avgAlbedo = cv_GISceneAlbedo->value;
                    const uint32_t tlasSrv = m_rtManager->GetTLASSRVIndex();
                    m_ddgiSystem.DispatchUpdate(
                        cmd, ddgiPipeline, tlasSrv,
                        m_cbRing.get(), ctx.frameIndex,
                        &m_globalDistanceField,
                        ctx.perFrameAlloc.cpuPtr,
                        Vec3{avgAlbedo, avgAlbedo, avgAlbedo},
                        (m_skyCubemap.IsValid()
                         && m_skyCubemapSrv != UINT32_MAX)
                            ? m_skyCubemapSrv : UINT32_MAX,
                        m_giMaterialTableSrv,
                        m_radianceCache.GetProbeSHBufferSrv());
                }
            }
        });
    });
}

// ── Brixelizer GI (AMD FidelityFX sparse SDF voxel GI) ─────────────────────
// Dispatches Brixelizer SDF bake + BrixelizerGI probe-based GI.
// Produces two RGBA16F textures: diffuseGI and specularGI.
// The SRV indices are written to PerFrameCB in frame_constants.cpp.

void SceneRenderer::RegisterBrixelizerGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    return; // DISABLED: BrixelizerGI crashing render — re-enable after fix
    if (!m_brixelizerGI || !m_brixelizerGI->IsInitialized()) return;
    if (!m_passInspector.IsPassEnabled("brixelizer_gi")) return;
    // Diagnostic gate — file-scope `cv_BrixDispatch` lives in
    // `brixelizer_gi_pass.cpp`. Reach the same value via the global CVar
    // registry so a CLI `--cvar r.BrixelizerGI.Dispatch=0` override is
    // honoured here even though the C++ pointer lives in another TU.
    if (!uz::CVarRegistry::Get().GetBool("r.BrixelizerGI.Dispatch", true)) return;

    rgBuilder.AddPass("BrixelizerGI", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);
        // HDR scene color is read by the BrixelizerGI dispatch as
        // `prevLitOutput` (temporal GI history). Without declaring this
        // dependency, the render graph leaves the HDR target in whatever
        // state the previous pass left it (UAV in many configurations),
        // and the FFX backend's internal barrier diverges from the
        // RG-tracked state. The resulting tonemap then samples a
        // misbarriered HDR target and reads zero — the screen turns black
        // even though the GBuffer + lighting wrote correct values.
        pb.Read(ctx.hdrRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("BrixelizerGI::Execute");

            // Register geometry from the draw list for SDF voxelization.
            // Pass the renderer-owned mesh array so the pass can extract
            // vertex/index buffers and AABBs by `meshId`.
            m_brixelizerGI->RegisterGeometry(
                ctx.drawList.GetCommands(),
                m_meshes.data(),
                static_cast<uint32_t>(m_meshes.size()));

            // Execute SDF bake + GI dispatch
            m_brixelizerGI->Execute(
                cmd,
                ctx.perFrame.viewProj.data(),    // view matrix
                ctx.perFrame.viewProj.data(),    // proj matrix (combined viewProj for now)
                ctx.perFrame.viewProj.data(),    // prev view
                ctx.perFrame.viewProj.data(),    // prev proj
                ctx.perFrame.cameraPos.x,
                ctx.perFrame.cameraPos.y,
                ctx.perFrame.cameraPos.z,
                ctx.frameIndex,
                m_gbuffer.depth,
                m_gbuffer.normal,
                m_gbuffer.velocity,
                m_skyCubemap,
                m_hdrSceneColor);
        });
    });
}

// ── SSR Compute Passes (after GBuffer + depth prepass + HiZ, before lighting)
// Two compute passes:
//   1. SSR_Trace:   Hi-Z ray march → trace output (rgb=hit color, a=confidence)
//   2. SSR_Resolve: Fresnel weight + roughness fade → resolved SSR for lighting
//
// The lighting pass blends SSR with IBL specular via ssrSrvIndex in PerFrameCB.

void SceneRenderer::RegisterSSRPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_showFlags.Get(ShowFlag::SSR)) return;
    // Screen-space reflections: Hi-Z ray march + Fresnel-weighted resolve.
    // Gate: skip if SSR not active OR if we don't have a prev-frame color copy yet.
    // On frame 0, m_ssrHasPrevColor is false — SSR starts on frame 1+.
    // @ref UE5 ScreenSpaceReflections.cpp — reads PrevViewInfo.TemporalAAHistory
    if (!ctx.ssrActive || !m_ssrHasPrevColor) return;

    // ── SSR Tile Classification ─────────────────────────────────────────
    // @ref AMD FidelityFX SSSR — classify tiles, compact ray list, prepare indirect args.
    // Skips tiles where all pixels have roughness >= MaxRoughness.
    rgBuilder.AddPass("SSR_TileClassify", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("SSR::TileClassify");

            auto classifyPSO = ctx.pipelines->GetSsrTileClassify();
            if (!classifyPSO.IsValid()) return;

            // Counter was reset to 0 by last frame's PrepareIndirect pass.
            // Transition counter buffer to UAV for the classify shader.
            {
                RHIBarrierDesc barrier{};
                barrier.buffer      = m_ssrRayCounterArgs;
                barrier.stateBefore = RHIResourceState::IndirectArgument;
                barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&barrier, 1);
            }

            cmd->SetPipeline(classifyPSO);

            const float ssrMaxRoughness = std::max(0.05f, cv_SSRMaxRoughness->value);

            struct SsrClassifyConstants {
                uint32_t normalSrvIdx, depthSrvIdx, rayListUavIdx, counterArgsUavIdx;
                uint32_t outputWidth, outputHeight;
                float roughnessThreshold;
                uint32_t pad7;
            };

            SsrClassifyConstants rc{};
            rc.normalSrvIdx       = m_gbuffer.normalSRV;
            rc.depthSrvIdx        = m_gbuffer.depthSRV;
            rc.rayListUavIdx      = m_ssrRayListUav;
            rc.counterArgsUavIdx  = m_ssrRayCounterArgsUav;
            rc.outputWidth        = m_renderWidth;
            rc.outputHeight       = m_renderHeight;
            rc.roughnessThreshold = ssrMaxRoughness;

            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);

            const uint32_t groupsX = (m_renderWidth + 7) / 8;
            const uint32_t groupsY = (m_renderHeight + 7) / 8;
            cmd->Dispatch(groupsX, groupsY, 1);

            // UAV barrier on ray list + counter before prepare indirect reads them
            {
                RHIBarrierDesc barrier{};
                barrier.buffer      = m_ssrRayCounterArgs;
                barrier.stateBefore = RHIResourceState::UnorderedAccess;
                barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&barrier, 1);
            }
        });
    });

    // ── SSR Prepare Indirect Args ───────────────────────────────────────
    rgBuilder.AddPass("SSR_PrepareIndirect", [&](rg::RgPassBuilder& pb) {
        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("SSR::PrepareIndirect");

            auto preparePSO = ctx.pipelines->GetSsrPrepareIndirect();
            if (!preparePSO.IsValid()) return;

            cmd->SetPipeline(preparePSO);

            struct SsrPrepareConstants {
                uint32_t counterArgsUavIdx;
            };
            SsrPrepareConstants prc{};
            prc.counterArgsUavIdx = m_ssrRayCounterArgsUav;
            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&prc), 1);

            cmd->Dispatch(1, 1, 1);

            // Transition counter/args buffer to IndirectArgument state for DispatchIndirect
            {
                RHIBarrierDesc barrier{};
                barrier.buffer      = m_ssrRayCounterArgs;
                barrier.stateBefore = RHIResourceState::UnorderedAccess;
                barrier.stateAfter  = RHIResourceState::IndirectArgument;
                cmd->ResourceBarrier(&barrier, 1);
            }
        });
    });

    // ── SSR Trace (now with indirect dispatch) ──────────────────────────
    rgBuilder.AddPass("SSR_Trace", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("SSR::Trace");

            auto ssrTracePSO = ctx.pipelines->GetSsrTrace();
            if (!ssrTracePSO.IsValid() || cv_SSRTrace->value < 0.5f) return;

            // Transition both trace output and hit distance to UAV
            {
                RHIBarrierDesc barriers[2]{};
                barriers[0].texture     = m_ssrTraceOutput;
                barriers[0].stateBefore = RHIResourceState::ShaderResource;
                barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
                barriers[1].texture     = m_ssrHitDistance;
                barriers[1].stateBefore = RHIResourceState::ShaderResource;
                barriers[1].stateAfter  = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(barriers, 2);
            }

            cmd->SetPipeline(ssrTracePSO);

            struct SsrTraceConstants {
                uint32_t depthSrvIdx, normalSrvIdx, hizSrvIdx, sceneColorSrvIdx;
                uint32_t outputWidth, outputHeight;
                float intensity, roughnessMaskScale;
                uint32_t hitDistUavIdx, hizMip0SrvIdx, hizMip1SrvIdx, hizMipCount;
                float exposureCorrection;
                uint32_t numSteps;
                float startMipLevel, slopeToleranceScale;
            };

            // Read SSR MaxRoughness from CVar (avoid hardcoded constant)
            const float ssrMaxRoughness = std::max(0.05f, cv_SSRMaxRoughness->value);

            // Exposure correction: prevColor was at m_ssrPrevExposure, current is ctx exposure.
            // @ref AMD FidelityFX SSSR — prevColor *= currentExposure / prevExposure.
            const float currentExposure = ctx.perFrame.exposure;
            const float exposureRatio = (m_ssrPrevExposure > 0.001f)
                ? currentExposure / m_ssrPrevExposure
                : 1.0f;

            SsrTraceConstants rc{};
            rc.depthSrvIdx      = m_gbuffer.depthSRV;
            rc.normalSrvIdx     = m_gbuffer.normalSRV;
            // Full-res depth for binary refinement (phase 3)
            rc.hizSrvIdx        = m_gbuffer.depthSRV;
            // Use previous frame's HDR copy — breaks SSR feedback loop (UE5 pattern)
            rc.sceneColorSrvIdx = m_ssrPrevSceneColorSrv;
            rc.outputWidth      = m_renderWidth;
            rc.outputHeight     = m_renderHeight;
            rc.intensity        = cv_SSRIntensity->value;
            rc.roughnessMaskScale = -2.0f / ssrMaxRoughness;
            rc.hitDistUavIdx    = m_ssrHitDistanceUav;
            // Hi-Z mip SRV indices for hierarchical march
            rc.hizMip0SrvIdx    = (m_hizMipCount >= 1) ? m_hizMipLevels[0].srvIndex : UINT32_MAX;
            rc.hizMip1SrvIdx    = (m_hizMipCount >= 2) ? m_hizMipLevels[1].srvIndex : UINT32_MAX;
            rc.hizMipCount      = m_hizMipCount;
            rc.exposureCorrection = exposureRatio;
            rc.numSteps           = static_cast<uint32_t>(cv_SSRNumSteps->value);
            rc.startMipLevel      = cv_SSRStartMip->value;
            rc.slopeToleranceScale = cv_SSRSlopeTolerance->value;

            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 16);
            cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                            ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                            sizeof(PerFrameCB));
            cmd->SetDescriptorTable(4, m_ssrTraceOutputUav);

            const uint32_t groupsX = (m_renderWidth + 7) / 8;
            const uint32_t groupsY = (m_renderHeight + 7) / 8;
            cmd->Dispatch(groupsX, groupsY, 1);

            // Transition both back to SRV
            {
                RHIBarrierDesc barriers[2]{};
                barriers[0].texture     = m_ssrTraceOutput;
                barriers[0].stateBefore = RHIResourceState::UnorderedAccess;
                barriers[0].stateAfter  = RHIResourceState::ShaderResource;
                barriers[1].texture     = m_ssrHitDistance;
                barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
                barriers[1].stateAfter  = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(barriers, 2);
            }
        });
    });

    rgBuilder.AddPass("SSR_Resolve", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("SSR::Resolve");

            auto ssrResolvePSO = ctx.pipelines->GetSsrResolve();
            if (!ssrResolvePSO.IsValid() || cv_SSRResolve->value < 0.5f) return;

            {
                RHIBarrierDesc barrier{};
                barrier.texture     = m_ssrResolvedOutput;
                barrier.stateBefore = RHIResourceState::ShaderResource;
                barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                cmd->ResourceBarrier(&barrier, 1);
            }

            cmd->SetPipeline(ssrResolvePSO);

            struct SsrResolveConstants {
                uint32_t ssrTraceSrvIdx, normalSrvIdx, depthSrvIdx, reserved;
                uint32_t outputWidth, outputHeight;
                float intensity, roughnessMaskScale;
            };

            SsrResolveConstants rc{};
            rc.ssrTraceSrvIdx   = m_ssrTraceOutputSrv;
            rc.normalSrvIdx     = m_gbuffer.normalSRV;
            rc.depthSrvIdx      = m_gbuffer.depthSRV;
            rc.outputWidth      = m_renderWidth;
            rc.outputHeight     = m_renderHeight;
            rc.intensity        = cv_SSRIntensity->value;
            rc.roughnessMaskScale = -2.0f / std::max(0.05f, cv_SSRMaxRoughness->value);

            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);
            cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                            ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                            sizeof(PerFrameCB));
            cmd->SetDescriptorTable(4, m_ssrResolvedOutputUav);

            const uint32_t groupsX = (m_renderWidth + 7) / 8;
            const uint32_t groupsY = (m_renderHeight + 7) / 8;
            cmd->Dispatch(groupsX, groupsY, 1);

            {
                RHIBarrierDesc barrier{};
                barrier.texture     = m_ssrResolvedOutput;
                barrier.stateBefore = RHIResourceState::UnorderedAccess;
                barrier.stateAfter  = RHIResourceState::ShaderResource;
                cmd->ResourceBarrier(&barrier, 1);
            }
        });
    });

    // SSR Temporal accumulation — virtual hit reprojection + YCoCg variance clip.
    // @ref AMD FidelityFX SSSR — temporal pass uses hit distance for virtual reprojection.
    {
        const uint32_t ssrWriteIdx = m_ssrHistoryIndex;
        const uint32_t ssrReadIdx  = 1 - m_ssrHistoryIndex;
        const bool ssrFirstFrame = !m_prevViewProjValid
                                 || !m_ssrHistory[ssrReadIdx].IsValid();

        rgBuilder.AddPass("SSR_Temporal", [&, ssrWriteIdx, ssrReadIdx, ssrFirstFrame](rg::RgPassBuilder& pb) {
            pb.Read(ctx.velocityRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx, ssrWriteIdx, ssrReadIdx, ssrFirstFrame](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("SSR::Temporal");

                auto ssrTemporalPSO = ctx.pipelines->GetSsrTemporal();
                if (!ssrTemporalPSO.IsValid() || cv_SSRTemporal->value < 0.5f) return;

                {
                    RHIBarrierDesc barriers[2]{};
                    barriers[0].texture     = m_ssrHistory[ssrWriteIdx];
                    barriers[0].stateBefore = RHIResourceState::ShaderResource;
                    barriers[0].stateAfter  = RHIResourceState::UnorderedAccess;
                    barriers[1].texture     = m_ssrHistory[ssrReadIdx];
                    barriers[1].stateBefore = RHIResourceState::UnorderedAccess;
                    barriers[1].stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(barriers, 2);
                }

                cmd->SetPipeline(ssrTemporalPSO);

                struct SsrTemporalConstants {
                    uint32_t currentSsrSrvIdx, historySsrSrvIdx, velocitySrvIdx, depthSrvIdx;
                    uint32_t outputWidth, outputHeight, firstFrame, hitDistSrvIdx;
                };

                SsrTemporalConstants rc{};
                rc.currentSsrSrvIdx = m_ssrResolvedOutputSrv;
                rc.historySsrSrvIdx = m_ssrHistorySrv[ssrReadIdx];
                rc.velocitySrvIdx   = m_gbuffer.velocitySRV;
                rc.depthSrvIdx      = m_gbuffer.depthSRV;
                rc.outputWidth      = m_renderWidth;
                rc.outputHeight     = m_renderHeight;
                rc.firstFrame       = ssrFirstFrame ? 1u : 0u;
                rc.hitDistSrvIdx    = m_ssrHitDistanceSrv;

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);
                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                sizeof(PerFrameCB));

                // Bind prevViewProj matrix at slot 2 (b2) via CBV from the CB ring.
                // @ref AMD FidelityFX SSSR — prevVP needed for virtual hit reprojection.
                struct SsrTemporalPrevVP {
                    Mat4 prevViewProj;
                };
                auto prevVPAlloc = m_cbRing->Allocate(sizeof(SsrTemporalPrevVP));
                if (prevVPAlloc.cpuPtr) {
                    auto* pvp = reinterpret_cast<SsrTemporalPrevVP*>(prevVPAlloc.cpuPtr);
                    pvp->prevViewProj = m_prevViewProj;
                    cmd->SetRootCBV(kSlotPerObject, prevVPAlloc.bufferHandle,
                                    prevVPAlloc.offsetBytes, prevVPAlloc.cpuPtr,
                                    sizeof(SsrTemporalPrevVP));
                }

                cmd->SetDescriptorTable(4, m_ssrHistoryUav[ssrWriteIdx]);

                const uint32_t groupsX = (m_renderWidth + 7) / 8;
                const uint32_t groupsY = (m_renderHeight + 7) / 8;
                cmd->Dispatch(groupsX, groupsY, 1);

                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrHistory[ssrWriteIdx];
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }
            });
        });

        m_ssrHistoryIndex = 1 - m_ssrHistoryIndex;
    }

    // SSR Spatial Bilateral Denoise — post-temporal cleanup.
    // @ref AMD FidelityFX SSSR DNSR_Prefilter — bilateral denoise after temporal.
    {
        const uint32_t ssrJustWritten = 1 - m_ssrHistoryIndex;

        rgBuilder.AddPass("SSR_SpatialDenoise", [&, ssrJustWritten](rg::RgPassBuilder& pb) {
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx, ssrJustWritten](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("SSR::SpatialDenoise");

                auto ssrDenoisePSO = ctx.pipelines->GetSsrSpatialDenoise();
                if (!ssrDenoisePSO.IsValid() || cv_SSRSpatialDenoise->value < 0.5f) return;

                // Transition resolved output to UAV for writing
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrResolvedOutput;
                    barrier.stateBefore = RHIResourceState::ShaderResource;
                    barrier.stateAfter  = RHIResourceState::UnorderedAccess;
                    cmd->ResourceBarrier(&barrier, 1);
                }

                cmd->SetPipeline(ssrDenoisePSO);

                const float ssrMaxRoughness = std::max(0.05f, cv_SSRMaxRoughness->value);

                struct SsrDenoiseConstants {
                    uint32_t ssrInputSrvIdx, normalSrvIdx, depthSrvIdx, reserved;
                    uint32_t outputWidth, outputHeight;
                    float intensity, roughnessMaskScale;
                };

                SsrDenoiseConstants rc{};
                rc.ssrInputSrvIdx   = m_ssrHistorySrv[ssrJustWritten];
                rc.normalSrvIdx     = m_gbuffer.normalSRV;
                rc.depthSrvIdx      = m_gbuffer.depthSRV;
                rc.outputWidth      = m_renderWidth;
                rc.outputHeight     = m_renderHeight;
                rc.intensity        = cv_SSRIntensity->value;
                rc.roughnessMaskScale = -2.0f / ssrMaxRoughness;

                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 8);
                cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                                ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                                sizeof(PerFrameCB));
                cmd->SetDescriptorTable(4, m_ssrResolvedOutputUav);

                const uint32_t groupsX = (m_renderWidth + 7) / 8;
                const uint32_t groupsY = (m_renderHeight + 7) / 8;
                cmd->Dispatch(groupsX, groupsY, 1);

                // Transition back to SRV for lighting to read
                {
                    RHIBarrierDesc barrier{};
                    barrier.texture     = m_ssrResolvedOutput;
                    barrier.stateBefore = RHIResourceState::UnorderedAccess;
                    barrier.stateAfter  = RHIResourceState::ShaderResource;
                    cmd->ResourceBarrier(&barrier, 1);
                }
            });
        });
    }
}

// ── SSGI Previous Frame Color Copy ─────────────────────────────────────────
// Copy the current HDR scene color (after lighting + atmosphere) to the
// prev-frame buffer for SSGI to use next frame. This must happen AFTER
// all scene color contributions (lighting, atmosphere, fog) and BEFORE
// post-process modifies the HDR buffer (bloom, tonemap, etc.).

void SceneRenderer::RegisterSSGIPrevColorCopy(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    const bool ssgiActive = m_ssgiRenderer.IsReady()
                          && m_device->IsBindless()
                          && m_passInspector.IsPassEnabled("ssgi");
    if (!ssgiActive) return;

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

} // namespace uz
