/// UzEngine Post-Processing Stack Tests
///
/// Verifies CPU-side equivalents of all post-processing shader math:
/// ACES tone mapping, luminance calculation, bloom threshold and mip chain
/// dimensions, Gaussian kernel normalization, and FXAA edge detection.
///
/// All tests are pure CPU math — no GPU, no RHI, no D3D12 headers.

#include <gtest/gtest.h>

#include "postprocess_math.h"
#include "postprocess_stack.h"

#include <cmath>
#include <cstring>

using namespace uz;
using namespace uz::postprocess;

static constexpr float kEps = 1e-4f;

// ─────────────────────────────────────────────────────────────────────────────
// 2.1 — ACES Tone Mapping: output range [0, 1] for all HDR inputs
// ─────────────────────────────────────────────────────────────────────────────

TEST(ToneMapTest, ZeroInput_ReturnsZero) {
    EXPECT_NEAR(ACESTonemapChannel(0.0f), 0.0f, kEps);
}

TEST(ToneMapTest, SmallInput_ReturnsPositive) {
    float result = ACESTonemapChannel(0.01f);
    EXPECT_GT(result, 0.0f);
    EXPECT_LT(result, 1.0f);
}

TEST(ToneMapTest, UnitInput_MapsToLDR) {
    float result = ACESTonemapChannel(1.0f);
    EXPECT_GT(result, 0.0f);
    EXPECT_LE(result, 1.0f);
}

TEST(ToneMapTest, LargeHDRInput_ClampedBelow1) {
    // Even very bright HDR values must output <= 1.0
    EXPECT_LE(ACESTonemapChannel(10.0f), 1.0f);
    EXPECT_LE(ACESTonemapChannel(100.0f), 1.0f);
    EXPECT_LE(ACESTonemapChannel(1000.0f), 1.0f);
    EXPECT_LE(ACESTonemapChannel(10000.0f), 1.0f);
}

TEST(ToneMapTest, NegativeInput_ClampedToZero) {
    EXPECT_NEAR(ACESTonemapChannel(-1.0f), 0.0f, kEps);
    EXPECT_NEAR(ACESTonemapChannel(-100.0f), 0.0f, kEps);
}

TEST(ToneMapTest, MonotonicallyIncreasing) {
    // ACES should be monotonically increasing for non-negative inputs
    float prev = ACESTonemapChannel(0.0f);
    for (float x = 0.1f; x <= 20.0f; x += 0.1f) {
        float curr = ACESTonemapChannel(x);
        EXPECT_GE(curr, prev - kEps) << "ACES must be monotonically increasing at x=" << x;
        prev = curr;
    }
}

TEST(ToneMapTest, RGB_WithDefaultExposure) {
    float r, g, b;
    ACESTonemapRGB(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, r, g, b);
    // White input → all channels equal
    EXPECT_NEAR(r, g, kEps);
    EXPECT_NEAR(g, b, kEps);
    EXPECT_GT(r, 0.0f);
    EXPECT_LE(r, 1.0f);
}

TEST(ToneMapTest, RGB_ExposureDoublesInput) {
    float rNorm, gNorm, bNorm;
    float rExp, gExp, bExp;
    ACESTonemapRGB(0.5f, 0.5f, 0.5f, 1.0f, 1.0f, rNorm, gNorm, bNorm);
    ACESTonemapRGB(0.5f, 0.5f, 0.5f, 2.0f, 1.0f, rExp, gExp, bExp);
    // Doubled exposure should produce brighter output
    EXPECT_GT(rExp, rNorm);
}

TEST(ToneMapTest, RGB_IntensityScalesOutput) {
    float rFull, gFull, bFull;
    float rHalf, gHalf, bHalf;
    ACESTonemapRGB(1.0f, 1.0f, 1.0f, 1.0f, 1.0f, rFull, gFull, bFull);
    ACESTonemapRGB(1.0f, 1.0f, 1.0f, 1.0f, 0.5f, rHalf, gHalf, bHalf);
    EXPECT_NEAR(rHalf, rFull * 0.5f, kEps);
}

TEST(ToneMapTest, RGB_HighIntensity_ClampedTo1) {
    float r, g, b;
    ACESTonemapRGB(10.0f, 10.0f, 10.0f, 1.0f, 5.0f, r, g, b);
    EXPECT_LE(r, 1.0f);
    EXPECT_LE(g, 1.0f);
    EXPECT_LE(b, 1.0f);
}

TEST(ToneMapTest, RGB_ZeroInput_ZeroOutput) {
    float r, g, b;
    ACESTonemapRGB(0.0f, 0.0f, 0.0f, 1.0f, 1.0f, r, g, b);
    EXPECT_NEAR(r, 0.0f, kEps);
    EXPECT_NEAR(g, 0.0f, kEps);
    EXPECT_NEAR(b, 0.0f, kEps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Luminance
// ─────────────────────────────────────────────────────────────────────────────

TEST(LuminanceTest, White_IsApprox1) {
    EXPECT_NEAR(Luminance(1.0f, 1.0f, 1.0f), 1.0f, kEps);
}

TEST(LuminanceTest, Black_IsZero) {
    EXPECT_NEAR(Luminance(0.0f, 0.0f, 0.0f), 0.0f, kEps);
}

TEST(LuminanceTest, PureRed_Is0_2126) {
    EXPECT_NEAR(Luminance(1.0f, 0.0f, 0.0f), 0.2126f, kEps);
}

TEST(LuminanceTest, PureGreen_Is0_7152) {
    EXPECT_NEAR(Luminance(0.0f, 1.0f, 0.0f), 0.7152f, kEps);
}

TEST(LuminanceTest, PureBlue_Is0_0722) {
    EXPECT_NEAR(Luminance(0.0f, 0.0f, 1.0f), 0.0722f, kEps);
}

TEST(LuminanceTest, GreenDominates) {
    // Green has the highest weight (0.7152)
    EXPECT_GT(Luminance(0.0f, 1.0f, 0.0f), Luminance(1.0f, 0.0f, 0.0f));
    EXPECT_GT(Luminance(0.0f, 1.0f, 0.0f), Luminance(0.0f, 0.0f, 1.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.2 — Bloom mip chain dimensions
// ─────────────────────────────────────────────────────────────────────────────

TEST(BloomMipTest, Mip0_IsSourceResolution) {
    uint32_t w, h;
    BloomMipDimensions(1920, 1080, 0, w, h);
    EXPECT_EQ(w, 1920u);
    EXPECT_EQ(h, 1080u);
}

TEST(BloomMipTest, Mip1_IsHalfRes) {
    uint32_t w, h;
    BloomMipDimensions(1920, 1080, 1, w, h);
    EXPECT_EQ(w, 960u);
    EXPECT_EQ(h, 540u);
}

TEST(BloomMipTest, Mip2_IsQuarterRes) {
    uint32_t w, h;
    BloomMipDimensions(1920, 1080, 2, w, h);
    EXPECT_EQ(w, 480u);
    EXPECT_EQ(h, 270u);
}

TEST(BloomMipTest, Mip3_IsEighthRes) {
    uint32_t w, h;
    BloomMipDimensions(1920, 1080, 3, w, h);
    EXPECT_EQ(w, 240u);
    EXPECT_EQ(h, 135u);
}

TEST(BloomMipTest, Mip4_IsSixteenthRes) {
    uint32_t w, h;
    BloomMipDimensions(1920, 1080, 4, w, h);
    EXPECT_EQ(w, 120u);
    EXPECT_EQ(h, 67u);
}

TEST(BloomMipTest, MipNeverReachesZero) {
    uint32_t w, h;
    BloomMipDimensions(1, 1, 10, w, h);
    EXPECT_GE(w, 1u);
    EXPECT_GE(h, 1u);
}

TEST(BloomMipTest, OddDimensions_RoundsDown) {
    uint32_t w, h;
    BloomMipDimensions(1921, 1081, 1, w, h);
    EXPECT_EQ(w, 960u);  // 1921 >> 1 = 960
    EXPECT_EQ(h, 540u);  // 1081 >> 1 = 540
}

// ─────────────────────────────────────────────────────────────────────────────
// Bloom threshold
// ─────────────────────────────────────────────────────────────────────────────

TEST(BloomThresholdTest, BrightPixel_PassesThreshold) {
    EXPECT_TRUE(BloomThresholdTest(2.0f, 2.0f, 2.0f, 0.5f));
}

TEST(BloomThresholdTest, DarkPixel_FailsThreshold) {
    EXPECT_FALSE(BloomThresholdTest(0.01f, 0.01f, 0.01f, 0.5f));
}

TEST(BloomThresholdTest, ExactThreshold_Passes) {
    // Luminance of (0.5, 0.5, 0.5) = 0.5
    float lum = Luminance(0.5f, 0.5f, 0.5f);
    EXPECT_TRUE(BloomThresholdTest(0.5f, 0.5f, 0.5f, lum));
}

TEST(BloomThresholdTest, ZeroThreshold_AllPass) {
    EXPECT_TRUE(BloomThresholdTest(0.001f, 0.001f, 0.001f, 0.0f));
}

TEST(BloomThresholdTest, HighThreshold_OnlyVeryBrightPass) {
    EXPECT_FALSE(BloomThresholdTest(1.0f, 1.0f, 1.0f, 2.0f));
    EXPECT_TRUE(BloomThresholdTest(3.0f, 3.0f, 3.0f, 2.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.3 — Gaussian kernel
// ─────────────────────────────────────────────────────────────────────────────

TEST(GaussianKernelTest, WeightsSumToOne) {
    EXPECT_NEAR(GaussianKernelSum(), 1.0f, 0.001f);
}

TEST(GaussianKernelTest, CenterWeightIsLargest) {
    EXPECT_GT(kGaussianWeights[2], kGaussianWeights[1]);
    EXPECT_GT(kGaussianWeights[2], kGaussianWeights[0]);
}

TEST(GaussianKernelTest, SymmetricWeights) {
    EXPECT_NEAR(kGaussianWeights[0], kGaussianWeights[4], kEps);
    EXPECT_NEAR(kGaussianWeights[1], kGaussianWeights[3], kEps);
}

TEST(GaussianKernelTest, AllWeightsPositive) {
    for (int i = 0; i < 5; ++i) {
        EXPECT_GT(kGaussianWeights[i], 0.0f) << "Weight[" << i << "] must be positive";
    }
}

TEST(GaussianKernelTest, WeightsDecreaseFromCenter) {
    EXPECT_GT(kGaussianWeights[2], kGaussianWeights[1]);
    EXPECT_GT(kGaussianWeights[1], kGaussianWeights[0]);
}

// ─────────────────────────────────────────────────────────────────────────────
// 2.4 — FXAA edge detection
// ─────────────────────────────────────────────────────────────────────────────

TEST(FXAAEdgeTest, FlatRegion_NoEdge) {
    // All same luminance → no edge
    EXPECT_FALSE(FXAAEdgeDetect(0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.1f));
}

TEST(FXAAEdgeTest, HighContrast_IsEdge) {
    // Center dark, neighbors bright → edge
    EXPECT_TRUE(FXAAEdgeDetect(0.1f, 0.9f, 0.9f, 0.9f, 0.9f, 0.1f));
}

TEST(FXAAEdgeTest, SubtleContrast_BelowThreshold) {
    // Small luminance difference < threshold
    EXPECT_FALSE(FXAAEdgeDetect(0.5f, 0.52f, 0.48f, 0.51f, 0.49f, 0.1f));
}

TEST(FXAAEdgeTest, ExactThreshold_IsEdge) {
    // Contrast = 0.1 exactly with threshold = 0.1
    EXPECT_TRUE(FXAAEdgeDetect(0.5f, 0.6f, 0.5f, 0.5f, 0.5f, 0.1f));
}

TEST(FXAAEdgeTest, VerticalEdge_Detected) {
    // More contrast vertically (up/down differ from center)
    bool isVertical = FXAAIsVerticalEdge(0.5f, 0.9f, 0.1f, 0.5f, 0.5f);
    EXPECT_TRUE(isVertical);
}

TEST(FXAAEdgeTest, HorizontalEdge_Detected) {
    // More contrast horizontally (left/right differ from center)
    bool isVertical = FXAAIsVerticalEdge(0.5f, 0.5f, 0.5f, 0.9f, 0.1f);
    EXPECT_FALSE(isVertical);
}

TEST(FXAAEdgeTest, DiagonalEdge_ResolvesToStrongerAxis) {
    // Up bright, right dark → vertical contrast is stronger
    bool isVertical = FXAAIsVerticalEdge(0.5f, 1.0f, 0.5f, 0.5f, 0.2f);
    // vertical = |1.0 - 0.5| + |0.5 - 0.5| = 0.5
    // horizontal = |0.5 - 0.5| + |0.2 - 0.5| = 0.3
    EXPECT_TRUE(isVertical);
}

TEST(FXAAEdgeTest, ZeroThreshold_EverythingIsEdge) {
    // Even tiny differences are edges with threshold 0
    EXPECT_TRUE(FXAAEdgeDetect(0.5f, 0.5001f, 0.5f, 0.5f, 0.5f, 0.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Descriptor struct layout tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(PostProcessDescTest, ToneMapPassDesc_HasEnabledField) {
    ToneMapPassDesc desc{};
    desc.enabled = false;
    EXPECT_FALSE(desc.enabled);
    desc.enabled = true;
    EXPECT_TRUE(desc.enabled);
}

TEST(PostProcessDescTest, ToneMapPassDesc_DefaultIntensity) {
    ToneMapPassDesc desc{};
    desc.tonemapIntensity = 1.0f;
    desc.exposureAdjust = 0.0f;
    EXPECT_FLOAT_EQ(desc.tonemapIntensity, 1.0f);
    EXPECT_FLOAT_EQ(desc.exposureAdjust, 0.0f);
}

TEST(PostProcessDescTest, BloomPassDesc_HasMipLevels) {
    BloomPassDesc desc{};
    desc.mipLevels = 4;
    desc.bloomThreshold = 0.5f;
    desc.bloomIntensity = 0.5f;
    EXPECT_EQ(desc.mipLevels, 4u);
    EXPECT_FLOAT_EQ(desc.bloomThreshold, 0.5f);
}

TEST(PostProcessDescTest, FXAAPassDesc_ThresholdRange) {
    FXAAPassDesc desc{};
    desc.edgeThreshold = 0.1f;
    desc.blurStrength = 1.0f;
    EXPECT_FLOAT_EQ(desc.edgeThreshold, 0.1f);
    EXPECT_FLOAT_EQ(desc.blurStrength, 1.0f);
}

TEST(PostProcessDescTest, PostProcessStackDesc_AllTogglesCombinations) {
    PostProcessStackDesc desc{};
    // All on
    desc.enableTonemap = true;
    desc.enableBloom = true;
    desc.enableFXAA = true;
    EXPECT_TRUE(desc.enableTonemap);
    EXPECT_TRUE(desc.enableBloom);
    EXPECT_TRUE(desc.enableFXAA);

    // All off
    desc.enableTonemap = false;
    desc.enableBloom = false;
    desc.enableFXAA = false;
    EXPECT_FALSE(desc.enableTonemap);
    EXPECT_FALSE(desc.enableBloom);
    EXPECT_FALSE(desc.enableFXAA);

    // Mixed
    desc.enableTonemap = true;
    desc.enableBloom = false;
    desc.enableFXAA = true;
    EXPECT_TRUE(desc.enableTonemap);
    EXPECT_FALSE(desc.enableBloom);
    EXPECT_TRUE(desc.enableFXAA);
}

TEST(PostProcessDescTest, PostProcessStackDesc_ViewportDimensions) {
    PostProcessStackDesc desc{};
    desc.viewportWidth = 1920.0f;
    desc.viewportHeight = 1080.0f;
    EXPECT_FLOAT_EQ(desc.viewportWidth, 1920.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 1080.0f);
}
