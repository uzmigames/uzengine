/// UzEngine Fog Renderer — Implementation.
///
/// Extracts HeightFogComponent from ECS, pre-computes UE5 RayOriginTerms,
/// and registers the height fog render graph pass.
///
/// @source UE5 FogRendering.cpp:208-304 — InitFogConstants
/// @source UE5 FogRendering.cpp:63-94  — SetupFogUniformParameters

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
#include <cfloat>

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

// ─────────────────────────────────────────────────────────────────────────────
// Luminance helper
// ─────────────────────────────────────────────────────────────────────────────

/// Compute Rec.709 luminance from linear RGB color.
/// @source UE5 ColorManagement.h — GetLuminance() uses Rec.709 coefficients.
static float GetLuminance(const Vec3& color) noexcept
{
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogCB population — UE5 InitFogConstants parity
// ─────────────────────────────────────────────────────────────────────────────

/// @source UE5 FogRendering.cpp:208-304 — FSceneRenderer::InitFogConstants
/// @source UE5 FogRendering.cpp:63-94  — SetupFogUniformParameters
/// @constants MaxObserverHeightDifference=65536.0f (FogRendering.cpp:223)
/// @constants CollapsedFogParameter clamp=(-125.0f, +126.0f) (FogRendering.cpp:247-250)
///
/// Fills HeightFogCB from a HeightFogComponent, matching UE5's InitFogConstants
/// computation exactly. Key pre-computations:
///
///   1. MaxObserverHeight = min across all active layers of (baseHeight + 65536)
///   2. ObserverHeight = min(cameraZ, MaxObserverHeight)   [single shared value]
///   3. Per-layer RayOriginTerms = density * pow(2, clamp(-falloff*(obsH-baseH), -125, 126))
///
/// The shader receives the pre-computed RayOriginTerms and also the raw density
/// so it can re-compute RayOriginTerms at the start-distance exclusion point.
static void PopulateHeightFogCB(
    HeightFogCB& fogCB,
    const HeightFogComponent& fogComp,
    const Camera& camera,
    const SkyAtmosphereComponent* skyAtmoPtr,
    float skyLightColorR,
    float skyLightColorG,
    float skyLightColorB,
    const Vec3& sunDir,
    const Vec3& sunColor,
    float sunIntensity,
    bool hasSun) noexcept
{
    // ── MaxObserverHeight computation ────────────────────────────────────
    // @source UE5 FogRendering.cpp:223-243
    //
    // UE5 starts MaxObserverHeight at FLT_MAX and reduces it per-layer.
    // Only layers with density > 0 contribute to the clamp.
    // Both layers share the same ObserverHeight (single min across all).
    constexpr float kMaxObserverHeightDiff = 65536.0f;

    float maxObserverHeight = FLT_MAX;

    if (fogComp.density > 0.0f) {
        maxObserverHeight = std::min(maxObserverHeight,
            fogComp.baseHeight + kMaxObserverHeightDiff);
    }
    if (fogComp.secondFogDensity > 0.0f) {
        maxObserverHeight = std::min(maxObserverHeight,
            fogComp.secondFogBaseHeight + kMaxObserverHeightDiff);
    }

    // @source UE5 FogRendering.cpp:243 — ObserverHeight = min(CameraZ, MaxObserverHeight)
    const float camZ = camera.GetPosition().z;
    const float observerHeight = std::min(camZ, maxObserverHeight);

    // ── Layer 1: CollapsedFogParameter (RayOriginTerms) ─────────────────
    // @source UE5 FogRendering.cpp:245-254
    // clamp exponent to IEEE float safe range: [-125, +126] (= -126+1, +127-1)
    const float clampedPower0 = std::clamp(
        -fogComp.heightFalloff * (observerHeight - fogComp.baseHeight),
        -126.0f + 1.0f,
        +127.0f - 1.0f);
    const float rayOriginTerms0 = fogComp.density * std::pow(2.0f, clampedPower0);

    // ── Layer 2: CollapsedFogParameter ──────────────────────────────────
    // @source UE5 FogRendering.cpp:245-254 (same loop, i=1)
    float rayOriginTerms1 = 0.0f;
    if (fogComp.secondFogDensity > 0.0f) {
        const float clampedPower1 = std::clamp(
            -fogComp.secondFogHeightFalloff * (observerHeight - fogComp.secondFogBaseHeight),
            -126.0f + 1.0f,
            +127.0f - 1.0f);
        rayOriginTerms1 = fogComp.secondFogDensity * std::pow(2.0f, clampedPower1);
    }

    // ── Fill CB — Primary layer ─────────────────────────────────────────
    // @source UE5 FogRendering.cpp:258 — ExponentialFogColor
    fogCB.fogColor = {fogComp.color.x, fogComp.color.y, fogComp.color.z, 0.0f};

    // @source UE5 FogRendering.cpp:256 — ExponentialFogParameters
    // .x = CollapsedFogParameter[0] (precomputed RayOriginTerms)
    // .y = FogData[0].HeightFalloff
    // .z = MaxObserverHeight
    // .w = StartDistance
    fogCB.fogDensity       = rayOriginTerms0;
    fogCB.fogHeightFalloff = fogComp.heightFalloff;
    fogCB.fogBaseHeight    = fogComp.baseHeight;

    // @source UE5 FogRendering.cpp:259 — ExponentialFogColorParameter.w = 1 - FogMaxOpacity
    // We store FogMaxOpacity directly; the shader computes (1 - fogMaxOpacity).
    fogCB.fogMaxOpacity    = fogComp.maxOpacity;

    // @source UE5 FogRendering.cpp:256 — ExponentialFogParameters.w = StartDistance
    fogCB.fogStartDistance = fogComp.startDistance;

    // @source UE5 FogRendering.cpp:260 — ExponentialFogParameters3.w = FogCutoffDistance
    fogCB.fogCutoffDistance = fogComp.fogCutoffDistance;

    // @source UE5 FogRendering.cpp:256 — ExponentialFogParameters.z = MaxObserverHeight
    fogCB.maxObserverHeight = maxObserverHeight;

    // @source UE5 FogRendering.cpp:260 — ExponentialFogParameters3.x = FogData[0].Density (raw)
    fogCB.rawDensity = fogComp.density;

    // ── Fill CB — Second layer ──────────────────────────────────────────
    // @source UE5 FogRendering.cpp:257 — ExponentialFogParameters2
    // .x = CollapsedFogParameter[1], .y = HeightFalloff, .z = Density(raw), .w = Height
    fogCB.secondFogDensity       = rayOriginTerms1;
    fogCB.secondFogHeightFalloff = (fogComp.secondFogDensity > 0.0f)
        ? fogComp.secondFogHeightFalloff : 0.0f;
    fogCB.secondFogRawDensity    = fogComp.secondFogDensity;
    fogCB.secondFogBaseHeight    = fogComp.secondFogBaseHeight;

    // ── Directional inscattering ────────────────────────────────────────
    // @source UE5 FogRendering.cpp:283 — DirectionalInscatteringColor * SunLight luminance
    // UE5 scales the artistic DirectionalInscatteringColor by the sun light's luminance.
    const float sunLuminance = hasSun ? GetLuminance(sunColor) : 0.0f;

    fogCB.directionalInscatteringColor = {
        fogComp.directionalInscatteringColor.x * sunLuminance,
        fogComp.directionalInscatteringColor.y * sunLuminance,
        fogComp.directionalInscatteringColor.z * sunLuminance,
        0.0f};

    // @source UE5 FogRendering.cpp:276 — DirectionalInscatteringExponent
    fogCB.directionalInscatteringExponent = fogComp.directionalInscatteringExponent;

    // @source UE5 FogRendering.cpp:277 — DirectionalInscatteringStartDistance
    fogCB.directionalInscatteringStartDistance = fogComp.directionalInscatteringStartDistance;

    // ── Sky atmosphere injection ────────────────────────────────────────
    // @source UE5 FogRendering.cpp:267 — SkyAtmosphereAmbientContributionColorScale
    fogCB.skyAtmContribColorScale = {1.0f, 1.0f, 1.0f, 0.0f};

    // @source UE5 HeightFogCommon.ush:174-180 — heightFogContribution
    fogCB.heightFogContribution = skyAtmoPtr ? skyAtmoPtr->heightFogContribution : 0.0f;

    fogCB.skyLightColorR = skyLightColorR;
    fogCB.skyLightColorG = skyLightColorG;
    fogCB.skyLightColorB = skyLightColorB;

    // ── Sun post-transmittance illuminance ──────────────────────────────
    // @source UE5 DirectionalLightComponent.cpp:605 — GetSunIlluminanceOnGroundPostTransmittance
    // @source UE5 HeightFogCommon.ush:319 — AtmosphereLightIlluminanceOnGroundPostTransmittance[0]
    //
    // SunIlluminancePostTransmittance = SunColor * Intensity * Transmittance
    // Transmittance is computed analytically through the atmosphere toward the sun.
    Vec3 transmittance = {1.0f, 1.0f, 1.0f};
    if (hasSun && skyAtmoPtr) {
        transmittance = ComputeTransmittanceToSun(*skyAtmoPtr, sunDir);
    }

    fogCB.sunIllumPostTransR = sunColor.x * sunIntensity * transmittance.x;
    fogCB.sunIllumPostTransG = sunColor.y * sunIntensity * transmittance.y;
    fogCB.sunIllumPostTransB = sunColor.z * sunIntensity * transmittance.z;
    fogCB.sunIllumPostTransA = hasSun ? 1.0f : 0.0f;

    // @source UE5 FogRendering.cpp:282 — InscatteringLightDirection = -SunDir (toward sun)
    // Our sunDir parameter already points toward the sun (negated from light forward).
    fogCB.sunDirX = sunDir.x;
    fogCB.sunDirY = sunDir.y;
    fogCB.sunDirZ = sunDir.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render graph pass registration
// ─────────────────────────────────────────────────────────────────────────────

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

        // ── Find SkyAtmosphereComponent (first found, singleton semantics) ──
        const SkyAtmosphereComponent* skyAtmoPtr = nullptr;
        auto skyView = world->GetRegistry().view<SkyAtmosphereComponent>();
        for (auto skyEnt : skyView) {
            skyAtmoPtr = &skyView.get<SkyAtmosphereComponent>(skyEnt);
            break;
        }

        // ── Find primary atmosphere DirectionalLight ────────────────────
        // @source UE5 FogRendering.cpp:279 — AtmosphereLights[0] or SimpleDirectionalLight
        // Fog only uses a single atmosphere light (index 0).
        Vec3 sunDir      = {0.0f, 0.0f, 1.0f}; // Default: straight up (no inscattering)
        Vec3 sunColor    = {1.0f, 1.0f, 1.0f};
        float sunIntensity = 10.0f;
        bool hasSun = false;

        auto lightView = world->GetRegistry().view<DirectionalLightComponent>();
        for (auto ent : lightView) {
            auto& dl = lightView.get<DirectionalLightComponent>(ent);
            if (dl.atmosphereSunLight) {
                // @source UE5 FogRendering.cpp:282 — InscatteringLightDirection = -SunLight->GetDirection()
                // GetDirection() returns the light's forward vector (toward scene).
                // Negate to get direction toward the sun (away from scene).
                sunDir       = {-dl.GetDirection().x, -dl.GetDirection().y, -dl.GetDirection().z};
                sunColor     = dl.color;
                sunIntensity = dl.intensity;
                hasSun       = true;
                break;
            }
        }

        // ── Populate HeightFogCB ────────────────────────────────────────
        HeightFogCB fogCB{};
        PopulateHeightFogCB(fogCB, fogComp, camera, skyAtmoPtr,
                            skyLightColorR, skyLightColorG, skyLightColorB,
                            sunDir, sunColor, sunIntensity, hasSun);

        // ── Upload to GPU and register render graph pass ────────────────
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

        break;  // First HeightFogComponent only (singleton semantics)
    }
}

} // namespace uz
