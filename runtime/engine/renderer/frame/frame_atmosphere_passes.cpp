/// UzEngine Scene Renderer — Atmosphere-related passes: sky atmosphere, height
/// fog, and volumetric cloud composite.
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
#include "world.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "atmosphere_renderer.h"
#include "atmosphere/volumetric_fog_pass.h"
#include "fog_renderer.h"
#include "height_fog_component.h"
#include "profiler.h"
#include "rhi.h"
#include "log.h"
#include "cvar.h"

#include <cstring>
#include <memory>

// Froxel volumetric fog (phase2_froxel-volumetric-fog): a VolumetricFogComponent (or a HeightFogComponent with
// enableVolumetricFog) fills a 160x90x64 grid with sun / point / spot in-scatter, shadowed by the cascaded and
// local shadow atlases, temporally filtered, integrated front to back and composited over the lit scene. Default
// off until the visual review; off, the passes do not run and the height fog is untouched.
static auto* cv_VolFog = UZ_CVAR_BOOL("r.VolumetricFog", false,
    "Froxel volumetric fog: sun + local lights in-scatter with volumetric shadows, temporally filtered (needs a VolumetricFogComponent or a HeightFogComponent with enable_volumetric_fog)");
static auto* cv_VolFogDebug = UZ_CVAR_INT("r.VolumetricFog.Debug", 0,
    "Froxel fog composite debug output (block B.4): 1 = (view depth cm, slice coordinate, transmittance), 2 = (raw depth, in-scatter r, g)", 0, 2);
static auto* cv_VolFogSunShadow = UZ_CVAR_BOOL("r.VolumetricFog.SunShadow", true,
    "Shadow the sun in-scatter of the froxel fog with the cascaded shadow maps (block B.4; 0 = unshadowed, for A/B runs)");
static auto* cv_VolFogLocalLights = UZ_CVAR_BOOL("r.VolumetricFog.LocalLights", true,
    "Inject the frame's point and spot lights into the froxel fog");
static auto* cv_VolFogLocalShadow = UZ_CVAR_BOOL("r.VolumetricFog.LocalShadow", true,
    "Shadow the local lights' in-scatter with the local shadow atlas (one tap per froxel)");
static auto* cv_VolFogTemporal = UZ_CVAR_BOOL("r.VolumetricFog.Temporal", true,
    "Temporal reprojection of the injected froxel volume (previous unjittered view-projection, EMA, neighbourhood clamp); 0 = each frame on its own");
static auto* cv_VolFogTemporalAlpha = UZ_CVAR_FLOAT("r.VolumetricFog.TemporalAlpha", 0.05f,
    "r.VolumetricFog.Temporal: weight of the new frame in the exponential moving average", 0.01f, 1.0f);
static auto* cv_VolFogTemporalClamp = UZ_CVAR_FLOAT("r.VolumetricFog.TemporalClamp", 1.5f,
    "r.VolumetricFog.Temporal: history clamp half-width in standard deviations of the current 3x3x3 neighbourhood (0 = no clamp; smaller follows moving lights closer)", 0.0f, 8.0f);
static auto* cv_VolFogJitter = UZ_CVAR_BOOL("r.VolumetricFog.Jitter", true,
    "r.VolumetricFog.Temporal: jitter the sample position inside each froxel per frame so the average integrates the froxel");
static auto* cv_VolFogSkyAmbient = UZ_CVAR_FLOAT("r.VolumetricFog.SkyAmbient", 1.0f,
    "Scale of the sky-light ambient in-scatter of the froxel fog (the sky irradiance SH L0 x the sky light intensity)", 0.0f, 10.0f);
static auto* cv_VolFogStage = UZ_CVAR_INT("r.VolumetricFog.Stage", 0,
    "Incremental verification stage of the froxel fog: 0 = full, 1 = solid checkerboard, 2 = density only (sky ambient), 3 = + sun and local lights (unshadowed), 4 = + volumetric shadows, 5 = + temporal filter (= full)", 0, 5);

namespace uz {

void SceneRenderer::RegisterCloudShadowPasses(rg::RgBuilder& builder, FrameContext& ctx) {
    if (!m_cloudPass) return;
    m_cloudPass->ResetShadowFrame();
    if (!CloudPass::GroundShadowsEnabled() || !ctx.bindless || !ctx.world
        || !m_showFlags.Get(ShowFlag::Clouds) || !m_cloudPass->IsInitialized()
        || !m_passInspector.IsPassEnabled("clouds")) return;
    auto clouds = ctx.world->GetRegistry().view<VolumetricCloudsComponent>();
    for (auto entity : clouds) {
        const auto& component = clouds.get<VolumetricCloudsComponent>(entity);
        if (!component.enabled) continue;
        CloudPassInput input{};
        input.layers = component.layers;
        input.layerCount = component.layerCount;
        input.useExplicitLayers = component.useExplicitLayers;
        input.weatherTexturePath = component.weatherTexturePath;
        input.cloudType = component.cloudType;
        const Mat4 inverse = ctx.unjitteredViewProj.Inverse();
        std::memcpy(input.invViewProj, inverse.data(), 64);
        input.cameraPos[0] = ctx.perFrame.cameraPos.x * 0.00001f;
        input.cameraPos[1] = ctx.perFrame.cameraPos.y * 0.00001f;
        input.cameraPos[2] = ctx.perFrame.cameraPos.z * 0.00001f;
        input.sunDir[0] = ctx.sunDir.x;
        input.sunDir[1] = ctx.sunDir.y;
        input.sunDir[2] = ctx.sunDir.z;
        input.time = m_totalTime;
        input.coverage = component.coverage;
        input.globalCoverage = component.coverage;
        input.density = component.density;
        input.extinction = component.extinction;
        input.layerBottomKm = component.layerBottomAltitudeKm;
        input.layerHeightKm = component.layerHeightKm;
        input.windVelocity[0] = component.windVelocity.x;
        input.windVelocity[1] = component.windVelocity.y;
        input.windVelocity[2] = component.windVelocity.z;
        input.detailWindScale = component.detailWindScale;
        input.primarySteps = component.primarySteps;
        input.shadowViewSampleCountScale = component.shadowViewSampleCountScale;
        input.minStepSize = component.minStepSize;
        input.maxStepSize = component.maxStepSize;
        input.jitterScale = component.jitterScale;
        input.frameIndex = ctx.frameIndex;
        input.shadowCascadeCount = component.shadowCascadeCount;
        input.shadowResolution = component.shadowResolution;
        input.shadowDistance = component.shadowDistance;
        m_cloudPass->SetVFS(ctx.vfs);
        m_cloudPass->RegisterShadowPasses(builder, m_rgStates.get(), ctx.cmdList, *m_cbRing, input);
        break;
    }
}

void SceneRenderer::RegisterAtmosphereFogCloudPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // ── Sky Atmosphere + Height Fog (HDR, before postprocess/tonemap) ────
    // Must run after deferred lighting but before bloom/TAA/tonemap so the
    // sky is included in the HDR scene color that postprocess operates on.
    // Atmosphere + fog render after deferred lighting, before postprocess/tonemap.

    if (m_shadowDebugMode == 1) return;

    AtmosphereFrameContext atmoCtx{};
    Vec3 cloudSunIlluminance{};
    atmoCtx.skySunIlluminance   = &cloudSunIlluminance;
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
    atmoCtx.scatteringLut3D        = m_scatteringLut3D;
    atmoCtx.scatteringLut3DSrv     = m_scatteringLut3DSrv;
    atmoCtx.scatteringLut3DUav     = m_scatteringLut3DUav;
    atmoCtx.scatteringLut3DDirty   = &m_scatteringLut3DDirty;
    atmoCtx.scatteringLut3DFilled  = &m_scatteringLut3DFilled;
    atmoCtx.deltaRayleigh3D        = m_deltaRayleigh3D;
    atmoCtx.deltaRayleigh3DSrv     = m_deltaRayleigh3DSrv;
    atmoCtx.deltaRayleigh3DUav     = m_deltaRayleigh3DUav;
    atmoCtx.deltaMie3D             = m_deltaMie3D;
    atmoCtx.deltaMie3DSrv          = m_deltaMie3DSrv;
    atmoCtx.deltaMie3DUav          = m_deltaMie3DUav;
    atmoCtx.scatteringDensity3D    = m_scatteringDensity3D;
    atmoCtx.scatteringDensity3DSrv = m_scatteringDensity3DSrv;
    atmoCtx.scatteringDensity3DUav = m_scatteringDensity3DUav;
    atmoCtx.irradianceLut          = m_irradianceLut;
    atmoCtx.irradianceLutSrv       = m_irradianceLutSrv;
    atmoCtx.irradianceLutUav       = m_irradianceLutUav;
    atmoCtx.deltaIrradiance        = m_deltaIrradiance;
    atmoCtx.deltaIrradianceSrv     = m_deltaIrradianceSrv;
    atmoCtx.deltaIrradianceUav     = m_deltaIrradianceUav;
    atmoCtx.skyCubemap          = m_skyCubemap;
    atmoCtx.skyCubemapSrv       = m_skyCubemapSrv;
    atmoCtx.skyCubemapUav       = m_skyCubemapUav;
    atmoCtx.skyIBLDirty         = &m_skyIBLDirty;
    atmoCtx.skyCubemapFilled    = &m_skyCubemapFilled;
    static_assert(sizeof(m_skyBakeSignature) / sizeof(float) == kSkyBakeSignatureValues + 1,
                  "m_skyBakeSignature holds the compared values plus the pass counter");
    atmoCtx.skyBakeSignature    = m_skyBakeSignature;
    atmoCtx.iblBaker            = m_iblBaker.get();
    atmoCtx.iblResult           = &m_iblResult;
    atmoCtx.brdfLutSrvIndex     = m_brdfLutSrvIndex;
    atmoCtx.envMapSampler       = m_envMapSampler.get();
    atmoCtx.skyIrradianceSH     = &m_skyIrradianceSH;
    atmoCtx.apVolume            = m_apVolume;
    atmoCtx.apVolumeSrv         = m_apVolumeSrv;
    atmoCtx.apVolumeUav         = m_apVolumeUav;
    atmoCtx.apVolumeFilled      = &m_apVolumeFilled;
    atmoCtx.apVolumeRes         = ctx.apVolumeRes;  // Pre-imported by RegisterLightingPasses
    atmoCtx.invViewProjData     = reinterpret_cast<const float*>(&ctx.perFrame.invViewProj);
    atmoCtx.cameraPos           = reinterpret_cast<const Vec3*>(&ctx.perFrame.cameraPos);
    atmoCtx.depthSrvIndex       = m_gbuffer.depthSRV;
    atmoCtx.depthTexture        = m_gbuffer.depth;
    atmoCtx.hdriActive          = m_hdriActive;
    atmoCtx.hdriCubemapSrv      = m_hdriActive ? m_hdriCubemapSrv : UINT32_MAX;
    atmoCtx.hdriCubemapTex      = m_hdriCubemap;

    if (m_showFlags.Get(ShowFlag::Atmosphere)) {
        AddAtmospherePasses(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc, atmoCtx);
    }

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


    // ── Froxel Volumetric Fog (phase2_froxel-volumetric-fog) ───────────────
    // The medium comes from a VolumetricFogComponent, else from a HeightFogComponent with
    // enableVolumetricFog (the pre-existing authoring). Without either, no froxel fog is rendered
    // (matches UE5 — fog requires an actor). The parameters are resolved here, the passes are
    // registered AFTER the height fog below: the height fog then starts at the grid's far edge
    // (no double application) and the composite blends the near froxel fog over it.
    VolumetricFogCB fogCB{};
    bool  volFogRuns = false;
    float volFogDistance = 0.f;   // > 0: the froxel grid covers [start, volFogDistance] this frame
    if (m_showFlags.Get(ShowFlag::Volumetrics) && m_volFogPass && m_volFogPass->IsInitialized()
        && cv_VolFog->value > 0.5f && ctx.bindless && ctx.world) {

        float ambientIntensity = 1.0f;
        float localLightScattering = 1.0f;
        {
            auto volView = ctx.world->GetRegistry().view<VolumetricFogComponent>();
            for (auto enttEntity : volView) {
                const auto& vc = volView.get<VolumetricFogComponent>(enttEntity);
                if (!vc.enabled) continue;
                fogCB.globalDensity      = vc.density * vc.globalDensity;
                fogCB.heightFalloff      = vc.heightFalloff;
                fogCB.heightOffset       = vc.heightOffset;
                fogCB.albedoR            = vc.albedo.x;
                fogCB.albedoG            = vc.albedo.y;
                fogCB.albedoB            = vc.albedo.z;
                fogCB.phaseG             = vc.phaseG;
                fogCB.extinctionScale    = vc.extinctionScale;
                fogCB.emissiveR          = vc.emissive.x;
                fogCB.emissiveG          = vc.emissive.y;
                fogCB.emissiveB          = vc.emissive.z;
                fogCB.farZ               = vc.distance;
                fogCB.fogStartDistance   = vc.startDistance;
                fogCB.nearFadeInDistance = vc.nearFadeInDistance;
                ambientIntensity         = vc.ambientIntensity;
                localLightScattering     = vc.localLightScattering;
                volFogRuns = true;
                break;
            }
        }
        if (!volFogRuns) {
            // Legacy authoring: the volumetric section of a HeightFogComponent.
            auto fogView = ctx.world->GetRegistry().view<HeightFogComponent>();
            for (auto enttEntity : fogView) {
                const auto& fc = fogView.get<HeightFogComponent>(enttEntity);
                if (!fc.enabled || !fc.enableVolumetricFog) continue;
                fogCB.globalDensity      = fc.density;
                fogCB.heightFalloff      = fc.heightFalloff;
                fogCB.heightOffset       = fc.baseHeight;
                fogCB.albedoR            = fc.volumetricFogAlbedo.x;
                fogCB.albedoG            = fc.volumetricFogAlbedo.y;
                fogCB.albedoB            = fc.volumetricFogAlbedo.z;
                fogCB.phaseG             = fc.volumetricFogScatteringDistribution;
                fogCB.extinctionScale    = fc.volumetricFogExtinctionScale;
                fogCB.emissiveR          = fc.volumetricFogEmissive.x;
                fogCB.emissiveG          = fc.volumetricFogEmissive.y;
                fogCB.emissiveB          = fc.volumetricFogEmissive.z;
                fogCB.farZ               = fc.volumetricFogDistance;   // cm, as the component documents
                fogCB.fogStartDistance   = fc.volumetricFogStartDistance;
                fogCB.nearFadeInDistance = fc.volumetricFogNearFadeInDistance;
                volFogRuns = true;
                break;
            }
        }

        if (volFogRuns) {
            // Unjittered: the froxel positions must not move with the TAA jitter (block B.4).
            const Mat4 unjitteredInvViewProj = ctx.unjitteredViewProj.Inverse();
            std::memcpy(&fogCB.invViewProj, &unjitteredInvViewProj, 64);
            std::memcpy(&fogCB.prevViewProj, &ctx.temporal.prevViewProj, 64);
            fogCB.cameraPosX     = ctx.perFrame.cameraPos.x;
            fogCB.cameraPosY     = ctx.perFrame.cameraPos.y;
            fogCB.cameraPosZ     = ctx.perFrame.cameraPos.z;
            fogCB.nearZ          = ctx.perFrame.nearPlane;
            fogCB.sunDirX        = ctx.sunDir.x;
            fogCB.sunDirY        = ctx.sunDir.y;
            fogCB.sunDirZ        = ctx.sunDir.z;
            // The colour already carries the intensity (light_renderer.cpp); before block B.4 the shader
            // multiplied it by its own length again.
            fogCB.sunIntensity   = 1.0f;
            fogCB.cameraFarZ     = ctx.perFrame.farPlane;
            fogCB.sunShadowEnabled = cv_VolFogSunShadow->value > 0.5f ? 1u : 0u;
            fogCB.debugMode        = cv_VolFogDebug->value > 0.5f ? static_cast<uint32_t>(cv_VolFogDebug->value + 0.5f) : 0u;
            fogCB.sunColorR      = ctx.sunColor.x;
            fogCB.sunColorG      = ctx.sunColor.y;
            fogCB.sunColorB      = ctx.sunColor.z;
            fogCB.temporalAlpha  = cv_VolFogTemporalAlpha->value;
            fogCB.temporalClamp  = cv_VolFogTemporalClamp->value;
            fogCB.depthSrvIdx    = m_gbuffer.depthSRV;
            fogCB.frameIndex     = ctx.frameIndex;
            // Sky ambient: the same SH L0 sky colour the height fog injects, so the two agree at the seam.
            const float ambientScale = cv_VolFogSkyAmbient->value * ambientIntensity;
            fogCB.ambientR = skyFogR * ambientScale;
            fogCB.ambientG = skyFogG * ambientScale;
            fogCB.ambientB = skyFogB * ambientScale;
            fogCB.localLightScale    = localLightScattering;
            fogCB.debugStage         = static_cast<uint32_t>(cv_VolFogStage->value + 0.5f);
            fogCB.localLightsEnabled = cv_VolFogLocalLights->value > 0.5f ? 1u : 0u;
            fogCB.localShadowEnabled = cv_VolFogLocalShadow->value > 0.5f ? 1u : 0u;
            fogCB.jitterEnabled      = cv_VolFogJitter->value > 0.5f ? 1u : 0u;
            volFogDistance = fogCB.farZ;
        }
    }
    if (!volFogRuns && m_volFogPass) m_volFogPass->InvalidateHistory();

    if (m_showFlags.Get(ShowFlag::Fog) && m_passInspector.IsPassEnabled("fog")) {
        AddHeightFogFromECS(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc,
                            ctx.world, m_cameraManager->GetActiveCamera(), ctx.pipelines, *m_cbRing,
                            m_gbuffer.depthSRV, m_gbuffer.depth,
                            ctx.bindless, ctx.renderWidth, ctx.renderHeight,
                            skyFogR, skyFogG, skyFogB, volFogDistance);
    }

    if (volFogRuns) {
        VolumetricFogFrameInputs volIn{};
        volIn.hdrRes              = ctx.hdrRes;
        volIn.depthRes            = ctx.depthRes;
        volIn.shadowAtlasRes      = ctx.shadowAtlasRes;
        volIn.localShadowAtlasRes = ctx.localShadowAtlasRes;
        volIn.perFrameAlloc       = ctx.perFrameAlloc;
        volIn.shadowAlloc         = ctx.shadowAlloc;
        volIn.lightingAlloc       = ctx.lightingAlloc;
        volIn.bindless            = ctx.bindless;
        volIn.vpWidth             = ctx.renderWidth;
        volIn.vpHeight            = ctx.renderHeight;
        // Stages 1-4 of the incremental verification run each frame on its own; 0 and 5 are the full pipeline.
        volIn.temporalWanted  = cv_VolFogTemporal->value > 0.5f && (fogCB.debugStage == 0u || fogCB.debugStage >= 5u);
        volIn.historyReusable = ctx.temporal.matrixValid;
        m_volFogPass->RegisterPasses(rgBuilder, volIn, fogCB, *m_cbRing, ctx.pipelines);
    }

    // ── Volumetric Clouds (compute, composited over HDR) ─────────────
    if (m_showFlags.Get(ShowFlag::Clouds) && m_cloudPass && m_cloudPass->IsInitialized()
        && m_passInspector.IsPassEnabled("clouds") && ctx.world) {

        auto cloudView = ctx.world->GetRegistry().view<VolumetricCloudsComponent>();
        for (auto enttEntity : cloudView) {
            auto& cloudComp = cloudView.get<VolumetricCloudsComponent>(enttEntity);
            if (!cloudComp.enabled) continue;

            // Fill CloudPassInput from component + frame data.
            // Use UNJITTERED invViewProj — jittered matrix causes cloud noise
            // to shift every frame (TSR subpixel jitter), creating false movement.
            CloudPassInput cloudInput{};
            // Clouds need atmospheric transport even when geometry AP is disabled.
            // Copy the existing per-frame contract only for this cloud composite;
            // never change the shipped geometry/sky constants or their global gate.
            PerFrameCB cloudPerFrame = ctx.perFrame;
            cloudPerFrame.lightColor = {cloudSunIlluminance.x, cloudSunIlluminance.y,
                                        cloudSunIlluminance.z, 0.0f};
            cloudPerFrame.aerialPerspectiveEnabled = 0;
            if (m_showFlags.Get(ShowFlag::Atmosphere) && !m_hdriActive
                && m_transmittanceLutFilled && m_transmittanceLutSrv != UINT32_MAX) {
                auto skyView = ctx.world->GetRegistry().view<SkyAtmosphereComponent>();
                for (auto skyEntity : skyView) {
                    const auto& sky = skyView.get<SkyAtmosphereComponent>(skyEntity);
                    if (!sky.enabled) continue;
                    cloudPerFrame.aerialPerspectiveEnabled = 1;
                    cloudPerFrame.transmittanceLutSrvIndex = m_transmittanceLutSrv;
                    cloudPerFrame.apBottomRadiusKm = sky.bottomRadiusKm;
                    cloudPerFrame.apTopRadiusKm = sky.bottomRadiusKm + sky.atmosphereHeightKm;
                    cloudPerFrame.apRayleighBetaX = sky.rayleighScattering.x;
                    cloudPerFrame.apRayleighBetaY = sky.rayleighScattering.y;
                    cloudPerFrame.apRayleighBetaZ = sky.rayleighScattering.z;
                    cloudPerFrame.apRayleighScaleH = sky.rayleighScaleHeightKm;
                    cloudPerFrame.apMieBeta = sky.mieScattering;
                    cloudPerFrame.apMieScaleH = sky.mieScaleHeightKm;
                    cloudPerFrame.apMieAnisotropy = sky.mieAnisotropy;
                    cloudPerFrame.apRayleighScale = sky.rayleighScatteringScale;
                    cloudPerFrame.apMieScale = sky.mieScatteringScale;
                    break;
                }
            }
            const auto cloudFrameAlloc = m_cbRing->Allocate(sizeof(PerFrameCB));
            std::memcpy(cloudFrameAlloc.cpuPtr, &cloudPerFrame, sizeof(PerFrameCB));
            cloudInput.layers = cloudComp.layers;
            cloudInput.layerCount = cloudComp.layerCount;
            cloudInput.useExplicitLayers = cloudComp.useExplicitLayers;
            cloudInput.weatherTexturePath = cloudComp.weatherTexturePath;
            Mat4 unjitteredInvVP = ctx.unjitteredViewProj.Inverse();
            std::memcpy(cloudInput.invViewProj, &unjitteredInvVP, 64);

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
            cloudInput.powderScale       = cloudComp.powderScale;
            cloudInput.powderExponent    = cloudComp.powderExponent;
            cloudInput.multiScatterOctaves = cloudComp.multiScatterOctaves;
            cloudInput.primarySteps      = cloudComp.primarySteps;
            cloudInput.minStepSize       = cloudComp.minStepSize;
            cloudInput.maxStepSize       = cloudComp.maxStepSize;
            cloudInput.perspectiveStepScale = cloudComp.perspectiveStepScale;
            cloudInput.jitterScale       = cloudComp.jitterScale;
            cloudInput.frameIndex        = ctx.frameIndex;
            cloudInput.lightSteps        = cloudComp.lightSteps;
            cloudInput.depthSrv          = m_gbuffer.depthSRV;
            cloudInput.velocitySrv       = m_gbuffer.velocitySRV;
            cloudInput.velocityRes       = ctx.velocityRes;
            cloudInput.transmittanceLut  = m_transmittanceLut;
            cloudInput.aerialPerspectiveAvailable = cloudPerFrame.aerialPerspectiveEnabled != 0;
            cloudInput.globalCoverage    = cloudComp.coverage;

            // ── Additional tracing/quality parameters from VolumetricCloudsComponent ──
            cloudInput.cloudType = cloudComp.cloudType;
            cloudInput.tracingMaxDistanceKm = cloudComp.tracingMaxDistanceKm;
            cloudInput.viewSampleCountScale = cloudComp.viewSampleCountScale;
            cloudInput.reflectionViewSampleCountScale = cloudComp.reflectionViewSampleCountScale;
            cloudInput.shadowViewSampleCountScale = cloudComp.shadowViewSampleCountScale;
            cloudInput.shadowReflectionViewSampleCountScale = cloudComp.shadowReflectionViewSampleCountScale;
            cloudInput.shadowTracingDistance = cloudComp.shadowTracingDistance;
            cloudInput.stopTracingTransmittanceThreshold = cloudComp.stopTracingTransmittanceThreshold;
            cloudInput.aerialPerspectiveRayleighScatteringStartDistance = cloudComp.aerialPerspectiveRayleighScatteringStartDistance;
            cloudInput.aerialPerspectiveRayleighScatteringFadeDistance = cloudComp.aerialPerspectiveRayleighScatteringFadeDistance;
            cloudInput.aerialPerspectiveMieScatteringStartDistance = cloudComp.aerialPerspectiveMieScatteringStartDistance;
            cloudInput.aerialPerspectiveMieScatteringFadeDistance = cloudComp.aerialPerspectiveMieScatteringFadeDistance;
            cloudInput.skyLightCloudBottomOcclusion = cloudComp.skyLightCloudBottomOcclusion;
            cloudInput.tracingMaxDistanceMode = static_cast<uint32_t>(cloudComp.tracingMaxDistanceMode);
            cloudInput.tracingStartMaxDistance = cloudComp.tracingStartMaxDistance;
            cloudInput.tracingStartDistanceFromCamera = cloudComp.tracingStartDistanceFromCamera;

            m_cloudPass->SetVFS(ctx.vfs);
            m_cloudPass->RegisterPasses(rgBuilder, m_rgStates.get(), ctx.cmdList,
                ctx.hdrRes, ctx.depthRes, cloudFrameAlloc, *m_cbRing, ctx.pipelines,
                ctx.bindless, ctx.renderWidth, ctx.renderHeight, cloudInput);

            break;  // First VolumetricCloudsComponent only (singleton semantics)
        }
    }
}

} // namespace uz
