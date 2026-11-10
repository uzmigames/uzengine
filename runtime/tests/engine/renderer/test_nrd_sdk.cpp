#include "engine/renderer/passes/pathtracing/nrd_sdk_denoiser.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cmath>

namespace {
using std::abs;
using std::exp2;
using std::max;
using std::pow;
#include "../../../shaders/runtime/pathtracing/nrd_sdk_math.hlsli"
}

// Reference values follow NRD.hlsli v4.17.3 (_NRD_GetSpecMagicCurve,
// _REBLUR_GetHitDistanceNormalization, REBLUR_FrontEnd_GetNormHitDist, YCoCg).
TEST(NrdSdk, SpecMagicCurveSpansMirrorToFullyRough) {
    EXPECT_FLOAT_EQ(NrdSdkSpecMagicCurve(0.f, .5f), 0.f);
    EXPECT_NEAR(NrdSdkSpecMagicCurve(1.f, .5f), 1.f, 1e-6f);
    for (int i = 1; i <= 100; ++i)
        EXPECT_GE(NrdSdkSpecMagicCurve(i / 100.f, .5f), NrdSdkSpecMagicCurve((i - 1) / 100.f, .5f));
}

TEST(NrdSdk, HitDistanceNormalizationMatchesNrdDefaults) {
    // Diffuse (roughness 1): f = A + |viewZ| B; the C scale only applies to mirror lobes.
    EXPECT_NEAR(NrdSdkHitDistNormalization(0.f, 3.f, .1f, 20.f, 1.f), 3.f, 1e-5f);
    EXPECT_NEAR(NrdSdkHitDistNormalization(10.f, 3.f, .1f, 20.f, 1.f), 4.f, 1e-5f);
    EXPECT_NEAR(NrdSdkHitDistNormalization(-10.f, 3.f, .1f, 20.f, 1.f), 4.f, 1e-5f);
    EXPECT_FLOAT_EQ(NrdSdkHitDistNormalization(10.f, 3.f, .1f, 20.f, 0.f), 80.f);
    // Scaling the metric defaults by units-per-meter scales the normalization the same way.
    EXPECT_NEAR(NrdSdkHitDistNormalization(1000.f, 300.f, .1f, 20.f, 1.f),
                100.f * NrdSdkHitDistNormalization(10.f, 3.f, .1f, 20.f, 1.f), 1e-3f);
}

TEST(NrdSdk, NormalizedHitDistanceSaturatesAndNeverReportsNoData) {
    EXPECT_FLOAT_EQ(NrdSdkNormHitDist(0.f, 4.f), 1e-6f);
    EXPECT_FLOAT_EQ(NrdSdkNormHitDist(2.f, 4.f), .5f);
    EXPECT_FLOAT_EQ(NrdSdkNormHitDist(10000.f, 4.f), 1.f);
}

TEST(NrdSdk, YCoCgRoundTripIsIdentityForNonNegativeRadiance) {
    const float colors[][3] = {{0.f, 0.f, 0.f}, {1.f, 1.f, 1.f}, {.2f, .7f, .1f}, {12.f, .5f, 3.f}};
    for (const auto& c : colors) {
        const float y = NrdSdkLumaY(c[0], c[1], c[2]);
        const float co = NrdSdkChromaCo(c[0], c[1], c[2]);
        const float cg = NrdSdkChromaCg(c[0], c[1], c[2]);
        EXPECT_NEAR(NrdSdkLinearR(y, co, cg), c[0], 1e-5f);
        EXPECT_NEAR(NrdSdkLinearG(y, co, cg), c[1], 1e-5f);
        EXPECT_NEAR(NrdSdkLinearB(y, co, cg), c[2], 1e-5f);
    }
    EXPECT_FLOAT_EQ(NrdSdkLumaY(1.f, 1.f, 1.f), 1.f);
}

TEST(NrdSdk, AdapterRootFitsD3D12BudgetWithoutGrowingSharedRoots) {
    EXPECT_EQ(sizeof(uz::NrdSdkRoot), 56u * sizeof(uint32_t));
    EXPECT_LE(sizeof(uz::NrdSdkRoot) / 4, 64u);
    EXPECT_EQ(sizeof(uz::NrdRoot), 16u * sizeof(uint32_t));
    EXPECT_EQ(sizeof(uz::NrdSignalRoot), 8u * sizeof(uint32_t));
}
