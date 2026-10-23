/// UzEngine Scene Renderer — Per-frame constant buffer population.
/// Fills PerFrameParams with camera matrices, IBL handles, atmosphere params,
/// SkyLight, SSGI, GI, reflection probes, shadows, and CVar-driven overrides.

#include "scene_renderer.h"
#include "region_dump_layout.h"

#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"
#include "raytracing/rt_manager.h"

#include "camera.h"
#include "post_process_volume.h"
#include "texture_loader.h"
#include "vfs.h"
#include "gpu_mesh.h"
#include "sh_projection.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"
#include "world.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "scene_update_flags.h"
#include "dirty_flags.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstring>

// ── CVars consumed by PerFrameCB population ─────────────────────────────
static auto* cv_SkyLightIntensity        = UZ_CVAR_FLOAT("r.SkyLight.Intensity", 1.0f, "IBL ambient multiplier", 0.f, 20.f);
static auto* cv_LightsClustered = UZ_CVAR_BOOL("r.Lights.Clustered", true,
    "Deferred lighting evaluates the lights of the pixel's cluster (32 px tiles x 24 depth slices, cluster_assign_cs) "
    "instead of every point and spot light of the frame (block B.7). 0 = the per-pixel loop, for an A/B on one binary.");
static auto* cv_LightsClusterSlots = UZ_CVAR_INT("r.Lights.ClusterSlots", 32,
    "Light slots per cluster (1..32). A cluster with more lights overflows and evaluates every light of the frame; "
    "a small value exercises that path.", 1, 32);
static auto* cv_LightsClusterSpotCone = UZ_CVAR_BOOL("r.Lights.ClusterSpotCone", true,
    "The light cull assigns a spot light to a cluster only when its cone reaches the cluster (a conservative cone "
    "against the cell's bounding sphere, after the range-sphere test); 0 = the range sphere alone, for an A/B.");
static auto* cv_LightsClusterDebug = UZ_CVAR_INT("r.Lights.ClusterDebug", 0,
    "1 = tint the frame by the pixel's cluster light count (blue 0, green 1-4, yellow 5-10, red 11+, magenta = overflow) "
    "and write (slice, count, first slot, tile) to the region dump's rc_support record; 2 = write the cluster's AABB "
    "(lo.x, lo.y, hi.x, hi.y) there; 3 = (lo.z, hi.z, count, first slot).", 0, 3);
static auto* cv_SkyLightMinAmbient       = UZ_CVAR_FLOAT("r.SkyLight.MinAmbient", 0.0f,
    "Floor raised over SkyLightComponent.minOcclusion for the AO clamp and the no-GI irradiance floor; "
    "0 = the component value (Sponza authors 0.01; the shader documents 0.005-0.02). The old 0.25 default "
    "overrode every scene (block A.2, 2026-10-05)", 0.f, 1.f);
static auto* cv_SkyLightIndirectIntensity = UZ_CVAR_FLOAT("r.SkyLight.IndirectIntensity", 1.0f, "Diffuse IBL multiplier (separate from specular)", 0.f, 10.f);
// Contact shadows ON by default — they are NOT GTAO. Pure screen-space
// ray-march from each pixel along the sun direction; produces hard contact
// shadows at object/floor intersections without the GTAO over-occlusion bug.
// 0.10 ≈ 10% of view depth — strong enough for column/floor contact in
// large interiors like Sponza without leaking onto distant geometry.
static auto* cv_ContactShadowLen   = UZ_CVAR_FLOAT("r.ContactShadow.Length", 0.01f, "Contact shadow ray length as a fraction of the far plane (0.01 = the length that was fixed in the shader until 2026-10-05); 0 disables", 0.f, 0.05f);
// Local (spot / point) shadow tiles, block A.1: receivers are pushed along the geometric normal by
// this fraction of their distance to the light before the comparison; the tiles are rendered with
// this near plane (cm). Both were literals in the shader / this file until 2026-10-05.
static auto* cv_LocalShadowNormalOffset = UZ_CVAR_FLOAT("r.Shadow.Local.NormalOffset", 0.005f, "Local light shadows: receiver offset along the geometric normal, as a fraction of the distance to the light", 0.f, 0.05f);
static auto* cv_LocalShadowNearPlane    = UZ_CVAR_FLOAT("r.Shadow.Local.NearPlane", 1.0f, "Local light shadow tiles: near plane in cm (spot tiles used 0.1 until 2026-10-05)", 0.01f, 50.f);
static auto* cv_FilmGrain          = UZ_CVAR_FLOAT("r.FilmGrain", 0.f, "Film grain intensity", 0.f, 1.f);
static auto* cv_GIFreezeSeed      = UZ_CVAR_INT("r.GI.Reflections.FreezeSeed", -1,
    "Diagnostic (followup-12): freezes the per-frame sampling seed of the SDF reflections and the\n"
    "world gather at this value. -1 (default) uses the frame count, i.e. production behaviour. "
    "Freezing the seed removes the stochastic component so a residual that survives is not noise.", -1, 65535);
static auto* cv_Vignette           = UZ_CVAR_FLOAT("r.Vignette", 0.0f, "Vignette intensity", 0.f, 2.f);
static auto* cv_ChromaAb           = UZ_CVAR_FLOAT("r.ChromaticAberration", 0.f, "Chromatic aberration", 0.f, 1.f);
static auto* cv_ColorSaturation   = UZ_CVAR_FLOAT("r.ColorSaturation", 1.1f, "Color saturation (1=neutral, >1=vivid)", 0.f, 3.f);
static auto* cv_ColorContrast     = UZ_CVAR_FLOAT("r.ColorContrast", 1.0f, "Color contrast power (1=neutral)", 0.1f, 3.f);
static auto* cv_TonemapSlope       = UZ_CVAR_FLOAT("r.Tonemap.Slope", 0.91f, "ACES film slope", 0.f, 2.f);
static auto* cv_TonemapToe         = UZ_CVAR_FLOAT("r.Tonemap.Toe", 0.45f, "ACES film toe", 0.f, 1.f);
static auto* cv_TonemapShoulder    = UZ_CVAR_FLOAT("r.Tonemap.Shoulder", 0.22f, "ACES film shoulder", 0.f, 1.f);
static auto* cv_TonemapBlackClip   = UZ_CVAR_FLOAT("r.Tonemap.BlackClip", 0.f, "ACES black clip", 0.f, 1.f);
static auto* cv_TonemapWhiteClip   = UZ_CVAR_FLOAT("r.Tonemap.WhiteClip", 0.04f, "ACES white clip", 0.f, 1.f);
static auto* cv_MipBias            = UZ_CVAR_FLOAT("r.MipBias", 0.f, "Global mipmap LOD bias", -4.f, 4.f);
static auto* cv_SdfReflections = UZ_CVAR_INT("r.GI.Reflections", 0, "Trace scene reflections through the global distance field", 0, 1);
static auto* cv_EnvMIS = UZ_CVAR_BOOL("r.EnvMIS", true,
    "Environment-MIS specular on smooth metals (replaces the split-sum term by its coverage; off = split-sum only). Inactive while r.GI.Reflections is on.");
static auto* cv_SdfFinalGather = UZ_CVAR_INT("r.GI.FinalGather", 0, "Gather diffuse illumination from surface hemisphere rays", 0, 1);
static auto* cv_SdfDiffuseSamples = UZ_CVAR_INT("r.GI.FinalGather.Samples", 8, "Cosine-weighted rays per diffuse surface gather", 1, 32);
static auto* cv_GIValidSampleReject = UZ_CVAR_BOOL("r.GI.ValidSampleReject", false,
    "Recover reliable receiver normals and exclude unavailable diffuse gather samples from reconstruction");
// Diagnostic candidate (ledger H-31): how the gather's sample set is rotated from frame to frame.
// 0 is the shipping path, whose rotation is locked to frame parity; 1 is the pre-registered
// Kronecker advance. Default 0 — this is evaluated, not adopted.
static auto* cv_SdfSampleRotation = UZ_CVAR_INT("r.GI.FinalGather.SampleRotation", 0,
    "Frame decorrelation of the gather's sample set: 0 radical inverse (shipping), 1 Kronecker "
    "(candidate), 2 independent per frame (reference generator only, maximises temporal noise)", 0, 2);
static auto* cv_SdfReflectionSamples = UZ_CVAR_INT("r.GI.Reflections.Samples", 4, "GGX rays per shaded pixel", 1, 16);
static auto* cv_SdfReflectionHdrWeight = UZ_CVAR_BOOL("r.GI.Reflections.HdrWeight", true,
    "Karis 1/(1+luma) weight on SDF reflection rays (firefly suppression, loses energy on bright samples); 0 = plain mean");
static auto* cv_SdfReflectionSteps = UZ_CVAR_INT("r.GI.Reflections.Steps", 96, "Maximum sphere tracing steps per reflection ray", 16, 256);
static auto* cv_SdfReflectionDistance = UZ_CVAR_FLOAT("r.GI.Reflections.Distance", 5000.f, "Reflection trace distance in world units", 100.f, 100000.f);

// ── Shader CVar parameters (Phase 1 — zero hardcoded values in shaders) ──
static auto* cv_ShadowPCFPenumbra     = UZ_CVAR_FLOAT("r.Shadow.PCFPenumbraRadius", 2.0f, "PCF texel radius", 0.5f, 8.0f);
static auto* cv_ShadowPCSSMin         = UZ_CVAR_FLOAT("r.Shadow.PCSSMinPenumbra", 0.5f, "PCSS min penumbra clamp", 0.1f, 4.0f);
static auto* cv_ShadowPCSSMax         = UZ_CVAR_FLOAT("r.Shadow.PCSSMaxPenumbra", 16.0f, "PCSS max penumbra clamp", 4.0f, 64.0f);
static auto* cv_ShadowESMExponent     = UZ_CVAR_FLOAT("r.Shadow.ESMExponentConstant", 80.0f, "ESM exponential warp constant", 10.0f, 200.0f);
static auto* cv_ShadowContactSteps    = UZ_CVAR_INT("r.Shadow.ContactShadowSteps", 16, "Contact shadow ray march steps", 4, 32);
static auto* cv_ShadowContactThick    = UZ_CVAR_FLOAT("r.Shadow.ContactShadowThickness", 0.005f, "Contact shadow depth thickness", 0.001f, 0.05f);
static auto* cv_BloomKnee             = UZ_CVAR_FLOAT("r.Bloom.Knee", 0.5f, "Bloom soft threshold knee", 0.0f, 1.0f);
static auto* cv_ExposureKeyValue      = UZ_CVAR_FLOAT("r.Exposure.KeyValue", 0.18f, "Reinhard mid-tone key value", 0.01f, 1.0f);
static auto* cv_ExposureSpeedUp       = UZ_CVAR_FLOAT("r.Exposure.SpeedUp", 3.0f, "Auto-exposure speed up (f-stops/sec)", 0.1f, 20.0f);
static auto* cv_ExposureSpeedDown     = UZ_CVAR_FLOAT("r.Exposure.SpeedDown", 1.0f, "Auto-exposure speed down (f-stops/sec)", 0.1f, 20.0f);
static auto* cv_ExposureLowPercent    = UZ_CVAR_FLOAT("r.Exposure.LowPercent", 10.0f, "Histogram low percentile", 1.0f, 50.0f);
static auto* cv_ExposureHighPercent   = UZ_CVAR_FLOAT("r.Exposure.HighPercent", 90.0f, "Histogram high percentile", 50.0f, 99.0f);
static auto* cv_MaterialRoughOverride = UZ_CVAR_FLOAT("r.Material.RoughnessOverride", -1.f,
    "Diagnostic: force every G-buffer pixel to this roughness (-1 = off). Lets a TAA change be measured on a material class the scene does not contain (followup-14 1.5).",
    -1.f, 1.f);
static auto* cv_MaterialMetalOverride = UZ_CVAR_FLOAT("r.Material.MetallicOverride", -1.f,
    "Diagnostic: force every G-buffer pixel to this metallic value (-1 = off).",
    -1.f, 1.f);
static auto* cv_MaterialNormalMap     = UZ_CVAR_BOOL("r.Material.NormalMap", true,
    "Diagnostic: 0 = shade opaque surfaces with the vertex normal, ignoring normal maps (G-Buffer pass)");
static auto* cv_MaterialAOMap         = UZ_CVAR_BOOL("r.Material.AOMap", true,
    "Diagnostic: 0 = write material AO = 1 to the G-Buffer, ignoring AO/ORM textures and graph AO");
static auto* cv_MaterialDielectricF0  = UZ_CVAR_FLOAT("r.Material.DielectricF0", 0.04f, "Dielectric Fresnel F0 reflectance", 0.01f, 0.08f);
static auto* cv_MaterialMinGGXAlpha   = UZ_CVAR_FLOAT("r.Material.MinGGXAlpha", 0.0016f, "Min GGX alpha = min roughness squared (0.0016 = the 0.04 roughness floor the shader had hardcoded until 2026-10-05)", 0.0001f, 0.01f);
/// Manual exposure multiplier. Applied on top of auto-exposure when active,
/// or used as the sole exposure when auto-exposure is inactive.
/// @ref UE5 default: pow(2, AutoExposureBias=1.0) = 2.0. For UzEngine's
/// light calibration, 10.0 matches typical indoor scene brightness.
static auto* cv_Exposure           = UZ_CVAR_FLOAT("r.Exposure", 1.0f, "Manual exposure multiplier (applied on auto-exposure)", 0.01f, 100.f);
static auto* cv_GIIntensity        = UZ_CVAR_FLOAT("r.GI.Intensity", 1.f, "GI intensity multiplier", 0.f, 10.f);
static auto* cv_AerialPersp        = UZ_CVAR_BOOL("r.AerialPerspective", false,
    "Aerial perspective in deferred lighting (single registration; atmosphere_renderer.cpp reads this one)");
// Default 1.0 = plain `ao`, matching the reference pipeline. Values above 1
// are an artistic crevice-darkening choice a scene opts into (the pre-port
// Sponza calibration used 3.0); they are not physically motivated.
static auto* cv_IndirectAOExp      = UZ_CVAR_FLOAT("r.IndirectAO.Exponent", 1.f, "AO exponent for indirect light (1 = linear AO; higher = more occlusion)", 1.f, 5.f);

namespace uz {

void SceneRenderer::PopulatePerFrameConstants(FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    // ── Reset per-frame CB skip counters ──────────────────────────────
    m_cbUpdatesSkipped = 0;
    m_cbUpdatesTotal   = 0;

    // ── Gather scene update flags from component dirty state ────────────
    ctx.updateFlags.Clear();
    // Camera always changes (timeParams increments every frame)
    ctx.updateFlags.Set(SceneUpdateFlags::kCamera);
    // Gather dirty flags from ECS components
    if (ctx.world) {
        // Lights
        auto lightView = ctx.world->GetRegistry().view<SceneComponent>();
        for (auto e : lightView) {
            auto& sc = lightView.get<SceneComponent>(e);
            if (sc.IsCBDirty()) {
                ctx.updateFlags.Set(SceneUpdateFlags::kTransforms);
                sc.ClearCBDirty();
            }
        }
        // Fog
        auto fogView = ctx.world->GetRegistry().view<HeightFogComponent>();
        for (auto e : fogView) {
            auto& fog = fogView.get<HeightFogComponent>(e);
            if (fog.IsCBDirty()) {
                ctx.updateFlags.Set(SceneUpdateFlags::kFog);
                fog.ClearCBDirty();
            }
        }
        // Clouds
        auto cloudView = ctx.world->GetRegistry().view<VolumetricCloudsComponent>();
        for (auto e : cloudView) {
            auto& cloud = cloudView.get<VolumetricCloudsComponent>(e);
            if (cloud.IsCBDirty()) {
                ctx.updateFlags.Set(SceneUpdateFlags::kClouds);
                cloud.ClearCBDirty();
            }
        }
    }

    auto& camera = m_cameraManager->GetActiveCamera();

    // ── Build PerFrameParams (scalar section byte-identical to PerFrameCB) ──
    auto& perFrame = ctx.perFrame;
    perFrame = PerFrameParams{};
    // Use the jittered viewProj for rendering (geometry pass, lighting).
    // The unjittered version is stored in m_prevViewProj for next frame's reprojection.
    perFrame.viewProj    = ctx.jitteredViewProj;
    perFrame.invViewProj = ctx.jitteredViewProj.Inverse();
    Vec3 camPos          = camera.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {m_totalTime, ctx.dt, static_cast<float>(ctx.frameIndex), 0.f};
    // Diagnostic: `r.GI.Reflections.FreezeSeed` rides in the unused w component so the sampling
    // seed can be frozen without changing the frame count every other pass depends on.
    perFrame.timeParams.w = static_cast<float>(cv_GIFreezeSeed->value);
    perFrame.nearPlane   = camera.GetNearPlane();
    perFrame.farPlane    = camera.GetFarPlane();

    // ── Region dump (diagnostics) ─────────────────────────────────────────
    // Buffer layout: rect 0 records, rect 0 footer (4 float4), rect 1 records, ... The rect
    // table (x, y, w, h | baseOffset, recordsPerPixel) lives in an upload buffer the shaders
    // read as a StructuredBuffer<uint4>, so a run can capture several ROIs at once.
    if (m_regionDumpRequested && !m_regionDumpActive && m_device && m_device->IsBindless()) {
        m_regionDumpRequested = false;
        // Counts come from kRegionGroupRecords in region_dump_layout.h — one table, used for the
        // total here and for the layout written into every file's header.
        m_regionRecordsPerPixel = RegionRecordsPerPixel(m_regionFields);
        constexpr uint32_t kFooterRecords = 4u;
        std::vector<uint32_t> rectTable;
        m_regionRectBase.clear();
        uint64_t records = 0;
        for (const auto& r : m_regionRects) {
            m_regionRectBase.push_back(static_cast<uint32_t>(records));
            rectTable.push_back(r.x); rectTable.push_back(r.y); rectTable.push_back(r.w); rectTable.push_back(r.h);
            rectTable.push_back(static_cast<uint32_t>(records)); rectTable.push_back(m_regionRecordsPerPixel);
            rectTable.push_back(0u); rectTable.push_back(0u);
            records += uint64_t(r.w) * r.h * m_regionRecordsPerPixel + kFooterRecords;
        }
        m_regionBufferBytes = records * 16ull;
        bool ok = m_regionRecordsPerPixel > 0 && !m_regionRects.empty();
        if (!ok) {
            UZ_LOG(Renderer, Error, "RegionDump: nothing to capture (rects={} fields={})",
                   m_regionRects.size(), m_regionFields);
        }
        if (ok) {
            RHIBufferDesc bd{};
            bd.sizeBytes = m_regionBufferBytes; bd.stride = 16;
            bd.usage = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::UnorderedAccess | RHIBufferUsage::CopySrc;
            bd.debugName = "RegionDump";
            m_regionDumpBuffer = m_device->CreateBuffer(bd);
            ok = m_regionDumpBuffer.IsValid();
        }
        if (ok) {
            RHIBufferDesc rd{};
            rd.sizeBytes = rectTable.size() * sizeof(uint32_t); rd.stride = 16;
            rd.usage = RHIBufferUsage::StructuredBuffer;
            rd.memory = RHIMemoryType::Upload; rd.debugName = "RegionDumpRects";
            m_regionRectBuffer = m_device->CreateBuffer(rd);
            ok = m_regionRectBuffer.IsValid();
            if (ok) {
                if (void* dst = m_device->MapBuffer(m_regionRectBuffer)) {
                    std::memcpy(dst, rectTable.data(), rectTable.size() * sizeof(uint32_t));
                    m_device->UnmapBuffer(m_regionRectBuffer);
                } else {
                    ok = false;
                }
            }
        }
        for (int i = 0; ok && i < 3; ++i) {
            RHIBufferDesc rb{}; rb.sizeBytes = m_regionBufferBytes;
            rb.memory = RHIMemoryType::Readback; rb.debugName = "RegionDumpReadback";
            m_regionReadback[i] = m_device->CreateBuffer(rb); ok = m_regionReadback[i].IsValid();
        }
        if (ok) {
            m_regionDumpUav = m_device->GetBufferUAVIndex(m_regionDumpBuffer);
            m_regionRectSrv = m_device->GetBufferSRVIndex(m_regionRectBuffer);
            ok = m_regionDumpUav != UINT32_MAX && m_regionRectSrv != UINT32_MAX;
        }
        if (ok) {
            m_regionDumpActive = true;
            std::string rectList;
            for (size_t i = 0; i < m_regionRects.size(); ++i) {
                const auto& r = m_regionRects[i];
                rectList += (i ? ", " : "") + r.name + "=(" + std::to_string(r.x) + "," + std::to_string(r.y)
                          + "," + std::to_string(r.w) + "x" + std::to_string(r.h) + ")";
            }
            UZ_LOG(Renderer, Info,
                   "RegionDump: active rects=[{}] fields={} records/px={} bytes={} frames {}..{} -> {}",
                   rectList, m_regionFields, m_regionRecordsPerPixel, m_regionBufferBytes,
                   m_regionFirstFrame, m_regionLastFrame, m_regionDumpDir);
        } else {
            UZ_LOG(Renderer, Error, "RegionDump: resource creation failed");
        }
    }
    if (m_regionDumpActive && ctx.frameIndex >= m_regionFirstFrame && ctx.frameIndex <= m_regionLastFrame) {
        perFrame.debugRegionUav = m_regionDumpUav;
        perFrame.debugRegionSrv = m_regionRectSrv;
        perFrame.debugRegionCount = static_cast<uint32_t>(m_regionRects.size());
        perFrame.debugRegionStamp = ctx.frameIndex;
        perFrame.debugRegionFields = m_regionFields;
    }

    perFrame.componentReplaySrv = m_componentReplaySrv;
    perFrame.materialRoughnessOverride = cv_MaterialRoughOverride->value;
    perFrame.materialMetallicOverride  = cv_MaterialMetalOverride->value;
    perFrame.materialMapMask = (cv_MaterialNormalMap->value != 0.0f ? 0u : 1u)
                             | (cv_MaterialAOMap->value     != 0.0f ? 0u : 2u);

    // IBL SRV indices are set AFTER the SkyLight processing loop below,
    // because SetPMREMEnvironment/SetHDRIEnvironment update m_iblResult
    // during that loop and we need the updated values in the PerFrameCB.
    perFrame.brdfLutSrvIndex     = m_brdfLutSrvIndex;
    // Low byte: the mip count. Bit 8: the prefiltered cubemap bound is a PMREM (its LODs follow the
    // Three.js roughness curve; the engine's own bake is linear in roughness) — split-sum item, 2026-10-08.
    perFrame.prefilteredMipLevels = IBLBaker::kPrefilteredMipLevels | (m_prefilteredIsPmrem ? 0x100u : 0u);


    // ── Environment Map Importance Sampling (MIS) ─────────────────────
    m_envMisEnabled = cv_EnvMIS->value;
    if (m_envMapSampler && !m_regionDumpDir.empty()) {
        // Diagnostics: the sampler dumps the environment it builds from beside the region dump.
        m_envMapSampler->SetDumpDirectory(m_regionDumpDir);
    }
    if (m_envMapSampler && m_envMapSampler->IsReady()) {
        perFrame.envSamplerAliasSrv       = m_envMapSampler->GetAliasSrv();
        perFrame.envSamplerLumMip0Srv     = m_envMapSampler->GetLuminanceMip0Srv();
        perFrame.envSamplerTotalLuminance = m_envMapSampler->GetTotalLuminance();
        perFrame.envSamplerTexelCount     = m_envMapSampler->GetTexelCount();
        perFrame.envSamplerLumWidth       = m_envMapSampler->GetLuminanceWidth();
        perFrame.envSamplerLumHeight      = m_envMapSampler->GetLuminanceHeight();
    }
    if (m_envMISPass && m_envMISPass->IsInitialized() && m_envMisEnabled) {
        perFrame.envMisAccumulatedSrv = m_envMISPass->GetAccumulatedSrv();
    }

    // ── Extract SkyLight from SkyLightComponent (standard) ──────────
    // Must be done BEFORE SH upload so intensity can be baked into the SH coefficients.
    // SkyLight extraction: intensity, color, minOcclusion from first SkyLightComponent.
    float skyIntensity = 1.0f;
    float skyIndirectIntensity = 1.0f;
    bool  lowerHemisphereIsBlack = true;
    Vec3  lowerHemisphereColor = {0.0f, 0.0f, 0.0f};
    if (ctx.world) {
        auto skyLightView = ctx.world->GetRegistry().view<SkyLightComponent>();
        for (auto enttEntity : skyLightView) {
            auto& sl = skyLightView.get<SkyLightComponent>(enttEntity);
            if (!sl.enabled) continue;
            skyIntensity = sl.intensity;
            skyIndirectIntensity = sl.indirectLightingIntensity;
            perFrame.skyLightIntensity  = sl.intensity;
            perFrame.skyLightColorR     = sl.lightColor.x;
            perFrame.skyLightColorG     = sl.lightColor.y;
            perFrame.skyLightColorB     = sl.lightColor.z;
            perFrame.skyLightMinAmbient = sl.minOcclusion;

            // ── SkyLight occlusion parameters ──
            perFrame.slOcclusionMaxDistance    = sl.occlusionMaxDistance;
            perFrame.slOcclusionExponent       = sl.occlusionExponent;
            perFrame.slOcclusionTintR          = sl.occlusionTint.x;
            perFrame.slOcclusionTintG          = sl.occlusionTint.y;
            perFrame.slOcclusionTintB          = sl.occlusionTint.z;
            perFrame.slCloudAmbientOcclusion   = sl.cloudAmbientOcclusion ? 1u : 0u;
            perFrame.slCloudAOStrength         = sl.cloudAmbientOcclusionStrength;
            perFrame.slCloudAOExtent           = sl.cloudAmbientOcclusionExtent;
            perFrame.slCloudAOMapResolutionScale = sl.cloudAmbientOcclusionMapResolutionScale;
            perFrame.slCloudAOApertureScale    = sl.cloudAmbientOcclusionApertureScale;
            perFrame.slSkyDistanceThreshold    = sl.skyDistanceThreshold;
            perFrame.slOcclusionContrast       = sl.occlusionContrast;

            lowerHemisphereIsBlack = sl.lowerHemisphereIsBlack;
            lowerHemisphereColor   = sl.lowerHemisphereColor;

            // ── Auto-load HDRI from cubemap_path (UE5 SpecifiedCubemap pattern) ──
            if (sl.sourceType == ESkyLightSourceType::SpecifiedCubemap
                && !sl.cubemapPath.empty()
                && !m_hdriActive) {
                IVFS* vfs = ctx.vfs;
                if (vfs) {
                    auto data = vfs->Read(sl.cubemapPath);
                    if (!data.empty()) {
                        // Detect format from extension
                        const char* fmt = "hdr";
                        const bool isKtx2 = sl.cubemapPath.size() > 5 &&
                            (sl.cubemapPath.find(".ktx2") != std::string::npos);
                        if (!isKtx2 && sl.cubemapPath.size() > 4 &&
                            sl.cubemapPath.substr(sl.cubemapPath.size() - 4) == ".exr") {
                            fmt = "exr";
                        }

                        TextureLoadResult hdrResult;
                        if (isKtx2) {
                            // KTX2 path — use basis_universal ktx2_transcoder
                            hdrResult = TextureLoader::LoadKTX2FromMemory(
                                m_device, data.data(), data.size(), sl.cubemapPath.c_str());
                        } else {
                            hdrResult = TextureLoader::LoadHDRFromMemory(
                                m_device, data.data(), data.size(), fmt, sl.cubemapPath.c_str());
                        }
                        if (hdrResult.handle.IsValid()) {
                            TrackExternalTexture(hdrResult);
                            if (hdrResult.isPMREM) {
                                // PMREM atlas: extract cubemap faces directly from the
                                // Three.js CubeUV atlas. Skips equirect→cubemap conversion
                                // and prefilter bake (PMREM is already prefiltered).
                                SetPMREMEnvironment(hdrResult);
                                UZ_LOG(Renderer, Info,
                                    "SkyLight: '{}' loaded as PMREM atlas ({}px face, {} LODs)",
                                    sl.cubemapPath, hdrResult.pmremFaceSize, hdrResult.pmremLodCount);
                            } else {
                                SetHDRIEnvironment(hdrResult.srvIndex, sl.cubemapResolution);
                            }
                            UZ_LOG(Renderer, Info,
                                "SkyLight: auto-loaded HDRI '{}' (SRV={}, res={}, PMREM={})",
                                sl.cubemapPath, hdrResult.srvIndex, sl.cubemapResolution,
                                hdrResult.isPMREM);
                        } else {
                            UZ_LOG(Renderer, Warning,
                                "SkyLight: failed to load HDRI '{}'", sl.cubemapPath);
                        }
                    }
                }
            }

            break;  // First SkyLightComponent only
        }
    }

    // ── IBL SRV indices (AFTER SkyLight loop — SetPMREMEnvironment may have updated m_iblResult) ──
    perFrame.irradianceSrvIndex  = m_iblResult.irradianceSrvIndex;
    perFrame.prefilteredSrvIndex = m_iblResult.prefilteredSrvIndex;


    // Apply CVar-driven skylight overrides on top of component values.
    // r.SkyLight.Intensity acts as a multiplier on the component intensity.
    // r.SkyLight.MinAmbient provides a floor that the component value is raised to.
    perFrame.skyLightIntensity *= cv_SkyLightIntensity->value;
    perFrame.skyLightMinAmbient = std::max(perFrame.skyLightMinAmbient, cv_SkyLightMinAmbient->value);
    // IndirectIntensity CVar overrides the component value for diffuse IBL.
    skyIndirectIntensity *= cv_SkyLightIndirectIntensity->value;

    // Upload SH coefficients for sky diffuse irradiance (standard).
    // The shader's deferred_lighting_ps.hlsl line 881 already multiplies
    // by skyLightIntensity, so we only scale SH by indirectLightingIntensity
    // here (which provides separate control of diffuse fill vs specular).
    // IndirectLightingIntensity scales SH separately from the main Intensity parameter.
    const float shScale = skyIndirectIntensity;
    for (int i = 0; i < 7; ++i) {
        perFrame.skyIrradianceSH[i] = {
            m_skyIrradianceSH.coeffs[i].x * shScale,
            m_skyIrradianceSH.coeffs[i].y * shScale,
            m_skyIrradianceSH.coeffs[i].z * shScale,
            m_skyIrradianceSH.coeffs[i].w * shScale
        };
    }

    // Lower hemisphere contribution: when lowerHemisphereIsBlack=false, a uniform radiance L below the
    // horizon. Its cosine-weighted irradiance over pi is exactly L * (1 - n.up) / 2 (a hemisphere light),
    // which the packed L0 + L1 terms carry as is: EvalSH_L2 returns dot(coeffs[c].xyz, n) + coeffs[c].w.
    // The packed SH lives in the cubemap's frame (+Y up, like sh_projection.cpp's face directions and
    // the normal the lighting converts with WorldToCubemap), so the slope goes into the y term.
    // Until the split-sum item (2026-10-08) the constant term carried Y00 / 2 of the colour (3.5 x too
    // little) and the slope was written into coeffs[3].xyz, the red channel's L2 band.
    // @ref Ramamoorthi 2001 "An Efficient Representation for Irradiance Environment Maps"
    if (!lowerHemisphereIsBlack) {
        const float halfWeight = 0.5f;
        perFrame.skyIrradianceSH[0].w += lowerHemisphereColor.x * halfWeight * shScale;
        perFrame.skyIrradianceSH[1].w += lowerHemisphereColor.y * halfWeight * shScale;
        perFrame.skyIrradianceSH[2].w += lowerHemisphereColor.z * halfWeight * shScale;
        perFrame.skyIrradianceSH[0].y -= lowerHemisphereColor.x * halfWeight * shScale;
        perFrame.skyIrradianceSH[1].y -= lowerHemisphereColor.y * halfWeight * shScale;
        perFrame.skyIrradianceSH[2].y -= lowerHemisphereColor.z * halfWeight * shScale;
    }

    // Log SH L0 coefficient once to diagnose shadow darkness.
    // In compact packed format: coeffs[0].w = R_L0, coeffs[1].w = G_L0, coeffs[2].w = B_L0
    static bool shLogged = false;
    if (!shLogged && ctx.frameIndex > 100) {
        UZ_LOG(Renderer, Info,
               "SkyLight SH L0: R={:.4f} G={:.4f} B={:.4f}, scale={:.2f}, "
               "scaled: R={:.4f} G={:.4f} B={:.4f}",
               m_skyIrradianceSH.coeffs[0].w,
               m_skyIrradianceSH.coeffs[1].w,
               m_skyIrradianceSH.coeffs[2].w,
               shScale,
               m_skyIrradianceSH.coeffs[0].w * shScale,
               m_skyIrradianceSH.coeffs[1].w * shScale,
               m_skyIrradianceSH.coeffs[2].w * shScale);
        shLogged = true;
    }

    // ── Extract lighting from ECS (directional + point lights) ─────────
    ctx.lightData = ExtractLightingFromECS(ctx.world, *m_cbRing, camera.GetPosition());
    ctx.sunDir       = ctx.lightData.sunDir;
    ctx.sunColor     = ctx.lightData.sunColor;
    ctx.ambientColor = ctx.lightData.ambientColor;
    ctx.lightingAlloc = ctx.lightData.lightingCBAlloc;

    // ── Inject VFX particle lights into the LightingCB ──────────────────
    // Emitters with particleLightEnabled inject a dynamic point light at their
    // world position, using the emitter's initial color and intensity.
    // Flicker modulation: intensity *= 1 + sin(totalTime * flickerSpeed * 2pi) * amplitude
    {
        VFXSystem* vfx = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
        if (vfx && vfx->IsInitialized() && ctx.lightingAlloc.cpuPtr) {
            auto* lightingCB = reinterpret_cast<LightingCB*>(ctx.lightingAlloc.cpuPtr);
            const auto& emitters = vfx->GetActiveEmitters();
            for (const auto* em : emitters) {
                if (!em || !em->initialized || !em->particleLightEnabled) continue;
                if (lightingCB->pointLightCount >= kMaxPointLights) break;

                float intensity = em->particleLightIntensity;
                // Flicker modulation
                if (em->particleLightFlicker > 0.0f) {
                    const float phase = m_totalTime * em->particleLightFlickerSpeed * 6.28318f;
                    intensity *= std::max(0.1f, 1.0f + std::sin(phase) * em->particleLightFlicker);
                }

                const uint32_t idx = lightingCB->pointLightCount;
                PointLightData& pld = lightingCB->pointLights[idx];
                pld.position    = em->worldPosition;
                pld.radius      = em->particleLightRadius;
                pld.color       = Vec3{em->initialColor.x, em->initialColor.y, em->initialColor.z} * intensity;
                pld.sourceRadius    = 0.0f;
                pld.falloffExponent = 0.0f;
                pld.specularScale   = 1.0f;
                pld.diffuseScale    = 1.0f;
                pld.flags           = kLightFlag_InverseSquared;
                pld.shadowIndex     = UINT32_MAX;
                lightingCB->pointLightCount++;
            }
        }
    }

    // ── Clustered light lists (block B.7): the cluster grid of this frame ─────────────
    // The list buffer's descriptors are filled by RegisterLightCullPass once the buffer exists.
    if (ctx.lightingAlloc.cpuPtr) {
        auto* lightingCB = reinterpret_cast<LightingCB*>(ctx.lightingAlloc.cpuPtr);
        const Vec3  fwd   = camera.GetForward().Normalized();
        // The slice range follows the lights: from the nearest point any light reaches to the
        // farthest, along the view direction (a pixel nearer than the first boundary falls into
        // slice 0, beyond the last into the last slice, and no light reaches either region).
        // Slicing the camera's whole [near, far] spent the slices near the camera and left the
        // cells at the lights' distance 37 % deep, so most clusters overflowed their slots.
        const Vec3  camPos  = camera.GetPosition();
        float lightNear = std::numeric_limits<float>::max();
        float lightFar  = 0.0f;
        for (uint32_t i = 0; i < lightingCB->pointLightCount; ++i) {
            const PointLightData& pl = lightingCB->pointLights[i];
            const float d = (pl.position - camPos).Dot(fwd);
            lightNear = std::min(lightNear, d - pl.radius);
            lightFar  = std::max(lightFar, d + pl.radius);
        }
        for (uint32_t i = 0; i < lightingCB->spotLightCount; ++i) {
            const SpotLightData& sl = lightingCB->spotLights[i];
            const float d = (sl.position - camPos).Dot(fwd);
            lightNear = std::min(lightNear, d - sl.radius);
            lightFar  = std::max(lightFar, d + sl.radius);
        }
        const float camNear = std::max(camera.GetNearPlane(), 1.0f);
        const float camFar  = std::max(camera.GetFarPlane(), camNear * 2.0f);
        const bool  anyLight = lightingCB->pointLightCount + lightingCB->spotLightCount > 0;
        const float nearZ = anyLight ? std::clamp(lightNear, camNear, camFar * 0.5f) : camNear;
        const float farZ  = anyLight ? std::clamp(lightFar, nearZ * 2.0f, camFar) : camFar;
        lightingCB->clusterForwardX  = fwd.x;
        lightingCB->clusterForwardY  = fwd.y;
        lightingCB->clusterForwardZ  = fwd.z;
        lightingCB->clusterNear      = nearZ;
        lightingCB->clusterFar       = farZ;
        lightingCB->clusterLogScale  = static_cast<float>(kClusterSlices) / std::log(farZ / nearZ);
        lightingCB->clusterTileSize  = kClusterTileSize;
        lightingCB->clusterTilesX    = (m_renderWidth + kClusterTileSize - 1) / kClusterTileSize;
        lightingCB->clusterTilesY    = (m_renderHeight + kClusterTileSize - 1) / kClusterTileSize;
        lightingCB->clusterSlices    = kClusterSlices;
        lightingCB->clusterSlots     = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_LightsClusterSlots->value), 1,
                                                                        static_cast<int>(kClusterSlotsMax)));
        const uint32_t debugMode = static_cast<uint32_t>(std::clamp(static_cast<int>(cv_LightsClusterDebug->value), 0, 3));
        lightingCB->clusterFlags     = (cv_LightsClustered->value != 0.0f ? kClusterFlag_Enabled : 0u)
                                     | (debugMode != 0u ? kClusterFlag_Debug : 0u) | (debugMode << 2)
                                     | (cv_LightsClusterSpotCone->value != 0.0f ? kClusterFlag_SpotCone : 0u);
        lightingCB->clusterListSrv   = UINT32_MAX;
        lightingCB->clusterListUav   = UINT32_MAX;
        lightingCB->clusterViewWidth  = static_cast<float>(m_renderWidth);
        lightingCB->clusterViewHeight = static_cast<float>(m_renderHeight);

        // Rect area lights (phase3a 3.33.0): this frame's lights go to the per-frame structured
        // buffer (created on first use) and the LTC tables' SRVs into the CB. Scenes without a rect
        // light leave count 0 and the sentinels, and create nothing.
        if (ctx.lightData.rectLightCount > 0 && m_ltcLut.IsReady()) {
            if (!m_rectLightBuffer.IsValid() && !m_rectLightBufferFailed) {
                m_rectLightBufferFailed = !CreateRectLightBuffer(m_device, m_rectLightBuffer);
            }
            const uint32_t srv = WriteRectLightBuffer(m_device, m_rectLightBuffer, ctx.frameIndex,
                                                      ctx.lightData.rectLights, ctx.lightData.rectLightCount);
            if (srv != UINT32_MAX) {
                lightingCB->rectLightCount  = ctx.lightData.rectLightCount;
                lightingCB->rectLightsSrv   = srv;
                lightingCB->ltcMatrixSrv    = m_ltcLut.matrixSrv;
                lightingCB->ltcMagnitudeSrv = m_ltcLut.magnitudeSrv;
            }
        }
    }

    // ── Extract cloud shadow & light shaft parameters from primary DirectionalLight ──
    if (ctx.world) {
        auto dirLightView = ctx.world->GetRegistry().view<DirectionalLightComponent>();
        for (auto enttEntity : dirLightView) {
            auto& dl = dirLightView.get<DirectionalLightComponent>(enttEntity);
            perFrame.cloudShadowStrength              = dl.cloudShadowStrength;
            perFrame.cloudShadowOnAtmosphereStrength  = dl.cloudShadowOnAtmosphereStrength;
            perFrame.cloudShadowOnSurfaceStrength     = dl.cloudShadowOnSurfaceStrength;
            perFrame.cloudShadowDepthBias             = dl.cloudShadowDepthBias;
            perFrame.cloudShadowExtent                = dl.cloudShadowExtent;
            perFrame.cloudShadowMapResolutionScale    = dl.cloudShadowMapResolutionScale;
            perFrame.cloudShadowRaySampleCountScale   = dl.cloudShadowRaySampleCountScale;
            perFrame.cloudScatteredLuminanceScaleR     = dl.cloudScatteredLuminanceScale.x;
            perFrame.cloudScatteredLuminanceScaleG     = dl.cloudScatteredLuminanceScale.y;
            perFrame.cloudScatteredLuminanceScaleB     = dl.cloudScatteredLuminanceScale.z;
            perFrame.castCloudShadows                 = dl.castCloudShadows ? 1u : 0u;
            perFrame.castShadowsOnClouds              = dl.castShadowsOnClouds ? 1u : 0u;
            perFrame.castShadowsOnAtmosphere          = dl.castShadowsOnAtmosphere ? 1u : 0u;
            perFrame.enableLightShaftOcclusion        = dl.enableLightShaftOcclusion ? 1u : 0u;
            perFrame.occlusionMaskDarkness            = dl.occlusionMaskDarkness;
            perFrame.occlusionDepthRange              = dl.occlusionDepthRange;
            perFrame.lightShaftOverrideDirectionX     = dl.lightShaftOverrideDirection.x;
            perFrame.lightShaftOverrideDirectionY     = dl.lightShaftOverrideDirection.y;
            perFrame.lightShaftOverrideDirectionZ     = dl.lightShaftOverrideDirection.z;
            perFrame.shadowSourceAngleFactor          = dl.shadowSourceAngleFactor;
            perFrame.shadowCascadeBiasDistribution    = dl.shadowCascadeBiasDistribution;
            break;  // First DirectionalLightComponent only
        }
    }

    perFrame.lightDir     = {ctx.sunDir.x, ctx.sunDir.y, ctx.sunDir.z, 0.f};
    perFrame.lightColor   = {ctx.sunColor.x, ctx.sunColor.y, ctx.sunColor.z, 0.f};

    // Ambient color from light extraction (directional light ambient term).
    // When IBL is available, the shader uses SH irradiance instead of this.
    perFrame.ambientColor = {ctx.ambientColor.x, ctx.ambientColor.y, ctx.ambientColor.z, 0.f};

    // ── Auto-Exposure ──────────────────────────────────────────────────
    // Run GPU histogram-based auto-exposure on the HDR scene color.
    // On the first few frames before HDR is filled, use a sensible default.
    if (m_autoExposure && m_hdrSceneColorSrv != UINT32_MAX && m_hdrHasContent) {
        float autoExp = m_autoExposure->Execute(ctx.cmdList, m_hdrSceneColorSrv,
            m_renderWidth, m_renderHeight, ctx.dt);
        // Apply manual exposure bias on top of auto-exposure
        perFrame.exposure = autoExp * cv_Exposure->value;
        m_lastFrameExposure = perFrame.exposure;  // run manifest / region dump header
        // Log only on first adaptation (avoid per-frame spam)
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Info, "AutoExposure: initial exposure={:.4f}", perFrame.exposure);
            loggedOnce = true;
        }
    } else {
        // Without auto-exposure, use the manual exposure CVar directly.
        // @ref UE5: manualExposure = pow(2, exposureBias), default 2.0.
        // UzEngine default r.Exposure = 10.0 for typical indoor scene brightness.
        perFrame.exposure = cv_Exposure->value;
        m_lastFrameExposure = perFrame.exposure;  // run manifest / region dump header
    }

    // ── Mip bias for screen percentage texture LOD compensation ────────
    // Mip bias compensates for screen-percentage rendering (lower res → over-minified textures).
    // When rendering at lower internal resolution, textures are over-minified
    // because the screen pixel density is lower than the display. A negative
    // mip bias shifts sampling toward finer mip levels to compensate.
    // Screen percentage compensation + CVar-driven manual mip bias.
    perFrame.mipBias = std::log2f(m_screenPercentage / 100.f) + cv_MipBias->value;

    // ── CVar-driven per-frame overrides ─────────────────────────────────
    // These are set by ApplyCVars() each frame and forwarded to the GPU
    // via PerFrameParams so shaders (tonemap, post-process) can read them.
    perFrame.toneCurveSlope      = cv_TonemapSlope->value;
    perFrame.toneCurveToe        = cv_TonemapToe->value;
    perFrame.toneCurveShoulder   = cv_TonemapShoulder->value;
    perFrame.toneCurveBlackClip  = cv_TonemapBlackClip->value;
    perFrame.toneCurveWhiteClip  = cv_TonemapWhiteClip->value;
    perFrame.filmGrainIntensity  = cv_FilmGrain->value;
    perFrame.vignetteIntensity   = cv_Vignette->value;
    perFrame.chromaAbIntensity   = cv_ChromaAb->value;
    perFrame.colorSaturation     = cv_ColorSaturation->value;
    perFrame.colorContrast       = cv_ColorContrast->value;
    perFrame.contactShadowLength = cv_ContactShadowLen->value;

    // ── Shader CVar parameters (Phase 1) ────────────────────────────────
    perFrame.shadowPCFPenumbra       = cv_ShadowPCFPenumbra->value;
    perFrame.shadowPCSSMinPenumbra   = cv_ShadowPCSSMin->value;
    perFrame.shadowPCSSMaxPenumbra   = cv_ShadowPCSSMax->value;
    perFrame.shadowESMExponent       = cv_ShadowESMExponent->value;
    perFrame.shadowContactSteps      = static_cast<uint32_t>(cv_ShadowContactSteps->value);
    perFrame.shadowContactThickness  = cv_ShadowContactThick->value;
    perFrame.bloomKnee               = cv_BloomKnee->value;
    perFrame.exposureKeyValue        = cv_ExposureKeyValue->value;
    perFrame.exposureSpeedUp         = cv_ExposureSpeedUp->value;
    perFrame.exposureSpeedDown       = cv_ExposureSpeedDown->value;
    perFrame.exposureLowPercent      = cv_ExposureLowPercent->value;
    perFrame.exposureHighPercent     = cv_ExposureHighPercent->value;
    perFrame.materialDielectricF0    = cv_MaterialDielectricF0->value;
    perFrame.materialMinGGXAlpha     = cv_MaterialMinGGXAlpha->value;

    // ── PostProcess Volume blending ─────────────────────────────────────
    // Gather all PostProcessVolumeComponents from the ECS, build transient
    // PostProcessVolume objects, compute the blended settings for the current
    // camera position, and apply overridden fields to the per-frame params.
    // This runs after CVar defaults so that volume overrides take precedence.
    if (ctx.world) {
        const Vec3 camWorldPos = camera.GetPosition();

        PostProcessVolumeManager ppvManager;
        auto ppvView = ctx.world->GetRegistry().view<PostProcessVolumeComponent>();
        for (auto enttEntity : ppvView) {
            const auto& ppvComp = ppvView.get<PostProcessVolumeComponent>(enttEntity);
            if (!ppvComp.enabled) continue;

            PostProcessVolume vol;
            vol.priority    = ppvComp.priority;
            vol.blendRadius = ppvComp.blendRadius;
            vol.blendWeight = ppvComp.blendWeight;
            vol.isUnbound   = ppvComp.isUnbound;
            vol.enabled     = true;

            // Compute world-space AABB from entity transform + local extents.
            if (!ppvComp.isUnbound) {
                Vec3 entityPos{};
                if (ctx.world->GetRegistry().all_of<SceneComponent>(enttEntity)) {
                    auto& scene = ctx.world->GetRegistry().get<SceneComponent>(enttEntity);
                    entityPos = scene.GetWorldPosition();
                }
                vol.boundsMin = {entityPos.x - ppvComp.extents.x,
                                 entityPos.y - ppvComp.extents.y,
                                 entityPos.z - ppvComp.extents.z};
                vol.boundsMax = {entityPos.x + ppvComp.extents.x,
                                 entityPos.y + ppvComp.extents.y,
                                 entityPos.z + ppvComp.extents.z};
            }

            // Convert per-field override bools into the bitmask + settings.
            auto& s = vol.settings;

            if (ppvComp.overrideBloomIntensity)                { s.SetOverride(PostProcessOverride::BloomIntensity);                s.bloomIntensity                = ppvComp.bloomIntensity; }
            if (ppvComp.overrideBloomThreshold)                { s.SetOverride(PostProcessOverride::BloomThreshold);                s.bloomThreshold                = ppvComp.bloomThreshold; }
            if (ppvComp.overrideAutoExposureMinBrightness)     { s.SetOverride(PostProcessOverride::AutoExposureMinBrightness);     s.autoExposureMinBrightness     = ppvComp.autoExposureMinBrightness; }
            if (ppvComp.overrideAutoExposureMaxBrightness)     { s.SetOverride(PostProcessOverride::AutoExposureMaxBrightness);     s.autoExposureMaxBrightness     = ppvComp.autoExposureMaxBrightness; }
            if (ppvComp.overrideAutoExposureBias)              { s.SetOverride(PostProcessOverride::AutoExposureBias);              s.autoExposureBias              = ppvComp.autoExposureBias; }
            if (ppvComp.overrideAutoExposureSpeedUp)           { s.SetOverride(PostProcessOverride::AutoExposureSpeedUp);           s.autoExposureSpeedUp           = ppvComp.autoExposureSpeedUp; }
            if (ppvComp.overrideAutoExposureSpeedDown)         { s.SetOverride(PostProcessOverride::AutoExposureSpeedDown);         s.autoExposureSpeedDown         = ppvComp.autoExposureSpeedDown; }
            if (ppvComp.overrideDofFocalDistance)               { s.SetOverride(PostProcessOverride::DepthOfFieldFocalDistance);     s.depthOfFieldFocalDistance      = ppvComp.dofFocalDistance; }
            if (ppvComp.overrideDofFocalRegion)                 { s.SetOverride(PostProcessOverride::DepthOfFieldFocalRegion);       s.depthOfFieldFocalRegion        = ppvComp.dofFocalRegion; }
            if (ppvComp.overrideDofNearTransition)              { s.SetOverride(PostProcessOverride::DepthOfFieldNearTransition);    s.depthOfFieldNearTransition     = ppvComp.dofNearTransition; }
            if (ppvComp.overrideDofFarTransition)               { s.SetOverride(PostProcessOverride::DepthOfFieldFarTransition);     s.depthOfFieldFarTransition      = ppvComp.dofFarTransition; }
            if (ppvComp.overrideDofFstop)                       { s.SetOverride(PostProcessOverride::DepthOfFieldFstop);             s.depthOfFieldFstop              = ppvComp.dofFstop; }
            if (ppvComp.overrideMotionBlurAmount)              { s.SetOverride(PostProcessOverride::MotionBlurAmount);              s.motionBlurAmount              = ppvComp.motionBlurAmount; }
            if (ppvComp.overrideMotionBlurMax)                 { s.SetOverride(PostProcessOverride::MotionBlurMax);                 s.motionBlurMax                 = ppvComp.motionBlurMax; }
            if (ppvComp.overrideMotionBlurTargetFPS)           { s.SetOverride(PostProcessOverride::MotionBlurTargetFPS);           s.motionBlurTargetFPS           = ppvComp.motionBlurTargetFPS; }
            if (ppvComp.overrideWhiteTemp)                     { s.SetOverride(PostProcessOverride::WhiteTemp);                     s.whiteTemp                     = ppvComp.whiteTemp; }
            if (ppvComp.overrideWhiteTint)                     { s.SetOverride(PostProcessOverride::WhiteTint);                     s.whiteTint                     = ppvComp.whiteTint; }
            if (ppvComp.overrideFilmSlope)                     { s.SetOverride(PostProcessOverride::FilmSlope);                     s.filmSlope                     = ppvComp.filmSlope; }
            if (ppvComp.overrideFilmToe)                       { s.SetOverride(PostProcessOverride::FilmToe);                       s.filmToe                       = ppvComp.filmToe; }
            if (ppvComp.overrideFilmShoulder)                  { s.SetOverride(PostProcessOverride::FilmShoulder);                  s.filmShoulder                  = ppvComp.filmShoulder; }
            if (ppvComp.overrideFilmBlackClip)                 { s.SetOverride(PostProcessOverride::FilmBlackClip);                 s.filmBlackClip                 = ppvComp.filmBlackClip; }
            if (ppvComp.overrideFilmWhiteClip)                 { s.SetOverride(PostProcessOverride::FilmWhiteClip);                 s.filmWhiteClip                 = ppvComp.filmWhiteClip; }
            if (ppvComp.overrideVignetteIntensity)             { s.SetOverride(PostProcessOverride::VignetteIntensity);             s.vignetteIntensity             = ppvComp.vignetteIntensity; }
            if (ppvComp.overrideAmbientOcclusionIntensity)     { s.SetOverride(PostProcessOverride::AmbientOcclusionIntensity);     s.ambientOcclusionIntensity     = ppvComp.ambientOcclusionIntensity; }
            if (ppvComp.overrideAmbientOcclusionRadius)        { s.SetOverride(PostProcessOverride::AmbientOcclusionRadius);        s.ambientOcclusionRadius        = ppvComp.ambientOcclusionRadius; }
            if (ppvComp.overrideAmbientOcclusionPower)         { s.SetOverride(PostProcessOverride::AmbientOcclusionPower);         s.ambientOcclusionPower         = ppvComp.ambientOcclusionPower; }
            if (ppvComp.overrideScreenSpaceReflectionIntensity){ s.SetOverride(PostProcessOverride::ScreenSpaceReflectionIntensity);s.screenSpaceReflectionIntensity = ppvComp.screenSpaceReflectionIntensity; }
            if (ppvComp.overrideScreenSpaceReflectionQuality)  { s.SetOverride(PostProcessOverride::ScreenSpaceReflectionQuality);  s.screenSpaceReflectionQuality  = ppvComp.screenSpaceReflectionQuality; }

            // Frame-local manager — the returned volume id is not needed
            // (volumes are re-collected every frame, never removed by id).
            (void)ppvManager.AddVolume(vol);
        }

        // Compute the final blended settings and apply overridden fields to perFrame.
        if (ppvManager.GetVolumeCount() > 0) {
            const PostProcessSettings blended = ppvManager.ComputeBlendedSettings(camWorldPos);

            // Apply only fields whose override bits are set in the blended result.
            // This preserves CVar defaults for non-overridden fields.
            // Apply blended ACES tonemap parameters to perFrame (overrides CVar defaults).
            if (blended.HasOverride(PostProcessOverride::FilmSlope))                     perFrame.toneCurveSlope      = blended.filmSlope;
            if (blended.HasOverride(PostProcessOverride::FilmToe))                       perFrame.toneCurveToe        = blended.filmToe;
            if (blended.HasOverride(PostProcessOverride::FilmShoulder))                  perFrame.toneCurveShoulder   = blended.filmShoulder;
            if (blended.HasOverride(PostProcessOverride::FilmBlackClip))                 perFrame.toneCurveBlackClip  = blended.filmBlackClip;
            if (blended.HasOverride(PostProcessOverride::FilmWhiteClip))                 perFrame.toneCurveWhiteClip  = blended.filmWhiteClip;
            if (blended.HasOverride(PostProcessOverride::VignetteIntensity))             perFrame.vignetteIntensity   = blended.vignetteIntensity;
            // Exposure bias: apply as 2^bias multiplier on the current exposure value.
            if (blended.HasOverride(PostProcessOverride::AutoExposureBias))              perFrame.exposure           *= std::pow(2.0f, blended.autoExposureBias);

            // Store the full blended settings in the frame context for subsystems
            // (bloom, DOF, motion blur, AO, SSR) that read from ctx directly.
            ctx.blendedPP = blended;
            ctx.hasBlendedPP = true;
        }
    }

    // ── GTAO SRV index (set early — actual compute runs in render graph) ──
    //
    // MUST use IsEnabledCVar() — NOT settings.enabled. The latter is only
    // refreshed at the top of GTAOPass::Execute(), which runs AFTER this
    // builder. Reading the struct here races and leaks a stale value into
    // `perFrame.gtaoSrvIndex`, which the deferred lighting shader then
    // samples unconditionally — manifesting as the "camera sphere of light"
    // Sponza bug (see .claude/rules/rendering-diagnostics.md, 2026-04-10).
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->IsEnabledCVar()) {
        perFrame.gtaoSrvIndex = m_gtaoPass->GetFilteredAOSrvIndex();
    } else {
        perFrame.gtaoSrvIndex = UINT32_MAX;
    }

    perFrame.shadowDebugMode = m_shadowDebugMode;
    perFrame.brdfDebugMode   = m_brdfDebugMode;

    // ── SSR SRV index (set early — actual compute runs in render graph) ──
    // When temporal accumulation is available, the lighting shader reads from
    // the SSR history buffer (temporally accumulated, smoother). Otherwise it
    // falls back to the raw resolved SSR output. The history write target for
    // this frame is m_ssrHistoryIndex — the temporal pass writes to it and
    // transitions it to SRV before the lighting pass runs.
    // m_ssrHasPrevColor gates the whole chain: without a previous colour target
    // the trace has nothing to sample and the resolved buffer holds stale data.
    if (m_ssrEnabled && m_ssrHasPrevColor && m_ssrResolvedOutput.IsValid()
        && m_ssrResolvedOutputSrv != UINT32_MAX) {
        // SSR pipeline: Trace → Resolve → Temporal → SpatialDenoise.
        // The SpatialDenoise pass writes its output to m_ssrResolvedOutput.
        // The lighting shader reads this denoised result.
        perFrame.ssrSrvIndex = m_ssrResolvedOutputSrv;
    } else if (RTReflectionsStandalone()) {
        // Hardware ray-traced reflections with SSR off (stage 3): the ray pass owns the resolved
        // buffer this frame — every texel written, zero weight where no ray was traced — so the
        // lighting blends it exactly as it blends the screen trace (frame_gi_passes.cpp
        // RegisterRTReflectionPass registers the pass under the same test).
        perFrame.ssrSrvIndex = m_ssrResolvedOutputSrv;
    } else {
        perFrame.ssrSrvIndex = UINT32_MAX;
    }

    // ── Radiance Cache (SDF GI) parameters ────────────────────────────
    m_radianceCache.Update(camPos);
    // HasProducedResult() is true from the first trace on and stays true across
    // grid recentres: lighting reads last frame's completed buffer, indexed with
    // the origin it was traced with (GetCompletedGridOrigin()), while the GI pass
    // retraces the recentred grid this frame with its history scrolled.
    // HasProducedResult(), not IsInitialized(): an allocated but never-written
    // probe buffer must not be consumed as if it held irradiance.
    if (m_sdfGiEnabled && m_radianceCache.HasProducedResult() && m_device->IsBindless()) {
        perFrame.radianceCacheEnabled = 1;
        perFrame.rcProbeSHBufferSrv   = m_radianceCache.GetProbeSHBufferSrv();
        perFrame.rcProbesPerAxis      = RadianceCache::kProbesPerAxis;
        perFrame.rcGridExtent         = m_radianceCache.GetGridExtent();
        // Not GetGridOrigin(): the buffer published above was traced last frame;
        // on a recentre frame the current origin is already one cell away.
        const Vec3 origin = m_radianceCache.GetCompletedGridOrigin();
        perFrame.rcGridOriginX        = origin.x;
        perFrame.rcGridOriginY        = origin.y;
        perFrame.rcGridOriginZ        = origin.z;
        perFrame.rcGiIntensity        = cv_GIIntensity->value;
    } else {
        perFrame.radianceCacheEnabled = 0;
        perFrame.rcProbeSHBufferSrv   = UINT32_MAX;
    }

    // ── Global distance field bindings for the SDF gather/reflection paths ──
    // Both consumers need the raw (unexpanded) field as well as the expanded one:
    // expansion is applied during the trace, and the raw field is what keeps a
    // narrow opening — a window, a doorway — from being closed by filtering.
    m_globalDistanceField.Update(camPos);
    if ((cv_SdfReflections->value != 0 || cv_SdfFinalGather->value != 0) && perFrame.radianceCacheEnabled != 0
        && m_globalDistanceField.IsInitialized()) {
        for (uint32_t cascade = 0; cascade < GlobalDistanceField::kNumClipmaps; ++cascade) {
            const auto& clipmap = m_globalDistanceField.GetClipmap(cascade);
            perFrame.sdfReflectionClipmaps[cascade] = {clipmap.center.x, clipmap.center.y, clipmap.center.z, clipmap.extent};
            perFrame.sdfReflectionClipmapSrv[cascade] = clipmap.hasData ? clipmap.srvIndex : UINT32_MAX;
            perFrame.sdfRawClipmapSrv[cascade] = clipmap.hasData ? clipmap.rawSrvIndex : UINT32_MAX;
        }
        const auto& nearClipmap = m_globalDistanceField.GetClipmap(0);
        const bool hasDistanceField = nearClipmap.hasData && nearClipmap.rawSrvIndex != UINT32_MAX;
        perFrame.sdfReflectionsEnabled = hasDistanceField && cv_SdfReflections->value != 0 ? 1u : 0u;
        perFrame.sdfDiffuseSamples = hasDistanceField && cv_SdfFinalGather->value != 0
            ? static_cast<uint32_t>(cv_SdfDiffuseSamples->value) : 0u;
        perFrame.sdfReflectionSkySrv = perFrame.prefilteredSrvIndex;
        perFrame.sdfReflectionSamples = static_cast<uint32_t>(cv_SdfReflectionSamples->value);
        perFrame.sdfReflectionPlainMean = cv_SdfReflectionHdrWeight->value ? 0u : 1u;
        perFrame.sdfSampleRotation = static_cast<uint32_t>(cv_SdfSampleRotation->value)
            | (cv_GIValidSampleReject->value ? 0x80000000u : 0u);
        perFrame.sdfReflectionTraceSteps = static_cast<uint32_t>(cv_SdfReflectionSteps->value);
        perFrame.sdfReflectionTraceDistance = cv_SdfReflectionDistance->value;
        perFrame.sdfReflectionVoxelSize = m_globalDistanceField.GetVoxelSize(0);
    }

    // ── Diffuse gather selection ────────────────────────────────────────
    // World mode (SDF final gather) and the legacy screen-space mode share the
    // same output SRV, so exactly one of them may be active. The SRV indices are
    // published only when the renderer actually prepared its resources this
    // frame; otherwise the lighting shader would sample an unwritten target.
    const bool worldGather = perFrame.sdfDiffuseSamples != 0
        && m_passInspector.IsPassEnabled("sdf_gi") && m_passInspector.IsPassEnabled("ssgi");
    const bool screenGather = m_showFlags.Get(ShowFlag::SSGI) && m_passInspector.IsPassEnabled("ssgi");
    const bool gatherReady = m_device->IsBindless()
        && m_ssgiRenderer.PrepareFrame(m_pipelines.get(), worldGather, screenGather);
    perFrame.ssgiSrvIndex = gatherReady ? m_ssgiRenderer.GetResultSrvIndex() : UINT32_MAX;
    perFrame.ssgiAOSrvIndex = gatherReady ? m_ssgiRenderer.GetAOResultSrvIndex() : UINT32_MAX;
    perFrame.ssgiEnabled = gatherReady ? 1u : 0u;
    if (!worldGather || !gatherReady) perFrame.sdfDiffuseSamples = 0;

    // ── Brixelizer GI (AMD FidelityFX sparse SDF voxel GI) ──────────
    // Expose the diffuse GI atlas SRV to the deferred lighting shader.
    // The previous black-screen issue was caused by FFX rebinding the
    // descriptor heap and leaving it bound — fixed by
    // `DX12CommandList::RebindBindlessHeaps()` after every FFX dispatch.
    if (m_brixelizerGI && m_brixelizerGI->IsInitialized() && m_device->IsBindless()
        && CVarRegistry::Get().GetBool("r.BrixelizerGI", false)) {
        perFrame.brixelizerGIDiffuseSrv = m_brixelizerGI->GetDiffuseGISrv();
    } else {
        perFrame.brixelizerGIDiffuseSrv = UINT32_MAX;
    }

    // ── DDGI octahedral atlases (Majercik 2019) ───────────────────────
    // Mirror the DDGISystem's atlas SRVs + grid layout into the CB so the
    // deferred lighting shader can sample with Chebyshev visibility. The
    // probe origin/spacing track the radiance cache so DDGI and the SDF
    // path use the same probe positions.
    perFrame.ddgiIrradianceSrv = UINT32_MAX;
    perFrame.ddgiDepthSrv      = UINT32_MAX;
    perFrame.ddgiProbesPerAxis = 0;
    perFrame.ddgiProbeSpacing  = 0.0f;
    perFrame.ddgiIntensity     = cv_GIIntensity->value;
    perFrame.indirectAOExponent = cv_IndirectAOExp->value;
    // Bind the DDGI atlases only while the writer is switched on (`r.RayTracing`
    // AND `r.RayTracing.GI`, the same test frame_gi_passes.cpp uses to dispatch it).
    // Without this the consumer kept reading the last atlas after the CVar was
    // turned off — DDGI has the highest priority in the shader, so a runtime
    // "DDGI off" left the lighting unchanged and the radiance cache unreachable.
    const bool ddgiWriterOn = m_rtManager && m_rtManager->IsGIEnabled();
    if (ddgiWriterOn && m_ddgiSystem.IsInitialized() && m_ddgiSystem.HasCompletedFirstUpdate()
        && m_radianceCache.IsInitialized() && m_device->IsBindless()) {
        const Vec3 origin = m_radianceCache.GetGridOrigin();
        m_ddgiSystem.SetGridOrigin(origin);
        m_ddgiSystem.SetProbeSpacing(m_radianceCache.GetProbeSpacing());

        perFrame.ddgiIrradianceSrv = m_ddgiSystem.GetIrradianceSrv();
        perFrame.ddgiDepthSrv      = m_ddgiSystem.GetDepthSrv();
        perFrame.ddgiProbesPerAxis = DDGISystem::kProbesPerAxis;
        perFrame.ddgiProbeSpacing  = m_ddgiSystem.GetProbeSpacing();
        perFrame.ddgiOriginX       = origin.x;
        perFrame.ddgiOriginY       = origin.y;
        perFrame.ddgiOriginZ       = origin.z;
    }

    // ── Light Probe Volume (author-placed irradiance grid) ────────────
    // @ref Tatarchuk 2005 "Irradiance Volumes for Games" — author-placed grids
    // @ref Greger 1998 "The Irradiance Volume" — SH-based volumetric irradiance
    perFrame.lpvEnabled        = 0;
    perFrame.lpvProbeBufferSrv = UINT32_MAX;
    if (ctx.world && m_device->IsBindless()) {
        auto lpvView = ctx.world->GetRegistry().view<LightProbeVolumeComponent>();
        for (auto enttEntity : lpvView) {
            auto& vol = lpvView.get<LightProbeVolumeComponent>(enttEntity);
            if (!vol.enabled) continue;

            // Create or rebuild the GPU grid on demand.
            if (!m_lightProbeGrid.IsInitialized()) {
                if (!m_lightProbeGrid.CreateResources(m_device, vol)) {
                    UZ_LOG(Renderer, Warning,
                           "LightProbeVolume: failed to create probe grid resources");
                    break;
                }
                vol.needsRebuild = true;
            } else if (vol.needsRebuild
                       || m_lightProbeGrid.GetDimX() != vol.gridDimensionsX
                       || m_lightProbeGrid.GetDimY() != vol.gridDimensionsY
                       || m_lightProbeGrid.GetDimZ() != vol.gridDimensionsZ) {
                if (!m_lightProbeGrid.Rebuild(vol)) {
                    UZ_LOG(Renderer, Warning,
                           "LightProbeVolume: rebuild failed");
                    break;
                }
            }

            if (vol.needsRebuild) {
                // Seed every probe with the current sky SH (positionally
                // invariant). Spatial variation comes from tasks 3.x/4.x.
                SHCoefficients skySH{};
                std::memcpy(&skySH.coeffs[0], &perFrame.skyIrradianceSH[0],
                            sizeof(SHCoefficients));
                // The per-frame sky SH lives in the cubemap's frame (+Y up) and the deferred lighting
                // evaluates the probe volume with the world normal, so the seed is converted once here.
                m_lightProbeGrid.FillFromSkySH(SHCubemapToWorld(skySH));
                m_lightProbeGrid.UploadToGpu();
                vol.needsRebuild = false;
            }

            // Populate CB fields consumed by deferred_lighting_ps.hlsl.
            const Vec3 extMin = m_lightProbeGrid.GetExtentMin();
            const Vec3 extMax = m_lightProbeGrid.GetExtentMax();
            const float sx = std::max(extMax.x - extMin.x, 1e-3f);
            const float sy = std::max(extMax.y - extMin.y, 1e-3f);
            const float sz = std::max(extMax.z - extMin.z, 1e-3f);

            perFrame.lpvEnabled        = 1;
            perFrame.lpvProbeBufferSrv = m_lightProbeGrid.GetProbeBufferSrv();
            perFrame.lpvDimX           = m_lightProbeGrid.GetDimX();
            perFrame.lpvDimY           = m_lightProbeGrid.GetDimY();
            perFrame.lpvDimZ           = m_lightProbeGrid.GetDimZ();
            perFrame.lpvOriginX        = extMin.x;
            perFrame.lpvOriginY        = extMin.y;
            perFrame.lpvOriginZ        = extMin.z;
            perFrame.lpvInvSizeX       = 1.0f / sx;
            perFrame.lpvInvSizeY       = 1.0f / sy;
            perFrame.lpvInvSizeZ       = 1.0f / sz;
            // Default to 0 — the LPV broadcast is just stale sky SH and
            // would otherwise replace the working sky SH / radiance cache
            // sample. Authors enable spatial variation via the SDF/DXR
            // probe-update path; LPV intensity is currently a no-op until
            // the LPV-specific writer lands.
            perFrame.lpvIntensity      = 0.0f;

            vol.probeBufferSrv = m_lightProbeGrid.GetProbeBufferSrv();
            break; // Only the first enabled volume is used.
        }
    }

    // ── Aerial Perspective parameters (from SkyAtmosphereComponent) ───
    // Populate AP atmosphere constants so deferred_lighting_ps can ray-march
    // inscattering per pixel. Only enabled when a SkyAtmosphereComponent exists
    // and the transmittance LUT has been baked.
    if (ctx.world) {
        auto skyView = ctx.world->GetRegistry().view<SkyAtmosphereComponent>();
        for (auto enttEntity : skyView) {
            auto& skyComp = skyView.get<SkyAtmosphereComponent>(enttEntity);
            if (!skyComp.enabled) continue;
            if (m_transmittanceLutSrv != UINT32_MAX && m_transmittanceLutFilled
                && cv_AerialPersp->value > 0.5f) {
                perFrame.aerialPerspectiveEnabled = 1;
                perFrame.transmittanceLutSrvIndex = m_transmittanceLutSrv;
                perFrame.apBottomRadiusKm = skyComp.bottomRadiusKm;
                perFrame.apTopRadiusKm    = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;
                perFrame.apRayleighBetaX  = skyComp.rayleighScattering.x;
                perFrame.apRayleighBetaY  = skyComp.rayleighScattering.y;
                perFrame.apRayleighBetaZ  = skyComp.rayleighScattering.z;
                perFrame.apRayleighScaleH = skyComp.rayleighScaleHeightKm;
                perFrame.apMieBeta        = skyComp.mieScattering;
                perFrame.apMieScaleH      = skyComp.mieScaleHeightKm;
                perFrame.apMieAnisotropy  = skyComp.mieAnisotropy;
                perFrame.apSunIntensity   = ctx.sunColor.Length();
                perFrame.apRayleighScale  = skyComp.rayleighScatteringScale;
                perFrame.apMieScale       = skyComp.mieScatteringScale;
                // AP volume SRV (precomputed 32x32x16 3D texture). Gate on m_apVolumeFilled:
                // before the first AP dispatch the 3D texture is allocator-recycled memory, and
                // reading it (frame 0 / cross-process) is layout-dependent (phase0 frame-allocation).
                if (m_apVolumeSrv != UINT32_MAX && m_apVolumeFilled) {
                    perFrame.apVolumeSrvIndex = m_apVolumeSrv;
                }
            }
            // HeightFogContribution reduces AP inscattering when height fog is active
            // Propagate heightFogContribution to reduce AP inscattering when height fog
            // is active, preventing double-fogging.
            perFrame.heightFogContribution = skyComp.heightFogContribution;
            break; // First SkyAtmosphereComponent only
        }
    }

    // ── IBL texture handles (for SM5 binder path) ─────────────────────
    // The binder's BindResources() reads these handles and calls
    // SetFragmentTexture at the metadata-defined register slots (t5-t7).
    if (m_iblBaker) {
        perFrame.irradianceCubemap   = m_iblBaker->GetIrradianceTexture();
        perFrame.prefilteredSpecular = m_iblBaker->GetPrefilteredTexture();
        perFrame.brdfLut             = m_iblBaker->GetBRDFLutTexture();
    }

    // ── Reflection Probes ────────────────────────────────────────────────
    // Phase 1: Capture — render the scene into a dirty probe's cubemap.
    // Phase 2: Update — bake IBL from the captured cubemap and upload GPU data.
    if (m_reflectionProbes.IsInitialized() && ctx.world && m_pipelines) {
        // Build a scene descriptor so the probe can render into its cubemap.
        // The forward PBR pipeline and draw list are used for simplified
        // scene capture at the probe's resolution.
        const bool bindless = m_device->IsBindless();
        ProbeCaptureSceneDesc captureDesc{};
        captureDesc.cmdList         = ctx.cmdList;
        captureDesc.drawList        = &ctx.drawList;
        captureDesc.meshes          = m_meshes.data();
        captureDesc.meshCount       = static_cast<uint32_t>(m_meshes.size());
        captureDesc.cbAllocator     = m_cbRing.get();
        captureDesc.forwardPipeline = m_pipelines->GetForwardPBR(bindless);
        captureDesc.bindless        = bindless;

        m_reflectionProbes.CaptureIfDirty(
            *ctx.world, captureDesc, m_skyCubemapSrv, m_skyCubemap);

        const auto probeResult = m_reflectionProbes.Update(
            *ctx.world, m_skyCubemapSrv, m_skyCubemap);
        perFrame.reflectionProbeCount          = probeResult.probeCount;
        perFrame.reflectionProbeBufferSrvIndex = probeResult.probeBufferSrvIndex;
    }

    // Allocate CB for per-frame data — only the scalar section (512 bytes)
    // is copied to the GPU. Resource handles live after the CB boundary
    // and are bound separately by BindResources().
    ctx.perFrameAlloc = m_cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(ctx.perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── Shadow cascades — delegated to shadow_renderer ─────────────────
    // Skip cascade recompute when camera + lights + transforms haven't changed.
    // The cascade ViewProj matrices depend on camera position/direction and light
    // direction — if none changed, the previous frame's ShadowDataCB is still valid.
    m_cbUpdatesTotal++;
    ctx.shadowCacheEnabled = false;
    const int shadowFreezeFrame = CVarRegistry::Get().GetInt("r.Shadow.FreezeAfterFrame", -1);
    const bool shadowFrozen = shadowFreezeFrame >= 0 && static_cast<int>(ctx.frameIndex) > shadowFreezeFrame;
    const bool shadowsDirty = (ctx.updateFlags.AreShadowsDirty() && !shadowFrozen) || !m_prevShadowValid;

    if (shadowsDirty) {
        ctx.shadowFrame = SetupShadowFrame(
            camera, ctx.sunDir,
            ctx.lightData.perLightShadowBias, ctx.lightData.perLightShadowSlopeBias,
            ctx.lightData.lightSourceAngleDeg,
            m_shadowAtlas.GetSrvIndex(), m_shadowAtlas.GetResolution(), *m_cbRing,
            ctx.lightData.dynamicShadowDistance,
            ctx.lightData.farShadowCascadeCount,
            ctx.lightData.farShadowDistance,
            ctx.lightData.farShadowLodBias,
            ctx.shadowCacheEnabled,
            m_staticShadowAtlasSrv);
        ctx.totalCascadeCount = ctx.shadowFrame.totalCascadeCount;
        ctx.shadowAlloc = ctx.shadowFrame.cbAllocation;

        // Cache for next frame comparison
        if (ctx.shadowAlloc.cpuPtr) {
            std::memcpy(&m_prevShadowCB, ctx.shadowAlloc.cpuPtr, sizeof(ShadowDataCB));
            m_prevShadowAlloc = ctx.shadowAlloc;
            m_prevShadowValid = true;
        }
    } else {
        // Shadow data unchanged — skip expensive cascade recompute.
        // Still allocate a fresh CB slot (ring is triple-buffered, old slot
        // will be overwritten) but copy from cache instead of recomputing.
        ctx.shadowAlloc = m_cbRing->Allocate(sizeof(ShadowDataCB));
        std::memcpy(ctx.shadowAlloc.cpuPtr, &m_prevShadowCB, sizeof(ShadowDataCB));
        ctx.totalCascadeCount = m_prevShadowCB.cascadeCount;
        ctx.shadowFrame.totalCascadeCount = m_prevShadowCB.cascadeCount;
        ctx.shadowFrame.cbAllocation = ctx.shadowAlloc;
        m_cbUpdatesSkipped++;
    }
    if (!ctx.lightData.hasDirectionalLight) {
        // No directional light: no cascades. shadow.hlsli returns full visibility for
        // cascadeCount 0 and frame_shadows.cpp skips the cascade pass; the local-light
        // entries below do not depend on the sun.
        reinterpret_cast<ShadowDataCB*>(ctx.shadowAlloc.cpuPtr)->cascadeCount = 0;
        ctx.totalCascadeCount             = 0;
        ctx.shadowFrame.totalCascadeCount = 0;
    }
    // r.Shadows.SoftPCSS (registered in shadow_renderer.cpp) is re-read every frame so a runtime
    // toggle applies at once: the CSM block above is reused while camera, lights and transforms
    // hold still, and SetupShadowFrame only runs on a dirty frame.
    reinterpret_cast<ShadowDataCB*>(ctx.shadowAlloc.cpuPtr)->softPCSS =
        CVarRegistry::Get().GetBool("r.Shadows.SoftPCSS", true) ? 1u : 0u;

    // ── Build ShadowDataParams for binder path ──────────────────────────
    // Copy the 768-byte scalar section from the CB allocation, then attach
    // the RHI texture handles so ShaderParameterBinder can bind them at
    // their SM5 register slots (t3, t8, t9).
    ctx.shadowDataParams = ShadowDataParams{};
    static_assert(offsetof(ShadowDataParams, shadowAtlas) == sizeof(ShadowDataCB),
                  "ShadowDataParams scalar section must match ShadowDataCB size");
    std::memcpy(&ctx.shadowDataParams, ctx.shadowAlloc.cpuPtr, sizeof(ShadowDataCB));
    ctx.shadowDataParams.shadowAtlas       = m_shadowAtlas.GetTexture();
    ctx.shadowDataParams.staticShadowAtlas = m_staticShadowAtlas;
    // Wire DFS output into shadow params when DFS is active.
    // The cone trace compute pass writes to m_dfsOutputTexture each frame.
    // The deferred lighting shader reads it via dfsOutputSrvIndex and composites
    // DFS shadows multiplicatively with CSM shadows.
    if (m_dfsOutputTexture.IsValid() && m_dfsConfig.enabled
        && m_sdfAtlas && m_sdfAtlas->IsInitialized()
        && m_sdfAtlas->GetRegisteredObjectCount() > 0) {
        ctx.shadowDataParams.dfsEnabled        = 1;
        ctx.shadowDataParams.dfsOutputSrvIndex = m_dfsOutputSrvIndex;
        ctx.shadowDataParams.dfsOutputTexture  = m_dfsOutputTexture;
    }

    // ── Local Light Shadows (spot + point lights with castShadows=true) ─
    // For each spot light that requests shadows, build a perspective
    // projection from the spot light's position, direction, and outer cone
    // angle, allocate a tile in the local shadow atlas, and fill the
    // LocalLightShadowData entry in the ShadowDataCB.
    //
    // For each point light that requests shadows, build 6 cubemap face
    // perspective projections (90-degree FOV), allocate 6 tiles in the
    // atlas, and fill 6 consecutive LocalLightShadowData entries.
    //
    // Spot: perspective projection from cone angle, near=0.1cm, far=radius.
    // Point: 6-face cubemap shadow, 90-deg FOV, near=1.0cm, far=radius.
    m_localShadowAtlas.Reset();
    const uint32_t totalLocalShadowLights = ctx.lightData.spotShadowCount + ctx.lightData.pointShadowCount;
    if (m_localShadowAtlas.IsValid() && totalLocalShadowLights > 0) {
        auto* shadowCBPtr = reinterpret_cast<ShadowDataCB*>(ctx.shadowAlloc.cpuPtr);
        shadowCBPtr->localShadowAtlasIndex = m_localShadowAtlas.GetSrvIndex();
        ctx.shadowDataParams.localShadowAtlasIndex = m_localShadowAtlas.GetSrvIndex();
        ctx.shadowDataParams.localShadowAtlas      = m_localShadowAtlas.GetTexture();

        uint32_t totalLocalShadowEntries = 0;

        // ── Spot light shadows (1 tile each) ────────────────────────────
        for (uint32_t si = 0; si < ctx.lightData.spotShadowCount; ++si) {
            const SpotLightShadowInfo& spotShadow = ctx.lightData.spotShadows[si];

            ShadowTileInfo tile{};
            if (!m_localShadowAtlas.AllocateTile(tile)) {
                UZ_LOG(Renderer, Warning,
                       "LocalShadow: atlas full — could not allocate tile for spot shadow {}",
                       si);
                break;
            }

            // Build perspective projection matrix for the spot light.
            // FOV = 2 * outerAngle (full apex angle of the cone).
            // Near = 0.1 cm (reference MinLightW), Far = radius (attenuation distance).
            // Near = 0.1 cm (minimum shadow near plane), Far = radius (light range).
            const float fovY     = 2.0f * spotShadow.outerAngle;
            const float nearZ    = cv_LocalShadowNearPlane->value;
            const float farZ     = spotShadow.radius;

            // Build view matrix (lookAt from spot position along spot direction).
            // Use Z-up world: pick an up vector that isn't collinear with direction.
            Vec3 up = Vec3::Up(); // Z-up (0, 0, 1)
            const float dotUp = std::abs(spotShadow.direction.Dot(up));
            if (dotUp > 0.99f) {
                // Direction is nearly vertical — use a horizontal up vector instead
                up = Vec3{0.f, 1.f, 0.f};
            }

            const Vec3 target = spotShadow.position + spotShadow.direction;
            Camera spotCamera;
            spotCamera.SetPosition(spotShadow.position);
            spotCamera.LookAt(target, up);
            // outerAngle is in radians, convert to degrees for SetHorizontalFOV.
            // For a square shadow map (aspect=1), horizontal FOV = vertical FOV.
            const float fovDeg = fovY * (180.0f / 3.14159265358979323846f);
            spotCamera.SetHorizontalFOV(fovDeg, 1.0f, nearZ, farZ);

            const Mat4 lightViewProj = spotCamera.GetViewProjection();

            // Fill LocalLightShadowData entry in ShadowDataCB
            LocalLightShadowData& sd = shadowCBPtr->localShadows[si];
            sd.viewProj     = lightViewProj;
            sd.atlasOffsetX = tile.uvOffsetX;
            sd.atlasOffsetY = tile.uvOffsetY;
            sd.atlasScale   = tile.uvScale;
            sd.bias         = spotShadow.bias;
            sd.lightPosX    = spotShadow.position.x;
            sd.lightPosY    = spotShadow.position.y;
            sd.lightPosZ    = spotShadow.position.z;
            sd.radius       = spotShadow.radius;

            // Also copy to shadowDataParams for the binder path
            ctx.shadowDataParams.localShadows[si] = sd;
            ++totalLocalShadowEntries;
        }

        // ── Point light shadows (6 tiles each — one per cubemap face) ───
        // Face layout matches HLSL SelectCubeFace():
        //   Face 0 (+X): look toward +X, up = +Z
        //   Face 1 (-X): look toward -X, up = +Z
        //   Face 2 (+Y): look toward +Y, up = +Z
        //   Face 3 (-Y): look toward -Y, up = +Z
        //   Face 4 (+Z): look toward +Z, up = -Y
        //   Face 5 (-Z): look toward -Z, up = +Y
        //
        // Standard cubemap face layout: +X/-X/+Y/-Y/+Z/-Z with Z-up convention.
        //
        // Each face uses a 90-degree FOV perspective projection (aspect=1) from the
        // light position, with near=1.0cm and far=radius.
        struct CubeFaceDir {
            Vec3 forward;
            Vec3 up;
        };
        static constexpr CubeFaceDir kCubeFaces[kPointLightShadowFaces] = {
            {{ 1.f,  0.f,  0.f}, { 0.f,  0.f,  1.f}},  // +X
            {{-1.f,  0.f,  0.f}, { 0.f,  0.f,  1.f}},  // -X
            {{ 0.f,  1.f,  0.f}, { 0.f,  0.f,  1.f}},  // +Y
            {{ 0.f, -1.f,  0.f}, { 0.f,  0.f,  1.f}},  // -Y
            {{ 0.f,  0.f,  1.f}, { 0.f, -1.f,  0.f}},  // +Z (up = -Y to avoid collinear)
            {{ 0.f,  0.f, -1.f}, { 0.f,  1.f,  0.f}},  // -Z (up = +Y to avoid collinear)
        };

        // 90-degree FOV for cubemap faces — standard cube projection.
        // Standard cubemap face FOV: 90 degrees.
        static constexpr float kCubeFovDeg = 90.0f;
        // Near plane for point light shadows (1.0 cm).
        // 1.0 cm near plane prevents self-intersection at light origin.
        const float kPointShadowNear = cv_LocalShadowNearPlane->value;

        for (uint32_t pi = 0; pi < ctx.lightData.pointShadowCount; ++pi) {
            const PointLightShadowInfo& ptShadow = ctx.lightData.pointShadows[pi];
            const uint32_t baseIdx = totalLocalShadowEntries;

            // Check that all 6 faces fit in the atlas
            bool allFacesAllocated = true;
            ShadowTileInfo faceTiles[kPointLightShadowFaces]{};
            for (uint32_t face = 0; face < kPointLightShadowFaces; ++face) {
                if (!m_localShadowAtlas.AllocateTile(faceTiles[face])) {
                    UZ_LOG(Renderer, Warning,
                           "LocalShadow: atlas full — could not allocate tile for point shadow {} face {}",
                           pi, face);
                    allFacesAllocated = false;
                    break;
                }
            }

            if (!allFacesAllocated) {
                // Cannot fit all 6 faces — skip this point light entirely.
                // The shadowIndex in PointLightData remains UINT32_MAX (unshadowed).
                break;
            }

            for (uint32_t face = 0; face < kPointLightShadowFaces; ++face) {
                const uint32_t entryIdx = baseIdx + face;
                const auto& faceDir = kCubeFaces[face];
                const ShadowTileInfo& tile = faceTiles[face];

                // Build view matrix: lookAt from light position along face direction
                const Vec3 target = ptShadow.position + faceDir.forward;
                Camera faceCamera;
                faceCamera.SetPosition(ptShadow.position);
                faceCamera.LookAt(target, faceDir.up);
                faceCamera.SetHorizontalFOV(kCubeFovDeg, 1.0f, kPointShadowNear, ptShadow.radius);

                const Mat4 faceViewProj = faceCamera.GetViewProjection();

                // Fill LocalLightShadowData entry in ShadowDataCB
                LocalLightShadowData& sd = shadowCBPtr->localShadows[entryIdx];
                sd.viewProj     = faceViewProj;
                sd.atlasOffsetX = tile.uvOffsetX;
                sd.atlasOffsetY = tile.uvOffsetY;
                sd.atlasScale   = tile.uvScale;
                sd.bias         = ptShadow.bias;
                sd.lightPosX    = ptShadow.position.x;
                sd.lightPosY    = ptShadow.position.y;
                sd.lightPosZ    = ptShadow.position.z;
                sd.radius       = ptShadow.radius;

                ctx.shadowDataParams.localShadows[entryIdx] = sd;
            }

            totalLocalShadowEntries += kPointLightShadowFaces;
        }

        shadowCBPtr->localShadowCount = totalLocalShadowEntries;
        ctx.shadowDataParams.localShadowCount = totalLocalShadowEntries;
        shadowCBPtr->localShadowNormalOffset = cv_LocalShadowNormalOffset->value;
        ctx.shadowDataParams.localShadowNormalOffset = cv_LocalShadowNormalOffset->value;

        static bool loggedLocalShadowOnce = false;
        if (!loggedLocalShadowOnce) {
            UZ_LOG(Renderer, Info,
                   "LocalShadow: {} spot + {} point light shadows allocated ({} tiles, atlas SRV={})",
                   ctx.lightData.spotShadowCount, ctx.lightData.pointShadowCount,
                   totalLocalShadowEntries, m_localShadowAtlas.GetSrvIndex());
            loggedLocalShadowOnce = true;
        }
    }

    // Resize shadow atlas if cascade count changed (e.g., far shadows toggled). With no
    // directional light there are no cascades (cascadeCount 0): keep the atlas as it is, the
    // frame still imports it and a 0-cascade atlas would be an invalid texture.
    if (ctx.totalCascadeCount > 0) {
        m_shadowAtlas.Resize(m_device, ctx.totalCascadeCount);
    }
}

} // namespace uz

// build-cache: GATHER grew to 31 records (cascade-at-hit and clipmap 0 at +29..+30)
