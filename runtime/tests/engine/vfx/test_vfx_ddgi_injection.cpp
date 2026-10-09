/// Unit tests for phase 8.23.0 — VFX emissive → DDGI injection.
///
/// Validates luminance ranking, Karis attenuation falloff, per-probe
/// contribution, top-N selection, and accumulation across samples.
///
/// @ref engine/vfx/lighting/vfx_ddgi_injection.h
/// @ref docs/engine/25-vfx/40-ddgi-emissive-injection.md

#include <gtest/gtest.h>

#include "engine/vfx/lighting/vfx_ddgi_injection.h"

#include <array>

using namespace uz;

namespace {

VfxEmissiveSample MakeSample(Vec3 pos, Vec3 emi, float r = 10.0f) {
    VfxEmissiveSample s;
    s.position = pos;
    s.emissive = emi;
    s.influenceRadius = r;
    return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Luminance
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGILuminance, Rec709Weights) {
    VfxEmissiveSample s = MakeSample(Vec3{0, 0, 0}, Vec3{1.0f, 1.0f, 1.0f});
    EXPECT_NEAR(s.Luminance(), 0.2126f + 0.7152f + 0.0722f, 1e-5f);
}

TEST(VfxDDGILuminance, GreenDominatesRed) {
    // Green is weighted ~3.4× more than red in Rec. 709 — test that
    // equal-intensity green > red.
    VfxEmissiveSample green = MakeSample(Vec3{0, 0, 0}, Vec3{0, 1, 0});
    VfxEmissiveSample red   = MakeSample(Vec3{0, 0, 0}, Vec3{1, 0, 0});
    EXPECT_GT(green.Luminance(), red.Luminance());
}

// ─────────────────────────────────────────────────────────────────────────────
// Karis attenuation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGIAttenuation, ZeroDistanceMaxValue) {
    const float a = VfxDDGIAttenuation(0.0f, 10.0f);
    // s2 = 1 · 1 = 1; invSq = 1/(0+1) = 1; total 1.
    EXPECT_NEAR(a, 1.0f, 1e-5f);
}

TEST(VfxDDGIAttenuation, BeyondRadiusIsZero) {
    EXPECT_FLOAT_EQ(VfxDDGIAttenuation(10.0f, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(VfxDDGIAttenuation(20.0f, 10.0f), 0.0f);
}

TEST(VfxDDGIAttenuation, ZeroRadiusReturnsZero) {
    EXPECT_FLOAT_EQ(VfxDDGIAttenuation(5.0f, 0.0f), 0.0f);
}

TEST(VfxDDGIAttenuation, Monotonic_NearerIsBrighter) {
    // Across the falloff curve, closer should always produce higher or
    // equal attenuation. Sample along [0, r).
    float prev = VfxDDGIAttenuation(0.0f, 10.0f);
    for (float d = 0.5f; d < 9.9f; d += 0.5f) {
        const float a = VfxDDGIAttenuation(d, 10.0f);
        EXPECT_LE(a, prev + 1e-6f) << "at d=" << d;
        prev = a;
    }
}

TEST(VfxDDGIAttenuation, NegativeDistanceClampedToZero) {
    EXPECT_NEAR(VfxDDGIAttenuation(-5.0f, 10.0f), 1.0f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxDDGIProbeContribution
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGIProbe, ContribZeroWhenOutsideRadius) {
    const auto s = MakeSample(Vec3{0, 0, 0}, Vec3{10, 10, 10}, /*r=*/5.0f);
    const Vec3 c = VfxDDGIProbeContribution(s, Vec3{100, 0, 0});
    EXPECT_FLOAT_EQ(c.x, 0.0f);
    EXPECT_FLOAT_EQ(c.y, 0.0f);
    EXPECT_FLOAT_EQ(c.z, 0.0f);
}

TEST(VfxDDGIProbe, ContribAtZeroDistanceMatchesEmissive) {
    const auto s = MakeSample(Vec3{1, 2, 3}, Vec3{5, 6, 7}, /*r=*/10.0f);
    const Vec3 c = VfxDDGIProbeContribution(s, Vec3{1, 2, 3});
    // Attenuation at d=0 is 1, so contribution == emissive.
    EXPECT_FLOAT_EQ(c.x, 5.0f);
    EXPECT_FLOAT_EQ(c.y, 6.0f);
    EXPECT_FLOAT_EQ(c.z, 7.0f);
}

TEST(VfxDDGIProbe, FalloffWithDistance) {
    const auto s = MakeSample(Vec3{0, 0, 0}, Vec3{1, 1, 1}, /*r=*/10.0f);
    const Vec3 near = VfxDDGIProbeContribution(s, Vec3{1, 0, 0});
    const Vec3 far  = VfxDDGIProbeContribution(s, Vec3{5, 0, 0});
    EXPECT_GT(near.x, far.x);
    EXPECT_GT(near.y, far.y);
    EXPECT_GT(near.z, far.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Top-N selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGITopN, ReturnsHighestLuminanceFirst) {
    std::array<VfxEmissiveSample, 4> in = {
        MakeSample(Vec3{0,0,0}, Vec3{0.1f, 0.1f, 0.1f}),
        MakeSample(Vec3{1,0,0}, Vec3{10.0f, 10.0f, 10.0f}), // brightest
        MakeSample(Vec3{2,0,0}, Vec3{1.0f, 1.0f, 1.0f}),
        MakeSample(Vec3{3,0,0}, Vec3{5.0f, 5.0f, 5.0f}),
    };
    std::array<VfxEmissiveSample, 4> out{};
    const uint32_t n = VfxDDGISelectTopN(in.data(), 4, out.data(), 2);
    ASSERT_EQ(n, 2u);
    EXPECT_FLOAT_EQ(out[0].emissive.x, 10.0f);
    EXPECT_FLOAT_EQ(out[1].emissive.x, 5.0f);
}

TEST(VfxDDGITopN, ClampsToInputCount) {
    std::array<VfxEmissiveSample, 2> in = {
        MakeSample(Vec3{0,0,0}, Vec3{1, 1, 1}),
        MakeSample(Vec3{0,0,0}, Vec3{2, 2, 2}),
    };
    std::array<VfxEmissiveSample, 4> out{};
    const uint32_t n = VfxDDGISelectTopN(in.data(), 2, out.data(), 10);
    EXPECT_EQ(n, 2u);
}

TEST(VfxDDGITopN, ZeroInputsReturnsZero) {
    std::array<VfxEmissiveSample, 1> out{};
    EXPECT_EQ(VfxDDGISelectTopN(nullptr, 0, out.data(), 1), 0u);
}

TEST(VfxDDGITopN, ZeroMaxNReturnsZero) {
    std::array<VfxEmissiveSample, 1> in = { MakeSample(Vec3{0,0,0}, Vec3{1,1,1}) };
    std::array<VfxEmissiveSample, 1> out{};
    EXPECT_EQ(VfxDDGISelectTopN(in.data(), 1, out.data(), 0), 0u);
}

TEST(VfxDDGITopN, InPlaceAliasingSafe) {
    std::array<VfxEmissiveSample, 3> buf = {
        MakeSample(Vec3{0,0,0}, Vec3{1, 1, 1}),
        MakeSample(Vec3{0,0,0}, Vec3{5, 5, 5}),
        MakeSample(Vec3{0,0,0}, Vec3{3, 3, 3}),
    };
    const uint32_t n = VfxDDGISelectTopN(buf.data(), 3, buf.data(), 2);
    EXPECT_EQ(n, 2u);
    EXPECT_FLOAT_EQ(buf[0].emissive.x, 5.0f);
    EXPECT_FLOAT_EQ(buf[1].emissive.x, 3.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Full-probe accumulation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDDGIAccumulate, SumsAcrossSamples) {
    std::array<VfxEmissiveSample, 3> samples = {
        MakeSample(Vec3{0, 0, 0}, Vec3{1, 0, 0}, 10.0f), // close red
        MakeSample(Vec3{0, 0, 0}, Vec3{0, 1, 0}, 10.0f), // close green
        MakeSample(Vec3{0, 0, 0}, Vec3{0, 0, 1}, 10.0f), // close blue
    };
    const Vec3 total = VfxDDGIAccumulateAtProbe(samples.data(), 3, Vec3{0, 0, 0});
    EXPECT_FLOAT_EQ(total.x, 1.0f);
    EXPECT_FLOAT_EQ(total.y, 1.0f);
    EXPECT_FLOAT_EQ(total.z, 1.0f);
}

TEST(VfxDDGIAccumulate, ZeroCountReturnsZero) {
    const Vec3 total = VfxDDGIAccumulateAtProbe(nullptr, 0, Vec3{0,0,0});
    EXPECT_FLOAT_EQ(total.x, 0.0f);
    EXPECT_FLOAT_EQ(total.y, 0.0f);
    EXPECT_FLOAT_EQ(total.z, 0.0f);
}
