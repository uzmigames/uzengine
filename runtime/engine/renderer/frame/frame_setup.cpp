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

#include <algorithm>
#include <cmath>
#include <cstring>

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
    // @source UE5 SceneVisibility.cpp:5240-5243 — TemporalJitterPixels
    // @source UE5 SceneVisibility.cpp:5243 — HackAddTemporalAAProjectionJitter(SampleX * 2/W, SampleY * -2/H)
    // @source UE5 Math/Halton.h:9 — Halton(Index, Base) radical-inverse sequence

    ctx.jitterX = 0.f;
    ctx.jitterY = 0.f;

    ctx.taaActive = m_taaEnabled
                 && m_taaHistory[0].IsValid()
                 && m_taaHistory[1].IsValid()
                 && m_device->IsBindless();  // TAA compute requires bindless

    if (ctx.taaActive) {
        // 1-based index into Halton(2,3) sequence, cycled every 8 samples.
        // @source UE5 SceneRendering.cpp:974 — TemporalJitterSequenceLength
        const int32_t jitterIdx = static_cast<int32_t>((ctx.frameIndex % kDefaultJitterSequenceLength) + 1);
        Halton23(jitterIdx, ctx.jitterX, ctx.jitterY);
    }

    // Store unjittered viewProj for reprojection and velocity computation.
    ctx.unjitteredViewProj = camera.GetViewProjection();

    // Apply jitter to the projection matrix as a sub-pixel translation in clip space.
    // @source UE5 SceneVisibility.cpp:5243 — HackAddTemporalAAProjectionJitter
    // The jitter is applied as an offset to the projection matrix's [2][0] and [2][1]
    // elements, which translates the clip-space output by a sub-pixel amount.
    // Uses RENDER resolution (not display) because the jitter is in pixel units of the
    // rendered image, which is the viewport the geometry is rasterized into.
    Mat4 jitteredViewProj = ctx.unjitteredViewProj;
    if (ctx.taaActive) {
        const float renderW = static_cast<float>(m_renderWidth);
        const float renderH = static_cast<float>(m_renderHeight);
        // UE5: SampleX * 2.0 / ViewRect.Width(), SampleY * -2.0 / ViewRect.Height()
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
    perFrame.prefilteredMipLevels = 5;

    // ── Extract SkyLight from SkyLightComponent (UE5 parity) ──────────
    // Must be done BEFORE SH upload so intensity can be baked into the SH coefficients.
    // @source UE5 SkyLightComponent.cpp:308 — MinOcclusion default = 0.0
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

    // Upload SH coefficients for sky diffuse irradiance (UE5 parity).
    // The shader's deferred_lighting_ps.hlsl line 881 already multiplies
    // by skyLightIntensity, so we only scale SH by indirectLightingIntensity
    // here (which provides separate control of diffuse fill vs specular).
    // @source UE5 SkyLightComponent.cpp — IndirectLightingIntensity separate from Intensity
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
    // In UE5 packed format: coeffs[0].w = R_L0, coeffs[1].w = G_L0, coeffs[2].w = B_L0
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
        perFrame.exposure = autoExp;
        // Log only on first adaptation (avoid per-frame spam)
        static bool loggedOnce = false;
        if (!loggedOnce) {
            UZ_LOG(Renderer, Info, "AutoExposure: initial exposure={:.4f}", perFrame.exposure);
            loggedOnce = true;
        }
    } else {
        // Without auto-exposure, use neutral exposure.
        // @source UE5 — default exposure = 1.0 (no bias applied)
        perFrame.exposure = 1.0f;
    }

    // ── Mip bias for screen percentage texture LOD compensation ────────
    // @source UE5 ViewUniformShaderParameters.h — MaterialTextureMipBias
    // When rendering at lower internal resolution, textures are over-minified
    // because the screen pixel density is lower than the display. A negative
    // mip bias shifts sampling toward finer mip levels to compensate.
    perFrame.mipBias = std::log2f(m_screenPercentage / 100.f);

    // ── GTAO SRV index (set early — actual compute runs in render graph) ──
    if (m_gtaoPass && m_gtaoPass->IsInitialized() && m_gtaoPass->settings.enabled) {
        perFrame.gtaoSrvIndex = m_gtaoPass->GetFilteredAOSrvIndex();
    } else {
        perFrame.gtaoSrvIndex = UINT32_MAX;
    }

    perFrame.shadowDebugMode = m_shadowDebugMode;
    perFrame.brdfDebugMode   = m_brdfDebugMode;

    // ── SSR SRV index (set early — actual compute runs in render graph) ──
    // The resolved SSR texture SRV is set here so the lighting shader can
    // sample it. When SSR is disabled or resources aren't ready, UINT32_MAX
    // tells the shader to skip SSR sampling.
    if (m_ssrEnabled && m_ssrResolvedOutput.IsValid()) {
        perFrame.ssrSrvIndex = m_ssrResolvedOutputSrv;
    } else {
        perFrame.ssrSrvIndex = UINT32_MAX;
    }

    // ── SSGI SRV indices (set early — actual compute runs in render graph) ──
    // @source UE5 IndirectLightRendering.cpp:1454 — DiffuseIndirect composite
    if (m_ssgiRenderer.IsReady() && m_device->IsBindless()) {
        perFrame.ssgiSrvIndex   = m_ssgiRenderer.GetResultSrvIndex();
        perFrame.ssgiAOSrvIndex = m_ssgiRenderer.GetAOResultSrvIndex();
        perFrame.ssgiEnabled    = 1;
    } else {
        perFrame.ssgiSrvIndex   = UINT32_MAX;
        perFrame.ssgiAOSrvIndex = UINT32_MAX;
        perFrame.ssgiEnabled    = 0;
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
            }
            // @source UE5 SceneRendering.cpp:1440 — SkyAtmosphereHeightFogContribution
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
    // @source UE5 SpotLightComponent.cpp:119-138 — GetWholeSceneProjectedShadowInitializer
    //   MinLightW = 0.1f (near plane), MaxDistanceToCastInLightW = Radius (far plane)
    //   Scales = (InvTanOuterCone, InvTanOuterCone) — perspective FOV from cone angle
    // @source UE5 ShadowSetup.cpp:960-1048 — SetupWholeSceneProjection
    // @source UE5 ShadowRendering.cpp:2500-2600 — point light cubemap shadow setup
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
            // Near = 0.1 cm (UE5 MinLightW), Far = radius (attenuation distance).
            // @source UE5 SpotLightComponent.cpp:134 — MinLightW = 0.1f
            // @source UE5 SpotLightComponent.cpp:135 — MaxDistanceToCastInLightW = Radius
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
        // @source UE5 ShadowRendering.cpp:2500-2600 — point light cubemap face setup
        // @source UE5 ShadowProjectionCommon.ush:132-147 — CalcCubemapVectorAndFaceIndex
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
        // @source UE5 ShadowRendering.cpp:2510 — FOV = PI/2 (90 degrees)
        static constexpr float kCubeFovDeg = 90.0f;
        // Near plane for point light shadows (1.0 cm).
        // @source UE5 PointLightComponent.cpp:78 — MinLightW for point lights
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

    // ── Compute post-process activation flags ─────────────────────────
    ctx.bloomEnabled = m_bloomPass && m_bloomPass->IsInitialized()
                     && m_bloomPass->settings.enabled
                     && m_shadowDebugMode == 0
                     && ctx.bindless;

    ctx.ssrActive = m_ssrEnabled
                  && m_ssrTraceOutput.IsValid()
                  && m_ssrResolvedOutput.IsValid()
                  && m_hdrHasContent
                  && m_hizMipCount > 0
                  && m_shadowDebugMode == 0;

    // Gate motion blur on m_prevViewProjValid to avoid reading uninitialized velocity on frame 0.
    // The velocity pass is skipped on the first frame (no prevViewProj yet), so velocity data is garbage.
    ctx.motionBlurActive = m_motionBlurEnabled && m_prevViewProjValid
                         && m_motionBlurOutput.IsValid()
                         && m_motionBlurTileMax.IsValid() && m_motionBlurNeighborMax.IsValid()
                         && m_gbuffer.velocity.IsValid() && ctx.bindless && m_shadowDebugMode == 0;

    ctx.dofActive = m_dofEnabled && m_dofFocusDistance > 0.0f
                  && m_dofCocTexture.IsValid() && m_dofOutputTexture.IsValid()
                  && ctx.bindless && m_shadowDebugMode == 0;

    ctx.fxaaActive = !ctx.taaActive && ctx.bindless
                   && m_shadowDebugMode == 0
                   && m_fxaaLdrIntermediate.IsValid()
                   && m_fxaaLdrIntermediateSrv != UINT32_MAX;

    // CAS (Contrast Adaptive Sharpening) — runs after tonemap, before FXAA.
    // Enabled by default. Requires the LDR intermediate texture (shared with FXAA).
    ctx.casActive = m_casEnabled && ctx.bindless
                  && m_shadowDebugMode == 0
                  && m_fxaaLdrIntermediate.IsValid()
                  && m_fxaaLdrIntermediateSrv != UINT32_MAX;

    // Initialize SRV routing chain — each post-process pass may override these.
    ctx.taaSrvForTonemap = m_hdrSceneColorSrv;
    ctx.motionBlurSrvForTonemap = ctx.taaSrvForTonemap;
    ctx.postDofSrvForTonemap = ctx.motionBlurSrvForTonemap;

    // TAA ping-pong indices
    ctx.taaWriteIdx = m_taaHistoryIndex;
    ctx.taaReadIdx  = 1 - m_taaHistoryIndex;
}

} // namespace uz
