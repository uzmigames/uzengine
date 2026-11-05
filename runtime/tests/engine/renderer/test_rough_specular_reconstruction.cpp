#include <gtest/gtest.h>
#include "../../../shaders/runtime/denoising/rough_specular_math.hlsli"

using namespace uz::roughspecular;

TEST(RoughSpecularReconstruction, MirrorIsExcludedAndCurvedRoughNormalsHaveSupport) {
    EXPECT_FALSE(Reconstructs(0.0f));
    EXPECT_FALSE(Reconstructs(0.04f));
    EXPECT_TRUE(Reconstructs(0.4f));
    EXPECT_FLOAT_EQ(NormalPower(0.04f), 512.f);
    EXPECT_NEAR(NormalPower(0.4f), 50.f, 0.0001f);
    EXPECT_GT(std::pow(0.99f, NormalPower(0.4f)), 0.6f);
    EXPECT_LT(std::pow(0.99f, 512.f), 0.006f);
}

TEST(RoughSpecularReconstruction, SpatialFootprintUsesHitDistanceAndRoughness) {
    EXPECT_GT(SpatialRadius(0.4f, 0.f, 1000.f), SpatialRadius(0.4f, 10.f, 1000.f));
    EXPECT_GT(SpatialRadius(0.8f, 1000.f, 1000.f), SpatialRadius(0.1f, 1000.f, 1000.f));
    EXPECT_LE(SpatialRadius(1.f, 0.f, 1000.f), 3.f);
}

TEST(RoughSpecularReconstruction, VarianceClampPreservesMeanAndRespondsToMotion) {
    EXPECT_FLOAT_EQ(ClampHistory(12.f, 12.f, 0.f, 0.f), 12.f);
    EXPECT_FLOAT_EQ(ClampHistory(0.f, 0.f, 0.f, 0.f), 0.f);
    EXPECT_FLOAT_EQ(ClampHistory(100.f, 10.f, 4.f, 0.f), 16.f);
    EXPECT_FLOAT_EQ(ClampHistory(100.f, 10.f, 4.f, 1.f), 12.f);
    EXPECT_NEAR(ClampHistory(100.f, 10.f, 4.f / 8.f, 0.f), 12.121320f, 0.00001f);
    EXPECT_GT(HistoryCap(0.4f, 0.f), 64.f);
    EXPECT_FLOAT_EQ(HistoryCap(0.4f, 1.f), 4.f);
    // Normalized spatial and temporal reuse preserve a constant HDR estimator.
    float estimate = 12.f;
    for (int i = 1; i < 150; ++i) {
        float alpha = 1.f / std::min(float(i + 1), HistoryCap(0.4f, 0.f));
        estimate = (1.f - alpha) * estimate + alpha * 12.f;
    }
    EXPECT_NEAR(estimate, 12.f, 0.0001f);
}

// Motion-aware reconstruction (doc 09 section 4.8): the thin lens, the parallax cap, the short-history
// firefly limit and the curvature gate; the OFF semantics above are untouched.
TEST(RoughSpecularReconstruction, ThinLensImageDistanceFollowsCurvatureAndHitDistance) {
    EXPECT_NEAR(ThinLensDistance(0.f, 1.f / 100.f), 100.f, 0.001f);   // plane: the planar virtual point
    EXPECT_NEAR(ThinLensDistance(1.f / 50.f, 0.f), 25.f, 0.001f);     // sphere, miss: rho / 2
    EXPECT_LT(ThinLensDistance(0.f, 0.f), 0.f);                        // plane, miss: infinite sentinel
    EXPECT_GT(ThinLensDistance(1.f / 50.f, 0.f), ThinLensDistance(1.f / 25.f, 0.f));
    EXPECT_GT(ThinLensDistance(1.f / 50.f, 0.f), ThinLensDistance(1.f / 50.f, 1.f / 100.f));
}

TEST(RoughSpecularReconstruction, MotionCapIsParallaxDrivenWithTodaysFloor) {
    EXPECT_NEAR(LobeHalfAngle(0.4f), 0.3173f, 0.001f);
    EXPECT_FLOAT_EQ(MotionCap(0.4f, 0.f, 2.f), HistoryCap(0.4f, 0.f));
    EXPECT_FLOAT_EQ(MotionCap(0.4f, LobeHalfAngle(0.4f), 2.f), 4.f);
    EXPECT_GE(MotionCap(0.4f, 0.035f, 2.f), 58.f);
    EXPECT_LE(MotionCap(0.4f, 0.035f, 2.f), 64.f);
    EXPECT_GT(MotionCap(0.8f, 0.008f, 2.f), 120.f);
    EXPECT_FLOAT_EQ(MotionCap(0.1f, 1.f, 2.f), 4.f);
}

TEST(RoughSpecularReconstruction, FireflyLimitRelaxesWithHistoryAndScalesWithReference) {
    EXPECT_FLOAT_EQ(FireflyLimit(1.f, 0.f, 8.f, 8.f), 8.f);
    EXPECT_FLOAT_EQ(FireflyLimit(1.f, 4.f, 8.f, 8.f), 8.f);
    EXPECT_FLOAT_EQ(FireflyLimit(1.f, 5.f, 8.f, 8.f), 16.f);
    EXPECT_FLOAT_EQ(FireflyLimit(1.f, 7.f, 8.f, 8.f), 64.f);
    EXPECT_GT(FireflyLimit(1.f, 8.f, 8.f, 8.f), 1e30f);
    EXPECT_FLOAT_EQ(FireflyLimit(2.5f, 0.f, 8.f, 8.f), 20.f);
}

TEST(RoughSpecularReconstruction, CurvatureGateKeepsTheReportSphereAndTightensSmallReceivers) {
    EXPECT_GT(CurvatureRadiusLimit(0.4f, 0.02f, 0.53f), 2.28f);
    EXPECT_LT(CurvatureRadiusLimit(0.4f, 0.2f, 0.53f), 1.0f);
    EXPECT_GT(CurvatureRadiusLimit(0.4f, 0.f, 0.53f), 100.f);
}
