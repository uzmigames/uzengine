/// UzEngine Fog Renderer — Implementation.
///
/// Extracts HeightFogComponent from ECS, pre-computes RayOriginTerms,
/// and registers the height fog render graph pass.
/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering"

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
static float GetLuminance(const Vec3& color) noexcept
{
    return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
}

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogCB population — exponential height fog constant setup
// ─────────────────────────────────────────────────────────────────────────────

/// @ref Hillaire 2016 "Physically Based & Unified Volumetric Rendering"
/// @constants MaxObserverHeightDifference=65536.0f, CollapsedFogParameter clamp=(-125.0f, +126.0f)
///
/// Fills HeightFogCB from a HeightFogComponent. Key pre-computations:
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
    // Starts at FLT_MAX and reduces per-layer. Only layers with density > 0
    // contribute. Both layers share a single ObserverHeight.
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

    const float camZ = camera.GetPosition().z;
    const float observerHeight = std::min(camZ, maxObserverHeight);

    // ── Layer 1: CollapsedFogParameter (RayOriginTerms) ─────────────────
    // Clamp exponent to IEEE float safe range: [-125, +126] (= -126+1, +127-1).
    const float clampedPower0 = std::clamp(
        -fogComp.heightFalloff * (observerHeight - fogComp.baseHeight),
        -126.0f + 1.0f,
        +127.0f - 1.0f);
    const float rayOriginTerms0 = fogComp.density * std::pow(2.0f, clampedPower0);

    // ── Layer 2: CollapsedFogParameter ──────────────────────────────────
    float rayOriginTerms1 = 0.0f;
    if (fogComp.secondFogDensity > 0.0f) {
        const float clampedPower1 = std::clamp(
            -fogComp.secondFogHeightFalloff * (observerHeight - fogComp.secondFogBaseHeight),
            -126.0f + 1.0f,
            +127.0f - 1.0f);
        rayOriginTerms1 = fogComp.secondFogDensity * std::pow(2.0f, clampedPower1);
    }

    // ── Fill CB — Primary layer ─────────────────────────────────────────
    fogCB.fogColor = {fogComp.color.x, fogComp.color.y, fogComp.color.z, 0.0f};
    fogCB.fogDensity       = rayOriginTerms0;
    fogCB.fogHeightFalloff = fogComp.heightFalloff;
    fogCB.fogBaseHeight    = fogComp.baseHeight;

    // Store FogMaxOpacity directly; the shader computes (1 - fogMaxOpacity).
    fogCB.fogMaxOpacity    = fogComp.maxOpacity;
    fogCB.fogStartDistance = fogComp.startDistance;
    fogCB.fogCutoffDistance = fogComp.fogCutoffDistance;
    fogCB.maxObserverHeight = maxObserverHeight;
    fogCB.rawDensity = fogComp.density;

    // ── Fill CB — Second layer ──────────────────────────────────────────
    fogCB.secondFogDensity       = rayOriginTerms1;
    fogCB.secondFogHeightFalloff = (fogComp.secondFogDensity > 0.0f)
        ? fogComp.secondFogHeightFalloff : 0.0f;
    fogCB.secondFogRawDensity    = fogComp.secondFogDensity;
    fogCB.secondFogBaseHeight    = fogComp.secondFogBaseHeight;

    // ── Directional inscattering ────────────────────────────────────────
    // DirectionalInscatteringColor is scaled by the sun light's luminance.
    const float sunLuminance = hasSun ? GetLuminance(sunColor) : 0.0f;

    fogCB.directionalInscatteringColor = {
        fogComp.directionalInscatteringColor.x * sunLuminance,
        fogComp.directionalInscatteringColor.y * sunLuminance,
        fogComp.directionalInscatteringColor.z * sunLuminance,
        0.0f};

    fogCB.directionalInscatteringExponent = fogComp.directionalInscatteringExponent;
    fogCB.directionalInscatteringStartDistance = fogComp.directionalInscatteringStartDistance;

    // ── Sky atmosphere injection ────────────────────────────────────────
    fogCB.skyAtmContribColorScale = {1.0f, 1.0f, 1.0f, 0.0f};
    fogCB.heightFogContribution = skyAtmoPtr ? skyAtmoPtr->heightFogContribution : 0.0f;

    fogCB.skyLightColorR = skyLightColorR;
    fogCB.skyLightColorG = skyLightColorG;
    fogCB.skyLightColorB = skyLightColorB;

    // ── Sun post-transmittance illuminance ──────────────────────────────
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
        // Fog only uses a single atmosphere light (index 0).
        Vec3 sunDir      = {0.0f, 0.0f, 1.0f}; // Default: straight up (no inscattering)
        Vec3 sunColor    = {1.0f, 1.0f, 1.0f};
        float sunIntensity = 10.0f;
        bool hasSun = false;

        auto lightView = world->GetRegistry().view<DirectionalLightComponent>();
        for (auto ent : lightView) {
            auto& dl = lightView.get<DirectionalLightComponent>(ent);
            if (dl.atmosphereSunLight) {
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
