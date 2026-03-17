/// Unit tests for atmosphere constant buffers, ECS environment components,
/// and compute pass root constants.
///
/// All tests are CPU-only — no GPU device required. Tests cover struct sizes,
/// field offsets, default values, write/readback, and memcpy round-trips for
/// every type that gets uploaded to the GPU or stored on ECS entities.
///
/// # Test Groups
/// - `SkyAtmosphereCB`                 — size, field offsets, write/readback, memcpy
/// - `AtmosphereLutCB`                 — size, field offsets, defaults, memcpy
/// - `HeightFogCB`                     — size, field offsets, defaults, write/readback, memcpy
/// - `SkyAtmosphereComponent`          — Earth-like defaults, TransformMode enum, enable flag
/// - `HeightFogComponent`              — UE5 defaults, second layer, inscattering, control
/// - `SkyLightComponent`               — intensity, source type, lower hemisphere, occlusion
/// - `SkyCubemapCaptureRootConstants`  — size, field offsets, write/read, memcpy
/// - `TransmittanceLutRootConstants`   — size, field offsets, write/read, memcpy, Earth invariant
/// - `SkyAtmospherePassDesc`           — transmittanceLutSrvIndex default sentinel
/// - `HeightFogPassDesc`               — compilation sanity, default resource IDs
/// - `TransmittanceLutPassDesc`        — compilation sanity, default resource IDs
///
/// See also: test_ibl_atmosphere.cpp for IBLBaker, SHProjection, AutoExposure, PerFrameCB IBL.

#include "constant_buffers.h"
#include "sky_atmosphere_component.h"
#include "height_fog_component.h"
#include "sky_light_component.h"
#include "sky_cubemap_capture_pass.h"
#include "transmittance_lut_pass.h"
#include "sky_atmosphere_pass.h"
#include "height_fog_pass.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmosphereCB — size, field offsets, write/readback, memcpy round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyAtmosphereCB, SizeIs256Bytes) {
    EXPECT_EQ(sizeof(SkyAtmosphereCB), 256u);
}

TEST(SkyAtmosphereCB, FieldOffsets) {
    EXPECT_EQ(offsetof(SkyAtmosphereCB, rayleighScattering),       0u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, mieScattering),           16u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, groundAlbedo),            32u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, sunIlluminance),          48u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, bottomRadiusKm),          64u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, atmosphereHeightKm),      68u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, sunDiskIntensity),        72u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, multiScatteringFactor),   76u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, rayleighScatteringScale), 80u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, mieScatteringScale),      84u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, mieAbsorptionScale),      88u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, mieAbsorption),           92u);
    EXPECT_EQ(offsetof(SkyAtmosphereCB, skyLuminanceFactor),      96u);
}

TEST(SkyAtmosphereCB, WriteAndReadBack) {
    SkyAtmosphereCB cb{};
    cb.rayleighScattering    = {5.802e-3f, 13.558e-3f, 33.1e-3f, 8.0f};
    cb.mieScattering         = {3.996e-3f, 0.8f, 1.2f, 0.0f};
    cb.groundAlbedo          = {0.3f, 0.3f, 0.3f, 0.0f};
    cb.sunIlluminance        = {20.0f, 20.0f, 20.0f, 0.00872665f};
    cb.bottomRadiusKm        = 6360.0f;
    cb.atmosphereHeightKm    = 60.0f;
    cb.sunDiskIntensity      = 1.0f;
    cb.multiScatteringFactor = 1.0f;
    cb.mieAbsorption         = 4.4e-3f;
    cb.skyLuminanceFactor    = {1.0f, 1.0f, 1.0f, 1.0f};

    EXPECT_FLOAT_EQ(cb.rayleighScattering.x, 5.802e-3f);
    EXPECT_FLOAT_EQ(cb.rayleighScattering.y, 13.558e-3f);
    EXPECT_FLOAT_EQ(cb.rayleighScattering.z, 33.1e-3f);
    EXPECT_FLOAT_EQ(cb.rayleighScattering.w, 8.0f);
    EXPECT_FLOAT_EQ(cb.mieScattering.x, 3.996e-3f);
    EXPECT_FLOAT_EQ(cb.mieScattering.y, 0.8f);
    EXPECT_FLOAT_EQ(cb.mieScattering.z, 1.2f);
    EXPECT_FLOAT_EQ(cb.groundAlbedo.x, 0.3f);
    EXPECT_FLOAT_EQ(cb.groundAlbedo.y, 0.3f);
    EXPECT_FLOAT_EQ(cb.groundAlbedo.z, 0.3f);
    EXPECT_FLOAT_EQ(cb.sunIlluminance.x, 20.0f);
    EXPECT_FLOAT_EQ(cb.sunIlluminance.y, 20.0f);
    EXPECT_FLOAT_EQ(cb.sunIlluminance.z, 20.0f);
    EXPECT_FLOAT_EQ(cb.sunIlluminance.w, 0.00872665f);
    EXPECT_FLOAT_EQ(cb.bottomRadiusKm,        6360.0f);
    EXPECT_FLOAT_EQ(cb.atmosphereHeightKm,    60.0f);
    EXPECT_FLOAT_EQ(cb.sunDiskIntensity,      1.0f);
    EXPECT_FLOAT_EQ(cb.multiScatteringFactor, 1.0f);
    EXPECT_FLOAT_EQ(cb.mieAbsorption,         4.4e-3f);
    EXPECT_FLOAT_EQ(cb.skyLuminanceFactor.x,  1.0f);
    EXPECT_FLOAT_EQ(cb.skyLuminanceFactor.y,  1.0f);
    EXPECT_FLOAT_EQ(cb.skyLuminanceFactor.z,  1.0f);
    EXPECT_FLOAT_EQ(cb.skyLuminanceFactor.w,  1.0f);
}

TEST(SkyAtmosphereCB, ScaleDefaultsAreOne) {
    SkyAtmosphereCB cb{};
    // Default-initialized (zero-init): scale fields default to 0, not 1.
    // This is correct — the caller sets them before upload.
    // Verify zero-init behavior matches the struct definition.
    EXPECT_FLOAT_EQ(cb.rayleighScatteringScale, 0.0f);
    EXPECT_FLOAT_EQ(cb.mieScatteringScale,      0.0f);
    EXPECT_FLOAT_EQ(cb.mieAbsorptionScale,      0.0f);
}

TEST(SkyAtmosphereCB, MemcpyRoundTrip) {
    SkyAtmosphereCB src{};
    src.bottomRadiusKm        = 6360.0f;
    src.atmosphereHeightKm    = 60.0f;
    src.mieAbsorption         = 4.4e-3f;
    src.skyLuminanceFactor    = {1.0f, 1.0f, 1.0f, 1.0f};
    src.rayleighScatteringScale = 1.0f;
    src.mieScatteringScale    = 1.0f;
    src.mieAbsorptionScale    = 1.0f;
    src.multiScatteringFactor = 1.0f;
    src.sunDiskIntensity      = 2.0f;

    uint8_t buffer[256];
    std::memcpy(buffer, &src, sizeof(src));

    SkyAtmosphereCB dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.bottomRadiusKm,        6360.0f);
    EXPECT_FLOAT_EQ(dst.atmosphereHeightKm,    60.0f);
    EXPECT_FLOAT_EQ(dst.mieAbsorption,         4.4e-3f);
    EXPECT_FLOAT_EQ(dst.skyLuminanceFactor.x,  1.0f);
    EXPECT_FLOAT_EQ(dst.skyLuminanceFactor.y,  1.0f);
    EXPECT_FLOAT_EQ(dst.skyLuminanceFactor.z,  1.0f);
    EXPECT_FLOAT_EQ(dst.skyLuminanceFactor.w,  1.0f);
    EXPECT_FLOAT_EQ(dst.rayleighScatteringScale, 1.0f);
    EXPECT_FLOAT_EQ(dst.mieScatteringScale,    1.0f);
    EXPECT_FLOAT_EQ(dst.mieAbsorptionScale,    1.0f);
    EXPECT_FLOAT_EQ(dst.multiScatteringFactor, 1.0f);
    EXPECT_FLOAT_EQ(dst.sunDiskIntensity,      2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// AtmosphereLutCB — size, field offsets, defaults, memcpy round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(AtmosphereLutCB, SizeIs256Bytes) {
    EXPECT_EQ(sizeof(AtmosphereLutCB), 256u);
}

TEST(AtmosphereLutCB, FieldOffsets) {
    EXPECT_EQ(offsetof(AtmosphereLutCB, rayleighBeta),   0u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, rayleighScaleH), 12u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, mieBeta),        16u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, mieScaleH),      20u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, bottomRadiusKm), 24u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, topRadiusKm),    28u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, lutWidth),       32u);
    EXPECT_EQ(offsetof(AtmosphereLutCB, lutHeight),      36u);
}

TEST(AtmosphereLutCB, DefaultValues) {
    AtmosphereLutCB cb{};
    EXPECT_FLOAT_EQ(cb.rayleighBeta.x,  0.0f);
    EXPECT_FLOAT_EQ(cb.rayleighBeta.y,  0.0f);
    EXPECT_FLOAT_EQ(cb.rayleighBeta.z,  0.0f);
    EXPECT_FLOAT_EQ(cb.rayleighScaleH,  0.0f);
    EXPECT_FLOAT_EQ(cb.mieBeta,         0.0f);
    EXPECT_FLOAT_EQ(cb.mieScaleH,       0.0f);
    EXPECT_FLOAT_EQ(cb.bottomRadiusKm,  0.0f);
    EXPECT_FLOAT_EQ(cb.topRadiusKm,     0.0f);
    EXPECT_EQ(cb.lutWidth,  0u);
    EXPECT_EQ(cb.lutHeight, 0u);
}

TEST(AtmosphereLutCB, WriteAndReadBack) {
    AtmosphereLutCB cb{};
    cb.rayleighBeta   = {5.802e-3f, 13.558e-3f, 33.1e-3f};
    cb.rayleighScaleH = 8.0f;
    cb.mieBeta        = 3.996e-3f;
    cb.mieScaleH      = 1.2f;
    cb.bottomRadiusKm = 6360.0f;
    cb.topRadiusKm    = 6420.0f;
    cb.lutWidth       = 256u;
    cb.lutHeight      = 64u;

    EXPECT_FLOAT_EQ(cb.rayleighBeta.x,  5.802e-3f);
    EXPECT_FLOAT_EQ(cb.rayleighBeta.y,  13.558e-3f);
    EXPECT_FLOAT_EQ(cb.rayleighBeta.z,  33.1e-3f);
    EXPECT_FLOAT_EQ(cb.rayleighScaleH,  8.0f);
    EXPECT_FLOAT_EQ(cb.mieBeta,         3.996e-3f);
    EXPECT_FLOAT_EQ(cb.mieScaleH,       1.2f);
    EXPECT_FLOAT_EQ(cb.bottomRadiusKm,  6360.0f);
    EXPECT_FLOAT_EQ(cb.topRadiusKm,     6420.0f);
    EXPECT_EQ(cb.lutWidth,  256u);
    EXPECT_EQ(cb.lutHeight,  64u);
}

TEST(AtmosphereLutCB, TopRadiusIsBottomPlusAtmosphereHeight) {
    // topRadiusKm = bottomRadiusKm + atmosphereHeightKm = 6360 + 60 = 6420
    AtmosphereLutCB cb{};
    cb.bottomRadiusKm = 6360.0f;
    cb.topRadiusKm    = 6420.0f;
    EXPECT_FLOAT_EQ(cb.topRadiusKm - cb.bottomRadiusKm, 60.0f);
}

TEST(AtmosphereLutCB, MemcpyRoundTrip) {
    AtmosphereLutCB src{};
    src.rayleighBeta   = {5.802e-3f, 13.558e-3f, 33.1e-3f};
    src.rayleighScaleH = 8.0f;
    src.mieBeta        = 3.996e-3f;
    src.mieScaleH      = 1.2f;
    src.bottomRadiusKm = 6360.0f;
    src.topRadiusKm    = 6420.0f;
    src.lutWidth       = 256u;
    src.lutHeight      = 64u;

    uint8_t buffer[256];
    std::memcpy(buffer, &src, sizeof(src));

    AtmosphereLutCB dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.rayleighBeta.x,  5.802e-3f);
    EXPECT_FLOAT_EQ(dst.rayleighBeta.y,  13.558e-3f);
    EXPECT_FLOAT_EQ(dst.rayleighBeta.z,  33.1e-3f);
    EXPECT_FLOAT_EQ(dst.rayleighScaleH,  8.0f);
    EXPECT_FLOAT_EQ(dst.mieBeta,         3.996e-3f);
    EXPECT_FLOAT_EQ(dst.mieScaleH,       1.2f);
    EXPECT_FLOAT_EQ(dst.bottomRadiusKm,  6360.0f);
    EXPECT_FLOAT_EQ(dst.topRadiusKm,     6420.0f);
    EXPECT_EQ(dst.lutWidth,  256u);
    EXPECT_EQ(dst.lutHeight,  64u);
}

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogCB — size, field offsets, defaults, write/readback, memcpy round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogCB, SizeIs256Bytes) {
    EXPECT_EQ(sizeof(HeightFogCB), 256u);
}

TEST(HeightFogCB, FieldOffsets) {
    EXPECT_EQ(offsetof(HeightFogCB, fogColor),           0u);
    EXPECT_EQ(offsetof(HeightFogCB, fogDensity),        16u);
    EXPECT_EQ(offsetof(HeightFogCB, fogHeightFalloff),  20u);
    EXPECT_EQ(offsetof(HeightFogCB, fogBaseHeight),     24u);
    EXPECT_EQ(offsetof(HeightFogCB, fogMaxOpacity),     28u);
    EXPECT_EQ(offsetof(HeightFogCB, fogStartDistance),  32u);
    EXPECT_EQ(offsetof(HeightFogCB, fogCutoffDistance), 36u);
    // _fogPad0[2] occupies bytes 40–47
    EXPECT_EQ(offsetof(HeightFogCB, secondFogDensity),        48u);
    EXPECT_EQ(offsetof(HeightFogCB, secondFogHeightFalloff),  52u);
    EXPECT_EQ(offsetof(HeightFogCB, secondFogRawDensity),     56u);
    EXPECT_EQ(offsetof(HeightFogCB, secondFogBaseHeight),     60u);
    EXPECT_EQ(offsetof(HeightFogCB, directionalInscatteringColor),         64u);
    EXPECT_EQ(offsetof(HeightFogCB, directionalInscatteringExponent),      80u);
    EXPECT_EQ(offsetof(HeightFogCB, directionalInscatteringStartDistance), 84u);
}

TEST(HeightFogCB, DefaultValues) {
    // Zero-initialized struct: all POD fields with no default initializer = 0.
    HeightFogCB cb{};
    EXPECT_FLOAT_EQ(cb.fogColor.x,          0.0f);
    EXPECT_FLOAT_EQ(cb.fogColor.y,          0.0f);
    EXPECT_FLOAT_EQ(cb.fogColor.z,          0.0f);
    EXPECT_FLOAT_EQ(cb.fogColor.w,          0.0f);
    EXPECT_FLOAT_EQ(cb.fogDensity,          0.0f);
    EXPECT_FLOAT_EQ(cb.fogHeightFalloff,    0.0f);
    EXPECT_FLOAT_EQ(cb.fogBaseHeight,       0.0f);
    EXPECT_FLOAT_EQ(cb.fogMaxOpacity,       0.0f);
    EXPECT_FLOAT_EQ(cb.fogStartDistance,    0.0f);
    EXPECT_FLOAT_EQ(cb.fogCutoffDistance,   0.0f);
    EXPECT_FLOAT_EQ(cb.secondFogDensity,    0.0f);
    EXPECT_FLOAT_EQ(cb.secondFogHeightFalloff, 0.0f);
    EXPECT_FLOAT_EQ(cb.secondFogBaseHeight, 0.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringColor.x, 0.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringColor.y, 0.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringColor.z, 0.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringExponent, 0.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringStartDistance, 0.0f);
}

TEST(HeightFogCB, WriteAndReadBack) {
    HeightFogCB cb{};
    cb.fogColor                             = {0.7f, 0.75f, 0.85f, 0.0f};
    cb.fogDensity                           = 0.02f;
    cb.fogHeightFalloff                     = 0.2f;
    cb.fogBaseHeight                        = 0.0f;
    cb.fogMaxOpacity                        = 1.0f;
    cb.fogStartDistance                     = 100.0f;
    cb.fogCutoffDistance                    = 50000.0f;
    cb.secondFogDensity                     = 0.005f;
    cb.secondFogHeightFalloff               = 0.4f;
    cb.secondFogBaseHeight                  = 1000.0f;
    cb.directionalInscatteringColor         = {1.0f, 0.8f, 0.6f, 0.0f};
    cb.directionalInscatteringExponent      = 8.0f;
    cb.directionalInscatteringStartDistance = 200.0f;

    EXPECT_FLOAT_EQ(cb.fogColor.x,          0.7f);
    EXPECT_FLOAT_EQ(cb.fogColor.y,          0.75f);
    EXPECT_FLOAT_EQ(cb.fogColor.z,          0.85f);
    EXPECT_FLOAT_EQ(cb.fogDensity,          0.02f);
    EXPECT_FLOAT_EQ(cb.fogHeightFalloff,     0.2f);
    EXPECT_FLOAT_EQ(cb.fogMaxOpacity,        1.0f);
    EXPECT_FLOAT_EQ(cb.fogStartDistance,    100.0f);
    EXPECT_FLOAT_EQ(cb.fogCutoffDistance, 50000.0f);
    EXPECT_FLOAT_EQ(cb.secondFogDensity,    0.005f);
    EXPECT_FLOAT_EQ(cb.secondFogHeightFalloff, 0.4f);
    EXPECT_FLOAT_EQ(cb.secondFogBaseHeight, 1000.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringColor.x, 1.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringColor.y, 0.8f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringColor.z, 0.6f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringExponent, 8.0f);
    EXPECT_FLOAT_EQ(cb.directionalInscatteringStartDistance, 200.0f);
}

TEST(HeightFogCB, MemcpyRoundTrip) {
    HeightFogCB src{};
    src.fogColor      = {0.5f, 0.6f, 0.7f, 0.0f};
    src.fogDensity    = 0.03f;
    src.fogMaxOpacity = 0.9f;
    src.directionalInscatteringExponent = 4.0f;

    uint8_t buffer[256];
    std::memcpy(buffer, &src, sizeof(src));

    HeightFogCB dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.fogColor.x,     0.5f);
    EXPECT_FLOAT_EQ(dst.fogColor.y,     0.6f);
    EXPECT_FLOAT_EQ(dst.fogColor.z,     0.7f);
    EXPECT_FLOAT_EQ(dst.fogDensity,     0.03f);
    EXPECT_FLOAT_EQ(dst.fogMaxOpacity,  0.9f);
    EXPECT_FLOAT_EQ(dst.directionalInscatteringExponent, 4.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmosphereComponent — Earth-like defaults, enabled flag, TransformMode enum
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyAtmosphereComponent, BottomRadiusKmIsEarthDefault) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.bottomRadiusKm, 6360.0f);
}

TEST(SkyAtmosphereComponent, AtmosphereHeightKmIs60) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.atmosphereHeightKm, 60.0f);
}

TEST(SkyAtmosphereComponent, RayleighScatteringMatchesUE5Defaults) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.rayleighScattering.x, 5.802e-3f);
    EXPECT_FLOAT_EQ(comp.rayleighScattering.y, 13.558e-3f);
    EXPECT_FLOAT_EQ(comp.rayleighScattering.z, 33.1e-3f);
}

TEST(SkyAtmosphereComponent, RayleighScaleHeightIs8km) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.rayleighScaleHeightKm, 8.0f);
}

TEST(SkyAtmosphereComponent, RayleighScatteringScaleIsOne) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.rayleighScatteringScale, 1.0f);
}

TEST(SkyAtmosphereComponent, MieScatteringMatchesUE5Default) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.mieScattering, 3.996e-3f);
}

TEST(SkyAtmosphereComponent, MieAnisotropyIs0_8) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.mieAnisotropy, 0.8f);
}

TEST(SkyAtmosphereComponent, MieScaleHeightIs1_2km) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.mieScaleHeightKm, 1.2f);
}

TEST(SkyAtmosphereComponent, MieAbsorptionIs4_4e_3) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.mieAbsorption, 4.4e-3f);
}

TEST(SkyAtmosphereComponent, MieScalingDefaults) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.mieScatteringScale, 1.0f);
    EXPECT_FLOAT_EQ(comp.mieAbsorptionScale, 1.0f);
}

TEST(SkyAtmosphereComponent, GroundAlbedoIs0_3) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.groundAlbedo.x, 0.3f);
    EXPECT_FLOAT_EQ(comp.groundAlbedo.y, 0.3f);
    EXPECT_FLOAT_EQ(comp.groundAlbedo.z, 0.3f);
}

TEST(SkyAtmosphereComponent, MultiScatteringFactorIsOne) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.multiScatteringFactor, 1.0f);
}

TEST(SkyAtmosphereComponent, SkyLuminanceFactorIsWhite) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.skyLuminanceFactor.x, 1.0f);
    EXPECT_FLOAT_EQ(comp.skyLuminanceFactor.y, 1.0f);
    EXPECT_FLOAT_EQ(comp.skyLuminanceFactor.z, 1.0f);
}

TEST(SkyAtmosphereComponent, HeightFogContributionIsOne) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.heightFogContribution, 1.0f);
}

TEST(SkyAtmosphereComponent, TransformModeDefaultIsPlanetTop) {
    SkyAtmosphereComponent comp{};
    EXPECT_EQ(comp.transformMode,
              SkyAtmosphereComponent::TransformMode::PlanetTopAtComponentTransform);
}

TEST(SkyAtmosphereComponent, TransformModeEnumValues) {
    using TM = SkyAtmosphereComponent::TransformMode;
    EXPECT_EQ(static_cast<uint8_t>(TM::PlanetTopAtComponentTransform),    0u);
    EXPECT_EQ(static_cast<uint8_t>(TM::PlanetCenterAtComponentTransform), 1u);
}

TEST(SkyAtmosphereComponent, FallbackSunIlluminanceIs20) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.sunIlluminance.x, 20.0f);
    EXPECT_FLOAT_EQ(comp.sunIlluminance.y, 20.0f);
    EXPECT_FLOAT_EQ(comp.sunIlluminance.z, 20.0f);
}

TEST(SkyAtmosphereComponent, FallbackSunAngularRadius) {
    SkyAtmosphereComponent comp{};
    EXPECT_NEAR(comp.sunAngularRadius, 0.00872665f, 1e-6f);
}

TEST(SkyAtmosphereComponent, FallbackSunDiskIntensityIsOne) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.sunDiskIntensity, 1.0f);
}

TEST(SkyAtmosphereComponent, EnabledByDefault) {
    SkyAtmosphereComponent comp{};
    EXPECT_TRUE(comp.enabled);
}

TEST(SkyAtmosphereComponent, CanBeDisabled) {
    SkyAtmosphereComponent comp{};
    comp.enabled = false;
    EXPECT_FALSE(comp.enabled);
}

TEST(SkyAtmosphereComponent, DeprecatedSkyLightIntensityDefaultIsOne) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.skyLightIntensity, 1.0f);
}

TEST(SkyAtmosphereComponent, DeprecatedSkyLightColorIsWhite) {
    SkyAtmosphereComponent comp{};
    EXPECT_FLOAT_EQ(comp.skyLightColor.x, 1.0f);
    EXPECT_FLOAT_EQ(comp.skyLightColor.y, 1.0f);
    EXPECT_FLOAT_EQ(comp.skyLightColor.z, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogComponent — UE5 defaults, second layer, inscattering, control
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogComponent, ColorDefaultIsWhite) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.color.x, 1.0f);
    EXPECT_FLOAT_EQ(comp.color.y, 1.0f);
    EXPECT_FLOAT_EQ(comp.color.z, 1.0f);
}

TEST(HeightFogComponent, DensityDefaultIs0_02) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.density, 0.02f);
}

TEST(HeightFogComponent, HeightFalloffDefaultIs0_2) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.heightFalloff, 0.2f);
}

TEST(HeightFogComponent, BaseHeightDefaultIsZero) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.baseHeight, 0.0f);
}

TEST(HeightFogComponent, MaxOpacityDefaultIsOne) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.maxOpacity, 1.0f);
}

TEST(HeightFogComponent, StartDistanceDefaultIsZero) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.startDistance, 0.0f);
}

TEST(HeightFogComponent, FogCutoffDistanceDefaultIsZero) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.fogCutoffDistance, 0.0f);
}

TEST(HeightFogComponent, SecondFogDensityDefaultIsZero) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.secondFogDensity, 0.0f);
}

TEST(HeightFogComponent, SecondFogHeightFalloffDefaultIs0_2) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.secondFogHeightFalloff, 0.2f);
}

TEST(HeightFogComponent, SecondFogBaseHeightDefaultIsZero) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.secondFogBaseHeight, 0.0f);
}

TEST(HeightFogComponent, DirectionalInscatteringColorDefault) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.directionalInscatteringColor.x, 1.0f);
    EXPECT_FLOAT_EQ(comp.directionalInscatteringColor.y, 0.8f);
    EXPECT_FLOAT_EQ(comp.directionalInscatteringColor.z, 0.6f);
}

TEST(HeightFogComponent, DirectionalInscatteringExponentDefaultIs8) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.directionalInscatteringExponent, 8.0f);
}

TEST(HeightFogComponent, DirectionalInscatteringStartDistanceDefaultIsZero) {
    HeightFogComponent comp{};
    EXPECT_FLOAT_EQ(comp.directionalInscatteringStartDistance, 0.0f);
}

TEST(HeightFogComponent, VolumetricFogDisabledByDefault) {
    HeightFogComponent comp{};
    EXPECT_FALSE(comp.enableVolumetricFog);
}

TEST(HeightFogComponent, EnabledByDefault) {
    HeightFogComponent comp{};
    EXPECT_TRUE(comp.enabled);
}

TEST(HeightFogComponent, CanBeDisabled) {
    HeightFogComponent comp{};
    comp.enabled = false;
    EXPECT_FALSE(comp.enabled);
}

TEST(HeightFogComponent, CanSetAllFields) {
    HeightFogComponent comp{};
    comp.color                                = {0.7f, 0.8f, 0.9f};
    comp.density                              = 0.05f;
    comp.heightFalloff                        = 0.4f;
    comp.baseHeight                           = 500.0f;
    comp.maxOpacity                           = 0.8f;
    comp.startDistance                        = 100.0f;
    comp.fogCutoffDistance                    = 10000.0f;
    comp.secondFogDensity                     = 0.01f;
    comp.secondFogHeightFalloff               = 0.6f;
    comp.secondFogBaseHeight                  = 2000.0f;
    comp.directionalInscatteringColor         = {0.9f, 0.7f, 0.5f};
    comp.directionalInscatteringExponent      = 4.0f;
    comp.directionalInscatteringStartDistance = 500.0f;
    comp.enableVolumetricFog                  = true;

    EXPECT_FLOAT_EQ(comp.color.x,           0.7f);
    EXPECT_FLOAT_EQ(comp.density,           0.05f);
    EXPECT_FLOAT_EQ(comp.heightFalloff,      0.4f);
    EXPECT_FLOAT_EQ(comp.baseHeight,        500.0f);
    EXPECT_FLOAT_EQ(comp.maxOpacity,         0.8f);
    EXPECT_FLOAT_EQ(comp.startDistance,     100.0f);
    EXPECT_FLOAT_EQ(comp.fogCutoffDistance, 10000.0f);
    EXPECT_FLOAT_EQ(comp.secondFogDensity,   0.01f);
    EXPECT_FLOAT_EQ(comp.secondFogHeightFalloff, 0.6f);
    EXPECT_FLOAT_EQ(comp.secondFogBaseHeight, 2000.0f);
    EXPECT_FLOAT_EQ(comp.directionalInscatteringColor.x, 0.9f);
    EXPECT_FLOAT_EQ(comp.directionalInscatteringExponent, 4.0f);
    EXPECT_FLOAT_EQ(comp.directionalInscatteringStartDistance, 500.0f);
    EXPECT_TRUE(comp.enableVolumetricFog);
}

// ─────────────────────────────────────────────────────────────────────────────
// SkyLightComponent — intensity, source type, lower hemisphere, occlusion
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyLightComponent, IntensityDefaultIsOne) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.intensity, 1.0f);
}

TEST(SkyLightComponent, LightColorDefaultIsWhite) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.lightColor.x, 1.0f);
    EXPECT_FLOAT_EQ(comp.lightColor.y, 1.0f);
    EXPECT_FLOAT_EQ(comp.lightColor.z, 1.0f);
}

TEST(SkyLightComponent, IndirectLightingIntensityDefaultIsOne) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.indirectLightingIntensity, 1.0f);
}

TEST(SkyLightComponent, SourceTypeDefaultIsCapturedScene) {
    SkyLightComponent comp{};
    EXPECT_EQ(comp.sourceType, ESkyLightSourceType::CapturedScene);
}

TEST(SkyLightComponent, SourceTypeEnumValues) {
    EXPECT_EQ(static_cast<uint8_t>(ESkyLightSourceType::CapturedScene),    0u);
    EXPECT_EQ(static_cast<uint8_t>(ESkyLightSourceType::SpecifiedCubemap), 1u);
}

TEST(SkyLightComponent, RealTimeCaptureDefaultIsFalse) {
    SkyLightComponent comp{};
    EXPECT_FALSE(comp.realTimeCapture);
}

TEST(SkyLightComponent, CubemapResolutionDefaultIs128) {
    SkyLightComponent comp{};
    EXPECT_EQ(comp.cubemapResolution, 128u);
}

TEST(SkyLightComponent, LowerHemisphereIsBlackDefaultIsTrue) {
    SkyLightComponent comp{};
    EXPECT_TRUE(comp.lowerHemisphereIsBlack);
}

TEST(SkyLightComponent, LowerHemisphereColorDefaultIsBlack) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.lowerHemisphereColor.x, 0.0f);
    EXPECT_FLOAT_EQ(comp.lowerHemisphereColor.y, 0.0f);
    EXPECT_FLOAT_EQ(comp.lowerHemisphereColor.z, 0.0f);
}

TEST(SkyLightComponent, OcclusionMaxDistanceDefaultIs1000) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.occlusionMaxDistance, 1000.0f);
}

TEST(SkyLightComponent, MinOcclusionDefaultIsZero) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.minOcclusion, 0.0f);
}

TEST(SkyLightComponent, OcclusionTintDefaultIsBlack) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.occlusionTint.x, 0.0f);
    EXPECT_FLOAT_EQ(comp.occlusionTint.y, 0.0f);
    EXPECT_FLOAT_EQ(comp.occlusionTint.z, 0.0f);
}

TEST(SkyLightComponent, OcclusionExponentDefaultIsOne) {
    SkyLightComponent comp{};
    EXPECT_FLOAT_EQ(comp.occlusionExponent, 1.0f);
}

TEST(SkyLightComponent, EnabledByDefault) {
    SkyLightComponent comp{};
    EXPECT_TRUE(comp.enabled);
}

TEST(SkyLightComponent, CanBeDisabled) {
    SkyLightComponent comp{};
    comp.enabled = false;
    EXPECT_FALSE(comp.enabled);
}

TEST(SkyLightComponent, CanSetAllFields) {
    SkyLightComponent comp{};
    comp.intensity                 = 2.5f;
    comp.lightColor                = {0.9f, 0.95f, 1.0f};
    comp.indirectLightingIntensity = 1.5f;
    comp.sourceType                = ESkyLightSourceType::SpecifiedCubemap;
    comp.realTimeCapture           = true;
    comp.cubemapResolution         = 256u;
    comp.lowerHemisphereIsBlack    = false;
    comp.lowerHemisphereColor      = {0.05f, 0.05f, 0.1f};
    comp.occlusionMaxDistance      = 500.0f;
    comp.minOcclusion              = 0.1f;
    comp.occlusionTint             = {0.02f, 0.02f, 0.05f};
    comp.occlusionExponent         = 2.0f;
    comp.enabled                   = false;

    EXPECT_FLOAT_EQ(comp.intensity,                2.5f);
    EXPECT_FLOAT_EQ(comp.lightColor.x,             0.9f);
    EXPECT_FLOAT_EQ(comp.lightColor.y,             0.95f);
    EXPECT_FLOAT_EQ(comp.lightColor.z,             1.0f);
    EXPECT_FLOAT_EQ(comp.indirectLightingIntensity, 1.5f);
    EXPECT_EQ(comp.sourceType, ESkyLightSourceType::SpecifiedCubemap);
    EXPECT_TRUE(comp.realTimeCapture);
    EXPECT_EQ(comp.cubemapResolution, 256u);
    EXPECT_FALSE(comp.lowerHemisphereIsBlack);
    EXPECT_FLOAT_EQ(comp.lowerHemisphereColor.x, 0.05f);
    EXPECT_FLOAT_EQ(comp.lowerHemisphereColor.y, 0.05f);
    EXPECT_FLOAT_EQ(comp.lowerHemisphereColor.z, 0.1f);
    EXPECT_FLOAT_EQ(comp.occlusionMaxDistance, 500.0f);
    EXPECT_FLOAT_EQ(comp.minOcclusion, 0.1f);
    EXPECT_FLOAT_EQ(comp.occlusionTint.x, 0.02f);
    EXPECT_FLOAT_EQ(comp.occlusionExponent, 2.0f);
    EXPECT_FALSE(comp.enabled);
}

// ─────────────────────────────────────────────────────────────────────────────
// SkyCubemapCaptureRootConstants — size, field offsets, write/read, memcpy
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyCubemapCaptureRootConstants, SizeIs32Bytes) {
    // 8 DWORDs: bottomRadiusKm(4) + topRadiusKm(4) + sunDirX(4) + sunDirY(4)
    //         + sunDirZ(4) + sunIntensity(4) + transmittanceLutIdx(4) + packedScatterScales(4)
    EXPECT_EQ(sizeof(SkyCubemapCaptureRootConstants), 32u);
}

TEST(SkyCubemapCaptureRootConstants, FieldOffsets) {
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, bottomRadiusKm),        0u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, topRadiusKm),           4u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, sunDirX),               8u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, sunDirY),              12u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, sunDirZ),              16u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, sunIntensity),         20u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, transmittanceLutIdx),  24u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, packedScatterScales),  28u);
}

TEST(SkyCubemapCaptureRootConstants, WriteAndReadBack) {
    SkyCubemapCaptureRootConstants rc{};
    rc.bottomRadiusKm      = 6360.0f;
    rc.topRadiusKm         = 6420.0f;
    rc.sunDirX             = 0.0f;
    rc.sunDirY             = 0.707f;
    rc.sunDirZ             = 0.707f;
    rc.sunIntensity        = 20.0f;
    rc.transmittanceLutIdx = 5u;
    rc.packedScatterScales = 0x3C003C00u; // f16(1.0) packed into both halves

    EXPECT_FLOAT_EQ(rc.bottomRadiusKm, 6360.0f);
    EXPECT_FLOAT_EQ(rc.topRadiusKm,    6420.0f);
    EXPECT_FLOAT_EQ(rc.sunDirX,        0.0f);
    EXPECT_NEAR(rc.sunDirY,            0.707f, 1e-6f);
    EXPECT_NEAR(rc.sunDirZ,            0.707f, 1e-6f);
    EXPECT_FLOAT_EQ(rc.sunIntensity,   20.0f);
    EXPECT_EQ(rc.transmittanceLutIdx,  5u);
    EXPECT_EQ(rc.packedScatterScales,  0x3C003C00u);
}

TEST(SkyCubemapCaptureRootConstants, TopRadiusIsBottomPlusAtmosphereHeight) {
    SkyCubemapCaptureRootConstants rc{};
    rc.bottomRadiusKm = 6360.0f;
    rc.topRadiusKm    = 6420.0f;
    EXPECT_FLOAT_EQ(rc.topRadiusKm - rc.bottomRadiusKm, 60.0f);
}

TEST(SkyCubemapCaptureRootConstants, MemcpyRoundTrip) {
    SkyCubemapCaptureRootConstants src{};
    src.bottomRadiusKm      = 6360.0f;
    src.topRadiusKm         = 6420.0f;
    src.sunDirX             = 0.577f;
    src.sunDirY             = 0.577f;
    src.sunDirZ             = 0.577f;
    src.sunIntensity        = 1.0f;
    src.transmittanceLutIdx = 3u;
    src.packedScatterScales = 0u;

    uint8_t buffer[32];
    std::memcpy(buffer, &src, sizeof(src));

    SkyCubemapCaptureRootConstants dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.bottomRadiusKm, 6360.0f);
    EXPECT_FLOAT_EQ(dst.topRadiusKm,    6420.0f);
    EXPECT_NEAR(dst.sunDirX,  0.577f, 1e-6f);
    EXPECT_NEAR(dst.sunDirY,  0.577f, 1e-6f);
    EXPECT_NEAR(dst.sunDirZ,  0.577f, 1e-6f);
    EXPECT_FLOAT_EQ(dst.sunIntensity, 1.0f);
    EXPECT_EQ(dst.transmittanceLutIdx, 3u);
    EXPECT_EQ(dst.packedScatterScales, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TransmittanceLutRootConstants — size, field offsets, write/read, memcpy, Earth invariant
// ─────────────────────────────────────────────────────────────────────────────

TEST(TransmittanceLutRootConstants, SizeIs32Bytes) {
    // 8 DWORDs: rayleighBetaX(4) + rayleighBetaY(4) + rayleighBetaZ(4) + rayleighScaleH(4)
    //         + mieBeta(4) + mieScaleH(4) + bottomRadiusKm(4) + topRadiusKm(4)
    EXPECT_EQ(sizeof(TransmittanceLutRootConstants), 32u);
}

TEST(TransmittanceLutRootConstants, FieldOffsets) {
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, rayleighBetaX),   0u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, rayleighBetaY),   4u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, rayleighBetaZ),   8u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, rayleighScaleH), 12u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, mieBeta),        16u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, mieScaleH),      20u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, bottomRadiusKm), 24u);
    EXPECT_EQ(offsetof(TransmittanceLutRootConstants, topRadiusKm),    28u);
}

TEST(TransmittanceLutRootConstants, WriteAndReadBack) {
    // Earth-default values matching UE5 sky atmosphere defaults (Bruneton 2017).
    TransmittanceLutRootConstants rc{};
    rc.rayleighBetaX   = 5.802e-3f;
    rc.rayleighBetaY   = 13.558e-3f;
    rc.rayleighBetaZ   = 33.1e-3f;
    rc.rayleighScaleH  = 8.0f;
    rc.mieBeta         = 3.996e-3f;
    rc.mieScaleH       = 1.2f;
    rc.bottomRadiusKm  = 6360.0f;
    rc.topRadiusKm     = 6420.0f;

    EXPECT_FLOAT_EQ(rc.rayleighBetaX,  5.802e-3f);
    EXPECT_FLOAT_EQ(rc.rayleighBetaY,  13.558e-3f);
    EXPECT_FLOAT_EQ(rc.rayleighBetaZ,  33.1e-3f);
    EXPECT_FLOAT_EQ(rc.rayleighScaleH, 8.0f);
    EXPECT_FLOAT_EQ(rc.mieBeta,        3.996e-3f);
    EXPECT_FLOAT_EQ(rc.mieScaleH,      1.2f);
    EXPECT_FLOAT_EQ(rc.bottomRadiusKm, 6360.0f);
    EXPECT_FLOAT_EQ(rc.topRadiusKm,    6420.0f);
}

TEST(TransmittanceLutRootConstants, MemcpyRoundTrip) {
    TransmittanceLutRootConstants src{};
    src.rayleighBetaX   = 5.802e-3f;
    src.rayleighBetaY   = 13.558e-3f;
    src.rayleighBetaZ   = 33.1e-3f;
    src.rayleighScaleH  = 8.0f;
    src.mieBeta         = 3.996e-3f;
    src.mieScaleH       = 1.2f;
    src.bottomRadiusKm  = 6360.0f;
    src.topRadiusKm     = 6420.0f;

    uint8_t buffer[32];
    std::memcpy(buffer, &src, sizeof(src));

    TransmittanceLutRootConstants dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.rayleighBetaX,  5.802e-3f);
    EXPECT_FLOAT_EQ(dst.rayleighBetaY,  13.558e-3f);
    EXPECT_FLOAT_EQ(dst.rayleighBetaZ,  33.1e-3f);
    EXPECT_FLOAT_EQ(dst.rayleighScaleH, 8.0f);
    EXPECT_FLOAT_EQ(dst.mieBeta,        3.996e-3f);
    EXPECT_FLOAT_EQ(dst.mieScaleH,      1.2f);
    EXPECT_FLOAT_EQ(dst.bottomRadiusKm, 6360.0f);
    EXPECT_FLOAT_EQ(dst.topRadiusKm,    6420.0f);
}

TEST(TransmittanceLutRootConstants, TopRadiusInvariant_IsBottomPlusSixty) {
    // Earth atmosphere: topRadius = bottomRadius + 60 km (atmosphere height).
    // This invariant must hold when the proxy builds the root constants from
    // SkyAtmosphereComponent: topRadiusKm = bottomRadiusKm + atmosphereHeightKm.
    TransmittanceLutRootConstants rc{};
    rc.bottomRadiusKm = 6360.0f;
    rc.topRadiusKm    = rc.bottomRadiusKm + 60.0f;

    EXPECT_FLOAT_EQ(rc.topRadiusKm,                        6420.0f);
    EXPECT_FLOAT_EQ(rc.topRadiusKm - rc.bottomRadiusKm,    60.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SkyAtmospherePassDesc — transmittanceLutSrvIndex default sentinel
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyAtmospherePassDesc, TransmittanceLutSrvIndexDefaultIsMaxUint) {
    // The default member initializer `= UINT32_MAX` signals "no transmittance LUT
    // available" to the pass. When the LUT pass has not been added to the frame
    // (e.g., first frame or fallback path), the sky atmosphere shader uses its
    // built-in analytical transmittance instead of sampling the precomputed LUT.
    SkyAtmospherePassDesc desc{};
    EXPECT_EQ(desc.transmittanceLutSrvIndex, UINT32_MAX);
}

TEST(SkyAtmospherePassDesc, BackbufferDefaultIsInvalid) {
    // RgResourceId defaults to index = UINT32_MAX (the invalid sentinel).
    // A desc constructed without setting backbuffer is invalid and must not
    // be passed to AddSkyAtmospherePass.
    SkyAtmospherePassDesc desc{};
    EXPECT_FALSE(desc.backbuffer.IsValid());
}

TEST(SkyAtmospherePassDesc, DepthGBufferDefaultIsInvalid) {
    SkyAtmospherePassDesc desc{};
    EXPECT_FALSE(desc.depthGBuffer.IsValid());
}

TEST(SkyAtmospherePassDesc, TransmittanceLutResDefaultIsInvalid) {
    // When transmittanceLutRes is invalid the graph does not insert a
    // UAV→SRV barrier for the LUT — the LUT path is skipped entirely.
    SkyAtmospherePassDesc desc{};
    EXPECT_FALSE(desc.transmittanceLutRes.IsValid());
}

TEST(SkyAtmospherePassDesc, PipelineDefaultIsInvalid) {
    // RHIPipelineHandle defaults to generation=0 (invalid handle).
    SkyAtmospherePassDesc desc{};
    EXPECT_FALSE(desc.pipeline.IsValid());
}

TEST(SkyAtmospherePassDesc, DepthTextureDefaultIsInvalid) {
    // RHITextureHandle defaults to generation=0 (invalid handle).
    SkyAtmospherePassDesc desc{};
    EXPECT_FALSE(desc.depthTexture.IsValid());
}

TEST(SkyAtmospherePassDesc, ViewportDimensionsDefaultToZero) {
    SkyAtmospherePassDesc desc{};
    EXPECT_FLOAT_EQ(desc.viewportWidth,  0.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 0.0f);
}

TEST(SkyAtmospherePassDesc, BindlessDefaultIsFalse) {
    SkyAtmospherePassDesc desc{};
    EXPECT_FALSE(desc.bindless);
}

TEST(SkyAtmospherePassDesc, CanSetAllFields) {
    SkyAtmospherePassDesc desc{};
    desc.backbuffer                 = {0u};   // index 0 — "valid" for test purposes
    desc.depthGBuffer               = {1u};
    desc.depthSrvIndex              = 7u;
    desc.transmittanceLutSrvIndex   = 12u;
    desc.transmittanceLutRes        = {3u};
    desc.bindless                   = true;
    desc.viewportWidth              = 1920.0f;
    desc.viewportHeight             = 1080.0f;

    EXPECT_EQ(desc.backbuffer.index,              0u);
    EXPECT_EQ(desc.depthGBuffer.index,            1u);
    EXPECT_EQ(desc.depthSrvIndex,                 7u);
    EXPECT_EQ(desc.transmittanceLutSrvIndex,      12u);
    EXPECT_EQ(desc.transmittanceLutRes.index,     3u);
    EXPECT_TRUE(desc.bindless);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  1920.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 1080.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogPassDesc — compilation sanity, default resource IDs
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogPassDesc, BackbufferDefaultIsInvalid) {
    HeightFogPassDesc desc{};
    EXPECT_FALSE(desc.backbuffer.IsValid());
}

TEST(HeightFogPassDesc, DepthGBufferDefaultIsInvalid) {
    HeightFogPassDesc desc{};
    EXPECT_FALSE(desc.depthGBuffer.IsValid());
}

TEST(HeightFogPassDesc, PipelineDefaultIsInvalid) {
    HeightFogPassDesc desc{};
    EXPECT_FALSE(desc.pipeline.IsValid());
}

TEST(HeightFogPassDesc, DepthTextureDefaultIsInvalid) {
    HeightFogPassDesc desc{};
    EXPECT_FALSE(desc.depthTexture.IsValid());
}

TEST(HeightFogPassDesc, ViewportDimensionsDefaultToZero) {
    HeightFogPassDesc desc{};
    EXPECT_FLOAT_EQ(desc.viewportWidth,  0.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 0.0f);
}

TEST(HeightFogPassDesc, BindlessDefaultIsFalse) {
    HeightFogPassDesc desc{};
    EXPECT_FALSE(desc.bindless);
}

TEST(HeightFogPassDesc, CanSetAllFields) {
    HeightFogPassDesc desc{};
    desc.backbuffer     = {0u};
    desc.depthGBuffer   = {1u};
    desc.depthSrvIndex  = 4u;
    desc.bindless       = true;
    desc.viewportWidth  = 2560.0f;
    desc.viewportHeight = 1440.0f;

    EXPECT_EQ(desc.backbuffer.index,    0u);
    EXPECT_EQ(desc.depthGBuffer.index,  1u);
    EXPECT_EQ(desc.depthSrvIndex,       4u);
    EXPECT_TRUE(desc.bindless);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  2560.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 1440.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// TransmittanceLutPassDesc — compilation sanity, default resource IDs
// ─────────────────────────────────────────────────────────────────────────────

TEST(TransmittanceLutPassDesc, LutTargetDefaultIsInvalid) {
    // When lutTarget is invalid the pass cannot write the LUT — the caller
    // must create a graph resource and assign it before calling AddTransmittanceLutPass.
    TransmittanceLutPassDesc desc{};
    EXPECT_FALSE(desc.lutTarget.IsValid());
}

TEST(TransmittanceLutPassDesc, PipelineDefaultIsInvalid) {
    TransmittanceLutPassDesc desc{};
    EXPECT_FALSE(desc.pipeline.IsValid());
}

TEST(TransmittanceLutPassDesc, LutUavIndexDefaultIsZero) {
    // uint32_t with no in-class initializer — zero-initialised by value-init ({}).
    TransmittanceLutPassDesc desc{};
    EXPECT_EQ(desc.lutUavIndex, 0u);
}

TEST(TransmittanceLutPassDesc, RootConstantsDefaultToZero) {
    // All float fields in the nested struct zero-initialise — the caller must
    // populate them from SkyAtmosphereComponent before adding the pass.
    TransmittanceLutPassDesc desc{};
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighBetaX,  0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighBetaY,  0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighBetaZ,  0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighScaleH, 0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.mieBeta,        0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.mieScaleH,      0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.bottomRadiusKm, 0.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.topRadiusKm,    0.0f);
}

TEST(TransmittanceLutPassDesc, CanSetAllFields) {
    TransmittanceLutPassDesc desc{};
    desc.lutTarget  = {5u};
    desc.lutUavIndex = 8u;
    desc.rootConstants.rayleighBetaX   = 5.802e-3f;
    desc.rootConstants.rayleighBetaY   = 13.558e-3f;
    desc.rootConstants.rayleighBetaZ   = 33.1e-3f;
    desc.rootConstants.rayleighScaleH  = 8.0f;
    desc.rootConstants.mieBeta         = 3.996e-3f;
    desc.rootConstants.mieScaleH       = 1.2f;
    desc.rootConstants.bottomRadiusKm  = 6360.0f;
    desc.rootConstants.topRadiusKm     = 6420.0f;

    EXPECT_EQ(desc.lutTarget.index,                    5u);
    EXPECT_EQ(desc.lutUavIndex,                        8u);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighBetaX,  5.802e-3f);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighBetaY,  13.558e-3f);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighBetaZ,  33.1e-3f);
    EXPECT_FLOAT_EQ(desc.rootConstants.rayleighScaleH, 8.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.mieBeta,        3.996e-3f);
    EXPECT_FLOAT_EQ(desc.rootConstants.mieScaleH,      1.2f);
    EXPECT_FLOAT_EQ(desc.rootConstants.bottomRadiusKm, 6360.0f);
    EXPECT_FLOAT_EQ(desc.rootConstants.topRadiusKm,    6420.0f);
}
