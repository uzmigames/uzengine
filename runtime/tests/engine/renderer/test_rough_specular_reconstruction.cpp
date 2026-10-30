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
