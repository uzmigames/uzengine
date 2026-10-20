#include "engine/renderer/passes/pathtracing/nrd_denoiser.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>

namespace {
using std::abs;
using std::exp;
using std::max;
using std::pow;
#include "../../../shaders/runtime/pathtracing/nrd_math.hlsli"
}

TEST(NrdStage1, ClipToPreviousUVUsesPerspectiveDivideAndFlippedY) {
    EXPECT_FLOAT_EQ(NrdClipToU(-0.2f, 2.0f), 0.45f);
    EXPECT_FLOAT_EQ(NrdClipToV(0.6f, 2.0f), 0.35f);
}

TEST(NrdStage1, PixelVelocityReprojectsCurrentMinusPrevious) {
    const float u = NrdClipToU(0.4f, 2.0f);
    const float v = NrdClipToV(0.2f, 2.0f);
    EXPECT_NEAR(NrdHistoryUV(u, 96.0f, 640.0f), NrdClipToU(-0.2f, 2.0f), 1e-6f);
    EXPECT_NEAR(NrdHistoryUV(v, 36.0f, 360.0f), NrdClipToV(0.6f, 2.0f), 1e-6f);
}

TEST(NrdStage1, AtrousDepthWeightStopsEdgesAndIsSymmetric) {
    EXPECT_FLOAT_EQ(NrdDepthWeight(0.0f, 0.02f), 1.0f);
    EXPECT_LT(NrdDepthWeight(0.2f, 0.02f), 0.0001f);
    EXPECT_FLOAT_EQ(NrdDepthWeight(0.1f, 0.02f), NrdDepthWeight(-0.1f, 0.02f));
    EXPECT_TRUE(std::isfinite(NrdDepthWeight(0.0f, 0.0f)));
}

TEST(NrdStage1, AtrousNormalWeightStopsOrthogonalAndOppositeSurfaces) {
    EXPECT_FLOAT_EQ(NrdNormalWeight(1.0f), 1.0f);
    EXPECT_FLOAT_EQ(NrdNormalWeight(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(NrdNormalWeight(-1.0f), 0.0f);
    EXPECT_GT(NrdNormalWeight(0.99f), NrdNormalWeight(0.9f));
}

TEST(NrdStage1, PrivateRootsDoNotGrowSharedGraphicsConstants) {
    EXPECT_EQ(sizeof(uz::NrdRoot), 16u * sizeof(uint32_t));
    EXPECT_EQ(sizeof(uz::NrdSignalRoot), 8u * sizeof(uint32_t));
}
