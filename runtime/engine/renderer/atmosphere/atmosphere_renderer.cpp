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
#include "sky_cubemap_capture_pass.h"
#include "light_component.h"
#include "color_utils.h"
#include "ibl_baker.h"
#include "sh_projection.h"
#include "log.h"
#include "rhi.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(Renderer);

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

            TransmittanceLutPassDesc lutPassDesc{};
            lutPassDesc.lutTarget      = lutRes;
            lutPassDesc.pipeline       = ctx.pipelines->GetTransmittanceLut();
            lutPassDesc.rootConstants  = lutRC;
            lutPassDesc.lutUavIndex    = ctx.transmittanceLutUav;
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
        if (*ctx.skyIBLDirty && ctx.skyCubemap.IsValid()
                && ctx.skyCubemapUav != UINT32_MAX
                && ctx.transmittanceLutSrv != UINT32_MAX) {
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
            cubeRC.packedScatterScales = packF16Pair(
                skyComp.rayleighScatteringScale,
                skyComp.mieScatteringScale);

            SkyCubemapCapturePassDesc cubePassDesc{};
            cubePassDesc.cubemapTarget   = cubeRes;
            cubePassDesc.pipeline        = ctx.pipelines->GetSkyCubemapCapture();
            cubePassDesc.rootConstants   = cubeRC;
            cubePassDesc.cubemapUavIndex = ctx.skyCubemapUav;
            AddSkyCubemapCapturePass(rgBuilder, cubePassDesc);
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

        CBAllocation skyAlloc = ctx.cbRing->Allocate(sizeof(SkyAtmosphereCB));
        std::memcpy(skyAlloc.cpuPtr, &skyCB, sizeof(SkyAtmosphereCB));

        SkyAtmospherePassDesc skyDesc{};
        skyDesc.backbuffer              = hdrRes;
        skyDesc.depthGBuffer            = depthRes;
        skyDesc.perFrameAlloc           = perFrameAlloc;
        skyDesc.skyAtmosphereAlloc      = skyAlloc;
        skyDesc.pipeline                = ctx.pipelines->GetSkyAtmosphere();
        skyDesc.depthSrvIndex           = ctx.depthSrvIndex;
        skyDesc.depthTexture            = ctx.depthTexture;
        skyDesc.transmittanceLutSrvIndex = ctx.transmittanceLutSrv;
        skyDesc.transmittanceLutRes      = lutRes;
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

    *ctx.iblResult = ctx.iblBaker->BakeEnvironment(ctx.skyCubemapSrv);
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
            const Vec3& sunDir   = *ctx.sunDir;
            const Vec3& sunColor = *ctx.sunColor;
            float sunZ = sunDir.z;
            float sunBright = sunColor.Length();
            float skyFactor = sunBright * (0.08f + 0.15f * std::max(0.f, sunZ));

            Vec3 skyCol = {0.90f * skyFactor, 0.92f * skyFactor, 1.0f * skyFactor};
            Vec3 gndCol = {0.05f * skyFactor, 0.04f * skyFactor, 0.03f * skyFactor};

            Vec3 avg = (skyCol + gndCol) * 0.5f;
            Vec3 dir = (skyCol - gndCol) * 0.5f;
            float c0 = 3.14159f * 0.282095f, c1 = 2.094395f * 0.488603f;
            for (int i = 0; i < 7; ++i) ctx.skyIrradianceSH->coeffs[i] = {0,0,0,0};
            Vec3 sd = {sunDir.x*0.3f, sunDir.y*0.3f, 0.7f};
            float sl = std::sqrt(sd.x*sd.x+sd.y*sd.y+sd.z*sd.z);
            if (sl > 0.001f) sd = sd * (1.f/sl);
            ctx.skyIrradianceSH->coeffs[0] = {sd.x*c1*dir.x, sd.y*c1*dir.x, sd.z*c1*dir.x, c0*avg.x};
            ctx.skyIrradianceSH->coeffs[1] = {sd.x*c1*dir.y, sd.y*c1*dir.y, sd.z*c1*dir.y, c0*avg.y};
            ctx.skyIrradianceSH->coeffs[2] = {sd.x*c1*dir.z, sd.y*c1*dir.z, sd.z*c1*dir.z, c0*avg.z};
            UZ_LOG(Renderer, Info, "SceneRenderer: SH from analytical fallback");
        }
    }

    cmdList->Begin();
}

} // namespace uz
