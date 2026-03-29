/// UzEngine Scene Renderer — Per-frame constant buffer population.
/// Fills PerFrameParams with camera matrices, IBL handles, atmosphere params,
/// SkyLight, SSGI, GI, reflection probes, shadows, and CVar-driven overrides.

#include "scene_renderer.h"

#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

#include "camera.h"
#include "gpu_mesh.h"
#include "sh_projection.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"
#include "world.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ── CVars consumed by PerFrameCB population ─────────────────────────────
static auto* cv_SkyLightIntensity  = UZ_CVAR_FLOAT("r.SkyLight.Intensity", 1.0f, "IBL ambient multiplier", 0.f, 20.f);
static auto* cv_SkyLightMinAmbient = UZ_CVAR_FLOAT("r.SkyLight.MinAmbient", 0.25f, "Minimum ambient floor", 0.f, 1.f);
static auto* cv_ContactShadowLen   = UZ_CVAR_FLOAT("r.ContactShadow.Length", 0.0f, "Contact shadow ray length (0=off)", 0.f, 0.2f);
static auto* cv_FilmGrain          = UZ_CVAR_FLOAT("r.FilmGrain", 0.f, "Film grain intensity", 0.f, 1.f);
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
static auto* cv_Exposure           = UZ_CVAR_FLOAT("r.Exposure", 1.0f, "Manual exposure bias on auto-exposure", 0.01f, 100.f);
static auto* cv_GIIntensity        = UZ_CVAR_FLOAT("r.GI.Intensity", 1.f, "GI intensity multiplier", 0.f, 10.f);

namespace uz {

void SceneRenderer::PopulatePerFrameConstants(FrameContext& ctx) {
    UZ_PROFILE_FUNCTION();

    auto& camera = *m_camera;

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
    perFrame.nearPlane   = camera.GetNearPlane();
    perFrame.farPlane    = camera.GetFarPlane();

    // ── IBL SRV indices (set once after first bake, UINT32_MAX = no IBL) ──
    perFrame.irradianceSrvIndex  = m_iblResult.irradianceSrvIndex;
    perFrame.prefilteredSrvIndex = m_iblResult.prefilteredSrvIndex;
    perFrame.brdfLutSrvIndex     = m_brdfLutSrvIndex;
    perFrame.prefilteredMipLevels = IBLBaker::kPrefilteredMipLevels;

    // ── Extract SkyLight from SkyLightComponent (standard) ──────────
    // Must be done BEFORE SH upload so intensity can be baked into the SH coefficients.
    // SkyLight extraction: intensity, color, minOcclusion from first SkyLightComponent.
    float skyIntensity = 1.0f;
    float skyIndirectIntensity = 1.0f;
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
            break;  // First SkyLightComponent only
        }
    }

    // Apply CVar-driven skylight overrides on top of component values.
    // r.SkyLight.Intensity acts as a multiplier on the component intensity.
    // r.SkyLight.MinAmbient provides a floor that the component value is raised to.
    perFrame.skyLightIntensity *= cv_SkyLightIntensity->value;
    perFrame.skyLightMinAmbient = std::max(perFrame.skyLightMinAmbient, cv_SkyLightMinAmbient->value);

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
    ctx.lightData = ExtractLightingFromECS(ctx.world, *m_cbRing);
    ctx.sunDir       = ctx.lightData.sunDir;
    ctx.sunColor     = ctx.lightData.sunColor;
    ctx.ambientColor = ctx.lightData.ambientColor;
    ctx.lightingAlloc = ctx.lightData.lightingCBAlloc;

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
        // Log only on first adaptation (avoid per-frame spam)
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Info, "AutoExposure: initial exposure={:.4f}", perFrame.exposure);
            loggedOnce = true;
        }
    } else {
        // Without auto-exposure, use neutral exposure.
        // Default exposure = 1.0 (neutral, no bias applied).
        perFrame.exposure = 1.0f;
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

    // ── GTAO SRV index (set early — actual compute runs in render graph) ──
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
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
    if (m_ssrEnabled && m_ssrResolvedOutput.IsValid()) {
        const uint32_t ssrWriteIdx = m_ssrHistoryIndex;
        if (m_ssrHistory[ssrWriteIdx].IsValid()
            && m_ssrHistorySrv[ssrWriteIdx] != UINT32_MAX) {
            perFrame.ssrSrvIndex = m_ssrHistorySrv[ssrWriteIdx];
        } else {
            // History not yet created — fall back to raw resolved output
            perFrame.ssrSrvIndex = m_ssrResolvedOutputSrv;
        }
    } else {
        perFrame.ssrSrvIndex = UINT32_MAX;
    }

    // ── SSGI SRV indices (set early — actual compute runs in render graph) ──
    // SSGI result SRV indices — used by deferred lighting to add indirect diffuse.
    if (m_ssgiRenderer.IsReady() && m_device->IsBindless()) {
        perFrame.ssgiSrvIndex   = m_ssgiRenderer.GetResultSrvIndex();
        perFrame.ssgiAOSrvIndex = m_ssgiRenderer.GetAOResultSrvIndex();
        perFrame.ssgiEnabled    = 1;
    } else {
        perFrame.ssgiSrvIndex   = UINT32_MAX;
        perFrame.ssgiAOSrvIndex = UINT32_MAX;
        perFrame.ssgiEnabled    = 0;
    }

    // ── Radiance Cache (SDF GI) parameters ────────────────────────────
    if (m_sdfGiEnabled && m_radianceCache.IsInitialized() && m_device->IsBindless()) {
        perFrame.radianceCacheEnabled = 1;
        perFrame.rcProbeSHBufferSrv   = m_radianceCache.GetProbeSHBufferSrv();
        perFrame.rcProbesPerAxis      = RadianceCache::kProbesPerAxis;
        perFrame.rcGridExtent         = m_rcGridExtentCVar;
        const Vec3 origin = m_radianceCache.GetGridOrigin();
        perFrame.rcGridOriginX        = origin.x;
        perFrame.rcGridOriginY        = origin.y;
        perFrame.rcGridOriginZ        = origin.z;
        perFrame.rcGiIntensity        = cv_GIIntensity->value;
    } else {
        perFrame.radianceCacheEnabled = 0;
        perFrame.rcProbeSHBufferSrv   = UINT32_MAX;
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
            if (m_transmittanceLutSrv != UINT32_MAX && m_transmittanceLutFilled) {
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
                // AP volume SRV (precomputed 32x32x16 3D texture)
                if (m_apVolumeSrv != UINT32_MAX) {
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
    // Determine shadow cache status early so ShadowDataCB gets the right fields.
    ctx.shadowCacheEnabled = m_shadowCacheManager.GetConfig().enabled
                           && m_staticShadowAtlas.IsValid()
                           && m_staticShadowAtlasSrv != UINT32_MAX;
    ctx.shadowFrame = SetupShadowFrame(
        camera, ctx.sunDir,
        ctx.lightData.perLightShadowBias, ctx.lightData.perLightShadowSlopeBias,
        ctx.lightData.lightSourceAngleDeg,
        m_shadowAtlas.GetSrvIndex(), kShadowResolution, *m_cbRing,
        ctx.lightData.farShadowCascadeCount,
        ctx.lightData.farShadowDistance,
        ctx.lightData.farShadowLodBias,
        ctx.shadowCacheEnabled,
        m_staticShadowAtlasSrv);
    ctx.totalCascadeCount = ctx.shadowFrame.totalCascadeCount;
    ctx.shadowAlloc = ctx.shadowFrame.cbAllocation;

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
            const float nearZ    = 0.1f;
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
        static constexpr float kPointShadowNear = 1.0f;

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

                ctx.shadowDataParams.localShadows[entryIdx] = sd;
            }

            totalLocalShadowEntries += kPointLightShadowFaces;
        }

        shadowCBPtr->localShadowCount = totalLocalShadowEntries;
        ctx.shadowDataParams.localShadowCount = totalLocalShadowEntries;

        static bool loggedLocalShadowOnce = false;
        if (!loggedLocalShadowOnce) {
            UZ_LOG(Renderer, Info,
                   "LocalShadow: {} spot + {} point light shadows allocated ({} tiles, atlas SRV={})",
                   ctx.lightData.spotShadowCount, ctx.lightData.pointShadowCount,
                   totalLocalShadowEntries, m_localShadowAtlas.GetSrvIndex());
            loggedLocalShadowOnce = true;
        }
    }

    // Resize shadow atlas if cascade count changed (e.g., far shadows toggled)
    m_shadowAtlas.Resize(m_device, ctx.totalCascadeCount);
}

} // namespace uz
