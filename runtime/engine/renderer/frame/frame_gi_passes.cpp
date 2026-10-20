/// UzEngine Scene Renderer — GI-related passes: SSGI, SDF GI (GDF composite +
/// radiance cache trace), SSR (trace + resolve + temporal), and SSGI prev-frame
/// color copy.
///
/// Extracted from frame_lighting.cpp. All functions are private methods of
/// SceneRenderer, called by RegisterLightingPasses().

// radiance cache readiness: see HasProducedResult() in radiance_cache.h
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
#include "passes/raytracing/rt_reflection_pass.h"
#include "gpu_mesh.h"
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_states.h"
#include "render_graph_resources.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

// SSR per-pass CVars (idempotent registration — same CVar pointers as frame_lighting.cpp)
static auto* cv_SSRMaxRoughness    = UZ_CVAR_FLOAT("r.SSR.MaxRoughness", 0.35f, "SSR max roughness cutoff", 0.05f, 1.0f);
static auto* cv_SSRIntensity       = UZ_CVAR_FLOAT("r.SSR.Intensity", 1.0f, "SSR reflection intensity", 0.0f, 5.0f);
static auto* cv_GatherFreezeAfterFrame = UZ_CVAR_INT("r.GI.FinalGather.FreezeAfterFrame", -1,
    "Diagnostic (followup-12): from this absolute frame on, stop dispatching the final "
    "gather so its output texture holds its last converged contents while the deferred "
    "lighting pass keeps reading it. Unlike skipping UpdateProbes (which disables the GI "
    "path) or freezing the sampling seed (which reuses one noise pattern), this removes "
    "only the gather's per-frame variation. -1 = off.", -1, 100000);
static auto* cv_SSRTrace           = UZ_CVAR_FLOAT("r.SSR.Trace", 1.0f, "Enable SSR trace pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRResolve         = UZ_CVAR_FLOAT("r.SSR.Resolve", 1.0f, "Enable SSR resolve pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRTemporal        = UZ_CVAR_FLOAT("r.SSR.Temporal", 1.0f, "Enable SSR temporal pass (0=skip)", 0.0f, 1.0f);
static auto* cv_SSRSpatialDenoise  = UZ_CVAR_FLOAT("r.SSR.SpatialDenoise", 1.0f, "Enable SSR spatial denoise pass (0=skip)", 0.0f, 1.0f);
// r.SSR.Composite is registered where it is read (frame_postprocess.cpp); the copy that lived here was never
// read (block A.8 list, removed 2026-10-08).
static auto* cv_SSRNumSteps        = UZ_CVAR_INT("r.SSR.NumSteps", 16, "SSR ray march step count", 8, 64);
static auto* cv_SSRStartMip        = UZ_CVAR_FLOAT("r.SSR.StartMipLevel", 1.0f, "SSR initial HZB mip level", 0.f, 3.f);
static auto* cv_SSRSlopeTolerance  = UZ_CVAR_FLOAT("r.SSR.SlopeToleranceScale", 4.0f, "SSR slope tolerance scale", 1.f, 16.f);
// Hardware ray-traced reflections, stage 1. The on/off switch `r.RayTracing.Reflections` is
// registered in rt_manager.cpp and read through RTManager::IsReflectionsEnabled(); the ray's
// reach is the pass's only parameter of its own.
static auto* cv_RTReflectionsMaxDistance = UZ_CVAR_FLOAT("r.RayTracing.Reflections.MaxDistance", 100000.0f,
    "Hardware ray-traced reflections (stage 1): mirror-ray TMax in cm", 100.0f, 1000000.0f);
// Stage 2, each opt-in; at the defaults the pass is the stage 1 pass (mirror rays, sun + sky at the
// hit, raw output after the SSR denoise).
static auto* cv_RTReflectionsMaxRoughness = UZ_CVAR_FLOAT("r.RayTracing.Reflections.MaxRoughness", 0.0f,
    "Hardware ray-traced reflections (stage 2): roughness cutoff of the ray-traced reflection and its "
    "blend-weight fade; 0 = the screen trace's r.SSR.MaxRoughness (the stage 1 range)", 0.0f, 1.0f);
static auto* cv_RTReflectionsGlossy = UZ_CVAR_BOOL("r.RayTracing.Reflections.Glossy", false,
    "Hardware ray-traced reflections (stage 2): GGX-VNDF sampled glossy ray for roughness above the "
    "mirror threshold (0.04) instead of the mirror vector");
static auto* cv_RTReflectionsIndirect = UZ_CVAR_BOOL("r.RayTracing.Reflections.Indirect", false,
    "Hardware ray-traced reflections (stage 2): add the radiance-cache probe irradiance at the hit "
    "(the diffuse bounce the reflected surface receives)");
static auto* cv_RTReflectionsDenoise = UZ_CVAR_BOOL("r.RayTracing.Reflections.Denoise", false,
    "Hardware ray-traced reflections (stage 2): run the pass between SSR_Resolve and SSR_Temporal so "
    "the ray texels go through the SSR temporal accumulation + spatial denoise (0 = after them, raw)");

/// Selects DXR ray-traced probe update over the SDF sphere-trace path.
/// Requires DXR 1.1 (`RayQuery<>`) and a built TLAS. When the device does not
/// support DXR or the TLAS is not yet ready, the runtime falls back to the
/// SDF radiance cache shader transparently. @ref Majercik 2019 "DDGI"
// r.RayTracing.GI is registered in rt_manager.cpp (read there); this file reads it through the registry
// (one registration per CVar, 2026-10-08).
static const uz::CVar* RayTracingGICVar() {
    static const uz::CVar* cvar = uz::CVarRegistry::Get().Find("r.RayTracing.GI");
    return cvar;
}

#include <cstring>

static auto* cv_RCFreezeAfter = UZ_CVAR_INT("r.GI.RadianceCache.FreezeAfterFrame", -1,
    "Diagnostic (followup-12/13): from this absolute frame index on, the radiance-cache probes "
    "stop updating and the last converged SH buffer is held. -1 (default) never freezes. The "
    "frame is a parameter because freezing from the first frame holds a state that never "
    "converged, which measures the warm-up instead of stale lighting.", -1, 100000);
// Block B.2 phase 4: how the radiance cache reacts to a regional field change. 1 = retrace every
// probe keeping the history except inside the change boxes; 0 = discard the whole history as a
// whole-window recompose does (A/B control).
static auto* cv_GdfRegionalRetrace = UZ_CVAR_BOOL("r.GI.GDF.RegionalRetrace", true, "Radiance cache on a regional distance-field change: 1 = retrace keeping the history outside the change boxes, 0 = discard the history (control)");
static auto* cv_GDFFreezeCentre = UZ_CVAR_BOOL("r.GI.GDF.FreezeCenter", false,
    "Diagnostic (followup-13): stops the global distance field clipmaps recentring. Valid only "
    "while the measured geometry stays inside the coverage the field already had.");
static auto* cv_GDFCentrePhase = UZ_CVAR_FLOAT("r.GI.GDF.CenterPhaseOffset", 0.f,
    "Diagnostic (followup-13): shifts the clipmap voxel snapping grid by this many centimetres, "
    "so the same camera path crosses clipmap boundaries at different frames. Separates a "
    "grid-boundary effect from a feature of the trajectory.", -1000.f, 1000.f);

namespace uz {

// Shared diffuse gather follows GDF/RC updates and precedes lighting.
// World mode uses current surface rays; legacy SSGI also reduces previous HDR.
// Both reconstruct filtered E/pi for deferred lighting.

void SceneRenderer::RegisterSSGIPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (!m_showFlags.Get(ShowFlag::SSGI) && ctx.perFrame.sdfDiffuseSamples == 0) return;
    // Diagnostic (followup-12): hold the gather's OUTPUT from this frame on. The dispatch is
    // skipped, so its texture keeps its last converged contents and the deferred lighting pass
    // goes on reading it — the lighting path stays active and only the gather's per-frame
    // variation is removed. This is the control that skipping UpdateProbes (which disabled the
    // path) and freezing the sampling seed (which reuses one noise pattern, H-19) both failed to
    // be. Costs nothing when off.
    if (cv_GatherFreezeAfterFrame->value >= 0
        && static_cast<int>(ctx.frameIndex) > cv_GatherFreezeAfterFrame->value) {
        return;
    }
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
            // Diagnostic (followup-12): `r.GI.Reflections.FreezeSeed` also freezes the world
            // gather's sampling seed, otherwise "freeze the seed" would only cover the deferred
            // reflections and the gather would keep re-sampling every frame.
            ssgiParams.frameIndex      = ctx.perFrame.timeParams.w >= 0.f
                                       ? static_cast<uint32_t>(ctx.perFrame.timeParams.w)
                                       : ctx.frameIndex;
            ssgiParams.worldGather = ctx.perFrame.sdfDiffuseSamples != 0;
            ssgiParams.resetHistory = !ctx.temporal.matrixValid;
            ssgiParams.sceneRevision = m_globalDistanceField.GetSceneRevision();
            ssgiParams.changedRegions = &m_globalDistanceField.GetChangedRegions();   // read at execution (block B.2)
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
    if (ctx.frameIndex % 120 == 0) {
        UZ_LOG(Renderer, Debug, "SDF GI: frame={}, active={}, probesReady={}, probeSrv={}",
               ctx.frameIndex, sdfGiActive, m_radianceCache.HasProducedResult(),
               m_radianceCache.GetProbeSHBufferSrv());
    }
    if (!sdfGiActive) return;

    // ── Pass: GDF Composite ──────────────────────────────────────────
    // Composite distance and average diffuse reflectance from resolved scene draws.
    rgBuilder.AddPass("GDF_Composite", [&](rg::RgPassBuilder& pb) {
        pb.SetExecutionCallback([this, &ctx](IRHICommandList* cmd, const rg::RgPassResources& /*res*/) {
            UZ_PROFILE_SCOPE("GDF::Composite");

            auto gdfPipeline = ctx.pipelines->GetGdfComposite();
            if (!gdfPipeline.IsValid()) return;

            // Update clipmap centers to track camera
            Vec3 camPos = ctx.perFrame.cameraPos.xyz();
            m_globalDistanceField.SetDiagnosticCentreControls(cv_GDFFreezeCentre->value >= 0.5f,
                                                             cv_GDFCentrePhase->value);
            m_globalDistanceField.Update(camPos);
            {
                // Recentre events, logged with the frame index so temporal analyses can
                // align lighting steps with them (phase0 flicker sampling, 2026-09-09).
                // previousCenter = the clipmap centre seen on the previous frame (tracked every frame,
                // so the first logged event after loading reports the real previous centre).
                static Vec3 prevCenter[GlobalDistanceField::kNumClipmaps];
                static bool prevCenterValid = false;
                uint32_t dirtyMask = 0;
                for (uint32_t i = 0; i < GlobalDistanceField::kNumClipmaps; ++i)
                    if (m_globalDistanceField.GetClipmap(i).dirty) dirtyMask |= 1u << i;
                if (dirtyMask != 0 && m_globalDistanceField.GetSceneRevision() != 0) {
                    for (uint32_t i = 0; i < GlobalDistanceField::kNumClipmaps; ++i) {
                        if (!(dirtyMask & (1u << i))) continue;
                        const auto& c = m_globalDistanceField.GetClipmap(i);
                        const Vec3 pc = prevCenterValid ? prevCenter[i] : c.center;
                        const bool moved = pc.x != c.center.x || pc.y != c.center.y || pc.z != c.center.z;
                        UZ_LOG(Renderer, Info, "GI recentre: frame={} gdfClipmap={} reason={} previousCenter=({},{},{}) newCenter=({},{},{}) camera=({},{},{})",
                               ctx.frameIndex, i, moved ? "recentre" : (prevCenterValid ? "revoxelise" : "initial"),
                               pc.x, pc.y, pc.z, c.center.x, c.center.y, c.center.z, camPos.x, camPos.y, camPos.z);
                    }
                }
                for (uint32_t i = 0; i < GlobalDistanceField::kNumClipmaps; ++i) prevCenter[i] = m_globalDistanceField.GetClipmap(i).center;
                prevCenterValid = true;
            }

            // The draw list contains the full scene before frustum culling and
            // resolves streamed material textures and instance parameters.
            std::vector<MeshSDF> meshSDFs;
            std::vector<Mat4> meshTransforms;

            for (const auto& draw : ctx.drawList.GetCommands()) {
                if (!IsGIOccluder(draw) || draw.meshId >= m_meshes.size()) continue;
                const GPUMesh& gpuMesh = m_meshes[draw.meshId];
                if (!gpuMesh.HasSDF()) continue;

                MeshSDF sdfCopy = gpuMesh.sdf;
                sdfCopy.albedoSrv = draw.albedoSrvIndex;
                sdfCopy.metallicSrv = (draw.materialFlags & 0x4u) != 0
                    ? draw.aoSrvIndex : draw.metallicSrvIndex;
                sdfCopy.baseColorTint[0] = draw.baseColorTintR;
                sdfCopy.baseColorTint[1] = draw.baseColorTintG;
                sdfCopy.baseColorTint[2] = draw.baseColorTintB;
                sdfCopy.metallicMultiplier = draw.metallicMultiplier;
                sdfCopy.materialFlags = draw.materialFlags;
                sdfCopy.instanceKey   = draw.entityId;
                Mat4 worldMat = draw.worldMatrix;
                Mat4 inv = worldMat.Inverse();
                sdfCopy.invTransform[0]  = inv.m._11;
                sdfCopy.invTransform[1]  = inv.m._21;
                sdfCopy.invTransform[2]  = inv.m._31;
                sdfCopy.invTransform[3]  = inv.m._41;
                sdfCopy.invTransform[4]  = inv.m._12;
                sdfCopy.invTransform[5]  = inv.m._22;
                sdfCopy.invTransform[6]  = inv.m._32;
                sdfCopy.invTransform[7]  = inv.m._42;
                sdfCopy.invTransform[8]  = inv.m._13;
                sdfCopy.invTransform[9]  = inv.m._23;
                sdfCopy.invTransform[10] = inv.m._33;
                sdfCopy.invTransform[11] = inv.m._43;

                meshSDFs.push_back(sdfCopy);
                meshTransforms.push_back(worldMat);
            }

            const auto sdfCount = static_cast<uint32_t>(meshSDFs.size());
            const uint64_t previousRevision = m_globalDistanceField.GetSceneRevision();
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
            if (m_globalDistanceField.GetSceneRevision() != previousRevision) {
                m_radianceCache.InvalidateHistory();      // whole windows rewritten: the history is gone
            } else if (!m_globalDistanceField.GetChangedRegions().empty()) {
                // Only the neighbourhood of changed instances was recomposed (block B.2): retrace
                // every probe but keep the history, as a recentre does.
                const auto& regions = m_globalDistanceField.GetChangedRegions();
                std::vector<RadianceCache::FieldRegion> boxes;
                boxes.reserve(regions.size());
                for (const auto& r : regions) boxes.push_back({r.lo, r.hi});
                if (cv_GdfRegionalRetrace->value != 0.0f) {
                    m_radianceCache.RetraceKeepingHistory(boxes.data(), boxes.size());
                } else {
                    m_radianceCache.InvalidateHistory();
                }
                UZ_LOG(Renderer, Info, "GDF regional recompose: frame={} regions={} first=({},{},{})-({},{},{})",
                       ctx.frameIndex, regions.size(), regions[0].lo.x, regions[0].lo.y, regions[0].lo.z,
                       regions[0].hi.x, regions[0].hi.y, regions[0].hi.z);
            }
        });
    });

    // ── Pass: Radiance Cache Trace (SDF) and DDGI Probe Update (DXR) ───
    // The SDF path always runs (writes to the L2 SH probe buffer sampled
    // by the existing `_rcEnabled` block in deferred lighting). When
    // `r.RayTracing.GI` is enabled and a TLAS is ready, the DDGI atlas
    // writer also runs and updates the octahedral irradiance + depth
    // atlases used by the Chebyshev sampler in deferred lighting.
    // @ref Majercik 2019 "DDGI" — atlases + Chebyshev visibility
    const bool visibleSeeds = RadianceCache::ClassificationMode() == 3
        && CVarRegistry::Get().GetBool("r.GI.RadianceCache.ReachVisibleSeeds", false)
        && !m_meshes.empty();
    rgBuilder.AddPass("RadianceCache_Trace", [&](rg::RgPassBuilder& pb) {
        // Mode 3's visible-surface seeds read the G-buffer world position (r.GI.RadianceCache.ReachVisibleSeeds).
        // Do not bind stale/unwritten positions when the G-buffer pass has no geometry.
        if (visibleSeeds) pb.Read(ctx.positionRes, RHIResourceState::ShaderResource);
        pb.SetExecutionCallback([this, &ctx, visibleSeeds](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("RadianceCache::Trace");

            // Always run the SDF SH path so the existing deferred lighting
            // SH sample stays populated regardless of DXR availability.
            auto rcPipeline = ctx.pipelines->GetRadianceCacheTrace();
            if (ctx.frameIndex % 120 == 0) {
                UZ_LOG(Renderer, Debug, "SDF GI trace: frame={}, pipelineValid={}",
                       ctx.frameIndex, rcPipeline.IsValid());
            }
            if (rcPipeline.IsValid()) {
                Vec3 camPos = ctx.perFrame.cameraPos.xyz();
                m_radianceCache.Update(camPos);
                if (m_radianceCache.IsDirty() && m_radianceCache.HasProducedResult()) {
                    const Vec3 o = m_radianceCache.GetGridOrigin();
                    const Vec3 c = m_radianceCache.GetCompletedGridOrigin();
                    const int32_t* sh = m_radianceCache.GetPendingHistoryShift();
                    const bool recentred = sh[0] != 0 || sh[1] != 0 || sh[2] != 0;
                    UZ_LOG(Renderer, Info, "GI recentre: frame={} radianceCache reason={} shiftProbes=({},{},{}) previousOrigin=({},{},{}) newOrigin=({},{},{})",
                           ctx.frameIndex, recentred ? (m_radianceCache.IsHistoryInvalid() ? "recentre+invalidate" : "recentre")
                                                     : (m_radianceCache.IsHistoryInvalid() ? "invalidate" : "retrace"),
                           sh[0], sh[1], sh[2], c.x, c.y, c.z, o.x, o.y, o.z);
                }
                m_radianceCache.SetFrozen(cv_RCFreezeAfter->value >= 0.f
                                          && ctx.frameIndex >= static_cast<uint32_t>(cv_RCFreezeAfter->value));
                // The probe side classifier is only built when the candidate classification is on
                // (r.GI.RadianceCache.ProbeClassification, followup-01); the current rule needs nothing.
                // Mode 3 (reachability from the camera) replaces the classifier with its own passes.
                const uint32_t classificationMode = RadianceCache::ClassificationMode();
                const RHIPipelineHandle sidePipeline = classificationMode == 3 ? ctx.pipelines->GetProbeReach()
                    : classificationMode != 0 ? ctx.pipelines->GetProbeSide() : RHIPipelineHandle::Invalid();
                const uint32_t positionSrv = visibleSeeds
                    ? m_device->GetTextureSRVIndex(res.GetTexture(ctx.positionRes)) : UINT32_MAX;
                m_radianceCache.SetVisibleSeedSource(positionSrv,
                    visibleSeeds ? ctx.renderWidth : 0u, visibleSeeds ? ctx.renderHeight : 0u);
                m_radianceCache.UpdateProbes(
                    cmd, m_globalDistanceField, rcPipeline,
                    m_cbRing.get(),
                    ctx.perFrameAlloc.bufferHandle,
                    ctx.perFrameAlloc.offsetBytes,
                    ctx.perFrameAlloc.cpuPtr,
                    sizeof(PerFrameCB),
                    ctx.frameIndex,
                    sidePipeline);
            }

            // DDGI atlas writer — only when DXR is ON (master switch `r.RayTracing`
            // AND `r.RayTracing.GI`), the TLAS actually holds instances and the
            // system is initialised. `IsReady()` alone was not enough: with the
            // master switch off RTManager::BeginFrame never rebuilds the TLAS, so
            // the writer ran against an EMPTY TLAS — every RayQuery missed and the
            // probes were filled with pure sky (the "brighter DXR path" of
            // followup-01; caught by scripts/rendering/test_gi_energy_gpu.ps1, skyFraction 1.0).
            const bool rtGiOn = m_rtManager && m_rtManager->IsGIEnabled();
            const uint32_t tlasInstances = m_rtManager ? m_rtManager->GetTLASInstanceCount() : 0u;
            const bool wantDdgi = rtGiOn && tlasInstances > 0u && m_ddgiSystem.IsInitialized();
            const uz::CVar* rayTracingGI = RayTracingGICVar();
            if (rayTracingGI && rayTracingGI->value && !wantDdgi && !m_ddgiSkipLogged) {
                m_ddgiSkipLogged = true;
                UZ_LOG(Renderer, Warning,
                       "DDGI: r.RayTracing.GI=1 but the writer is skipped (r.RayTracing master switch on={}, TLAS instances={}, ddgi initialised={}); deferred lighting stays on the SDF radiance cache",
                       m_rtManager && m_rtManager->IsEnabled(), tlasInstances, m_ddgiSystem.IsInitialized());
            }
            if (wantDdgi) {
                auto ddgiPipeline   = ctx.pipelines->GetDdgiProbeUpdate();
                auto tracePipeline  = ctx.pipelines->GetDdgiProbeTrace();   // block B.6; invalid = per-texel trace
                auto borderPipeline = ctx.pipelines->GetDdgiBorderFill();
                if (ddgiPipeline.IsValid() && borderPipeline.IsValid()) {
                    const uint32_t tlasSrv = m_rtManager->GetTLASSRVIndex();

                    // VFX emissive feed — `GatherAndUploadVfxEmissiveSamples`
                    // runs earlier in the frame (after the simulation tick)
                    // and stages the top-N most-emissive samples into the
                    // bindless StructuredBuffer. When the `r.VFX.DDGIInjection`
                    // CVar is off the gather path calls `Reset()` which forces
                    // `GetSampleCount() == 0`, so passing the values verbatim
                    // here naturally disables the in-shader accumulation
                    // without an extra branch on the C++ side.
                    const uint32_t vfxSrv =
                        m_vfxDDGIEmissiveFeed.IsInitialized()
                            ? m_vfxDDGIEmissiveFeed.GetBufferSrvIndex()
                            : UINT32_MAX;
                    const uint32_t vfxCount =
                        m_vfxDDGIEmissiveFeed.IsInitialized()
                            ? m_vfxDDGIEmissiveFeed.GetSampleCount()
                            : 0u;

                    m_ddgiSystem.DispatchUpdate(
                        cmd, ddgiPipeline, tracePipeline, borderPipeline, tlasSrv,
                        m_cbRing.get(), ctx.frameIndex,
                        &m_globalDistanceField, ctx.perFrameAlloc.cpuPtr,
                        vfxSrv, vfxCount);
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
    if (!CVarRegistry::Get().GetBool("r.BrixelizerGI", false)) return;
    if (!m_brixelizerGI || !m_brixelizerGI->IsInitialized()) return;
    if (!m_passInspector.IsPassEnabled("brixelizer_gi")) return;
    // Diagnostic gate — file-scope `cv_BrixDispatch` lives in
    // `brixelizer_gi_pass.cpp`. Reach the same value via the global CVar
    // registry so a CLI `--cvar r.BrixelizerGI.Dispatch=0` override is
    // honoured here even though the C++ pointer lives in another TU.
    if (!uz::CVarRegistry::Get().GetBool("r.BrixelizerGI.Dispatch", true)) return;

    ctx.brixDiffuseRes = rgBuilder.ImportTrackedTexture("Brixelizer_Diffuse", m_brixelizerGI->GetDiffuseGITexture());
    ctx.brixSpecularRes = rgBuilder.ImportTrackedTexture("Brixelizer_Specular", m_brixelizerGI->GetSpecularGITexture());
    rgBuilder.AddPass("BrixelizerGI", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.velocityRes, RHIResourceState::ShaderResource);
        pb.Write(ctx.brixDiffuseRes, RHIResourceState::UnorderedAccess);
        pb.Write(ctx.brixSpecularRes, RHIResourceState::UnorderedAccess);
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
            const auto& camera = m_cameraManager->GetActiveCamera();
            m_brixelizerGI->Execute(
                cmd,
                camera.GetView().data(),
                camera.GetProjection().data(),
                camera.GetView().data(),
                camera.GetProjection().data(),
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
                m_rgStates->Transition(cmd, m_ssrRayCounterArgs, RHIResourceState::UnorderedAccess);
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
                m_rgStates->UavBarrier(cmd, m_ssrRayCounterArgs);
            }
        });
    });

    // ── SSR Prepare Indirect Args ───────────────────────────────────────
    rgBuilder.AddPass("SSR_PrepareIndirect", [&](rg::RgPassBuilder& pb) {
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);   // block B.5: keeps it after the tile classify
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
                m_rgStates->Transition(cmd, m_ssrRayCounterArgs, RHIResourceState::IndirectArgument);
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
                m_rgStates->Transition(cmd, m_ssrTraceOutput, RHIResourceState::UnorderedAccess);
                m_rgStates->Transition(cmd, m_ssrHitDistance, RHIResourceState::UnorderedAccess);
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
            // The previous scene colour is linear HDR, not pre-exposed.
            rc.exposureCorrection = 1.0f;
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
                m_rgStates->Transition(cmd, m_ssrTraceOutput, RHIResourceState::ShaderResource);
                m_rgStates->Transition(cmd, m_ssrHitDistance, RHIResourceState::ShaderResource);
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
                m_rgStates->Transition(cmd, m_ssrResolvedOutput, RHIResourceState::UnorderedAccess);
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
                m_rgStates->Transition(cmd, m_ssrResolvedOutput, RHIResourceState::ShaderResource);
            }
        });
    });

    // Stage 2 denoise route (`r.RayTracing.Reflections.Denoise`): the ray texels are written into the
    // resolved buffer here, before the temporal pass reads it as this frame's input, so they are
    // accumulated and filtered with the screen-trace texels. Registered only when that route is on.
    RegisterRTReflectionPass(rgBuilder, ctx, /*beforeDenoise=*/true);

    // SSR Temporal accumulation — virtual hit reprojection + YCoCg variance clip.
    // @ref AMD FidelityFX SSSR — temporal pass uses hit distance for virtual reprojection.
    {
        const uint32_t ssrWriteIdx = m_ssrHistoryIndex;
        const uint32_t ssrReadIdx  = 1 - m_ssrHistoryIndex;
        const bool ssrFirstFrame = !ctx.temporal.matrixValid
                                 || !m_ssrHistory[ssrReadIdx].IsValid();

        rgBuilder.AddPass("SSR_Temporal", [&, ssrWriteIdx, ssrReadIdx, ssrFirstFrame](rg::RgPassBuilder& pb) {
            // Block B.5: the depth read was undeclared, so the graph (which orders passes only through
            // the resources they declare) scheduled this pass right after the velocity pass, before the
            // trace and the resolve; it read last frame's resolved texture, the denoise then read its
            // stale history and wrote zeros that it read back the next frame: no reflection ever reached
            // the lighting while the denoise was on.
            pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
            pb.Read(ctx.velocityRes, RHIResourceState::ShaderResource);

            pb.SetExecutionCallback([this, &ctx, ssrWriteIdx, ssrReadIdx, ssrFirstFrame](IRHICommandList* cmd, const rg::RgPassResources&) {
                UZ_PROFILE_SCOPE("SSR::Temporal");

                auto ssrTemporalPSO = ctx.pipelines->GetSsrTemporal();
                if (!ssrTemporalPSO.IsValid() || cv_SSRTemporal->value < 0.5f) return;

                {
                    m_rgStates->Transition(cmd, m_ssrHistory[ssrWriteIdx], RHIResourceState::UnorderedAccess);
                    m_rgStates->Transition(cmd, m_ssrHistory[ssrReadIdx], RHIResourceState::ShaderResource);
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
                    pvp->prevViewProj = ctx.temporal.prevViewProj;
                    cmd->SetRootCBV(kSlotPerObject, prevVPAlloc.bufferHandle,
                                    prevVPAlloc.offsetBytes, prevVPAlloc.cpuPtr,
                                    sizeof(SsrTemporalPrevVP));
                }

                cmd->SetDescriptorTable(4, m_ssrHistoryUav[ssrWriteIdx]);

                const uint32_t groupsX = (m_renderWidth + 7) / 8;
                const uint32_t groupsY = (m_renderHeight + 7) / 8;
                cmd->Dispatch(groupsX, groupsY, 1);

                {
                    m_rgStates->Transition(cmd, m_ssrHistory[ssrWriteIdx], RHIResourceState::ShaderResource);
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
                    m_rgStates->Transition(cmd, m_ssrResolvedOutput, RHIResourceState::UnorderedAccess);
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
                    m_rgStates->Transition(cmd, m_ssrResolvedOutput, RHIResourceState::ShaderResource);
                }
            });
        });
    }
}

// ── Hardware Ray-Traced Reflections (stage 1) ───────────────────────────────────
// After the SSR chain: one mirror ray per pixel whose screen trace found no confident hit this
// frame (the ray left the screen or found nothing), inline RayQuery against the TLAS, the hit
// shaded from the global distance field + sun + shadow ray as the DDGI probe writer shades its
// rays (ddgi_probe_common.hlsli::TraceProbeRay). The result replaces that texel of the resolved
// SSR buffer, which the deferred lighting already blends before the BRDF, so the ladder is
// SSR (on screen) -> ray (off screen) -> the lighting's prefiltered probe / sky (texel untouched
// on a miss or a hit the field does not cover). Stage 1 = mirror / low roughness only (the
// r.SSR.MaxRoughness cutoff); glossy GGX-VNDF rays, the radiance-cache indirect term at the hit
// and a denoiser are stage 2. @ref docs/specs/rendering/rt-reflections.md

void SceneRenderer::RegisterRTReflectionPass(rg::RgBuilder& rgBuilder, FrameContext& ctx, bool beforeDenoise) {
    // Opt-in (`r.RayTracing.Reflections`, default off) on top of the `r.RayTracing` master
    // switch, with a built, non-empty TLAS: with the master switch off RTManager::BeginFrame never
    // rebuilds it and every ray would miss (the DDGI writer's lesson, followup-08). Off = the pass
    // is not registered at all: nothing is compiled, bound or dispatched and the frame is the
    // frame without it.
    if (!m_rtManager || !m_rtManager->IsReflectionsEnabled() || !m_rtManager->IsReady()
        || m_rtManager->GetTLASInstanceCount() == 0u) return;
    // Placement (stage 2): called once from RegisterSSRPasses after SSR_Resolve and once from
    // RegisterLightingPasses after the SSR chain; the denoise CVar selects which call registers.
    // The render graph chains every access to a resource in registration order (depth is read by
    // every SSR pass), so the registration point is the execution point.
    const bool denoise = cv_RTReflectionsDenoise->value;
    if (denoise != beforeDenoise) return;
    // The same gate as RegisterSSRPasses: the pass fills the holes of this frame's screen trace
    // in the resolved buffer the lighting reads this frame (PerFrameCB::ssrSrvIndex).
    if (!m_showFlags.Get(ShowFlag::SSR) || !ctx.ssrActive || !m_ssrHasPrevColor) return;
    // Hit attributes: the per-mesh geometry + material table by TLAS InstanceID the path tracer
    // shades its bounce vertices with (refreshed here, as the path tracer does, only while the
    // pass is on), the global distance field for meshes without an entry.
    const uint32_t materialTableSrv = UpdateGIMaterialTable(ctx);

    rgBuilder.AddPass("RT_Reflections", [&, materialTableSrv](rg::RgPassBuilder& pb) {
        // The GBuffer reads order this pass after the GBuffer and, in registration order, after
        // SSR_SpatialDenoise (the last writer of the resolved buffer) and before the lighting.
        // The TLAS (built in RTManager::BeginFrame, before the graph executes) and the SSR
        // textures are not graph resources; the latter go through the state registry exactly
        // as the SSR passes do.
        pb.Read(ctx.depthRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.normalRes, RHIResourceState::ShaderResource);
        pb.Read(ctx.velocityRes, RHIResourceState::ShaderResource);

        pb.SetExecutionCallback([this, &ctx, materialTableSrv](IRHICommandList* cmd, const rg::RgPassResources&) {
            UZ_PROFILE_SCOPE("RTReflections::Trace");

            auto rtReflectionPSO = ctx.pipelines->GetRtReflection();
            if (!rtReflectionPSO.IsValid()) return;

            // The shared b2 trace constants, built at execution time like the path tracer's:
            // the PerFrameCB is populated by then, not at registration.
            const auto* perFrame = static_cast<const PerFrameCB*>(ctx.perFrameAlloc.cpuPtr);
            if (perFrame == nullptr) return;
            RadianceCacheTraceConstants traceConstants{};
            FillRTReflectionTraceConstants(traceConstants, m_globalDistanceField, *perFrame, ctx.frameIndex);
            // Stage 2 indirect term: the radiance cache's published SH buffer and the grid it was
            // traced with, under the gate the deferred lighting consumes it (frame_constants.cpp):
            // an allocated but never-written buffer is not irradiance.
            const bool indirect = cv_RTReflectionsIndirect->value && m_sdfGiEnabled
                               && m_radianceCache.HasProducedResult();
            if (indirect) {
                SetRTReflectionProbeGrid(traceConstants, m_radianceCache.GetCompletedGridOrigin(),
                                         m_radianceCache.GetProbeSpacing(), RadianceCache::kProbesPerAxis);
            }
            auto traceAlloc = m_cbRing->Allocate(sizeof(traceConstants));
            if (!traceAlloc.cpuPtr) return;
            std::memcpy(traceAlloc.cpuPtr, &traceConstants, sizeof(traceConstants));

            const bool denoiseRoute = cv_RTReflectionsDenoise->value;
            {
                m_rgStates->Transition(cmd, m_ssrTraceOutput, RHIResourceState::ShaderResource);
                m_rgStates->Transition(cmd, m_ssrResolvedOutput, RHIResourceState::UnorderedAccess);
                // The hit distance feeds the temporal pass's virtual-hit reprojection on the
                // denoise route; the trace pass left it in ShaderResource.
                if (denoiseRoute) m_rgStates->Transition(cmd, m_ssrHitDistance, RHIResourceState::UnorderedAccess);
            }

            cmd->SetPipeline(rtReflectionPSO);

            const float ssrMaxRoughness = std::max(0.05f, cv_SSRMaxRoughness->value);
            // Stage 2: the ray-traced cutoff may exceed the screen trace's; unset = the SSR cutoff,
            // which keeps the stage 1 range and blend weights.
            const float rtMaxRoughness  = RTReflectionMaxRoughness(cv_RTReflectionsMaxRoughness->value,
                                                                   ssrMaxRoughness);

            RTReflectionRootConstants rc{};
            rc.depthSrvIdx        = m_gbuffer.depthSRV;
            rc.normalSrvIdx       = m_gbuffer.normalSRV;
            rc.ssrTraceSrvIdx     = m_ssrTraceOutputSrv;
            rc.outputUavIdx       = m_ssrResolvedOutputUav;
            rc.tlasSrvIdx         = m_rtManager->GetTLASSRVIndex();
            rc.outputWidth        = m_renderWidth;
            rc.outputHeight       = m_renderHeight;
            rc.maxRoughness       = rtMaxRoughness;
            rc.rayMaxDistance     = cv_RTReflectionsMaxDistance->value;
            rc.intensity          = cv_SSRIntensity->value;
            rc.roughnessMaskScale = -2.0f / rtMaxRoughness;
            rc.materialTableSrv   = materialTableSrv;
            // The sky an escaping ray returns: the same SRV every path-tracer sky read uses (HDRI
            // when active, else the atmosphere capture once filled, else the flat skylight).
            rc.skyCubemapSrv      = m_hdriActive ? m_hdriCubemapSrv
                                  : (m_skyCubemapFilled ? m_skyCubemapSrv : UINT32_MAX);
            rc.probeShSrv         = indirect ? m_radianceCache.GetProbeSHBufferSrv() : UINT32_MAX;
            rc.hitDistUavIdx      = denoiseRoute ? m_ssrHitDistanceUav : UINT32_MAX;
            rc.flags              = (cv_RTReflectionsGlossy->value ? kRTReflectionFlagGlossy : 0u)
                                  | (indirect ? kRTReflectionFlagIndirect : 0u)
                                  | (denoiseRoute ? kRTReflectionFlagHitDistance : 0u);

            cmd->SetRootConstants(kSlotRootConstants, reinterpret_cast<const uint32_t*>(&rc),
                                  kRTReflectionRootDwords);
            cmd->SetRootCBV(kSlotPerFrame, ctx.perFrameAlloc.bufferHandle,
                            ctx.perFrameAlloc.offsetBytes, ctx.perFrameAlloc.cpuPtr,
                            sizeof(PerFrameCB));
            cmd->SetRootCBV(kSlotPerObject, traceAlloc.bufferHandle, traceAlloc.offsetBytes,
                            traceAlloc.cpuPtr, sizeof(traceConstants));

            const uint32_t groupsX = (m_renderWidth + 7) / 8;
            const uint32_t groupsY = (m_renderHeight + 7) / 8;
            cmd->Dispatch(groupsX, groupsY, 1);

            // Back to SRV for the lighting (or, on the denoise route, the temporal pass) to read
            {
                m_rgStates->Transition(cmd, m_ssrResolvedOutput, RHIResourceState::ShaderResource);
                if (denoiseRoute) m_rgStates->Transition(cmd, m_ssrHitDistance, RHIResourceState::ShaderResource);
            }
        });
    });
}

// ── SSGI Previous Frame Color Copy ─────────────────────────────────────────
// Copy the current HDR scene color (after lighting + atmosphere) to the
// prev-frame buffer for SSGI to use next frame. This must happen AFTER
// all scene color contributions (lighting, atmosphere, fog) and BEFORE
// post-process modifies the HDR buffer (bloom, tonemap, etc.).

void SceneRenderer::RegisterSSGIPrevColorCopy(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    if (ctx.perFrame.sdfDiffuseSamples != 0) return;
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
