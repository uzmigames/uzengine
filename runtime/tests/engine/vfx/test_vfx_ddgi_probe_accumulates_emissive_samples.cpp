/// Integration tests for phase16_vfx-lumen-gi-hardware-validation — items 3.1, 4.2, 4.3.
///
/// Validates that the CPU-side DDGI probe accumulation helpers produce the
/// analytically expected Karis-attenuated irradiance for a single-probe grid
/// receiving one emissive sample 50 cm away.  The GPU shader mirrors these
/// helpers byte-for-byte (ddgi_probe_update_cs.hlsl:316-330), so correct CPU
/// output guarantees correct GPU output for the same formula.
///
/// @ref Karis 2013 "Real Shading in Unreal Engine 4"
/// @ref engine/vfx/lighting/vfx_ddgi_injection.h
/// @ref runtime/engine/vfx/lighting/vfx_ddgi_injection.cpp:21-47

#include <gtest/gtest.h>

#include "engine/vfx/lighting/vfx_ddgi_injection.h"

#include <array>
#include <cmath>

using namespace uz;

namespace {

// Recomputes VfxDDGIAttenuation analytically so the test does not just
// call the same function it is verifying — it independently derives the
// expected value using the published Karis formula.
float KarisAttenuationAnalytical(float d, float r) {
    if (r <= 0.0f) return 0.0f;
    const float dOverR = d / r;
    const float t  = dOverR * dOverR * dOverR * dOverR; // (d/r)^4
    const float s  = (t >= 1.0f) ? 0.0f : (1.0f - t);
    const float s2 = s * s;
    const float invSq = 1.0f / (d * d + 1.0f);
    return s2 * invSq;
}

VfxEmissiveSample MakeSample(Vec3 pos, Vec3 emi, float r) {
    VfxEmissiveSample s;
    s.position        = pos;
    s.emissive        = emi;
    s.influenceRadius = r;
    return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Canonical scenario: single probe at origin, sample 50 cm away
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, SingleSample_50cm_MatchesAnalytical) {
    // Emissive = (10, 5, 2) as in spec; influenceRadius = 200 cm.
    const Vec3 emissive{10.0f, 5.0f, 2.0f};
    const float radius      = 200.0f;
    const Vec3 samplePos{50.0f, 0.0f, 0.0f};
    const Vec3 probePos{0.0f, 0.0f, 0.0f};

    const auto sample = MakeSample(samplePos, emissive, radius);

    // Analytical expected: d = 50, r = 200
    const float d        = 50.0f;
    const float expected_a = KarisAttenuationAnalytical(d, radius);
    const Vec3 expected{
        emissive.x * expected_a,
        emissive.y * expected_a,
        emissive.z * expected_a,
    };

    const Vec3 got = VfxDDGIProbeContribution(sample, probePos);

    EXPECT_NEAR(got.x, expected.x, 1e-3f)
        << "Band-0 R channel mismatch at d=50cm, r=200cm";
    EXPECT_NEAR(got.y, expected.y, 1e-3f)
        << "Band-0 G channel mismatch at d=50cm, r=200cm";
    EXPECT_NEAR(got.z, expected.z, 1e-3f)
        << "Band-0 B channel mismatch at d=50cm, r=200cm";
}

// ─────────────────────────────────────────────────────────────────────────────
// Accumulation via VfxDDGIAccumulateAtProbe (multi-sample sum)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, AccumulateAtProbe_SingleSample_EqualsSingleContrib) {
    const auto sample = MakeSample(Vec3{50.0f, 0.0f, 0.0f},
                                   Vec3{10.0f, 5.0f, 2.0f}, 200.0f);
    const Vec3 probePos{0.0f, 0.0f, 0.0f};

    const Vec3 single  = VfxDDGIProbeContribution(sample, probePos);
    const Vec3 summed  = VfxDDGIAccumulateAtProbe(&sample, 1u, probePos);

    EXPECT_FLOAT_EQ(summed.x, single.x);
    EXPECT_FLOAT_EQ(summed.y, single.y);
    EXPECT_FLOAT_EQ(summed.z, single.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge case: distance >= influenceRadius → zero contribution
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, DistanceBeyondRadius_ZeroContribution) {
    // Sample at distance exactly equal to influenceRadius — early-out fires.
    const float radius = 200.0f;
    const auto sample  = MakeSample(Vec3{radius, 0.0f, 0.0f},
                                    Vec3{100.0f, 50.0f, 20.0f}, radius);
    const Vec3 contrib = VfxDDGIProbeContribution(sample, Vec3{0.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(contrib.x, 0.0f) << "At d==r contribution must be 0";
    EXPECT_FLOAT_EQ(contrib.y, 0.0f);
    EXPECT_FLOAT_EQ(contrib.z, 0.0f);
}

TEST(VfxDdgiProbeAccumulate, DistanceFarBeyondRadius_ZeroContribution) {
    // Distance 10× influenceRadius.
    const float radius = 50.0f;
    const auto sample  = MakeSample(Vec3{500.0f, 0.0f, 0.0f},
                                    Vec3{1000.0f, 500.0f, 200.0f}, radius);
    const Vec3 contrib = VfxDDGIProbeContribution(sample, Vec3{0.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(contrib.x, 0.0f);
    EXPECT_FLOAT_EQ(contrib.y, 0.0f);
    EXPECT_FLOAT_EQ(contrib.z, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge case: distance == 0 → attenuation clamped to 1 (maximum)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, ZeroDistance_AttenuationIsOne) {
    // Probe coincides with sample — contribution must equal emissive exactly.
    const Vec3 emissive{10.0f, 5.0f, 2.0f};
    const auto sample = MakeSample(Vec3{0.0f, 0.0f, 0.0f}, emissive, 200.0f);
    const Vec3 contrib = VfxDDGIProbeContribution(sample, Vec3{0.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(contrib.x, emissive.x)
        << "At d=0, contribution must equal emissive.r";
    EXPECT_FLOAT_EQ(contrib.y, emissive.y)
        << "At d=0, contribution must equal emissive.g";
    EXPECT_FLOAT_EQ(contrib.z, emissive.z)
        << "At d=0, contribution must equal emissive.b";
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge case: very bright sample — no overflow or NaN
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, BrightSample_NoNanNoInf) {
    // 1e6 intensity; probe 1 cm away, radius 200 cm.
    const Vec3 emissive{1e6f, 1e6f, 1e6f};
    const auto sample = MakeSample(Vec3{1.0f, 0.0f, 0.0f}, emissive, 200.0f);
    const Vec3 contrib = VfxDDGIProbeContribution(sample, Vec3{0.0f, 0.0f, 0.0f});

    EXPECT_FALSE(std::isnan(contrib.x)) << "R channel must not be NaN for very bright emitter";
    EXPECT_FALSE(std::isnan(contrib.y)) << "G channel must not be NaN";
    EXPECT_FALSE(std::isnan(contrib.z)) << "B channel must not be NaN";
    EXPECT_FALSE(std::isinf(contrib.x)) << "R channel must not be Inf";
    EXPECT_FALSE(std::isinf(contrib.y)) << "G channel must not be Inf";
    EXPECT_FALSE(std::isinf(contrib.z)) << "B channel must not be Inf";
    // Sanity check: value is finite and positive
    EXPECT_GT(contrib.x, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge case: sampleCount == 0 → zero contribution (shader gate equivalent)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, ZeroSampleCount_ZeroContribution) {
    // Mimics the shader gate: vfxEmissiveSampleCount == 0 → skip accumulation.
    const Vec3 total = VfxDDGIAccumulateAtProbe(nullptr, 0u, Vec3{0.0f, 0.0f, 0.0f});

    EXPECT_FLOAT_EQ(total.x, 0.0f) << "Zero sample count must produce zero irradiance";
    EXPECT_FLOAT_EQ(total.y, 0.0f);
    EXPECT_FLOAT_EQ(total.z, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-sample accumulation: sum is >= each individual contribution
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, MultiSample_SumExceedsEachIndividual) {
    const Vec3 probePos{0.0f, 0.0f, 0.0f};
    std::array<VfxEmissiveSample, 3> samples = {
        MakeSample(Vec3{10.0f, 0.0f, 0.0f}, Vec3{5.0f, 2.0f, 1.0f}, 200.0f),
        MakeSample(Vec3{20.0f, 0.0f, 0.0f}, Vec3{3.0f, 4.0f, 0.5f}, 200.0f),
        MakeSample(Vec3{30.0f, 0.0f, 0.0f}, Vec3{1.0f, 1.0f, 1.0f}, 200.0f),
    };

    const Vec3 total = VfxDDGIAccumulateAtProbe(samples.data(), 3u, probePos);
    const Vec3 c0    = VfxDDGIProbeContribution(samples[0], probePos);
    const Vec3 c1    = VfxDDGIProbeContribution(samples[1], probePos);
    const Vec3 c2    = VfxDDGIProbeContribution(samples[2], probePos);

    // Total must be the exact arithmetic sum (all within radius).
    EXPECT_NEAR(total.x, c0.x + c1.x + c2.x, 1e-5f);
    EXPECT_NEAR(total.y, c0.y + c1.y + c2.y, 1e-5f);
    EXPECT_NEAR(total.z, c0.z + c1.z + c2.z, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Attenuation monotonicity along the canonical 50cm scenario
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxDdgiProbeAccumulate, Attenuation_MonotonicDecay_50cm_Scenario) {
    // Verify that as the probe moves farther from the sample (staying within
    // influenceRadius=200 cm), irradiance decreases monotonically.
    const Vec3 emissive{10.0f, 5.0f, 2.0f};
    const float radius = 200.0f;
    const auto sample  = MakeSample(Vec3{0.0f, 0.0f, 0.0f}, emissive, radius);

    float prevX = VfxDDGIProbeContribution(sample, Vec3{0.0f, 0.0f, 0.0f}).x;
    for (float probe_x = 5.0f; probe_x < 199.0f; probe_x += 10.0f) {
        const float currX = VfxDDGIProbeContribution(
            sample, Vec3{probe_x, 0.0f, 0.0f}).x;
        EXPECT_LE(currX, prevX + 1e-5f)
            << "Irradiance must not increase as probe moves farther from sample at x="
            << probe_x;
        prevX = currX;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU verification gap note
// ─────────────────────────────────────────────────────────────────────────────
// The GPU CS path at ddgi_probe_update_cs.hlsl:307-358 mirrors the above
// math byte-for-byte (verified in task 1.4 cross-check).  A full GPU
// integration test requires DXR hardware and cannot run in the headless
// gtest suite.  Visual A/B smoke test (task 2.1) covers GPU execution.
