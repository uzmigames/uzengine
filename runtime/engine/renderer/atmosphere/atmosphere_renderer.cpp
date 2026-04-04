/// UzEngine Atmosphere Renderer — Implementation.
///
/// Registers sky atmosphere render graph passes and handles post-frame
/// IBL bake + SH projection.

#include "atmosphere_renderer.h"
#include "env_map_sampler.h"

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
#include <string>

UZ_DECLARE_LOG_CATEGORY(Renderer);

// ── CVars ────────────────────────────────────────────────────────────────
static auto* cv_AerialPerspective = UZ_CVAR_BOOL("r.AerialPerspective", true, "Aerial perspective");
static auto* cv_ScatteringLut = UZ_CVAR_BOOL("r.ScatteringLut", false, "Use Bruneton 3D scattering LUT for sky (experimental)");

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
                    // Sun disk intensity uses the component's artistic control,
                    // NOT the directional light intensity (which would double-multiply
                    // with sunIlluminance, producing HDR values of 200+).
                    skySunDiskIntensity = skyComp.sunDiskIntensity;
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
                // Bruneton 3D scattering LUT for cubemap capture — avoids 16-step ray march.
                // Use 0 when LUT is not available (shader falls back to ray march).
                cubeRC.scatteringLut3DIdx  = (ctx.scatteringLut3DSrv != UINT32_MAX)
                                           ? ctx.scatteringLut3DSrv : 0u;
                // muSMin: cos(102 deg) = cos(1.78 rad) — Bruneton 2017 default minimum sun zenith cosine
                cubeRC.muSMin              = -0.2079116908f;

                SkyCubemapCapturePassDesc cubePassDesc{};
                cubePassDesc.cubemapTarget      = cubeRes;
                cubePassDesc.transmittanceLutDep = lutRes;
                cubePassDesc.pipeline           = ctx.pipelines->GetSkyCubemapCapture();
                cubePassDesc.rootConstants      = cubeRC;
                cubePassDesc.cubemapUavIndex    = ctx.skyCubemapUav;

                // Import scattering LUT 3D as read dependency if available
                if (ctx.scatteringLut3D.IsValid() && ctx.scatteringLut3DFilled
                        && *ctx.scatteringLut3DFilled
                        && ctx.scatteringLut3DSrv != UINT32_MAX) {
                    cubePassDesc.scatteringLut3DDep = rgBuilder.ImportTexture(
                        "ScatteringLUT3D_CubeCapture", ctx.scatteringLut3D,
                        RHIResourceState::ShaderResource);
                }

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
        // Multi-scattering LUT is only precomputed on bindless (SM6.6+).
        // On non-bindless, the LUT texture exists but is uninitialized — set
        // factor to 0 so the sky PS skips the multi-scatter contribution and
        // avoids sampling garbage data (which causes orange/pink sky).
        skyCB.multiScatteringFactor = ctx.bindless
            ? skyComp.multiScatteringFactor : 0.0f;
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

        // ── Scattering LUT 3D — import every frame so RG can track state ──
        rg::RgResourceId scatRes;
        if (ctx.scatteringLut3D.IsValid() && ctx.scatteringLut3DFilled) {
            RHIResourceState scatImportState = *ctx.scatteringLut3DFilled
                ? RHIResourceState::ShaderResource
                : RHIResourceState::UnorderedAccess;
            scatRes = rgBuilder.ImportTexture("ScatteringLUT3D",
                ctx.scatteringLut3D, scatImportState);
        }

        // ── Irradiance LUT — import every frame so RG can track state ────
        rg::RgResourceId irrLutRes;
        if (ctx.irradianceLut.IsValid() && ctx.scatteringLut3DFilled
            && *ctx.scatteringLut3DFilled) {
            irrLutRes = rgBuilder.ImportTexture("IrradianceLUT_Read",
                ctx.irradianceLut, RHIResourceState::ShaderResource);
        }

        // ── Bruneton 3D Scattering LUT Precomputation ───────────────────
        // @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
        // Precomputes the full scattering LUT: single scatter -> (density -> irradiance -> indirect) x 4 orders.
        // Runs once when dirty, recomputes when atmosphere parameters change.
        if (*ctx.scatteringLut3DDirty && ctx.scatteringLut3D.IsValid()
            && *ctx.transmittanceLutFilled && ctx.deltaRayleigh3D.IsValid()) {

            // Import helper textures into the render graph (scatRes already imported above)
            rg::RgResourceId deltaRayRes = rgBuilder.ImportTexture("DeltaRayleigh3D",
                ctx.deltaRayleigh3D, RHIResourceState::UnorderedAccess);
            rg::RgResourceId deltaMieRes = rgBuilder.ImportTexture("DeltaMie3D",
                ctx.deltaMie3D, RHIResourceState::UnorderedAccess);
            rg::RgResourceId scatDensRes = rgBuilder.ImportTexture("ScatteringDensity3D",
                ctx.scatteringDensity3D, RHIResourceState::UnorderedAccess);
            rg::RgResourceId irrRes = rgBuilder.ImportTexture("IrradianceLUT",
                ctx.irradianceLut, RHIResourceState::UnorderedAccess);
            rg::RgResourceId deltaIrrRes = rgBuilder.ImportTexture("DeltaIrradiance",
                ctx.deltaIrradiance, RHIResourceState::UnorderedAccess);

            // Common root constants (same atmosphere params as transmittance LUT)
            TransmittanceLutRootConstants scatRC{};
            scatRC.rayleighBetaX  = skyComp.rayleighScattering.x;
            scatRC.rayleighBetaY  = skyComp.rayleighScattering.y;
            scatRC.rayleighBetaZ  = skyComp.rayleighScattering.z;
            scatRC.rayleighScaleH = skyComp.rayleighScaleHeightKm;
            scatRC.mieBeta        = skyComp.mieScattering;
            scatRC.mieScaleH      = skyComp.mieScaleHeightKm;
            scatRC.bottomRadiusKm = skyComp.bottomRadiusKm;
            scatRC.topRadiusKm    = skyComp.bottomRadiusKm + skyComp.atmosphereHeightKm;

            // Ozone tent coefficients (same math as transmittance LUT section)
            const float tipAlt  = skyComp.ozoneTipAltitude;
            const float tipVal  = skyComp.ozoneTipValue;
            const float ozWidth = skyComp.ozoneWidth;
            const float slopeUp = (ozWidth > 0.0f) ? tipVal / ozWidth : 0.0f;
            const float slopeDn = (ozWidth > 0.0f) ? -tipVal / ozWidth : 0.0f;

            // ── Pass 1: Single Scatter ──────────────────────────────────
            // CBV layout matches single_scattering_lut_cs.hlsl ScatteringParams at b2
            struct SingleScatterCBV {
                float ozoneAbsX, ozoneAbsY, ozoneAbsZ;
                float ozoneLayerWidth;
                float ozoneLinTerm0, ozoneConstTerm0;
                float ozoneLinTerm1, ozoneConstTerm1;
                uint32_t transmittanceLutSrvIdx;
                uint32_t deltaRayleighUavIdx;
                uint32_t deltaMieUavIdx;
                uint32_t scatteringUavIdx;
                float    mieAnisotropy;
                float    _pad[3];
                float    _pad2[48];
            };
            static_assert(sizeof(SingleScatterCBV) == 256);

            SingleScatterCBV ssCBV{};
            ssCBV.ozoneAbsX          = skyComp.ozoneAbsorption.x;
            ssCBV.ozoneAbsY          = skyComp.ozoneAbsorption.y;
            ssCBV.ozoneAbsZ          = skyComp.ozoneAbsorption.z;
            ssCBV.ozoneLayerWidth    = tipAlt;
            ssCBV.ozoneLinTerm0      = slopeUp;
            ssCBV.ozoneConstTerm0    = 0.0f;
            ssCBV.ozoneLinTerm1      = slopeDn;
            ssCBV.ozoneConstTerm1    = tipVal - slopeDn * tipAlt;
            ssCBV.transmittanceLutSrvIdx = ctx.transmittanceLutSrv;
            ssCBV.deltaRayleighUavIdx    = ctx.deltaRayleigh3DUav;
            ssCBV.deltaMieUavIdx         = ctx.deltaMie3DUav;
            ssCBV.scatteringUavIdx       = ctx.scatteringLut3DUav;
            ssCBV.mieAnisotropy          = skyComp.mieAnisotropy;

            CBAllocation ssAlloc = ctx.cbRing->Allocate(sizeof(SingleScatterCBV));
            std::memcpy(ssAlloc.cpuPtr, &ssCBV, sizeof(SingleScatterCBV));

            auto ssPipeline = ctx.pipelines->GetSingleScatterLut();
            if (ssPipeline.IsValid()) {
                rgBuilder.AddPass("SingleScatterLUT", [=, &ctx](rg::RgPassBuilder& pb) {
                    pb.Read(lutRes, RHIResourceState::ShaderResource);
                    pb.Write(deltaRayRes, RHIResourceState::UnorderedAccess);
                    pb.Write(deltaMieRes, RHIResourceState::UnorderedAccess);
                    pb.Write(scatRes, RHIResourceState::UnorderedAccess);

                    pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                        cmd->SetPipeline(ssPipeline);
                        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&scatRC), 8);
                        cmd->SetRootCBV(2, ssAlloc.gpuAddr);
                        cmd->Dispatch(32, 16, 32); // 256x128x32 at [8,8,1]
                    });
                });
            }

            // ── Pass 2: Direct Irradiance (order 1) ─────────────────────
            // CBV layout matches irradiance_lut_cs.hlsl IrradianceParams at b2
            struct IrradianceCBV {
                float ozoneAbsX, ozoneAbsY, ozoneAbsZ;
                float ozoneLayerWidth;
                float ozoneLinTerm0, ozoneConstTerm0;
                float ozoneLinTerm1, ozoneConstTerm1;
                uint32_t transmittanceLutSrvIdx;
                uint32_t deltaRayleighSrvIdx;
                uint32_t deltaMieSrvIdx;
                uint32_t irradianceUavIdx;
                uint32_t deltaIrradianceUavIdx;
                float    mieAnisotropy;
                int32_t  scatteringOrder;
                float    muSMin;
                float    _pad[48];
            };
            static_assert(sizeof(IrradianceCBV) == 256);

            IrradianceCBV irrCBV{};
            irrCBV.ozoneAbsX          = skyComp.ozoneAbsorption.x;
            irrCBV.ozoneAbsY          = skyComp.ozoneAbsorption.y;
            irrCBV.ozoneAbsZ          = skyComp.ozoneAbsorption.z;
            irrCBV.ozoneLayerWidth    = tipAlt;
            irrCBV.ozoneLinTerm0      = slopeUp;
            irrCBV.ozoneConstTerm0    = 0.0f;
            irrCBV.ozoneLinTerm1      = slopeDn;
            irrCBV.ozoneConstTerm1    = tipVal - slopeDn * tipAlt;
            irrCBV.transmittanceLutSrvIdx = ctx.transmittanceLutSrv;
            irrCBV.deltaRayleighSrvIdx    = ctx.deltaRayleigh3DSrv;
            irrCBV.deltaMieSrvIdx         = ctx.deltaMie3DSrv;
            irrCBV.irradianceUavIdx       = ctx.irradianceLutUav;
            irrCBV.deltaIrradianceUavIdx  = ctx.deltaIrradianceUav;
            irrCBV.mieAnisotropy          = skyComp.mieAnisotropy;
            irrCBV.scatteringOrder        = 1;
            // muSMin: cos(102 deg) = cos(1.78 rad) — Bruneton 2017 default minimum sun zenith cosine
            irrCBV.muSMin = -0.2079116908f;

            CBAllocation irrAlloc = ctx.cbRing->Allocate(sizeof(IrradianceCBV));
            std::memcpy(irrAlloc.cpuPtr, &irrCBV, sizeof(IrradianceCBV));

            auto irrPipeline = ctx.pipelines->GetIrradianceLut();
            if (irrPipeline.IsValid()) {
                rgBuilder.AddPass("DirectIrradiance", [=](rg::RgPassBuilder& pb) {
                    pb.Read(lutRes, RHIResourceState::ShaderResource);
                    pb.Write(irrRes, RHIResourceState::UnorderedAccess);
                    pb.Write(deltaIrrRes, RHIResourceState::UnorderedAccess);

                    pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                        cmd->SetPipeline(irrPipeline);
                        cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&scatRC), 8);
                        cmd->SetRootCBV(2, irrAlloc.gpuAddr);
                        cmd->Dispatch(8, 2, 1); // 64x16 at [8,8,1]
                    });
                });
            }

            // ── Iterative Multi-Scatter (orders 2-4) ────────────────────
            // @ref Bruneton 2017 — iterative accumulation of higher-order scattering.
            // Each order: scattering density -> indirect irradiance -> indirect scattering.
            constexpr int kNumScatterOrders = 4;
            auto densPipeline = ctx.pipelines->GetScatteringDensity();
            auto indPipeline  = ctx.pipelines->GetIndirectScattering();

            for (int order = 2; order <= kNumScatterOrders; ++order) {
                // ── Scattering Density pass ─────────────────────────────
                struct ScatteringDensityCBV {
                    float ozoneAbsX, ozoneAbsY, ozoneAbsZ;
                    float ozoneLayerWidth;
                    float ozoneLinTerm0, ozoneConstTerm0;
                    float ozoneLinTerm1, ozoneConstTerm1;
                    uint32_t transmittanceLutSrvIdx;
                    uint32_t deltaRayleighSrvIdx;
                    uint32_t deltaMieSrvIdx;
                    uint32_t deltaIrradianceSrvIdx;
                    uint32_t scatteringDensityUavIdx;
                    float    mieAnisotropy;
                    float    groundAlbedoR, groundAlbedoG, groundAlbedoB;
                    int32_t  scatteringOrder;
                    float    muSMin;
                    float    _pad[45];
                };
                static_assert(sizeof(ScatteringDensityCBV) == 256);

                ScatteringDensityCBV densCBV{};
                densCBV.ozoneAbsX          = skyComp.ozoneAbsorption.x;
                densCBV.ozoneAbsY          = skyComp.ozoneAbsorption.y;
                densCBV.ozoneAbsZ          = skyComp.ozoneAbsorption.z;
                densCBV.ozoneLayerWidth    = tipAlt;
                densCBV.ozoneLinTerm0      = slopeUp;
                densCBV.ozoneConstTerm0    = 0.0f;
                densCBV.ozoneLinTerm1      = slopeDn;
                densCBV.ozoneConstTerm1    = tipVal - slopeDn * tipAlt;
                densCBV.transmittanceLutSrvIdx  = ctx.transmittanceLutSrv;
                densCBV.deltaRayleighSrvIdx     = ctx.deltaRayleigh3DSrv;
                densCBV.deltaMieSrvIdx          = (order == 2) ? ctx.deltaMie3DSrv : UINT32_MAX;
                densCBV.deltaIrradianceSrvIdx   = ctx.deltaIrradianceSrv;
                densCBV.scatteringDensityUavIdx = ctx.scatteringDensity3DUav;
                densCBV.mieAnisotropy    = skyComp.mieAnisotropy;
                densCBV.groundAlbedoR    = skyComp.groundAlbedo.x;
                densCBV.groundAlbedoG    = skyComp.groundAlbedo.y;
                densCBV.groundAlbedoB    = skyComp.groundAlbedo.z;
                densCBV.scatteringOrder  = order;
                densCBV.muSMin = -0.2079116908f;

                CBAllocation densAlloc = ctx.cbRing->Allocate(sizeof(ScatteringDensityCBV));
                std::memcpy(densAlloc.cpuPtr, &densCBV, sizeof(ScatteringDensityCBV));

                if (densPipeline.IsValid()) {
                    std::string densPassName = std::string("ScatteringDensity_Order") + std::to_string(order);
                    rgBuilder.AddPass(densPassName.c_str(), [=](rg::RgPassBuilder& pb) {
                        pb.Read(lutRes, RHIResourceState::ShaderResource);
                        pb.Read(deltaRayRes, RHIResourceState::ShaderResource);
                        if (order == 2) pb.Read(deltaMieRes, RHIResourceState::ShaderResource);
                        pb.Read(deltaIrrRes, RHIResourceState::ShaderResource);
                        pb.Write(scatDensRes, RHIResourceState::UnorderedAccess);

                        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                            cmd->SetPipeline(densPipeline);
                            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&scatRC), 8);
                            cmd->SetRootCBV(2, densAlloc.gpuAddr);
                            cmd->Dispatch(32, 16, 32);
                        });
                    });
                }

                // ── Indirect Irradiance pass (accumulate) ───────────────
                IrradianceCBV indIrrCBV = irrCBV;
                indIrrCBV.scatteringOrder     = order;
                indIrrCBV.deltaRayleighSrvIdx = ctx.deltaRayleigh3DSrv;
                indIrrCBV.deltaMieSrvIdx      = (order == 2) ? ctx.deltaMie3DSrv : UINT32_MAX;

                CBAllocation indIrrAlloc = ctx.cbRing->Allocate(sizeof(IrradianceCBV));
                std::memcpy(indIrrAlloc.cpuPtr, &indIrrCBV, sizeof(IrradianceCBV));

                if (irrPipeline.IsValid()) {
                    std::string irrPassName = std::string("IndirectIrradiance_Order") + std::to_string(order);
                    rgBuilder.AddPass(irrPassName.c_str(), [=](rg::RgPassBuilder& pb) {
                        pb.Read(deltaRayRes, RHIResourceState::ShaderResource);
                        if (order == 2) pb.Read(deltaMieRes, RHIResourceState::ShaderResource);
                        pb.Write(irrRes, RHIResourceState::UnorderedAccess);
                        pb.Write(deltaIrrRes, RHIResourceState::UnorderedAccess);

                        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                            cmd->SetPipeline(irrPipeline);
                            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&scatRC), 8);
                            cmd->SetRootCBV(2, indIrrAlloc.gpuAddr);
                            cmd->Dispatch(8, 2, 1);
                        });
                    });
                }

                // ── Indirect Scattering pass (integrate density, accumulate) ─
                struct IndirectScatteringCBV {
                    float ozoneAbsX, ozoneAbsY, ozoneAbsZ;
                    float ozoneLayerWidth;
                    float ozoneLinTerm0, ozoneConstTerm0;
                    float ozoneLinTerm1, ozoneConstTerm1;
                    uint32_t transmittanceLutSrvIdx;
                    uint32_t scatteringDensitySrvIdx;
                    uint32_t scatteringUavIdx;
                    uint32_t deltaScatteringUavIdx;
                    float    muSMin;
                    float    _pad[51];
                };
                static_assert(sizeof(IndirectScatteringCBV) == 256);

                IndirectScatteringCBV indCBV{};
                indCBV.ozoneAbsX          = skyComp.ozoneAbsorption.x;
                indCBV.ozoneAbsY          = skyComp.ozoneAbsorption.y;
                indCBV.ozoneAbsZ          = skyComp.ozoneAbsorption.z;
                indCBV.ozoneLayerWidth    = tipAlt;
                indCBV.ozoneLinTerm0      = slopeUp;
                indCBV.ozoneConstTerm0    = 0.0f;
                indCBV.ozoneLinTerm1      = slopeDn;
                indCBV.ozoneConstTerm1    = tipVal - slopeDn * tipAlt;
                indCBV.transmittanceLutSrvIdx  = ctx.transmittanceLutSrv;
                indCBV.scatteringDensitySrvIdx = ctx.scatteringDensity3DSrv;
                indCBV.scatteringUavIdx        = ctx.scatteringLut3DUav;
                indCBV.deltaScatteringUavIdx   = ctx.deltaRayleigh3DUav; // Reuse deltaRayleigh as delta storage
                indCBV.muSMin = -0.2079116908f;

                CBAllocation indAlloc = ctx.cbRing->Allocate(sizeof(IndirectScatteringCBV));
                std::memcpy(indAlloc.cpuPtr, &indCBV, sizeof(IndirectScatteringCBV));

                if (indPipeline.IsValid()) {
                    std::string indPassName = std::string("IndirectScattering_Order") + std::to_string(order);
                    rgBuilder.AddPass(indPassName.c_str(), [=](rg::RgPassBuilder& pb) {
                        pb.Read(lutRes, RHIResourceState::ShaderResource);
                        pb.Read(scatDensRes, RHIResourceState::ShaderResource);
                        pb.Write(scatRes, RHIResourceState::UnorderedAccess);
                        pb.Write(deltaRayRes, RHIResourceState::UnorderedAccess); // Delta storage

                        pb.SetExecutionCallback([=](IRHICommandList* cmd, const rg::RgPassResources&) {
                            cmd->SetPipeline(indPipeline);
                            cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&scatRC), 8);
                            cmd->SetRootCBV(2, indAlloc.gpuAddr);
                            cmd->Dispatch(32, 16, 32);
                        });
                    });
                }
            }

            *ctx.scatteringLut3DDirty  = false;
            *ctx.scatteringLut3DFilled = true;
            UZ_LOG(Renderer, Info, "Bruneton 3D scattering LUT precomputed ({} orders)", kNumScatterOrders);
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

            // Bruneton 3D scattering LUT for LUT-based aerial perspective.
            // When available and enabled via CVar, the compute shader uses dual 4D
            // LUT lookups instead of the 8-step ray march (Phase 5).
            // @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
            //      Section 5 — GetSkyRadianceToPoint
            if (cv_ScatteringLut->value > 0.5f
                && ctx.scatteringLut3DFilled && *ctx.scatteringLut3DFilled
                && ctx.scatteringLut3DSrv != UINT32_MAX) {
                apCB.scatteringLut3DIdx = ctx.scatteringLut3DSrv;
            } else {
                apCB.scatteringLut3DIdx = 0; // 0 = LUT not available, shader uses ray march fallback
            }
            // muSMin: cos(102 deg) = cos(1.78 rad) — Bruneton 2017 default minimum
            // sun zenith cosine for the scattering LUT parameterization.
            apCB.muSMin = -0.2079116908f;

            CBAllocation apAlloc = ctx.cbRing->Allocate(sizeof(APVolumeCB));
            std::memcpy(apAlloc.cpuPtr, &apCB, sizeof(APVolumeCB));

            APVolumePassDesc apPassDesc{};
            apPassDesc.volumeTarget        = apRes;
            apPassDesc.transmittanceLutDep = lutRes;
            apPassDesc.multiScatterLutDep  = msLutRes;
            // Scattering 3D LUT dependency — only when LUT is filled and CVar enabled.
            if (cv_ScatteringLut->value > 0.5f
                && ctx.scatteringLut3DFilled && *ctx.scatteringLut3DFilled
                && scatRes.IsValid()) {
                apPassDesc.scatteringLut3DDep = scatRes;
            }
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
        // Bruneton LUT sky path — gated behind CVar (experimental, dark zenith issue).
        // When disabled, sky uses proven 32-step march fallback.
        if (cv_ScatteringLut->value > 0.5f) {
            skyDesc.scatteringLut3DSrvIndex = ctx.scatteringLut3DSrv;
            skyDesc.scatteringLut3DTexture  = ctx.scatteringLut3D;
            skyDesc.scatteringLut3DRes      = scatRes;
            skyDesc.irradianceLutSrvIndex   = ctx.irradianceLutSrv;
            skyDesc.irradianceLutRes        = irrLutRes;
        }
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

    // Rebuild env map importance sampler from the same sky cubemap
    if (ctx.envMapSampler) {
        if (ctx.envMapSampler->Rebuild(ctx.skyCubemapSrv)) {
            UZ_LOG(Renderer, Info, "SceneRenderer: EnvMapSampler rebuilt (alias={}, lumMip0={}, totalLum={:.4f})",
                   ctx.envMapSampler->GetAliasSrv(),
                   ctx.envMapSampler->GetLuminanceMip0Srv(),
                   ctx.envMapSampler->GetTotalLuminance());
        }
    }

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
