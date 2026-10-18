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

TEST(NrdStage2, RoughnessWidensSpecularRadiusMonotonically) {
    EXPECT_FLOAT_EQ(NrdSpecularRadius(0.f), 1.f);
    EXPECT_FLOAT_EQ(NrdSpecularRadius(1.f), 4.f);
    for (int i = 1; i <= 100; ++i)
        EXPECT_GT(NrdSpecularRadius(i / 100.f), NrdSpecularRadius((i - 1) / 100.f));
}

TEST(NrdStage2, RoughnessSlowsTemporalAccumulationMonotonically) {
    EXPECT_FLOAT_EQ(NrdSpecularHistory(0.f), 2.f);
    EXPECT_FLOAT_EQ(NrdSpecularHistory(1.f), 32.f);
    EXPECT_FLOAT_EQ(NrdSpecularHistory(.5f), 9.5f);
    for (int i = 1; i <= 100; ++i)
        EXPECT_LT(1.f / NrdSpecularHistory(i / 100.f),
                  1.f / NrdSpecularHistory((i - 1) / 100.f));
}

TEST(NrdStage2, HistoryClampUsesMeanPlusMinusKSigma) {
    EXPECT_FLOAT_EQ(NrdClampHistory(100.f, 10.f, 2.f, 3.f), 16.f);
    EXPECT_FLOAT_EQ(NrdClampHistory(-100.f, 10.f, 2.f, 3.f), 4.f);
    EXPECT_FLOAT_EQ(NrdClampHistory(12.f, 10.f, 2.f, 3.f), 12.f);
    EXPECT_FLOAT_EQ(NrdClampHistory(100.f, 10.f, 0.f, 3.f), 10.f);
    EXPECT_FLOAT_EQ(NrdClampHistory(100.f, 10.f, 2.f, .5f), 11.f);
}

TEST(NrdStage2, FullyRoughNormalGateIsFiniteAtOrthogonalEdges) {
    EXPECT_FLOAT_EQ(NrdSpecularNormalWeight(0.f, 1.f), 1.f);
    EXPECT_FLOAT_EQ(NrdSpecularNormalWeight(1.f, 0.f), 1.f);
    EXPECT_LT(NrdSpecularNormalWeight(0.f, 0.f), 1e-6f);
    for (int i = 0; i <= 100; ++i)
        EXPECT_TRUE(std::isfinite(NrdSpecularNormalWeight(0.f, i / 100.f)));
}

TEST(NrdStage2, VirtualRayUnfoldsReflectedHitThroughTangentPlane) {
    // Plane y=0, reflected outgoing direction (0,.8,.6), distance=10:
    // virtual hit is behind the plane, not on the visible reflected object.
    float x = NrdVirtualComponent(1.f, 0.f, 0.f, .8f, 10.f);
    float y = NrdVirtualComponent(0.f, .8f, 1.f, .8f, 10.f);
    float z = NrdVirtualComponent(2.f, .6f, 0.f, .8f, 10.f);
    EXPECT_FLOAT_EQ(x, 1.f);
    EXPECT_FLOAT_EQ(y, -8.f);
    EXPECT_FLOAT_EQ(z, 8.f);
    EXPECT_LT(NrdClipToV(y, 20.f) - NrdClipToV(y, 10.f), 0.f);
}

TEST(NrdStage2, PrivateComputeRootFitsD3D12Budget) {
    EXPECT_EQ(sizeof(uz::NrdStage2Root), 24u * sizeof(uint32_t));
    EXPECT_LE((sizeof(uz::NrdRoot) + sizeof(uz::NrdStage2Root)) / 4 + 2, 64u);
}
