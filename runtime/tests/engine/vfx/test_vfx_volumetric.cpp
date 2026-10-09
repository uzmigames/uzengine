/// Unit tests for phase 8.15.0 — VFX volumetric particle renderer CPU math.
///
/// The GPU shaders mirror these helpers, so validating the CPU versions
/// validates the underlying scattering model without requiring a GPU.
///
/// @ref engine/vfx/modules/vfx_volumetric.h
/// @ref docs/engine/25-vfx/32-volumetric-renderer.md

#include <gtest/gtest.h>

#include "engine/vfx/modules/vfx_volumetric.h"

#include <cmath>

using namespace uz;

namespace {

VfxVolumetricConfig MakeCfg() {
    VfxVolumetricConfig cfg;
    cfg.gridResolution = 64;
    cfg.halfExtent     = 10.0f;
    cfg.gridCenter     = Vec3{0, 0, 0};
    cfg.densityPerParticle = 0.1f;
    return cfg;
}

constexpr float kPi    = 3.14159265358979f;
constexpr float kFourPi = 4.0f * kPi;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxVolumetricWorldToVoxel
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxVolumetricGrid, CenterMapsToMiddleVoxel) {
    const auto cfg = MakeCfg();
    uint32_t x, y, z;
    EXPECT_TRUE(VfxVolumetricWorldToVoxel(cfg, Vec3{0, 0, 0}, x, y, z));
    // For res=64, center (nx=0.5) → floor(0.5*64) = 32.
    EXPECT_EQ(x, 32u);
    EXPECT_EQ(y, 32u);
    EXPECT_EQ(z, 32u);
}

TEST(VfxVolumetricGrid, MinCornerMapsToZero) {
    const auto cfg = MakeCfg();
    uint32_t x, y, z;
    EXPECT_TRUE(VfxVolumetricWorldToVoxel(
        cfg, Vec3{-10.0f, -10.0f, -10.0f}, x, y, z));
    EXPECT_EQ(x, 0u);
    EXPECT_EQ(y, 0u);
    EXPECT_EQ(z, 0u);
}

TEST(VfxVolumetricGrid, MaxCornerMapsToLastIndex) {
    const auto cfg = MakeCfg();
    uint32_t x, y, z;
    EXPECT_TRUE(VfxVolumetricWorldToVoxel(
        cfg, Vec3{10.0f, 10.0f, 10.0f}, x, y, z));
    EXPECT_EQ(x, 63u);
    EXPECT_EQ(y, 63u);
    EXPECT_EQ(z, 63u);
}

TEST(VfxVolumetricGrid, OutOfBoundsReturnsFalseButClamps) {
    const auto cfg = MakeCfg();
    uint32_t x, y, z;
    EXPECT_FALSE(VfxVolumetricWorldToVoxel(
        cfg, Vec3{100.0f, 0, 0}, x, y, z));
    EXPECT_EQ(x, 63u); // clamped to edge
}

TEST(VfxVolumetricGrid, ZeroResolutionFails) {
    VfxVolumetricConfig cfg;
    cfg.gridResolution = 0;
    cfg.halfExtent     = 10.0f;
    uint32_t x, y, z;
    EXPECT_FALSE(VfxVolumetricWorldToVoxel(cfg, Vec3{0,0,0}, x, y, z));
}

TEST(VfxVolumetricGrid, VoxelToWorld_RoundTripsThroughCenter) {
    const auto cfg = MakeCfg();
    const Vec3 worldCenter = VfxVolumetricVoxelToWorld(cfg, 32, 32, 32);
    // Voxel 32 center for res=64 over [-10,10] = -10 + (32.5) * 20/64 = 0.15625
    EXPECT_NEAR(worldCenter.x, 0.15625f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Henyey–Greenstein phase function
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxHenyeyGreenstein, IsotropicAtG0) {
    // At g = 0, HG collapses to 1/(4π) regardless of angle.
    const float p = VfxHenyeyGreenstein(0.5f, 0.0f);
    EXPECT_NEAR(p, 1.0f / kFourPi, 1e-5f);
}

TEST(VfxHenyeyGreenstein, IntegratesToOneOverSphere_AtMultipleG) {
    // Numerically integrate the phase function over the sphere — the
    // normalization property states ∫ p(θ) dΩ = 1 for any g. 32 samples
    // over θ with Simpson's rule is accurate enough to hit 1% tolerance.
    for (float g : {-0.5f, 0.0f, 0.5f, 0.8f}) {
        const int N = 200;
        double sum = 0.0;
        for (int i = 0; i <= N; ++i) {
            const double theta = (double)i * (double)kPi / (double)N;
            const double cosT  = std::cos(theta);
            const double sinT  = std::sin(theta);
            const double p     = VfxHenyeyGreenstein((float)cosT, g);
            // Weight for trapezoidal rule over azimuth (2π) × polar (sinθ dθ).
            const double w = (i == 0 || i == N) ? 0.5 : 1.0;
            sum += 2.0 * (double)kPi * p * sinT * ((double)kPi / (double)N) * w;
        }
        EXPECT_NEAR(sum, 1.0, 0.01) << "g=" << g;
    }
}

TEST(VfxHenyeyGreenstein, ForwardPeakAtPositiveG) {
    // Forward direction has cosθ = 1. With g = 0.8 the peak is many times
    // larger than the isotropic value.
    const float pForward  = VfxHenyeyGreenstein( 1.0f, 0.8f);
    const float pBackward = VfxHenyeyGreenstein(-1.0f, 0.8f);
    EXPECT_GT(pForward, pBackward * 10.0f);
}

TEST(VfxHenyeyGreenstein, BackPeakAtNegativeG) {
    const float pForward  = VfxHenyeyGreenstein( 1.0f, -0.8f);
    const float pBackward = VfxHenyeyGreenstein(-1.0f, -0.8f);
    EXPECT_GT(pBackward, pForward * 10.0f);
}

TEST(VfxHenyeyGreenstein, ClampedAtPoleValues) {
    // |g| = 1 would divide by zero; the implementation clamps to ±0.9999.
    const float p1 = VfxHenyeyGreenstein(0.0f,  1.0f);
    const float p2 = VfxHenyeyGreenstein(0.0f, -1.0f);
    EXPECT_TRUE(std::isfinite(p1));
    EXPECT_TRUE(std::isfinite(p2));
}

// ─────────────────────────────────────────────────────────────────────────────
// Beer–Lambert
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxBeerLambert, ZeroDensityGivesFullTransmission) {
    EXPECT_FLOAT_EQ(VfxBeerLambertTransmittance(1.0f, 0.0f, 1.0f), 1.0f);
}

TEST(VfxBeerLambert, ZeroStepGivesFullTransmission) {
    EXPECT_FLOAT_EQ(VfxBeerLambertTransmittance(1.0f, 1.0f, 0.0f), 1.0f);
}

TEST(VfxBeerLambert, ExpectedExponentialDecay) {
    // σ=1, density=1, step=1 → exp(-1) ≈ 0.3679
    EXPECT_NEAR(VfxBeerLambertTransmittance(1.0f, 1.0f, 1.0f), std::exp(-1.0f), 1e-5f);
    // σ=2, density=0.5, step=1 → exp(-1)
    EXPECT_NEAR(VfxBeerLambertTransmittance(2.0f, 0.5f, 1.0f), std::exp(-1.0f), 1e-5f);
    // σ=1, density=1, step=2 → exp(-2)
    EXPECT_NEAR(VfxBeerLambertTransmittance(1.0f, 1.0f, 2.0f), std::exp(-2.0f), 1e-5f);
}

TEST(VfxBeerLambert, NegativeInputsClampedToZero) {
    EXPECT_FLOAT_EQ(VfxBeerLambertTransmittance(-1.0f, 1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(VfxBeerLambertTransmittance(1.0f, -1.0f, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(VfxBeerLambertTransmittance(1.0f, 1.0f, -1.0f), 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step contribution
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxVolumetricStep, ZeroDensityContributesNothing) {
    const auto cfg = MakeCfg();
    const Vec3 c = VfxVolumetricStepContribution(cfg, 0.0f, 1.0f, 1.0f, 1.0f);
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 0.0f);
}

TEST(VfxVolumetricStep, ZeroTransmittanceSaturates) {
    const auto cfg = MakeCfg();
    const Vec3 c = VfxVolumetricStepContribution(cfg, 1.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(c.x, 0.0f);
}

TEST(VfxVolumetricStep, ColourMatchesScatteringColour) {
    VfxVolumetricConfig cfg = MakeCfg();
    cfg.scatteringColor = Vec3{1.0f, 0.5f, 0.25f};
    cfg.hgAnisotropy    = 0.0f; // isotropic so phase is a known constant
    const Vec3 c = VfxVolumetricStepContribution(cfg, 1.0f, 0.5f, 0.5f, 1.0f);
    // Red : green : blue ratio must match the scattering colour ratios.
    EXPECT_GT(c.x, 0.0f);
    EXPECT_NEAR(c.y / c.x, 0.5f,  1e-5f);
    EXPECT_NEAR(c.z / c.x, 0.25f, 1e-5f);
}
