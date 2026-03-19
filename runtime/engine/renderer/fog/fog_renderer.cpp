/// UzEngine Fog Renderer — Implementation.
///
/// Extracts HeightFogComponent from ECS, pre-computes UE5 RayOriginTerms,
/// and registers the height fog render graph pass.

#include "fog_renderer.h"

#include "world.h"
#include "height_fog_component.h"
#include "sky_atmosphere_component.h"
#include "light_component.h"
#include "height_fog_pass.h"
#include "camera.h"
#include "pipeline_library.h"
#include "render_graph_builder.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Analytical transmittance from sea level toward the sun
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 SkyAtmosphereRendering.cpp:454 — GetTransmittanceAtGroundLevel
/// @source UE5 DirectionalLightComponent.cpp:605 — GetSunIlluminanceOnGroundPostTransmittance
///
/// Computes atmospheric transmittance from sea level toward the sun direction
/// by numerically integrating optical depth (Rayleigh + Mie + Ozone extinction)
/// along the ray from the planet surface to the top of atmosphere.
///
/// This is the CPU equivalent of the transmittance LUT computation in
/// `transmittance_lut_cs.hlsl`, evaluated once per frame for the sun direction.
///
/// # Arguments
/// * `atmo`   — SkyAtmosphereComponent with physical atmosphere parameters.
/// * `sunDir` — Unit direction toward the sun (world-space, Z-up).
///
/// # Returns
/// RGB transmittance in [0, 1] per channel. (0,0,0) if the sun is below horizon.
static Vec3 ComputeTransmittanceToSun(
    const SkyAtmosphereComponent& atmo,
    const Vec3& sunDir) noexcept
{
    const float bottomR = atmo.bottomRadiusKm;
    const float topR    = bottomR + atmo.atmosphereHeightKm;

    // Camera at sea level: radius from planet center = bottomR.
    // Sun zenith cosine in Z-up coordinates.
    const float cosAngle = sunDir.z;

    // Ray-sphere intersection: find distance along sunDir from (0,0,bottomR)
    // to the atmosphere top sphere (radius = topR).
    // Parametric: |P + t*D|^2 = topR^2, where P = (0, 0, bottomR).
    // Expanding: t^2 + 2*bottomR*cosAngle*t + (bottomR^2 - topR^2) = 0.
    const float b    = 2.0f * bottomR * cosAngle;
    const float c    = bottomR * bottomR - topR * topR;
    const float disc = b * b - 4.0f * c;
    if (disc < 0.0f) return {0.0f, 0.0f, 0.0f}; // Below horizon — no path to space.

    const float tMax = (-b + std::sqrt(disc)) / 2.0f;
    if (tMax <= 0.0f) return {0.0f, 0.0f, 0.0f};

    // Numerical integration of optical depth (32 steps, same as our transmittance LUT).
    constexpr int kSteps = 32;
    const float dt = tMax / static_cast<float>(kSteps);

    float optDepthR = 0.0f;
    float optDepthG = 0.0f;
    float optDepthB = 0.0f;

    const float sinAngle = std::sqrt(std::max(0.0f, 1.0f - cosAngle * cosAngle));

    for (int i = 0; i < kSteps; ++i) {
        const float t = (static_cast<float>(i) + 0.5f) * dt;

        // Position along ray: starting at (0, 0, bottomR), direction = sunDir.
        // Only need the radius (distance from planet center) for density lookup.
        const float pz = bottomR + t * cosAngle;
        const float px = t * sinAngle;
        const float radius = std::sqrt(px * px + pz * pz);
        const float altitude = std::max(0.0f, radius - bottomR);

        // Rayleigh density (exponential profile).
        const float densityR = std::exp(-altitude / atmo.rayleighScaleHeightKm);

        // Mie density (exponential profile).
        const float densityM = std::exp(-altitude / atmo.mieScaleHeightKm);

        // Ozone density (tent distribution).
        // @source UE5 SkyAtmosphereCommonData.h:38-42 — tent distribution
        const float ozoneDist = std::abs(altitude - atmo.ozoneTipAltitude);
        const float densityO  = std::max(0.0f, 1.0f - ozoneDist / atmo.ozoneWidth) * atmo.ozoneTipValue;

        // Extinction = scattering + absorption for each species.
        // Rayleigh: scattering only (no absorption).
        const float rayleighExtR = atmo.rayleighScattering.x * atmo.rayleighScatteringScale * densityR;
        const float rayleighExtG = atmo.rayleighScattering.y * atmo.rayleighScatteringScale * densityR;
        const float rayleighExtB = atmo.rayleighScattering.z * atmo.rayleighScatteringScale * densityR;

        // Mie: scattering + absorption.
        const float mieExt = (atmo.mieScattering * atmo.mieScatteringScale
                            + atmo.mieAbsorption * atmo.mieAbsorptionScale) * densityM;

        // Ozone: absorption only (no scattering).
        const float ozoneExtR = atmo.ozoneAbsorption.x * densityO;
        const float ozoneExtG = atmo.ozoneAbsorption.y * densityO;
        const float ozoneExtB = atmo.ozoneAbsorption.z * densityO;

        // Accumulate optical depth.
        optDepthR += (rayleighExtR + mieExt + ozoneExtR) * dt;
        optDepthG += (rayleighExtG + mieExt + ozoneExtG) * dt;
        optDepthB += (rayleighExtB + mieExt + ozoneExtB) * dt;
    }

    return {std::exp(-optDepthR), std::exp(-optDepthG), std::exp(-optDepthB)};
}

void AddHeightFogFromECS(
    rg::RgBuilder& rgBuilder,
    rg::RgResourceId hdrRes,
    rg::RgResourceId depthRes,
    const CBAllocation& perFrameAlloc,
    World* world,
    const Camera& camera,
    PipelineLibrary* pipelines,
    CBRingAllocator& cbRing,
    uint32_t depthSrvIndex,
    RHITextureHandle depthTexture,
    bool bindless,
    float vpWidth,
    float vpHeight,
    float skyLightColorR,
    float skyLightColorG,
    float skyLightColorB)
{
    if (!world) return;

    auto fogView = world->GetRegistry().view<HeightFogComponent>();
    for (auto enttEntity : fogView) {
        auto& fogComp = fogView.get<HeightFogComponent>(enttEntity);
        if (!fogComp.enabled) continue;

        /// @source UE5 FogRendering.cpp:223-253 — InitFogConstants
        /// MaxObserverHeightDifference clamp prevents fog from vanishing at high camera Z.
        /// UE5: ObserverHeight = min(CameraZ, FogHeight + 65536.0f)
        constexpr float kMaxObserverHeightDiff = 65536.0f;
        float camZ = camera.GetPosition().z;
        float observerHeight = std::min(camZ, fogComp.baseHeight + kMaxObserverHeightDiff);
        float fogHeightDiff = observerHeight - fogComp.baseHeight;
        float clampedPower = std::clamp(-fogComp.heightFalloff * fogHeightDiff, -126.f + 1.f, 127.f - 1.f);
        float rayOriginTerms = fogComp.density * std::pow(2.0f, clampedPower);

        HeightFogCB fogCB{};
        fogCB.fogColor         = {fogComp.color.x, fogComp.color.y, fogComp.color.z, 0.f};
        fogCB.fogDensity       = rayOriginTerms;
        fogCB.fogHeightFalloff = fogComp.heightFalloff;
        fogCB.fogBaseHeight    = fogComp.baseHeight;
        fogCB.fogMaxOpacity    = fogComp.maxOpacity;
        fogCB.fogStartDistance = fogComp.startDistance;
        fogCB.fogCutoffDistance = fogComp.fogCutoffDistance;
        /// @source UE5 FogRendering.cpp:257 — ExponentialFogParameters.z = MaxObserverHeight
        fogCB.maxObserverHeight = fogComp.baseHeight + kMaxObserverHeightDiff;
        /// @source UE5 FogRendering.cpp:261 — ExponentialFogParameters3.x = raw density
        fogCB.rawDensity = fogComp.density;

        // @source UE5 FogRendering.cpp:245-254 — InitFogConstants
        // Second layer precomputation identical to first layer:
        //   CollapsedFogParameter[1] = Density * pow(2, clamp(-Falloff*(ObserverH - FogH), -125, 126))
        // @source UE5 FogRendering.cpp:257 — ExponentialFogParameters2.x = precomputed, .z = raw density, .w = height
        // @deviations NONE
        if (fogComp.secondFogDensity > 0.0f) {
            float secondObserverH = std::min(camZ, fogComp.secondFogBaseHeight + kMaxObserverHeightDiff);
            float secondFogHeightDiff = secondObserverH - fogComp.secondFogBaseHeight;
            float secondClampedPower = std::clamp(-fogComp.secondFogHeightFalloff * secondFogHeightDiff, -126.f + 1.f, 127.f - 1.f);
            fogCB.secondFogDensity       = fogComp.secondFogDensity * std::pow(2.0f, secondClampedPower);
            fogCB.secondFogHeightFalloff = fogComp.secondFogHeightFalloff;
            fogCB.secondFogRawDensity    = fogComp.secondFogDensity;
            fogCB.secondFogBaseHeight    = fogComp.secondFogBaseHeight;
        } else {
            fogCB.secondFogDensity       = 0.0f;
            fogCB.secondFogHeightFalloff = 0.0f;
            fogCB.secondFogRawDensity    = 0.0f;
            fogCB.secondFogBaseHeight    = 0.0f;
        }

        fogCB.directionalInscatteringColor = {
            fogComp.directionalInscatteringColor.x,
            fogComp.directionalInscatteringColor.y,
            fogComp.directionalInscatteringColor.z, 0.f};
        fogCB.directionalInscatteringExponent      = fogComp.directionalInscatteringExponent;
        fogCB.directionalInscatteringStartDistance  = fogComp.directionalInscatteringStartDistance;

        // ── Sky atmosphere injection ──
        /// @source UE5 HeightFogCommon.ush:174-180
        /// @source UE5 FogRendering.cpp:85 — SkyAtmosphereAmbientContributionColorScale
        /// When SkyAtmosphere exists, inject sky light color into fog inscattering.
        fogCB.skyAtmContribColorScale = {1.f, 1.f, 1.f, 0.f}; // UE5 default: white
        fogCB.heightFogContribution = 0.f; // Will be set if SkyAtmosphereComponent found
        fogCB.skyLightColorR = skyLightColorR;
        fogCB.skyLightColorG = skyLightColorG;
        fogCB.skyLightColorB = skyLightColorB;

        // ── Find SkyAtmosphereComponent for height fog contribution + transmittance ──
        const SkyAtmosphereComponent* skyAtmoPtr = nullptr;
        auto skyView = world->GetRegistry().view<SkyAtmosphereComponent>();
        for (auto skyEnt : skyView) {
            auto& skyComp = skyView.get<SkyAtmosphereComponent>(skyEnt);
            fogCB.heightFogContribution = skyComp.heightFogContribution;
            skyAtmoPtr = &skyComp;
            break;
        }

        // ── Sun transmittance injection ──
        /// @source UE5 SkyAtmosphereRendering.cpp:454 — GetTransmittanceAtGroundLevel
        /// @source UE5 DirectionalLightComponent.cpp:605 — GetSunIlluminanceOnGroundPostTransmittance
        /// @source UE5 HeightFogCommon.ush:319 — AtmosphereLightIlluminanceOnGroundPostTransmittance
        ///
        /// Find the primary atmosphere DirectionalLight, compute analytical
        /// transmittance through the atmosphere, and produce the post-transmittance
        /// sun illuminance that the fog shader uses for directional inscattering.
        {
            Vec3 sunDir      = {0.f, 0.f, 1.f}; // Default: straight up (no inscattering)
            Vec3 sunColor    = {1.f, 1.f, 1.f};
            float sunIntensity = 10.0f;
            bool hasSun = false;

            auto lightView = world->GetRegistry().view<DirectionalLightComponent>();
            for (auto ent : lightView) {
                auto& dl = lightView.get<DirectionalLightComponent>(ent);
                if (dl.atmosphereSunLight) {
                    sunDir       = dl.GetDirection();
                    sunColor     = dl.color;
                    sunIntensity = dl.intensity;
                    hasSun       = true;
                    break;
                }
            }

            // Compute atmospheric transmittance from sea level toward the sun.
            Vec3 transmittance = {1.f, 1.f, 1.f};
            if (hasSun && skyAtmoPtr) {
                transmittance = ComputeTransmittanceToSun(*skyAtmoPtr, sunDir);
            }

            /// @source UE5 DirectionalLightComponent.cpp:605
            /// SunIlluminanceOnGroundPostTransmittance = SunColor * Intensity * Transmittance
            fogCB.sunIllumPostTransR = sunColor.x * sunIntensity * transmittance.x;
            fogCB.sunIllumPostTransG = sunColor.y * sunIntensity * transmittance.y;
            fogCB.sunIllumPostTransB = sunColor.z * sunIntensity * transmittance.z;
            fogCB.sunIllumPostTransA = hasSun ? 1.0f : 0.0f;

            fogCB.sunDirX = sunDir.x;
            fogCB.sunDirY = sunDir.y;
            fogCB.sunDirZ = sunDir.z;
        }

        CBAllocation fogAlloc = cbRing.Allocate(sizeof(HeightFogCB));
        std::memcpy(fogAlloc.cpuPtr, &fogCB, sizeof(HeightFogCB));

        HeightFogPassDesc fogDesc{};
        fogDesc.backbuffer    = hdrRes;
        fogDesc.depthGBuffer  = depthRes;
        fogDesc.perFrameAlloc = perFrameAlloc;
        fogDesc.fogAlloc      = fogAlloc;
        fogDesc.pipeline      = pipelines->GetHeightFog();
        fogDesc.depthSrvIndex = depthSrvIndex;
        fogDesc.depthTexture  = depthTexture;
        fogDesc.bindless      = bindless;
        fogDesc.viewportWidth = vpWidth;
        fogDesc.viewportHeight = vpHeight;
        AddHeightFogPass(rgBuilder, fogDesc);

        break;  // First HeightFogComponent only
    }
}

} // namespace uz
