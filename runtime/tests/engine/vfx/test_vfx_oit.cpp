/// Unit tests for phase 8.16.0 — VFX Weighted Blended OIT.
///
/// Validates the CPU-mirror weight / resolve helpers. The HLSL resolve
/// shader mirrors `VfxOITResolve` byte-for-byte so these tests cover the
/// GPU math without requiring a device.
///
/// @ref engine/vfx/modules/vfx_oit.h
/// @ref docs/engine/25-vfx/33-weighted-oit.md

#include <gtest/gtest.h>

#include "engine/vfx/modules/vfx_oit.h"

#include <cmath>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Blend mode enum
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxBlendMode, StringNamesStable) {
    EXPECT_STREQ(VfxBlendModeToString(VfxBlendMode::AlphaBlend),  "alpha_blend");
    EXPECT_STREQ(VfxBlendModeToString(VfxBlendMode::Additive),    "additive");
    EXPECT_STREQ(VfxBlendModeToString(VfxBlendMode::PremulAlpha), "premul_alpha");
    EXPECT_STREQ(VfxBlendModeToString(VfxBlendMode::WeightedOIT), "weighted_oit");
}

TEST(VfxBlendMode, NumericValuesMatchCBLayout) {
    // VFXBillboardCB::blendModeHint is a uint32; matching the pre-8.16
    // numeric assignments ensures saved assets keep rendering identically.
    EXPECT_EQ(static_cast<uint32_t>(VfxBlendMode::AlphaBlend),  0u);
    EXPECT_EQ(static_cast<uint32_t>(VfxBlendMode::Additive),    1u);
    EXPECT_EQ(static_cast<uint32_t>(VfxBlendMode::PremulAlpha), 2u);
    EXPECT_EQ(static_cast<uint32_t>(VfxBlendMode::WeightedOIT), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxOITWeight
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOITWeight, NearerAlwaysBeatsFarther_AtEqualAlpha) {
    // Two samples with the same alpha: the nearer one must receive the
    // larger weight so the resolve lands the closer colour dominant.
    const float near = VfxOITWeight(0.1f, 0.5f);
    const float far  = VfxOITWeight(0.9f, 0.5f);
    EXPECT_GT(near, far);
}

TEST(VfxOITWeight, HigherAlphaGetsMoreWeight_AtEqualDepth) {
    const float hi = VfxOITWeight(0.5f, 0.8f);
    const float lo = VfxOITWeight(0.5f, 0.2f);
    EXPECT_GT(hi, lo);
}

TEST(VfxOITWeight, WeightMonotonicInAlphaAtSameDepth) {
    float prev = VfxOITWeight(0.3f, 0.0f);
    for (float a = 0.05f; a <= 1.0f; a += 0.05f) {
        const float w = VfxOITWeight(0.3f, a);
        EXPECT_GE(w, prev - 1e-6f) << "alpha=" << a;
        prev = w;
    }
}

TEST(VfxOITWeight, BlendFactorLowerBound) {
    // At α = 0, the α·max(...) term collapses to 0 — the weight is 0
    // regardless of blendFactor (the formula multiplies by α outside the
    // max). Authors rely on this so fully-transparent fragments contribute
    // nothing to the accumulation buffer.
    EXPECT_FLOAT_EQ(VfxOITWeight(0.5f, 0.0f, 0.01f), 0.0f);
}

TEST(VfxOITWeight, ClampOnOutOfRangeInputs) {
    // Ensure negative / >1 inputs don't produce NaN or extreme values.
    const float w1 = VfxOITWeight(-1.0f, 0.5f);
    const float w2 = VfxOITWeight(2.0f,  0.5f);
    const float w3 = VfxOITWeight(0.5f,  -0.5f);
    const float w4 = VfxOITWeight(0.5f,   2.0f);
    EXPECT_TRUE(std::isfinite(w1));
    EXPECT_TRUE(std::isfinite(w2));
    EXPECT_TRUE(std::isfinite(w3));
    EXPECT_TRUE(std::isfinite(w4));
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxOITResolve — composite math
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOITResolve, FullReveal_EqualsBackground) {
    // revealage = 1 means nothing occluded the background.
    const Vec3 accum{0.3f, 0.3f, 0.3f};
    const Vec3 bg{0.1f, 0.2f, 0.4f};
    const Vec3 out = VfxOITResolve(accum, 1.0f, 1.0f, bg);
    EXPECT_NEAR(out.x, bg.x, 1e-5f);
    EXPECT_NEAR(out.y, bg.y, 1e-5f);
    EXPECT_NEAR(out.z, bg.z, 1e-5f);
}

TEST(VfxOITResolve, ZeroReveal_EqualsAverage) {
    // revealage = 0 means the particles fully occlude the background.
    // final = accum / accumSum.
    const Vec3 accum{1.0f, 0.5f, 0.25f};
    const Vec3 bg{0.9f, 0.9f, 0.9f};
    const Vec3 out = VfxOITResolve(accum, 2.0f, 0.0f, bg);
    EXPECT_NEAR(out.x, 0.5f,  1e-5f);
    EXPECT_NEAR(out.y, 0.25f, 1e-5f);
    EXPECT_NEAR(out.z, 0.125f, 1e-5f);
}

TEST(VfxOITResolve, ZeroAccumSumClampedToSafe) {
    // Guard against accum.a == 0 producing NaN — the implementation
    // clamps to max(sum, 1e-4).
    const Vec3 accum{0.0f, 0.0f, 0.0f};
    const Vec3 bg{0.5f, 0.5f, 0.5f};
    const Vec3 out = VfxOITResolve(accum, 0.0f, 1.0f, bg);
    EXPECT_TRUE(std::isfinite(out.x));
    EXPECT_TRUE(std::isfinite(out.y));
    EXPECT_TRUE(std::isfinite(out.z));
}

TEST(VfxOITResolve, HalfRevealMixesHalfHalf) {
    const Vec3 accum{1.0f, 1.0f, 1.0f};
    const Vec3 bg{0.0f, 0.0f, 0.0f};
    const Vec3 out = VfxOITResolve(accum, 1.0f, 0.5f, bg);
    // avg = (1,1,1); blend = 0.5 → result = 0.5,0.5,0.5
    EXPECT_NEAR(out.x, 0.5f, 1e-5f);
    EXPECT_NEAR(out.y, 0.5f, 1e-5f);
    EXPECT_NEAR(out.z, 0.5f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resolved alpha
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOITResolveAlpha, IsOneMinusRevealage) {
    EXPECT_FLOAT_EQ(VfxOITResolveAlpha(0.0f), 1.0f);
    EXPECT_FLOAT_EQ(VfxOITResolveAlpha(0.25f), 0.75f);
    EXPECT_FLOAT_EQ(VfxOITResolveAlpha(1.0f), 0.0f);
}

TEST(VfxOITResolveAlpha, ClampsNegativeRevealage) {
    EXPECT_FLOAT_EQ(VfxOITResolveAlpha(-0.5f), 1.0f);
    EXPECT_FLOAT_EQ(VfxOITResolveAlpha( 2.0f), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end: simulated 3-sample accumulation and resolve
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxOITEndToEnd, ThreeSamplesResolveToWeightedAverage) {
    // Simulate three overlapping fragments drawn in arbitrary order.
    struct Sample { Vec3 color; float alpha; float linearZ; };
    const Sample s[] = {
        { {1.0f, 0.0f, 0.0f}, 0.5f, 0.2f }, // near red
        { {0.0f, 1.0f, 0.0f}, 0.5f, 0.6f }, // mid green
        { {0.0f, 0.0f, 1.0f}, 0.5f, 0.9f }, // far blue
    };

    // Accumulation pass — additive blend.
    Vec3  accumRGB{0,0,0};
    float accumSum  = 0.0f;
    float reveal    = 1.0f;
    for (const auto& x : s) {
        const float w = VfxOITWeight(x.linearZ, x.alpha);
        accumRGB.x += x.color.x * x.alpha * w;
        accumRGB.y += x.color.y * x.alpha * w;
        accumRGB.z += x.color.z * x.alpha * w;
        accumSum   += x.alpha * w;
        reveal     *= (1.0f - x.alpha);
    }

    const Vec3 bg{0.1f, 0.1f, 0.1f};
    const Vec3 resolved = VfxOITResolve(accumRGB, accumSum, reveal, bg);

    // Sanity: all three channels are finite and non-negative.
    EXPECT_TRUE(std::isfinite(resolved.x));
    EXPECT_TRUE(std::isfinite(resolved.y));
    EXPECT_TRUE(std::isfinite(resolved.z));
    EXPECT_GE(resolved.x, 0.0f);
    EXPECT_GE(resolved.y, 0.0f);
    EXPECT_GE(resolved.z, 0.0f);
    // Nearer red should dominate green/blue because the near weight is
    // largest.
    EXPECT_GT(resolved.x, resolved.y);
    EXPECT_GT(resolved.x, resolved.z);
}

TEST(VfxOITEndToEnd, ReorderingFragmentsYieldsSameResult) {
    // Commutative additive blend — two different draw orders must produce
    // identical resolve output.
    struct Sample { Vec3 color; float alpha; float linearZ; };
    const Sample samples[] = {
        { {0.2f, 0.8f, 0.1f}, 0.3f, 0.2f },
        { {0.9f, 0.1f, 0.4f}, 0.6f, 0.5f },
        { {0.5f, 0.5f, 0.5f}, 0.8f, 0.8f },
    };

    auto accumulate = [](const std::vector<Sample>& order, Vec3& rgb,
                         float& accumSum, float& reveal) {
        rgb = Vec3{0,0,0}; accumSum = 0.0f; reveal = 1.0f;
        for (const auto& x : order) {
            const float w = VfxOITWeight(x.linearZ, x.alpha);
            rgb.x   += x.color.x * x.alpha * w;
            rgb.y   += x.color.y * x.alpha * w;
            rgb.z   += x.color.z * x.alpha * w;
            accumSum += x.alpha * w;
            reveal  *= (1.0f - x.alpha);
        }
    };

    Vec3 rgbA, rgbB; float sumA, sumB, revA, revB;
    accumulate({samples[0], samples[1], samples[2]}, rgbA, sumA, revA);
    accumulate({samples[2], samples[0], samples[1]}, rgbB, sumB, revB);

    EXPECT_NEAR(rgbA.x, rgbB.x, 1e-5f);
    EXPECT_NEAR(rgbA.y, rgbB.y, 1e-5f);
    EXPECT_NEAR(rgbA.z, rgbB.z, 1e-5f);
    EXPECT_NEAR(sumA,   sumB,   1e-5f);
    EXPECT_NEAR(revA,   revB,   1e-5f);
}
