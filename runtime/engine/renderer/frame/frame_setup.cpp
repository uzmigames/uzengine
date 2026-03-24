/// UzEngine Scene Renderer — Frame setup: camera, input, CB uploads, draw list,
/// instancing, and all per-frame state preparation.

#include "scene_renderer.h"

// Umbrella headers
#include "renderer_core.h"
#include "passes.h"
#include "subsystems.h"
#include "components.h"

// Individual includes needed by setup logic
#include "camera.h"
#include "fps_camera_controller.h"
#include "gpu_mesh.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "sh_projection.h"
#include "profiler.h"
#include "texture_loader.h"
#include "upload_context.h"
#include "vfs.h"
#include "world.h"
#include "material_interface.h"
#include "rhi.h"
#include "dynamic_resolution.h"
#include "input.h"
#include "platform_window.h"
#include "log.h"
#include "editor_grid.h"
#include "stats_overlay.h"
#include "halton.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

// ── CVars ────────────────────────────────────────────────────────────────
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
static auto* cv_TAA                = UZ_CVAR_BOOL("r.TAA", false, "TAA anti-aliasing");
static auto* cv_TAACurrentWeight   = UZ_CVAR_FLOAT("r.TAA.CurrentFrameWeight", 0.04f, "TAA current frame blend weight", 0.01f, 0.2f);
static auto* cv_TAAJitterSeqLen    = UZ_CVAR_INT("r.TAA.JitterSequenceLength", 8, "TAA Halton jitter sequence length", 1, 16);
static auto* cv_PBRMinRoughness    = UZ_CVAR_FLOAT("r.PBR.MinRoughness", 0.045f, "Minimum roughness clamp", 0.01f, 0.2f);
static auto* cv_DepthPrepass       = UZ_CVAR_BOOL("r.DepthPrepass", true, "Depth prepass before GBuffer");
static auto* cv_ShadowDebug        = UZ_CVAR_INT("r.ShadowDebug", 0, "Shadow debug grid mode (0=off, 1-8)", 0, 8);

namespace uz {

void SceneRenderer::SetupFrameData(FrameContext& ctx, rg::RgBuilder& rgBuilder) {
    UZ_PROFILE_FUNCTION();

    ctx.bindless = m_device->IsBindless();
    m_totalTime += ctx.dt;

    // ── Pass Inspector: reset per-frame target registrations ─────────────
    m_passInspector.BeginFrame();

    // ── Deferred StatsOverlay + fallback texture init (first frame only) ─
    if (!m_statsOverlayInitialized && m_statsOverlay) {
        UploadContext upload(m_device);
        if (m_statsOverlay->Init(m_device, ctx.cmdList, upload,
                m_shaderMgr->GetPipeline(), RHIFormat::RGBA8Unorm,
                m_width, m_height, ctx.bindless)) {
            m_statsOverlayInitialized = true;
        } else {
            UZ_LOG(Renderer, Error, "SceneRenderer: failed to init stats overlay");
        }

        // Upload 1x1 fallback textures for SM5 unconditional sampling.
        {
            const uint8_t whitePixel[4]  = {255, 255, 255, 255};
            const uint8_t blackPixel[4]  = {  0,   0,   0, 255}; // metallic=0 fallback
            const uint8_t normalPixel[4] = {128, 128, 255, 255}; // flat tangent-space normal

            upload.UploadTexture(ctx.cmdList, m_fallbackWhite,  whitePixel,  sizeof(whitePixel));
            upload.UploadTexture(ctx.cmdList, m_fallbackBlack,  blackPixel,  sizeof(blackPixel));
            upload.UploadTexture(ctx.cmdList, m_fallbackNormal, normalPixel, sizeof(normalPixel));
        }

        // Flush the upload commands
        ctx.cmdList->End();
        ctx.cmdList->Execute();
        m_device->WaitIdle();
        upload.ReleaseStaging();
        ctx.cmdList->Begin();
    }

    // ── Destroy GPU buffers deferred from previous frame ────────────────
    // Previous frame's fence has completed by now (Application::WaitForFrame),
    // so it's safe to destroy buffers that were referenced by GPU commands.
    for (auto& buf : m_deferredDestroyBuffers) {
        if (buf.IsValid()) {
            m_device->DestroyBuffer(buf);
        }
    }
    m_deferredDestroyBuffers.clear();

    // ── Flush deferred asset loads (queued from Lua callbacks) ─────────
    FlushPendingLoads(ctx.cmdList, ctx.vfs);

    // ── Apply CVar changes ─────────────────────────────────────────────
    ApplyCVars();

    // ── Dynamic Resolution update ────────────────────────────────────────
    // Uses CPU frame time as GPU time proxy until RHI exposes GPU timestamp queries.
    if (m_dynRes && m_dynRes->IsEnabled()) {
        const float newPct = m_dynRes->Update(ctx.dt);
        if (std::abs(newPct - m_screenPercentage) > 0.5f) {
            SetScreenPercentage(newPct);
        }
    }

    // ── CB Ring begin frame ─────────────────────────────────────────────
    m_cbRing->BeginFrame(ctx.frameIndex);

    // ── Stats begin frame ───────────────────────────────────────────────
    if (m_statsOverlayInitialized) {
        m_statsOverlay->BeginFrame(ctx.dt);
    }

    // ── Input ───────────────────────────────────────────────────────────
    m_input->BeginFrame();

    // ── Debug mode toggle: '0' = off, '1'-'7' = G-Buffer channels ──────
    if (m_input->IsKeyJustPressed(Key::Num0)) {
        m_debugMode = GBufferDebugMode::Count;
    }
    for (uint32_t k = 0; k < static_cast<uint32_t>(GBufferDebugMode::Count); ++k) {
        if (m_input->IsKeyJustPressed(static_cast<Key>(static_cast<uint32_t>(Key::Num1) + k))) {
            m_debugMode = static_cast<GBufferDebugMode>(k);
        }
    }

    // ── Render path toggle: F ───────────────────────────────────────────
    if (m_input->IsKeyJustPressed(Key::F)) {
        m_renderPath = (m_renderPath == RenderPath::Deferred)
            ? RenderPath::Forward : RenderPath::Deferred;
        UZ_LOG(Renderer, Info, "Render path switched to: {}",
               RenderPathToString(m_renderPath));
    }

    // ── Camera: prefer ECS CameraComponent, fallback to FPS controller ──
    bool cameraFromECS = false;
    if (ctx.world) {
        auto cameraView = ctx.world->View<CameraComponent>();
        for (auto [enttEntity, cam] : cameraView.each()) {
            if (cam.isActive) {
                // Sync ECS camera into our Camera object.
                // Camera::LookAt handles Z-up → Y-up conversion internally.
                const Vec3 camPos = cam.GetWorldPosition();
                m_camera->SetPosition(camPos);
                if (cam.hasLookTarget) {
                    m_camera->LookAt(cam.lookTarget);
                } else {
                    // Fallback: use rotation quaternion to derive forward direction.
                    // Note: FromEuler produces Y-up quaternions, so we convert via
                    // the same yaw/pitch extraction that FPSCameraController uses.
                    const Vec3 fwd = (cam.GetRotation() * Vec3::Forward()).Normalized();
                    m_camera->LookAt(camPos + fwd * 1000.f);  // 10m ahead in cm
                }
                m_camera->SetPerspective(
                    cam.fov * (3.14159265f / 180.f),
                    static_cast<float>(m_width) / static_cast<float>(m_height),
                    cam.nearPlane, cam.farPlane);
                cameraFromECS = true;
                break;
            }
        }
    }

    if (!cameraFromECS) {
        m_cameraCtrl->Update(*m_camera, *m_input, ctx.dt);
    }

    auto& camera = *m_camera;

    // ── TAA Jitter — Halton(2,3) sub-pixel offset on the projection matrix ──
    // TAA jitter: Halton(2,3) sub-pixel offset applied to the projection matrix.
    // Jitter = SampleXY * 2/Resolution, flipped Y for D3D convention.

    ctx.jitterX = 0.f;
    ctx.jitterY = 0.f;

    ctx.taaActive = m_taaEnabled
                 && m_taaHistory[0].IsValid()
                 && m_taaHistory[1].IsValid()
                 && m_device->IsBindless()
                 && m_passInspector.IsPassEnabled("taa");

    if (ctx.taaActive) {
        // 1-based index into Halton(2,3) sequence, cycled every 8 samples.
        // 8-sample Halton sequence (standard TAA jitter length).
        const int32_t jitterIdx = static_cast<int32_t>((ctx.frameIndex % kDefaultJitterSequenceLength) + 1);
        Halton23(jitterIdx, ctx.jitterX, ctx.jitterY);
    }

    // Store unjittered viewProj for reprojection and velocity computation.
    ctx.unjitteredViewProj = camera.GetViewProjection();

    // Apply jitter to the projection matrix as a sub-pixel translation in clip space.
    // The jitter is applied as an offset to the projection matrix's [2][0] and [2][1]
    // elements, which translates the clip-space output by a sub-pixel amount.
    // Uses RENDER resolution (not display) because the jitter is in pixel units of the
    // rendered image, which is the viewport the geometry is rasterized into.
    Mat4 jitteredViewProj = ctx.unjitteredViewProj;
    if (ctx.taaActive) {
        const float renderW = static_cast<float>(m_renderWidth);
        const float renderH = static_cast<float>(m_renderHeight);
        // Equivalent: SampleX * 2.0 / ViewRect.Width(), SampleY * -2.0 / ViewRect.Height()
        // The *2 converts from pixel units to NDC (-1 to 1 range = 2 units per viewport dim).
        // Y is negated because clip-space Y increases upward but pixel Y increases downward.
        jitteredViewProj.m.m[2][0] += ctx.jitterX * 2.0f / renderW;
        jitteredViewProj.m.m[2][1] += ctx.jitterY * -2.0f / renderH;
    }

    // ── Build PerFrameParams (scalar section byte-identical to PerFrameCB) ──
    auto& perFrame = ctx.perFrame;
    perFrame = PerFrameParams{};
    // Use the jittered viewProj for rendering (geometry pass, lighting).
    // The unjittered version is stored in m_prevViewProj for next frame's reprojection.
    perFrame.viewProj    = jitteredViewProj;
    perFrame.invViewProj = jitteredViewProj.Inverse();
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

    // ── Build draw list from ECS ────────────────────────────────────────
    ctx.drawList.Clear();
    SyncWorldToDrawList(ctx.world, ctx.vfs, ctx.cmdList, ctx.drawList);
    ctx.drawList.Sort();

    // ── GPU Instancing — batch identical mesh+material draw commands ────
    BuildInstanceBatches(ctx.drawList, ctx.instanceBatches, ctx.instanceDataVec);

    // Ensure instance buffer has enough capacity (recreate only when too small)
    const auto instanceCount = static_cast<uint32_t>(ctx.instanceDataVec.size());
    // Disable instancing when multiple mesh types are loaded — the instanced path
    // currently only supports homogeneous batches (all mesh[0]). Per-mesh batching
    // is future work. Fall back to per-draw-command path for multi-mesh scenes.
    ctx.useInstancing = ctx.bindless && instanceCount > 0 && m_meshes.size() <= 1;

    // Triple-buffered instance data: CPU writes to frame N's buffer while GPU
    // reads from frame N-2. No WaitIdle needed — fence sync in the app layer
    // guarantees frame N-2 is complete before we reach this point in frame N.
    const uint32_t ibSlot = ctx.frameIndex % kInstanceBufferCount;

    if (ctx.useInstancing && instanceCount > m_instanceBufferCapacity) {
        // Need bigger buffers — wait for GPU idle before destroying all slots
        m_device->WaitIdle();
        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            if (m_instanceBuffers[i].IsValid()) {
                m_device->DestroyBuffer(m_instanceBuffers[i]);
                m_instanceBuffers[i] = RHIBufferHandle{};
                m_instanceBufferSrvIndices[i] = UINT32_MAX;
            }
        }

        // Round up to next power of two for stable allocation
        uint32_t newCapacity = instanceCount;
        newCapacity |= (newCapacity >> 1);
        newCapacity |= (newCapacity >> 2);
        newCapacity |= (newCapacity >> 4);
        newCapacity |= (newCapacity >> 8);
        newCapacity |= (newCapacity >> 16);
        newCapacity++;
        if (newCapacity < 16) newCapacity = 16;

        RHIBufferDesc ibDesc{};
        ibDesc.sizeBytes = newCapacity * sizeof(InstanceData);
        ibDesc.stride    = sizeof(InstanceData);
        ibDesc.usage     = RHIBufferUsage::StructuredBuffer;
        ibDesc.memory    = RHIMemoryType::Upload;

        bool allCreated = true;
        for (uint32_t i = 0; i < kInstanceBufferCount; ++i) {
            ibDesc.debugName = (i == 0) ? "InstanceBuffer[0]"
                             : (i == 1) ? "InstanceBuffer[1]"
                                        : "InstanceBuffer[2]";
            m_instanceBuffers[i] = m_device->CreateBuffer(ibDesc);
            if (m_instanceBuffers[i].IsValid()) {
                m_instanceBufferSrvIndices[i] = m_device->GetBufferSRVIndex(m_instanceBuffers[i]);
            } else {
                allCreated = false;
            }
        }
        m_instanceBufferCapacity = allCreated ? newCapacity : 0;
    }

    // Upload instance data to the current frame's buffer (no GPU sync needed)
    ctx.instanceBufferSrvIndex = UINT32_MAX;
    if (ctx.useInstancing && m_instanceBuffers[ibSlot].IsValid()) {
        void* mapped = m_device->MapBuffer(m_instanceBuffers[ibSlot]);
        if (mapped) {
            std::memcpy(mapped, ctx.instanceDataVec.data(),
                        instanceCount * sizeof(InstanceData));
            m_device->UnmapBuffer(m_instanceBuffers[ibSlot]);
        }
        ctx.instanceBufferSrvIndex = m_instanceBufferSrvIndices[ibSlot];
    }

    // ── GPU Occlusion Culling — bounding sphere upload + cull constants ──
    ctx.gpuCullingActive = false;
    if (m_gpuOcclusionCullingEnabled && m_depthPrepassEnabled && instanceCount > 0 && ctx.bindless) {
        UZ_PROFILE_SCOPE("GpuCull_Setup");

        // Grow GPU cull buffers if instance count exceeds current capacity.
        if (instanceCount > m_gpuCullBufferCapacity) {
            m_device->WaitIdle();
            // Round up to next power of two for stable allocation.
            uint32_t newCap = instanceCount;
            newCap |= (newCap >> 1);
            newCap |= (newCap >> 2);
            newCap |= (newCap >> 4);
            newCap |= (newCap >> 8);
            newCap |= (newCap >> 16);
            newCap++;
            if (newCap < 256) newCap = 256;
            CreateGpuCullBuffers(newCap);
        }

        // Compute world-space bounding spheres from per-instance transforms + mesh AABB.
        // Each sphere packs as float4(center.xyz, radius).
        std::vector<Vec4> boundingSpheres(instanceCount);
        uint32_t batchIdx = 0;
        uint32_t batchInstanceBase = 0;
        for (uint32_t i = 0; i < instanceCount; ++i) {
            // Find which batch this instance belongs to (walk batches sequentially).
            while (batchIdx < ctx.instanceBatches.size()
                   && i >= batchInstanceBase + ctx.instanceBatches[batchIdx].instanceCount) {
                batchInstanceBase += ctx.instanceBatches[batchIdx].instanceCount;
                ++batchIdx;
            }

            const uint32_t meshId = (batchIdx < ctx.instanceBatches.size())
                ? ctx.instanceBatches[batchIdx].meshId : 0;

            // Object-space AABB center and radius.
            Vec3 bMin{}, bMax{};
            if (meshId < m_meshBoundsMin.size()) {
                bMin = m_meshBoundsMin[meshId];
                bMax = m_meshBoundsMax[meshId];
            }
            const Vec3 center_os{
                (bMin.x + bMax.x) * 0.5f,
                (bMin.y + bMax.y) * 0.5f,
                (bMin.z + bMax.z) * 0.5f
            };
            const Vec3 extent{
                bMax.x - center_os.x,
                bMax.y - center_os.y,
                bMax.z - center_os.z
            };
            const float radius_os = extent.Length();

            // Transform center to world space and extract max axis scale.
            const Mat4& world = ctx.instanceDataVec[i].world;
            // center_ws = world * float4(center_os, 1.0)
            const float cx = world.m.m[0][0] * center_os.x + world.m.m[1][0] * center_os.y + world.m.m[2][0] * center_os.z + world.m.m[3][0];
            const float cy = world.m.m[0][1] * center_os.x + world.m.m[1][1] * center_os.y + world.m.m[2][1] * center_os.z + world.m.m[3][1];
            const float cz = world.m.m[0][2] * center_os.x + world.m.m[1][2] * center_os.y + world.m.m[2][2] * center_os.z + world.m.m[3][2];

            // Max axis scale: largest column vector length of the 3x3 rotation/scale.
            const float scaleX = std::sqrt(world.m.m[0][0] * world.m.m[0][0] + world.m.m[0][1] * world.m.m[0][1] + world.m.m[0][2] * world.m.m[0][2]);
            const float scaleY = std::sqrt(world.m.m[1][0] * world.m.m[1][0] + world.m.m[1][1] * world.m.m[1][1] + world.m.m[1][2] * world.m.m[1][2]);
            const float scaleZ = std::sqrt(world.m.m[2][0] * world.m.m[2][0] + world.m.m[2][1] * world.m.m[2][1] + world.m.m[2][2] * world.m.m[2][2]);
            const float maxScale = std::max({scaleX, scaleY, scaleZ});

            boundingSpheres[i] = Vec4{cx, cy, cz, radius_os * maxScale};
        }

        // Upload bounding spheres to the current frame's buffer.
        if (m_boundingSphereBuffers[ibSlot].IsValid()) {
            void* mapped = m_device->MapBuffer(m_boundingSphereBuffers[ibSlot]);
            if (mapped) {
                std::memcpy(mapped, boundingSpheres.data(),
                            instanceCount * sizeof(Vec4));
                m_device->UnmapBuffer(m_boundingSphereBuffers[ibSlot]);
            }
        }

        // ── Phase 1 Cull Constants — use previous frame's HiZ ─────────────
        // Phase 1 tests against last frame's HiZ pyramid to identify objects
        // that are "definitely visible" (were visible last frame and still pass
        // frustum + occlusion). This is conservative: objects newly revealed
        // by camera movement may be incorrectly rejected.
        // If no previous HiZ exists (first frame), Phase 1 runs frustum-only.
        GpuCullConstants phase1Constants{};
        phase1Constants.viewProjection = camera.GetViewProjection();
        phase1Constants.viewMatrix     = camera.GetView();
        phase1Constants.viewToClip     = camera.GetProjection();
        phase1Constants.instanceCount  = instanceCount;
        phase1Constants.hizEnabled     = (m_prevHizMipCount > 0) ? 1u : 0u;
        if (m_prevHizMipCount > 0) {
            phase1Constants.hizWidth    = m_prevHizWidth;
            phase1Constants.hizHeight   = m_prevHizHeight;
            phase1Constants.hizMipCount = m_prevHizMipCount;
            for (uint32_t m = 0; m < m_prevHizMipCount && m < kGpuCullMaxHiZMips; ++m) {
                phase1Constants.hizMipSrvIndices[m] = m_prevHizMipSrvIndices[m];
            }
        }

        ctx.gpuCullCBAlloc = m_cbRing->Allocate(sizeof(GpuCullConstants));
        if (ctx.gpuCullCBAlloc.cpuPtr) {
            std::memcpy(ctx.gpuCullCBAlloc.cpuPtr, &phase1Constants, sizeof(GpuCullConstants));
        }

        // ── Phase 2 Cull Constants — use current frame's HiZ ─────────────
        // Phase 2 re-tests ALL instances against the updated HiZ pyramid built
        // from Phase 1 visible geometry. Objects that pass Phase 2 but failed
        // Phase 1 are "newly visible" (e.g., revealed by camera movement).
        // The GBuffer draws both Phase 1 and Phase 2 visible objects.
        GpuCullConstants phase2Constants{};
        phase2Constants.viewProjection = camera.GetViewProjection();
        phase2Constants.viewMatrix     = camera.GetView();
        phase2Constants.viewToClip     = camera.GetProjection();
        phase2Constants.instanceCount  = instanceCount;
        phase2Constants.hizEnabled     = (m_hizMipCount > 0) ? 1u : 0u;
        if (m_hizMipCount > 0) {
            phase2Constants.hizWidth    = m_hizMipLevels[0].width;
            phase2Constants.hizHeight   = m_hizMipLevels[0].height;
            phase2Constants.hizMipCount = m_hizMipCount;
            for (uint32_t m = 0; m < m_hizMipCount && m < kGpuCullMaxHiZMips; ++m) {
                phase2Constants.hizMipSrvIndices[m] = m_hizMipLevels[m].srvIndex;
            }
        }

        ctx.gpuCullCBAlloc2 = m_cbRing->Allocate(sizeof(GpuCullConstants));
        if (ctx.gpuCullCBAlloc2.cpuPtr) {
            std::memcpy(ctx.gpuCullCBAlloc2.cpuPtr, &phase2Constants, sizeof(GpuCullConstants));
        }

        // Populate FrameContext fields for downstream pass registration.
        ctx.boundingSphereSrvIndex = m_boundingSphereSrvIndices[ibSlot];
        ctx.visibleIndexUavIndex   = m_visibleIndexUavIndices[ibSlot];
        ctx.indirectArgsUavIndex   = m_indirectArgsUavIndices[ibSlot];
        ctx.visibleIndexBuffer     = m_visibleIndexBuffers[ibSlot];
        ctx.indirectArgsBuffer     = m_indirectArgsBuffers[ibSlot];

        // Phase 2 output buffers.
        ctx.visibleIndexUavIndex2  = m_visibleIndexUavIndices2[ibSlot];
        ctx.indirectArgsUavIndex2  = m_indirectArgsUavIndices2[ibSlot];
        ctx.visibleIndexBuffer2    = m_visibleIndexBuffers2[ibSlot];
        ctx.indirectArgsBuffer2    = m_indirectArgsBuffers2[ibSlot];

        // Two-phase is active when previous frame's HiZ data is available.
        ctx.twoPhaseOcclusion = (m_prevHizMipCount > 0);
        ctx.gpuCullingActive  = true;
    }

    // ── Resolve viewport dimensions ────────────────────────────────────
    ctx.vpWidth  = static_cast<float>(ctx.swapChain->GetWidth());
    ctx.vpHeight = static_cast<float>(ctx.swapChain->GetHeight());
    ctx.renderWidth  = static_cast<float>(m_renderWidth);
    ctx.renderHeight = static_cast<float>(m_renderHeight);
    ctx.pipelines = m_pipelines.get();

    // ── Import render graph resources ──────────────────────────────────
    RHITextureHandle backBuffer = ctx.swapChain->GetCurrentBackBuffer();
    ctx.bbRes       = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
    ctx.hdrRes      = rgBuilder.ImportTexture("HDR_SceneColor", m_hdrSceneColor, RHIResourceState::RenderTarget);
    ctx.albedoRes   = rgBuilder.ImportTexture("GBuf_Albedo",    m_gbuffer.albedo,     RHIResourceState::ShaderResource);
    ctx.normalRes   = rgBuilder.ImportTexture("GBuf_Normal",    m_gbuffer.normal,     RHIResourceState::ShaderResource);
    ctx.depthRes    = rgBuilder.ImportTexture("GBuf_Depth",     m_gbuffer.depth,      RHIResourceState::ShaderResource);
    ctx.positionRes = rgBuilder.ImportTexture("GBuf_Position",  m_gbuffer.positionWS, RHIResourceState::ShaderResource);
    ctx.emissiveRes = rgBuilder.ImportTexture("GBuf_Emissive",  m_gbuffer.emissive,   RHIResourceState::ShaderResource);
    ctx.velocityRes = rgBuilder.ImportTexture("GBuf_Velocity",  m_gbuffer.velocity,   RHIResourceState::ShaderResource);
    ctx.shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", m_shadowAtlas.GetTexture(), RHIResourceState::ShaderResource);

    // ── DBuffer resource imports (decal system) ──────────────────────────
    if (m_dbufferA.IsValid() && m_dbufferB.IsValid() && m_dbufferC.IsValid()) {
        ctx.dbufferARes = rgBuilder.ImportTexture("DBuffer_A", m_dbufferA, RHIResourceState::RenderTarget);
        ctx.dbufferBRes = rgBuilder.ImportTexture("DBuffer_B", m_dbufferB, RHIResourceState::RenderTarget);
        ctx.dbufferCRes = rgBuilder.ImportTexture("DBuffer_C", m_dbufferC, RHIResourceState::RenderTarget);
    }

    // ── Compute post-process activation flags ─────────────────────────
    ctx.bloomEnabled = m_bloomPass && m_bloomPass->IsInitialized()
                     && m_bloomPass->settings.enabled
                     && m_shadowDebugMode != 1
                     && ctx.bindless
                     && m_passInspector.IsPassEnabled("bloom");

    ctx.ssrActive = m_ssrEnabled
                  && m_ssrTraceOutput.IsValid()
                  && m_ssrResolvedOutput.IsValid()
                  && m_hdrHasContent
                  && m_hizMipCount > 0
                  && m_shadowDebugMode != 1
                  && m_passInspector.IsPassEnabled("ssr");

    // Gate motion blur on m_prevViewProjValid to avoid reading uninitialized velocity on frame 0.
    // The velocity pass is skipped on the first frame (no prevViewProj yet), so velocity data is garbage.
    ctx.motionBlurActive = m_motionBlurEnabled && m_prevViewProjValid
                         && m_motionBlurOutput.IsValid()
                         && m_motionBlurTileMax.IsValid() && m_motionBlurNeighborMax.IsValid()
                         && m_gbuffer.velocity.IsValid() && ctx.bindless && m_shadowDebugMode != 1
                         && m_passInspector.IsPassEnabled("motionblur");

    ctx.dofActive = m_dofEnabled && m_dofFocusDistance > 0.0f
                  && m_dofCocTexture.IsValid() && m_dofOutputTexture.IsValid()
                  && ctx.bindless && m_shadowDebugMode != 1
                  && m_passInspector.IsPassEnabled("dof");

    ctx.fxaaActive = !ctx.taaActive && ctx.bindless
                   && m_shadowDebugMode != 1
                   && m_fxaaLdrIntermediate.IsValid()
                   && m_fxaaLdrIntermediateSrv != UINT32_MAX
                   && m_passInspector.IsPassEnabled("fxaa");

    // CAS (Contrast Adaptive Sharpening) — runs after tonemap, before FXAA.
    // Enabled by default. Requires the LDR intermediate texture (shared with FXAA).
    ctx.casActive = m_casEnabled && ctx.bindless
                  && m_shadowDebugMode != 1
                  && m_fxaaLdrIntermediate.IsValid()
                  && m_fxaaLdrIntermediateSrv != UINT32_MAX
                  && m_passInspector.IsPassEnabled("cas");

    // Initialize SRV routing chain — each post-process pass may override these.
    ctx.taaSrvForTonemap = m_hdrSceneColorSrv;
    ctx.motionBlurSrvForTonemap = ctx.taaSrvForTonemap;
    ctx.postDofSrvForTonemap = ctx.motionBlurSrvForTonemap;

    // TAA ping-pong indices
    ctx.taaWriteIdx = m_taaHistoryIndex;
    ctx.taaReadIdx  = 1 - m_taaHistoryIndex;
}

} // namespace uz
