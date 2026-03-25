/// UzEngine Atmosphere Renderer — Implementation.
///
/// Registers sky atmosphere render graph passes and handles post-frame
/// IBL bake + SH projection.

#include "atmosphere_renderer.h"

#include "world.h"
#include "camera.h"
#include "pipeline_library.h"
#include "render_graph_builder.h"
#include "sky_atmosphere_component.h"
#include "sky_atmosphere_pass.h"
#include "transmittance_lut_pass.h"
#include "aerial_perspective_volume_pass.h"
#include "sky_cubemap_capture_pass.h"
#include "sky_cubemap_capture_ps_pass.h"
#include "hdri_skybox_pass.h"
#include "light_component.h"
#include "color_utils.h"
#include "ibl_baker.h"
#include "sh_projection.h"
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_AerialPerspective = UZ_CVAR_BOOL("r.AerialPerspective", true, "Aerial perspective");

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// AddAtmospherePasses
// ─────────────────────────────────────────────────────────────────────────────

void AddAtmospherePasses(
    rg::RgBuilder& rgBuilder,
    rg::RgResourceId hdrRes,
    rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc,
    const AtmosphereFrameContext& ctx)
{
    if (!ctx.world) return;

    // ── HDRI skybox path — replaces procedural sky entirely ─────────────
    // When an external HDRI environment map is active, skip all procedural
    // atmosphere passes (transmittance LUT, multi-scatter LUT, sky cubemap
    // capture, aerial perspective) and render the HDRI cubemap directly as
    // the sky background. The HDRI cubemap was already baked into IBL maps
    // during SetHDRIEnvironment().
    if (ctx.hdriActive && ctx.hdriCubemapSrv != UINT32_MAX) {
        auto hdriPipeline = ctx.pipelines->GetHdriSkybox();
        if (hdriPipeline.IsValid()) {
            HdriSkyboxPassDesc hdriDesc{};
            hdriDesc.backbuffer      = hdrRes;
            hdriDesc.depthGBuffer    = depthRes;
            hdriDesc.perFrameAlloc   = perFrameAlloc;
            hdriDesc.pipeline        = hdriPipeline;
            hdriDesc.depthSrvIndex   = ctx.depthSrvIndex;
            hdriDesc.depthTexture    = ctx.depthTexture;
            hdriDesc.cubemapSrvIndex = ctx.hdriCubemapSrv;
            hdriDesc.cubemapTexture  = ctx.hdriCubemapTex;
            hdriDesc.bindless        = ctx.bindless;
            hdriDesc.viewportWidth   = ctx.vpWidth;
            hdriDesc.viewportHeight  = ctx.vpHeight;
            AddHdriSkyboxPass(rgBuilder, hdriDesc);
        } else {
            UZ_LOG(Renderer, Error,
                   "AddAtmospherePasses: HdriSkybox pipeline creation failed — "
                   "sky will be black");
        }
        return;  // No procedural atmosphere passes needed when HDRI is active
    }

    auto skyView = ctx.world->GetRegistry().view<SkyAtmosphereComponent>();
    for (auto enttEntity : skyView) {
        auto& skyComp = skyView.get<SkyAtmosphereComponent>(enttEntity);
        if (!skyComp.enabled) continue;

        // ── Transmittance LUT — import every frame so RG can track state ──
        rg::RgResourceId lutRes;
        if (ctx.transmittanceLut.IsValid()) {
            RHIResourceState lutImportState = *ctx.transmittanceLutFilled
                ? RHIResourceState::ShaderResource
                : RHIResourceState::UnorderedAccess;
            lutRes = rgBuilder.ImportTexture("TransmittanceLUT",
                ctx.transmittanceLut, lutImportState);
        }

        if (*ctx.transmittanceLutDirty && ctx.transmittanceLut.IsValid()
                && ctx.transmittanceLutUav != UINT32_MAX) {
            TransmittanceLutRootConstants lutRC{};
            lutRC.rayleighBetaX  = skyComp.rayleighScattering.x;
            lutRC.rayleighBetaY  = skyComp.rayleighScattering.y;
            lutRC.rayleighBetaZ  = skyComp.rayleighScattering.z;
            lutRC.rayleighScaleH = skyComp.rayleighScaleHeightKm;
            lutRC.mieBeta        = skyComp.mieScattering;
            lutRC.mieScaleH      = skyComp.mieScaleHeightKm;
            lutRC.bottomRadiusKm = skyComp.bottomRadiusKm;
            lutRC.topRadiusKm    = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;

            // ── Ozone CBV for transmittance LUT ───────────────────────
            // Convert ozone tent distribution to linear piecewise coefficients.
            TransmittanceLutOzoneCB ozoneCB{};
            {
                const float px    = skyComp.ozoneTipAltitude;
                const float py    = skyComp.ozoneTipValue;
                const float width = skyComp.ozoneWidth;
                if (width > 0.f && py > 0.f) {
                    const float slope = py / width;
                    ozoneCB.absorptionR = skyComp.ozoneAbsorption.x;
                    ozoneCB.absorptionG = skyComp.ozoneAbsorption.y;
                    ozoneCB.absorptionB = skyComp.ozoneAbsorption.z;
                    ozoneCB.layerWidth  = px;
                    ozoneCB.linTerm0    =  slope;
                    ozoneCB.constTerm0  = py - px * slope;
                    ozoneCB.linTerm1    = -slope;
                    ozoneCB.constTerm1  = py - px * (-slope);
                }
            }
            CBAllocation ozoneAlloc = ctx.cbRing->Allocate(sizeof(TransmittanceLutOzoneCB));
            std::memcpy(ozoneAlloc.cpuPtr, &ozoneCB, sizeof(TransmittanceLutOzoneCB));

            TransmittanceLutPassDesc lutPassDesc{};
            lutPassDesc.lutTarget      = lutRes;
            lutPassDesc.pipeline       = ctx.pipelines->GetTransmittanceLut();
            lutPassDesc.rootConstants  = lutRC;
            lutPassDesc.lutUavIndex    = ctx.transmittanceLutUav;
            lutPassDesc.ozoneCbAlloc   = ozoneAlloc;
            AddTransmittanceLutPass(rgBuilder, lutPassDesc);

            *ctx.transmittanceLutDirty  = false;
            *ctx.transmittanceLutFilled = true;
        }

        // ── Compute atmosphere sun parameters ─────────────────────────────
        const Vec3& sunDir   = *ctx.sunDir;
        const Vec3& sunColor = *ctx.sunColor;
        Vec3 skySunColor = skyComp.sunIlluminance;
        float skySunAngularRadius = skyComp.sunAngularRadius;
        float skySunDiskIntensity = skyComp.sunDiskIntensity;

        {
            auto dirView2 = ctx.world->GetRegistry().view<DirectionalLightComponent>();
            for (auto e2 : dirView2) {
                auto& dl2 = dirView2.get<DirectionalLightComponent>(e2);
                if (dl2.atmosphereSunLight && dl2.atmosphereSunLightIndex == 0) {
                    Vec3 lightTint = dl2.color;
                    if (dl2.useTemperature) {
                        lightTint = lightTint * KelvinToRgb(dl2.temperature);
                    }
                    skySunColor = skyComp.sunIlluminance * lightTint;
                    static constexpr float kDegToRad2 = 3.14159265358979323846f / 180.f;
                    skySunAngularRadius = dl2.lightSourceAngle * kDegToRad2;
                    skySunDiskIntensity = dl2.intensity;
                    break;
                }
            }
        }

        // ── Sky Cubemap Capture (for IBL) ─────────────────────────────────
        // Two paths: compute shader (bindless, SM6.6) or pixel shader (SM5).
        // The compute path dispatches all 6 faces in a single call via
        // ResourceDescriptorHeap. The PS path draws 6 fullscreen triangles
        // with explicit register(t0) binding — no bindless required.
        //
        // When HDRI is active, the HDRI cubemap is used directly for IBL —
        // skip the procedural sky cubemap capture entirely.
        if (!ctx.hdriActive && *ctx.skyIBLDirty && ctx.skyCubemap.IsValid()
                && ctx.transmittanceLutSrv != UINT32_MAX) {

            // Shared f16 packing helper for scattering scale factors
            auto packF16Pair = [](float a, float b) -> uint32_t {
                auto toF16 = [](float f) -> uint16_t {
                    uint32_t bits;
                    std::memcpy(&bits, &f, 4);
                    uint32_t sign = (bits >> 16) & 0x8000;
                    int32_t  exp  = ((bits >> 23) & 0xFF) - 127 + 15;
                    uint32_t frac = (bits >> 13) & 0x3FF;
                    if (exp <= 0)    return static_cast<uint16_t>(sign);
                    if (exp >= 0x1F) return static_cast<uint16_t>(sign | 0x7C00);
                    return static_cast<uint16_t>(sign | (exp << 10) | frac);
                };
                return static_cast<uint32_t>(toF16(a))
                     | (static_cast<uint32_t>(toF16(b)) << 16);
            };

            const uint32_t packed = packF16Pair(
                skyComp.rayleighScatteringScale,
                skyComp.mieScatteringScale);

            if (ctx.bindless && ctx.skyCubemapUav != UINT32_MAX) {
                // ── Compute shader path (SM6.6 bindless) ─────────────────
                auto cubeRes = rgBuilder.ImportTexture(
                    "SkyCubemap", ctx.skyCubemap, RHIResourceState::UnorderedAccess);

                SkyCubemapCaptureRootConstants cubeRC{};
                cubeRC.bottomRadiusKm      = skyComp.bottomRadiusKm;
                cubeRC.topRadiusKm         = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;
                cubeRC.sunDirX             = sunDir.x;
                cubeRC.sunDirY             = sunDir.y;
                cubeRC.sunDirZ             = sunDir.z;
                cubeRC.sunIntensity        = skySunColor.Length();
                cubeRC.transmittanceLutIdx = ctx.transmittanceLutSrv;
                cubeRC.packedScatterScales = packed;

                SkyCubemapCapturePassDesc cubePassDesc{};
                cubePassDesc.cubemapTarget      = cubeRes;
                cubePassDesc.transmittanceLutDep = lutRes;
                cubePassDesc.pipeline           = ctx.pipelines->GetSkyCubemapCapture();
                cubePassDesc.rootConstants      = cubeRC;
                cubePassDesc.cubemapUavIndex    = ctx.skyCubemapUav;
                AddSkyCubemapCapturePass(rgBuilder, cubePassDesc);
            } else {
                // ── Pixel shader path (SM5, no bindless) ─────────────────
                auto cubeRes = rgBuilder.ImportTexture(
                    "SkyCubemap", ctx.skyCubemap, RHIResourceState::RenderTarget);

                SkyCubemapCapturePSRootConstants psRC{};
                psRC.bottomRadiusKm      = skyComp.bottomRadiusKm;
                psRC.topRadiusKm         = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;
                psRC.sunDirX             = sunDir.x;
                psRC.sunDirY             = sunDir.y;
                psRC.sunDirZ             = sunDir.z;
                psRC.sunIntensity        = skySunColor.Length();
                psRC.cubeFaceIndex       = 0; // Updated per-face in the pass
                psRC.packedScatterScales = packed;

                SkyCubemapCapturePSPassDesc psPassDesc{};
                psPassDesc.cubemapTarget       = cubeRes;
                psPassDesc.transmittanceLut    = lutRes;
                psPassDesc.pipeline            = ctx.pipelines->GetSkyCubemapCapturePS();
                psPassDesc.transmittanceLutTex = ctx.transmittanceLut;
                psPassDesc.rootConstants       = psRC;
                AddSkyCubemapCapturePSPass(rgBuilder, psPassDesc);
            }
        }

        // ── Build SkyAtmosphereCB ─────────────────────────────────────────
        SkyAtmosphereCB skyCB{};
        skyCB.rayleighScattering = {skyComp.rayleighScattering.x,
                                    skyComp.rayleighScattering.y,
                                    skyComp.rayleighScattering.z,
                                    skyComp.rayleighScaleHeightKm};
        skyCB.mieScattering      = {skyComp.mieScattering,
                                    skyComp.mieAnisotropy,
                                    skyComp.mieScaleHeightKm, 0.f};
        skyCB.groundAlbedo       = {skyComp.groundAlbedo.x,
                                    skyComp.groundAlbedo.y,
                                    skyComp.groundAlbedo.z, 0.f};
        skyCB.sunIlluminance     = {skySunColor.x,
                                    skySunColor.y,
                                    skySunColor.z,
                                    skySunAngularRadius};
        skyCB.bottomRadiusKm        = skyComp.bottomRadiusKm;
        skyCB.atmosphereHeightKm    = skyComp.atmosphereHeightKm;
        skyCB.sunDiskIntensity      = skySunDiskIntensity;
        skyCB.multiScatteringFactor = skyComp.multiScatteringFactor;
        skyCB.rayleighScatteringScale = skyComp.rayleighScatteringScale;
        skyCB.mieScatteringScale    = skyComp.mieScatteringScale;
        skyCB.mieAbsorptionScale    = skyComp.mieAbsorptionScale;
        skyCB.mieAbsorption         = skyComp.mieAbsorption;
        skyCB.skyLuminanceFactor    = {skyComp.skyLuminanceFactor.x,
                                        skyComp.skyLuminanceFactor.y,
                                        skyComp.skyLuminanceFactor.z, 1.f};

        // ── Ozone absorption (reference "Other Absorption") ───────────────
        // Convert ozone tent distribution to piecewise-linear coefficients.
        // The tent is: density rises linearly from 0 to tipValue over
        // [tipAltitude - width, tipAltitude], then falls to 0 over
        // [tipAltitude, tipAltitude + width].
        {
            const float px    = skyComp.ozoneTipAltitude;
            const float py    = skyComp.ozoneTipValue;
            const float width = skyComp.ozoneWidth;

            if (width > 0.f && py > 0.f) {
                const float slope = py / width;
                skyCB.ozoneAbsorption = {skyComp.ozoneAbsorption.x,
                                         skyComp.ozoneAbsorption.y,
                                         skyComp.ozoneAbsorption.z,
                                         px};  // w = layerWidth (boundary altitude)
                skyCB.ozoneLinTerm0   =  slope;
                skyCB.ozoneConstTerm0 = py - px * slope;
                skyCB.ozoneLinTerm1   = -slope;
                skyCB.ozoneConstTerm1 = py - px * (-slope);
            } else {
                skyCB.ozoneAbsorption = {0.f, 0.f, 0.f, 0.f};
                skyCB.ozoneLinTerm0   = 0.f;
                skyCB.ozoneConstTerm0 = 0.f;
                skyCB.ozoneLinTerm1   = 0.f;
                skyCB.ozoneConstTerm1 = 0.f;
            }
        }

        CBAllocation skyAlloc = ctx.cbRing->Allocate(sizeof(SkyAtmosphereCB));
        std::memcpy(skyAlloc.cpuPtr, &skyCB, sizeof(SkyAtmosphereCB));

        // ── Multi-Scattering LUT pass ─────────────────────────────────
        rg::RgResourceId msLutRes;
        if (ctx.multiScatterLut.IsValid()) {
            RHIResourceState msImportState = *ctx.multiScatterLutFilled
                ? RHIResourceState::ShaderResource
                : RHIResourceState::UnorderedAccess;
            msLutRes = rgBuilder.ImportTexture("MultiScatterLUT",
                ctx.multiScatterLut, msImportState);
        }

        // ── Multi-Scattering LUT compute pass ─────────────────────
        /// @ref Hillaire 2020 — multi-scattering LUT via hemispherical integration.
        if (*ctx.multiScatterLutDirty && ctx.multiScatterLut.IsValid()
                && ctx.multiScatterLutUav != UINT32_MAX
                && *ctx.transmittanceLutFilled) {

            // Build root constants (same atmosphere params as transmittance LUT)
            TransmittanceLutRootConstants msRC{};
            msRC.rayleighBetaX  = skyComp.rayleighScattering.x;
            msRC.rayleighBetaY  = skyComp.rayleighScattering.y;
            msRC.rayleighBetaZ  = skyComp.rayleighScattering.z;
            msRC.rayleighScaleH = skyComp.rayleighScaleHeightKm;
            msRC.mieBeta        = skyComp.mieScattering;
            msRC.mieScaleH      = skyComp.mieScaleHeightKm;
            msRC.bottomRadiusKm = skyComp.bottomRadiusKm;
            msRC.topRadiusKm    = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;

            // Ozone CBV
            TransmittanceLutOzoneCB msOzoneCB{};
            {
                const float px    = skyComp.ozoneTipAltitude;
                const float py    = skyComp.ozoneTipValue;
                const float width = skyComp.ozoneWidth;
                if (width > 0.f && py > 0.f) {
                    const float slope = py / width;
                    msOzoneCB.absorptionR = skyComp.ozoneAbsorption.x;
                    msOzoneCB.absorptionG = skyComp.ozoneAbsorption.y;
                    msOzoneCB.absorptionB = skyComp.ozoneAbsorption.z;
                    msOzoneCB.layerWidth  = px;
                    msOzoneCB.linTerm0    =  slope;
                    msOzoneCB.constTerm0  = py - px * slope;
                    msOzoneCB.linTerm1    = -slope;
                    msOzoneCB.constTerm1  = py - px * (-slope);
                }
            }
            msOzoneCB.transmittanceLutSrvIdx = ctx.transmittanceLutSrv;
            CBAllocation ozoneAllocMS = ctx.cbRing->Allocate(sizeof(TransmittanceLutOzoneCB));
            std::memcpy(ozoneAllocMS.cpuPtr, &msOzoneCB, sizeof(TransmittanceLutOzoneCB));

            rgBuilder.AddPass("MultiScatterLUT", [&, msRC, ozoneAllocMS](rg::RgPassBuilder& pb) {
                pb.Write(msLutRes, RHIResourceState::UnorderedAccess);
                pb.Read(lutRes, RHIResourceState::ShaderResource);

                // Capture ctx members by value — ctx (AtmosphereFrameContext) lives on
                // RegisterOverlayPasses stack and is destroyed before Execute().
                auto* pipelines = ctx.pipelines;
                const bool bindless = ctx.bindless;
                const uint32_t transmittanceSrv = ctx.transmittanceLutSrv;
                const uint32_t msLutUav = ctx.multiScatterLutUav;

                pb.SetExecutionCallback([pipelines, bindless, transmittanceSrv, msLutUav,
                                         msRC, ozoneAllocMS](IRHICommandList* cmd, const rg::RgPassResources&) {
                    cmd->SetPipeline(pipelines->GetMultiScatterLut());

                    // Root constants at slot 0 (b0)
                    cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&msRC), 8);

                    // Ozone CBV at slot 2 (b2)
                    if (ozoneAllocMS.gpuAddr != 0) {
                        cmd->SetRootCBV(2, ozoneAllocMS.gpuAddr);
                    }

                    // Transmittance LUT SRV at slot 5 (t0 descriptor table)
                    if (!bindless) {
                        cmd->SetDescriptorTable(5, transmittanceSrv);
                    }

                    // Output UAV at slot 4 (u0 descriptor table)
                    cmd->SetDescriptorTable(4, msLutUav);

                    // Dispatch 32x32 / [8,8] = [4,4,1]
                    cmd->Dispatch(4, 4, 1);
                });
            });

            *ctx.multiScatterLutDirty  = false;
            *ctx.multiScatterLutFilled = true;
        }

        // ── Aerial Perspective Volume — precomputed 3D AP texture ─────────
        // @ref Hillaire 2020 — camera-relative aerial perspective volume (3D LUT).
        // Dispatched every frame (view-dependent). Requires transmittance LUT filled.
        if (ctx.apVolume.IsValid() && ctx.apVolumeUav != UINT32_MAX
            && *ctx.transmittanceLutFilled && ctx.invViewProjData && ctx.cameraPos) {

            rg::RgResourceId apRes = rgBuilder.ImportTexture("APVolume",
                ctx.apVolume, RHIResourceState::UnorderedAccess);

            APVolumeRootConstants apRC{};
            apRC.rayleighBetaX  = skyComp.rayleighScattering.x;
            apRC.rayleighBetaY  = skyComp.rayleighScattering.y;
            apRC.rayleighBetaZ  = skyComp.rayleighScattering.z;
            apRC.rayleighScaleH = skyComp.rayleighScaleHeightKm;
            apRC.mieBeta        = skyComp.mieScattering;
            apRC.mieScaleH      = skyComp.mieScaleHeightKm;
            apRC.bottomRadiusKm = skyComp.bottomRadiusKm;
            apRC.topRadiusKm    = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;

            APVolumeCB apCB{};
            std::memcpy(&apCB.invViewProj, ctx.invViewProjData, 64);
            apCB.cameraPosX         = ctx.cameraPos->x;
            apCB.cameraPosY         = ctx.cameraPos->y;
            apCB.cameraPosZ         = ctx.cameraPos->z;
            apCB._camPadW           = 0.0f;
            apCB.sunDirX            = sunDir.x;
            apCB.sunDirY            = sunDir.y;
            apCB.sunDirZ            = sunDir.z;
            apCB.sunIntensity       = skyComp.sunIlluminance.x; // Use X as scalar (white sun)
            apCB.mieAnisotropy      = skyComp.mieAnisotropy;
            apCB.rayleighScale      = skyComp.rayleighScatteringScale;
            apCB.mieScale           = skyComp.mieScatteringScale;
            apCB.transmittanceLutIdx = ctx.transmittanceLutSrv;
            apCB.outputUavIdx       = ctx.apVolumeUav;
            apCB.multiScatterLutIdx = ctx.multiScatterLutSrv;
            apCB.depthSlices        = 16;
            apCB.totalDepthKm       = 100.0f;

            CBAllocation apAlloc = ctx.cbRing->Allocate(sizeof(APVolumeCB));
            std::memcpy(apAlloc.cpuPtr, &apCB, sizeof(APVolumeCB));

            APVolumePassDesc apPassDesc{};
            apPassDesc.volumeTarget        = apRes;
            apPassDesc.transmittanceLutDep = lutRes;
            apPassDesc.multiScatterLutDep  = msLutRes;
            apPassDesc.pipeline            = ctx.pipelines->GetAerialPerspectiveVolume();
            apPassDesc.rootConstants       = apRC;
            apPassDesc.cbAlloc             = apAlloc;
            apPassDesc.depthSlices         = 16;
            AddAerialPerspectiveVolumePass(rgBuilder, apPassDesc);
        }

        SkyAtmospherePassDesc skyDesc{};
        skyDesc.backbuffer              = hdrRes;
        skyDesc.depthGBuffer            = depthRes;
        skyDesc.perFrameAlloc           = perFrameAlloc;
        skyDesc.skyAtmosphereAlloc      = skyAlloc;
        skyDesc.pipeline                = ctx.pipelines->GetSkyAtmosphere();
        skyDesc.depthSrvIndex           = ctx.depthSrvIndex;
        skyDesc.depthTexture            = ctx.depthTexture;
        skyDesc.transmittanceLutSrvIndex = ctx.transmittanceLutSrv;
        skyDesc.transmittanceLutTexture  = ctx.transmittanceLut;
        skyDesc.transmittanceLutRes      = lutRes;
        skyDesc.multiScatterLutSrvIndex = ctx.multiScatterLutSrv;
        skyDesc.multiScatterLutTexture  = ctx.multiScatterLut;
        skyDesc.multiScatterLutRes      = msLutRes;
        skyDesc.apVolumeSrvIndex        = ctx.apVolumeSrv;
        skyDesc.bindless                = ctx.bindless;
        skyDesc.viewportWidth           = ctx.vpWidth;
        skyDesc.viewportHeight          = ctx.vpHeight;
        AddSkyAtmospherePass(rgBuilder, skyDesc);

        break;  // First SkyAtmosphereComponent only
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// PostFrameAtmosphereBake
// ─────────────────────────────────────────────────────────────────────────────

void PostFrameAtmosphereBake(
    IRHICommandList* cmdList,
    AtmosphereFrameContext& ctx)
{
    if (!*ctx.skyIBLDirty || !ctx.iblBaker || !ctx.iblBaker->IsInitialized()
            || ctx.skyCubemapSrv == UINT32_MAX) {
        return;
    }

    cmdList->End();
    cmdList->Execute();
    ctx.device->WaitIdle();

    *ctx.iblResult = ctx.iblBaker->BakeEnvironment(ctx.skyCubemapSrv, ctx.skyCubemap);
    *ctx.skyIBLDirty = false;
    UZ_LOG(Renderer, Info, "SceneRenderer: Sky IBL baked (irrad={}, prefilt={})",
           ctx.iblResult->irradianceSrvIndex, ctx.iblResult->prefilteredSrvIndex);

    // SH projection from sky cubemap
    {
        constexpr uint32_t kSkyCubeFaceSize = 64;
        auto shResult = ProjectCubemapToSH(ctx.device, cmdList,
                                            ctx.skyCubemap, kSkyCubeFaceSize);

        bool shValid = (std::abs(shResult.coeffs[0].w) > 1e-6f ||
                       std::abs(shResult.coeffs[1].w) > 1e-6f ||
                       std::abs(shResult.coeffs[2].w) > 1e-6f);

        if (shValid) {
            *ctx.skyIrradianceSH = shResult;
            UZ_LOG(Renderer, Info, "SceneRenderer: SH from GPU readback");
        } else {
            // GPU readback returned zero SH — use black ambient rather than
            // invented analytical values. reference always projects from a cubemap
            // and never falls back to analytical SH coefficients.
            for (int i = 0; i < 7; ++i) ctx.skyIrradianceSH->coeffs[i] = {0,0,0,0};
            UZ_LOG(Renderer, Warning, "SceneRenderer: SH readback returned zeros — using black ambient");
        }
    }

    cmdList->Begin();
}

} // namespace uz
