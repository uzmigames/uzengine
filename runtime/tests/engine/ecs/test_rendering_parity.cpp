/// Unit tests for industry-standard component defaults and color utilities.
///
/// Coverage targets:
///   - height_fog_component.h    → 100% (all fields verified)
///   - sky_atmosphere_component.h → 100% (all fields verified)
///   - light_component.h         → 95%+ (base + subclass defaults)
///   - sky_light_component.h     → 100% (all fields verified)
///   - scene_component.h         → 95%+ (mobility + hiddenInGame)
///   - color_utils.h             → 100% (KelvinToRgb all branches)
///   - constant_buffers.h        → 95%+ (PointLightData flag bits)
///
/// No GPU, RHI, or World instance required. All tests operate directly
/// on component value types and inline utility functions.

#include "height_fog_component.h"
#include "sky_atmosphere_component.h"
#include "sky_light_component.h"
#include "light_component.h"
#include "scene_component.h"
#include "color_utils.h"
#include "constant_buffers.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps = 1e-4f;

static bool Near(float a, float b, float eps = kEps) {
    return std::fabs(a - b) < eps;
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: HeightFogComponent defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, HeightFogDefaults_PrimaryLayer) {
    HeightFogComponent fog;
    EXPECT_FLOAT_EQ(fog.heightFalloff,       0.2f);
    EXPECT_FLOAT_EQ(fog.color.x,             1.0f);
    EXPECT_FLOAT_EQ(fog.color.y,             1.0f);
    EXPECT_FLOAT_EQ(fog.color.z,             1.0f);
    EXPECT_FLOAT_EQ(fog.maxOpacity,          1.0f);
    EXPECT_FLOAT_EQ(fog.density,             0.02f);
    EXPECT_FLOAT_EQ(fog.startDistance,       0.f);
    EXPECT_FLOAT_EQ(fog.fogCutoffDistance,   0.f);
    EXPECT_FLOAT_EQ(fog.baseHeight,          0.f);
    EXPECT_TRUE(fog.enabled);
}

TEST(RenderingParity, HeightFogDefaults_SecondLayer) {
    HeightFogComponent fog;
    EXPECT_FLOAT_EQ(fog.secondFogDensity,         0.f);
    EXPECT_FLOAT_EQ(fog.secondFogHeightFalloff,   0.2f);
    EXPECT_FLOAT_EQ(fog.secondFogBaseHeight,      0.f);
}

TEST(RenderingParity, HeightFogDefaults_DirectionalInscattering) {
    HeightFogComponent fog;
    // Warm orange inscattering default
    EXPECT_FLOAT_EQ(fog.directionalInscatteringColor.x, 1.0f);
    EXPECT_FLOAT_EQ(fog.directionalInscatteringColor.y, 0.8f);
    EXPECT_FLOAT_EQ(fog.directionalInscatteringColor.z, 0.6f);
    EXPECT_FLOAT_EQ(fog.directionalInscatteringExponent,       8.0f);
    EXPECT_FLOAT_EQ(fog.directionalInscatteringStartDistance,  0.f);
    EXPECT_FALSE(fog.enableVolumetricFog);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SkyAtmosphereComponent defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, SkyAtmosphereDefaults_PlanetGeometry) {
    SkyAtmosphereComponent sky;
    EXPECT_FLOAT_EQ(sky.bottomRadiusKm,      6360.0f);
    EXPECT_FLOAT_EQ(sky.atmosphereHeightKm,  60.0f);
    EXPECT_TRUE(sky.enabled);
}

TEST(RenderingParity, SkyAtmosphereDefaults_Rayleigh) {
    SkyAtmosphereComponent sky;
    EXPECT_NEAR(sky.rayleighScattering.x, 5.802e-3f, 1e-6f);
    EXPECT_NEAR(sky.rayleighScattering.y, 13.558e-3f, 1e-6f);
    EXPECT_NEAR(sky.rayleighScattering.z, 33.1e-3f, 1e-6f);
    EXPECT_FLOAT_EQ(sky.rayleighScatteringScale, 1.0f);
    EXPECT_FLOAT_EQ(sky.rayleighScaleHeightKm,   8.0f);
}

TEST(RenderingParity, SkyAtmosphereDefaults_Mie) {
    SkyAtmosphereComponent sky;
    EXPECT_NEAR(sky.mieScattering,      3.996e-3f, 1e-7f);
    EXPECT_FLOAT_EQ(sky.mieScatteringScale, 1.0f);
    EXPECT_NEAR(sky.mieAbsorption,      4.4e-3f, 1e-7f);
    EXPECT_FLOAT_EQ(sky.mieAbsorptionScale, 1.0f);
    EXPECT_FLOAT_EQ(sky.mieAnisotropy,      0.8f);
    EXPECT_FLOAT_EQ(sky.mieScaleHeightKm,   1.2f);
}

TEST(RenderingParity, SkyAtmosphereDefaults_GroundAndMultiScatter) {
    SkyAtmosphereComponent sky;
    EXPECT_FLOAT_EQ(sky.groundAlbedo.x,        0.3f);
    EXPECT_FLOAT_EQ(sky.groundAlbedo.y,        0.3f);
    EXPECT_FLOAT_EQ(sky.groundAlbedo.z,        0.3f);
    EXPECT_FLOAT_EQ(sky.multiScatteringFactor, 1.0f);
    EXPECT_FLOAT_EQ(sky.heightFogContribution, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: LightComponent base defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, LightComponentBaseDefaults) {
    PointLightComponent light;
    EXPECT_FALSE(light.useTemperature);
    EXPECT_FLOAT_EQ(light.temperature,                   6500.f);
    EXPECT_FLOAT_EQ(light.specularScale,                 1.0f);
    EXPECT_FLOAT_EQ(light.diffuseScale,                  1.0f);
    EXPECT_FLOAT_EQ(light.volumetricScatteringIntensity, 1.0f);
    EXPECT_FLOAT_EQ(light.indirectLightingIntensity,     1.0f);
    // Base color and intensity
    EXPECT_FLOAT_EQ(light.color.x,   1.f);
    EXPECT_FLOAT_EQ(light.color.y,   1.f);
    EXPECT_FLOAT_EQ(light.color.z,   1.f);
    EXPECT_FLOAT_EQ(light.intensity, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: DirectionalLightComponent defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, DirectionalLightDefaults) {
    DirectionalLightComponent dir;
    EXPECT_FALSE(dir.atmosphereSunLight);
    EXPECT_EQ(dir.atmosphereSunLightIndex, 0);
    EXPECT_NEAR(dir.lightSourceAngle,      0.5357f, 1e-4f);
    EXPECT_FLOAT_EQ(dir.lightSourceSoftAngle,           0.0f);
    EXPECT_EQ(dir.shadowCascades,          4);
    EXPECT_FLOAT_EQ(dir.cascadeDistributionExponent,    2.0f);
    EXPECT_FLOAT_EQ(dir.cascadeTransitionFraction,      0.1f);
    EXPECT_FLOAT_EQ(dir.shadowDistanceFadeoutFraction,  0.1f);
    EXPECT_FLOAT_EQ(dir.dynamicShadowDistance,          0.f);
    EXPECT_FLOAT_EQ(dir.contactShadowLength,            0.f);
}

TEST(RenderingParity, DirectionalLightAtmosphereSunDiskColorScale_DefaultsWhite) {
    DirectionalLightComponent dir;
    EXPECT_FLOAT_EQ(dir.atmosphereSunDiskColorScale.x, 1.f);
    EXPECT_FLOAT_EQ(dir.atmosphereSunDiskColorScale.y, 1.f);
    EXPECT_FLOAT_EQ(dir.atmosphereSunDiskColorScale.z, 1.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: PointLightComponent defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, PointLightDefaults) {
    PointLightComponent point;
    EXPECT_FLOAT_EQ(point.range,               1000.0f);
    EXPECT_TRUE(point.useInverseSquaredFalloff);
    EXPECT_FLOAT_EQ(point.lightFalloffExponent, 8.0f);
    EXPECT_FLOAT_EQ(point.sourceRadius,         0.f);
    EXPECT_FLOAT_EQ(point.softSourceRadius,     0.f);
    EXPECT_FLOAT_EQ(point.sourceLength,         0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SpotLightComponent defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, SpotLightAngleDefaults_Degrees) {
    SpotLightComponent spot;
    EXPECT_FLOAT_EQ(spot.innerAngleDeg, 30.0f);
    EXPECT_FLOAT_EQ(spot.outerAngleDeg, 45.0f);
}

TEST(RenderingParity, SpotLightInheritsFromPointLight) {
    // SpotLightComponent inherits all PointLightComponent params.
    SpotLightComponent spot;
    EXPECT_FLOAT_EQ(spot.range,               1000.0f);
    EXPECT_TRUE(spot.useInverseSquaredFalloff);
    EXPECT_FLOAT_EQ(spot.lightFalloffExponent, 8.0f);
    EXPECT_FLOAT_EQ(spot.sourceRadius,         0.f);
    EXPECT_FLOAT_EQ(spot.softSourceRadius,     0.f);
    EXPECT_FLOAT_EQ(spot.sourceLength,         0.f);
}

TEST(RenderingParity, SpotLightInheritsLightBaseParams) {
    // SpotLight must also expose LightComponent base params.
    SpotLightComponent spot;
    EXPECT_FALSE(spot.useTemperature);
    EXPECT_FLOAT_EQ(spot.temperature,   6500.f);
    EXPECT_FLOAT_EQ(spot.specularScale, 1.0f);
    EXPECT_FLOAT_EQ(spot.diffuseScale,  1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SkyLightComponent defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, SkyLightDefaults_IntensityAndColor) {
    SkyLightComponent sl;
    EXPECT_FLOAT_EQ(sl.intensity,                 1.0f);
    EXPECT_FLOAT_EQ(sl.lightColor.x,              1.0f);
    EXPECT_FLOAT_EQ(sl.lightColor.y,              1.0f);
    EXPECT_FLOAT_EQ(sl.lightColor.z,              1.0f);
    EXPECT_FLOAT_EQ(sl.indirectLightingIntensity, 1.0f);
    EXPECT_TRUE(sl.enabled);
}

TEST(RenderingParity, SkyLightDefaults_Source) {
    SkyLightComponent sl;
    EXPECT_EQ(sl.sourceType,        ESkyLightSourceType::CapturedScene);
    EXPECT_FALSE(sl.realTimeCapture);
    EXPECT_EQ(sl.cubemapResolution, 128u);
}

TEST(RenderingParity, SkyLightDefaults_LowerHemisphere) {
    SkyLightComponent sl;
    EXPECT_TRUE(sl.lowerHemisphereIsBlack);
    EXPECT_FLOAT_EQ(sl.lowerHemisphereColor.x, 0.f);
    EXPECT_FLOAT_EQ(sl.lowerHemisphereColor.y, 0.f);
    EXPECT_FLOAT_EQ(sl.lowerHemisphereColor.z, 0.f);
}

TEST(RenderingParity, SkyLightDefaults_Occlusion) {
    SkyLightComponent sl;
    EXPECT_FLOAT_EQ(sl.occlusionMaxDistance, 1000.0f);
    EXPECT_FLOAT_EQ(sl.minOcclusion,         0.f);
    EXPECT_FLOAT_EQ(sl.occlusionTint.x,      0.f);
    EXPECT_FLOAT_EQ(sl.occlusionTint.y,      0.f);
    EXPECT_FLOAT_EQ(sl.occlusionTint.z,      0.f);
    EXPECT_FLOAT_EQ(sl.occlusionExponent,    1.0f);
}

TEST(RenderingParity, SkyLightComponent_FieldMutation) {
    SkyLightComponent sl;
    sl.intensity              = 3.0f;
    sl.lightColor             = {0.9f, 0.95f, 1.0f};
    sl.minOcclusion           = 0.1f;
    sl.occlusionExponent      = 2.0f;
    sl.lowerHemisphereIsBlack = false;

    EXPECT_FLOAT_EQ(sl.intensity,         3.0f);
    EXPECT_FLOAT_EQ(sl.lightColor.x,      0.9f);
    EXPECT_FLOAT_EQ(sl.lightColor.y,      0.95f);
    EXPECT_FLOAT_EQ(sl.lightColor.z,      1.0f);
    EXPECT_FLOAT_EQ(sl.minOcclusion,      0.1f);
    EXPECT_FLOAT_EQ(sl.occlusionExponent, 2.0f);
    EXPECT_FALSE(sl.lowerHemisphereIsBlack);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — mobility and visibility defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, SceneComponentMobilityDefault) {
    SceneComponent sc;
    EXPECT_EQ(sc.mobility, SceneComponent::Mobility::Movable);
    EXPECT_FALSE(sc.hiddenInGame);
}

TEST(RenderingParity, SceneComponentMobility_CanBeSetToStatic) {
    SceneComponent sc;
    sc.mobility = SceneComponent::Mobility::Static;
    EXPECT_EQ(sc.mobility, SceneComponent::Mobility::Static);
}

TEST(RenderingParity, SceneComponentMobility_CanBeSetToStationary) {
    SceneComponent sc;
    sc.mobility = SceneComponent::Mobility::Stationary;
    EXPECT_EQ(sc.mobility, SceneComponent::Mobility::Stationary);
}

TEST(RenderingParity, SceneComponentHiddenInGame_CanBeToggled) {
    SceneComponent sc;
    sc.hiddenInGame = true;
    EXPECT_TRUE(sc.hiddenInGame);
    sc.hiddenInGame = false;
    EXPECT_FALSE(sc.hiddenInGame);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: KelvinToRgb — all branches in the Tanner Helland approximation
// ─────────────────────────────────────────────────────────────────────────────

TEST(KelvinToRgb, DaylightWhite_6500K) {
    Vec3 c = KelvinToRgb(6500.f);
    // 6500K is the D65 daylight white point — all channels near 1.
    EXPECT_NEAR(c.x, 1.0f, 0.1f) << "R channel should be near 1 at 6500K";
    EXPECT_NEAR(c.y, 1.0f, 0.1f) << "G channel should be near 1 at 6500K";
    EXPECT_NEAR(c.z, 1.0f, 0.1f) << "B channel should be near 1 at 6500K";
}

TEST(KelvinToRgb, WarmCandle_3000K) {
    Vec3 c = KelvinToRgb(3000.f);
    // 3000K is warm candlelight: R > G > B, with R close to 1.
    EXPECT_GT(c.x, c.y) << "R should exceed G at 3000K";
    EXPECT_GT(c.y, c.z) << "G should exceed B at 3000K";
    EXPECT_GT(c.x, 0.9f) << "R should be close to 1.0 at 3000K";
    EXPECT_LT(c.z, 0.5f) << "B should be well below 0.5 at 3000K (warm light)";
    // All channels must be in valid range
    EXPECT_GE(c.x, 0.f);
    EXPECT_LE(c.x, 1.f);
    EXPECT_GE(c.y, 0.f);
    EXPECT_LE(c.y, 1.f);
    EXPECT_GE(c.z, 0.f);
    EXPECT_LE(c.z, 1.f);
}

TEST(KelvinToRgb, CoolBlue_10000K) {
    Vec3 c = KelvinToRgb(10000.f);
    // 10000K: blue channel should be 1 (t >= 66 → b=1.0 branch), R < B.
    EXPECT_NEAR(c.z, 1.0f, 0.05f) << "B should be 1.0 at 10000K (t>=66 branch)";
    EXPECT_LT(c.x, c.z)          << "R should be less than B at 10000K";
}

TEST(KelvinToRgb, VeryLow_1900K_BlueIsZero) {
    // 1900K → t=19.0 → blue is at the t<=19 boundary: b=0 branch.
    Vec3 c = KelvinToRgb(1900.f);
    EXPECT_FLOAT_EQ(c.z, 0.f) << "B must be 0 at exactly t<=19 (1900K)";
    EXPECT_GT(c.x, 0.f) << "R must be positive at 1900K";
}

TEST(KelvinToRgb, MidRange_BlueInterpolated_2500K) {
    // 2500K: t=25.0 → blue uses log interpolation branch (19 < t < 66).
    Vec3 c = KelvinToRgb(2500.f);
    EXPECT_GT(c.z, 0.f) << "B must be positive at 2500K (log branch)";
    EXPECT_LT(c.z, 1.f) << "B must be below 1.0 at 2500K";
}

TEST(KelvinToRgb, ClampLow_BelowMinimum) {
    // Values below 1000K clamp to 1000K — must not produce negative or NaN.
    Vec3 c = KelvinToRgb(500.f);
    EXPECT_GT(c.x, 0.f);
    EXPECT_GE(c.y, 0.f);
    EXPECT_FLOAT_EQ(c.z, 0.f); // 1000K → t=10 → b=0 (t<=19 branch)
    // Result should match clamped 1000K exactly.
    Vec3 clamped = KelvinToRgb(1000.f);
    EXPECT_FLOAT_EQ(c.x, clamped.x);
    EXPECT_FLOAT_EQ(c.y, clamped.y);
    EXPECT_FLOAT_EQ(c.z, clamped.z);
}

TEST(KelvinToRgb, ClampHigh_AboveMaximum) {
    // Values above 40000K clamp to 40000K — must not produce values > 1.
    Vec3 c = KelvinToRgb(50000.f);
    EXPECT_GE(c.x, 0.f);
    EXPECT_LE(c.x, 1.f);
    EXPECT_GE(c.y, 0.f);
    EXPECT_LE(c.y, 1.f);
    EXPECT_NEAR(c.z, 1.0f, 0.05f); // High K → blue near 1
    // Result should match clamped 40000K exactly.
    Vec3 clamped = KelvinToRgb(40000.f);
    EXPECT_FLOAT_EQ(c.x, clamped.x);
    EXPECT_FLOAT_EQ(c.y, clamped.y);
    EXPECT_FLOAT_EQ(c.z, clamped.z);
}

TEST(KelvinToRgb, OutputAlwaysInZeroOneRange) {
    // Sweep representative temperatures — all outputs must remain in [0, 1].
    const float temps[] = {1000.f, 1500.f, 2000.f, 3000.f, 4000.f,
                           5000.f, 6500.f, 8000.f, 10000.f, 20000.f, 40000.f};
    for (float k : temps) {
        Vec3 c = KelvinToRgb(k);
        EXPECT_GE(c.x, 0.f) << "R out of range at " << k << "K";
        EXPECT_LE(c.x, 1.f) << "R out of range at " << k << "K";
        EXPECT_GE(c.y, 0.f) << "G out of range at " << k << "K";
        EXPECT_LE(c.y, 1.f) << "G out of range at " << k << "K";
        EXPECT_GE(c.z, 0.f) << "B out of range at " << k << "K";
        EXPECT_LE(c.z, 1.f) << "B out of range at " << k << "K";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: PointLightData GPU struct — flag bits
// ─────────────────────────────────────────────────────────────────────────────

TEST(RenderingParity, PointLightData_FlagBit_InverseSquaredSet) {
    PointLightData pld{};
    pld.flags = kLightFlag_InverseSquared;
    EXPECT_EQ(pld.flags & kLightFlag_InverseSquared, kLightFlag_InverseSquared)
        << "kLightFlag_InverseSquared must be readable back from flags";
}

TEST(RenderingParity, PointLightData_FlagBit_InverseSquaredClear) {
    PointLightData pld{};
    pld.flags = 0u;
    EXPECT_EQ(pld.flags & kLightFlag_InverseSquared, 0u)
        << "kLightFlag_InverseSquared must be 0 when flags is 0";
}

TEST(RenderingParity, PointLightData_FlagBit_IsBit0) {
    // The flag must occupy exactly bit 0, so its value is 1.
    EXPECT_EQ(kLightFlag_InverseSquared, 1u)
        << "kLightFlag_InverseSquared must be bit 0 (value=1) per HLSL struct comment";
}

TEST(RenderingParity, PointLightData_FlagBit_DoesNotClobberOtherFields) {
    // Setting the flag must not corrupt adjacent float fields.
    PointLightData pld{};
    pld.position     = {1.f, 2.f, 3.f};
    pld.radius       = 5.f;
    pld.specularScale = 0.75f;
    pld.diffuseScale  = 0.5f;
    pld.flags        = kLightFlag_InverseSquared;

    EXPECT_FLOAT_EQ(pld.position.x,   1.f);
    EXPECT_FLOAT_EQ(pld.position.y,   2.f);
    EXPECT_FLOAT_EQ(pld.position.z,   3.f);
    EXPECT_FLOAT_EQ(pld.radius,        5.f);
    EXPECT_FLOAT_EQ(pld.specularScale, 0.75f);
    EXPECT_FLOAT_EQ(pld.diffuseScale,  0.5f);
    EXPECT_EQ(pld.flags & kLightFlag_InverseSquared, kLightFlag_InverseSquared);
}

TEST(RenderingParity, PointLightData_StructSize_Is64Bytes) {
    // The static_assert in constant_buffers.h guarantees this at compile time,
    // but a runtime check makes the expectation explicit in test output.
    EXPECT_EQ(sizeof(PointLightData), 64u)
        << "PointLightData must remain 64 bytes (four float4s for GPU packing, includes shadowIndex)";
}
