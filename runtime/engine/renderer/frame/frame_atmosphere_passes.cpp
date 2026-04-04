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

static auto* cv_VolFog = UZ_CVAR_BOOL("r.VolumetricFog", true, "Froxel volumetric fog");

namespace uz {

void SceneRenderer::RegisterAtmosphereFogCloudPasses(rg::RgBuilder& rgBuilder, FrameContext& ctx) {
    // ── Sky Atmosphere + Height Fog (HDR, before postprocess/tonemap) ────
    // Must run after deferred lighting but before bloom/TAA/tonemap so the
    // sky is included in the HDR scene color that postprocess operates on.
    // Atmosphere + fog render after deferred lighting, before postprocess/tonemap.

    if (m_shadowDebugMode == 1) return;

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
    atmoCtx.iblBaker            = m_iblBaker.get();
    atmoCtx.iblResult           = &m_iblResult;
    atmoCtx.brdfLutSrvIndex     = m_brdfLutSrvIndex;
    atmoCtx.envMapSampler       = m_envMapSampler.get();
    atmoCtx.skyIrradianceSH     = &m_skyIrradianceSH;
    atmoCtx.apVolume            = m_apVolume;
    atmoCtx.apVolumeSrv         = m_apVolumeSrv;
    atmoCtx.apVolumeUav         = m_apVolumeUav;
    atmoCtx.apVolumeRes         = ctx.apVolumeRes;  // Pre-imported by RegisterLightingPasses
    atmoCtx.invViewProjData     = reinterpret_cast<const float*>(&ctx.perFrame.invViewProj);
    atmoCtx.cameraPos           = reinterpret_cast<const Vec3*>(&ctx.perFrame.cameraPos);
    atmoCtx.depthSrvIndex       = m_gbuffer.depthSRV;
    atmoCtx.depthTexture        = m_gbuffer.depth;
    atmoCtx.hdriActive          = m_hdriActive;
    atmoCtx.hdriCubemapSrv      = m_hdriActive ? m_hdriCubemapSrv : UINT32_MAX;
    atmoCtx.hdriCubemapTex      = m_hdriCubemap;

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


    // ── Froxel Volumetric Fog (compute, applied before height fog) ─────
    // Only runs when a HeightFogComponent with enableVolumetricFog=true exists.
    // Without a component, no fog is rendered (matches UE5 — fog requires an actor).
    if (m_volFogPass && m_volFogPass->IsInitialized()
        && cv_VolFog->value > 0.5f && ctx.bindless && ctx.world) {

        // Find a HeightFogComponent with volumetric fog enabled
        bool foundVolFog = false;
        HeightFogComponent const* volFogComp = nullptr;
        {
            auto fogView = ctx.world->GetRegistry().view<HeightFogComponent>();
            for (auto enttEntity : fogView) {
                auto& fc = fogView.get<HeightFogComponent>(enttEntity);
                if (fc.enabled && fc.enableVolumetricFog) {
                    volFogComp = &fc;
                    foundVolFog = true;
                    break;
                }
            }
        }

        if (foundVolFog && volFogComp) {
            VolumetricFogCB fogCB{};
            std::memcpy(&fogCB.invViewProj, &ctx.perFrame.invViewProj, 64);
            fogCB.cameraPosX     = ctx.perFrame.cameraPos.x;
            fogCB.cameraPosY     = ctx.perFrame.cameraPos.y;
            fogCB.cameraPosZ     = ctx.perFrame.cameraPos.z;
            fogCB.nearZ          = ctx.perFrame.nearPlane;
            fogCB.sunDirX        = ctx.sunDir.x;
            fogCB.sunDirY        = ctx.sunDir.y;
            fogCB.sunDirZ        = ctx.sunDir.z;
            fogCB.sunIntensity   = ctx.sunColor.Length();
            fogCB.sunColorR      = ctx.sunColor.x;
            fogCB.sunColorG      = ctx.sunColor.y;
            fogCB.sunColorB      = ctx.sunColor.z;
            fogCB.temporalAlpha  = 0.05f;
            fogCB.depthSrvIdx    = m_gbuffer.depthSRV;
            fogCB.frameIndex     = ctx.frameIndex;

            // All fog params come from the component
            fogCB.globalDensity     = volFogComp->density;
            fogCB.heightFalloff     = volFogComp->heightFalloff;
            fogCB.heightOffset      = volFogComp->baseHeight;
            fogCB.albedoR           = volFogComp->volumetricFogAlbedo.x;
            fogCB.albedoG           = volFogComp->volumetricFogAlbedo.y;
            fogCB.albedoB           = volFogComp->volumetricFogAlbedo.z;
            fogCB.phaseG            = volFogComp->volumetricFogScatteringDistribution;
            fogCB.extinctionScale   = volFogComp->volumetricFogExtinctionScale;
            fogCB.emissiveR         = volFogComp->volumetricFogEmissive.x;
            fogCB.emissiveG         = volFogComp->volumetricFogEmissive.y;
            fogCB.emissiveB         = volFogComp->volumetricFogEmissive.z;
            fogCB.farZ              = volFogComp->volumetricFogDistance * 100.0f;
            fogCB.fogStartDistance   = volFogComp->volumetricFogStartDistance;
            fogCB.nearFadeInDistance = volFogComp->volumetricFogNearFadeInDistance;

            m_volFogPass->RegisterPasses(
                rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc,
                fogCB, *m_cbRing, ctx.pipelines,
                ctx.bindless, ctx.renderWidth, ctx.renderHeight);
        }
    }

    if (m_passInspector.IsPassEnabled("fog")) {
        AddHeightFogFromECS(rgBuilder, ctx.hdrRes, ctx.depthRes, ctx.perFrameAlloc,
                            ctx.world, m_cameraManager->GetActiveCamera(), ctx.pipelines, *m_cbRing,
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
            if (m_cloudOutputSrv != UINT32_MAX && m_cloudOutputReady && ctx.bindless)
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
                        UZ_LOG(Renderer, Info, "CloudComposite: executing — cloudSRV={}, depthSRV={}, bindless={}",
                               m_cloudOutputSrv, m_gbuffer.depthSRV, ctx.bindless);
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

                    if (ctx.bindless) {
                        // SM6.6 bindless: push descriptor indices via root constants.
                        const uint32_t rootConst[4] = { m_cloudOutputSrv, m_gbuffer.depthSRV, 0, 0 };
                        cmd->SetRootConstants(kSlotRootConstants, rootConst, 4);
                    } else {
                        // SM5 non-bindless: bind textures directly at register slots.
                        // t0 = cloud output, t1 = scene depth
                        if (m_cloudPass) {
                            cmd->SetFragmentTexture(0, m_cloudPass->GetCloudOutputTexture());
                        }
                        cmd->SetFragmentTexture(1, m_gbuffer.depth);
                    }
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

} // namespace uz
