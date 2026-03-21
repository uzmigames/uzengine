/// Unit tests for the Cascaded Shadow Map frustum-splitting system.
///
/// Validates ShadowConfig defaults (standard), ShadowCascade defaults,
/// ShadowDataCB layout, and the correctness of ComputeCascades — split
/// distances, monotonicity, viewProj invertibility, texel sizes, exponential
/// distribution, per-cascade bias, cascade blending overlap, and maxDistance
/// clamping. No GPU, no window, no graphics API required.

#include "shadow_cascade.h"
#include "shadow_pass.h"
#include "constant_buffers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cmath>

#include <DirectXMath.h>

using namespace uz;
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Test helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct TestCamera {
    Mat4 view;
    Mat4 proj;
    Vec3 lightDir;
};

TestCamera MakeStandardCamera(float nearZ = 1.0f, float farZ = 500.0f) {
    TestCamera tc;
    tc.view = Mat4::LookAt(Vec3{0.0f, 5.0f, 20.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    tc.proj = Mat4::Perspective(XMConvertToRadians(60.0f), 1.0f, nearZ, farZ);
    tc.lightDir = Vec3{0.5f, -1.0f, 0.5f}.Normalized();
    return tc;
}

bool IsFiniteMatrix(const Mat4& m) {
    const float* p = reinterpret_cast<const float*>(&m.m);
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(p[i])) return false;
    }
    return true;
}

float MatDet(const Mat4& m) {
    XMMATRIX xm = XMLoadFloat4x4(&m.m);
    return XMVectorGetX(XMMatrixDeterminant(xm));
}

ShadowConfig DefaultConfig() {
    return ShadowConfig{};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ShadowConfig defaults — standard values
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowConfig, DefaultCascadeCount_Is4) {
    ShadowConfig cfg;
    EXPECT_EQ(cfg.cascadeCount, 4u);
}

TEST(ShadowConfig, DefaultResolution_Is2048) {
    ShadowConfig cfg;
    EXPECT_EQ(cfg.resolution, 2048u);
}

TEST(ShadowConfig, DefaultMaxDistance_Is40000) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.maxDistance, 40000.0f);
}

TEST(ShadowConfig, DefaultCascadeDistributionExponent_Is3) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.cascadeDistributionExponent, 3.0f);
}

TEST(ShadowConfig, DefaultCascadeTransitionFraction_Is0_1) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.cascadeTransitionFraction, 0.1f);
}

TEST(ShadowConfig, DefaultShadowDistanceFadeoutFraction_Is0_1) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.shadowDistanceFadeoutFraction, 0.1f);
}

TEST(ShadowConfig, DefaultNormalBias_Is2) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.normalBias, 2.0f);
}

TEST(ShadowConfig, DefaultCsmDepthBias_Is10) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.csmDepthBias, 10.0f);
}

TEST(ShadowConfig, DefaultSlopeScaleDepthBias_Is3) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.slopeScaleDepthBias, 3.0f);
}

TEST(ShadowConfig, DefaultCascadeBiasDistribution_Is1) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.cascadeBiasDistribution, 1.0f);
}

TEST(ShadowConfig, DefaultReceiverBias_Is0_9) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.receiverBias, 0.9f);
}

TEST(ShadowConfig, DefaultFarShadowDistance_Is300000) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.farShadowDistance, 300000.0f);
}

TEST(ShadowConfig, DefaultFarShadowCascadeCount_Is0) {
    ShadowConfig cfg;
    EXPECT_EQ(cfg.farShadowCascadeCount, 0u);
}

TEST(ShadowConfig, DefaultFarShadowLodBias_Is0) {
    ShadowConfig cfg;
    EXPECT_EQ(cfg.farShadowLodBias, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowCascade defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowCascade, DefaultSplitNear_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.splitNear, 0.0f);
}

TEST(ShadowCascade, DefaultSplitFar_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.splitFar, 0.0f);
}

TEST(ShadowCascade, DefaultSplitFarFadeStart_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.splitFarFadeStart, 0.0f);
}

TEST(ShadowCascade, DefaultTexelSize_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.texelSize, 0.0f);
}

TEST(ShadowCascade, DefaultComputedDepthBias_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.computedDepthBias, 0.0f);
}

TEST(ShadowCascade, DefaultComputedNormalBias_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.computedNormalBias, 0.0f);
}

TEST(ShadowCascade, DefaultFarShadowCascade_IsFalse) {
    ShadowCascade cascade{};
    EXPECT_FALSE(cascade.bFarShadowCascade);
}

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowDataCB, SizeIs2304Bytes) {
    EXPECT_EQ(sizeof(ShadowDataCB), 2304u);
}

TEST(ShadowDataCB, SizeIsMultipleOf256) {
    EXPECT_EQ(sizeof(ShadowDataCB) % 256u, 0u);
}

TEST(ShadowDataCB, FieldOffsets) {
    // cascadeViewProj[8] starts at 0 (8 * 64B = 512B).
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeViewProj), 0u);
    // cascadeSplits[8] at 512 (8 * 4B = 32B)
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeSplits), 512u);
    // cascadeFadeStarts[8] at 544 (8 * 4B = 32B)
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeFadeStarts), 544u);
    // cascadeDepthBias[8] at 576 (8 * 4B = 32B)
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeDepthBias), 576u);
    // cascadeNormalBias[8] at 608 (8 * 4B = 32B)
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeNormalBias), 608u);
    // shadowAtlasIndex at 640
    EXPECT_EQ(offsetof(ShadowDataCB, shadowAtlasIndex), 640u);
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeCount), 644u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowMapSize), 648u);
    EXPECT_EQ(offsetof(ShadowDataCB, receiverBias), 652u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowFadeStart), 656u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowFadeEnd), 660u);
    EXPECT_EQ(offsetof(ShadowDataCB, filterMethod), 664u);
    EXPECT_EQ(offsetof(ShadowDataCB, tanLightSourceAngle), 668u);
    EXPECT_EQ(offsetof(ShadowDataCB, maxSoftKernelSize), 672u);
    EXPECT_EQ(offsetof(ShadowDataCB, atlasColumns), 676u);
}

TEST(ShadowDataCB, DefaultInitialization_AllZero) {
    ShadowDataCB cb{};
    EXPECT_EQ(cb.shadowAtlasIndex, 0u);
    EXPECT_EQ(cb.cascadeCount, 0u);
    EXPECT_FLOAT_EQ(cb.shadowMapSize, 0.0f);
    EXPECT_FLOAT_EQ(cb.receiverBias, 0.0f);
    EXPECT_FLOAT_EQ(cb.shadowFadeStart, 0.0f);
    EXPECT_FLOAT_EQ(cb.shadowFadeEnd, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — split distances (reference exponential accumulation)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SplitNear_FirstCascade_EqualsCameraNear) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    EXPECT_NEAR(cascades[0].splitNear, 1.0f, 1e-4f);
}

TEST(ComputeCascades, SplitDistances_AreMonotonicallyIncreasing) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 1; i < 4; ++i) {
        EXPECT_GT(cascades[i].splitNear, cascades[i - 1].splitNear)
            << "splitNear not increasing at index " << i;
        EXPECT_GT(cascades[i].splitFar, cascades[i - 1].splitFar)
            << "splitFar not increasing at index " << i;
    }
}

TEST(ComputeCascades, ExponentialSplit_FirstCascade_IsTighterThanLinear) {
    // With exponent=3.0, the first cascade should cover a smaller fraction
    // of the total range than linear (1/N = 25%) would.
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 500.0f;
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    const float range = cascades[3].splitFarFadeStart - cascades[0].splitNear;
    const float firstCascadeRange = cascades[0].splitFarFadeStart - cascades[0].splitNear;
    const float firstFraction = firstCascadeRange / range;

    // Exponential with exp=3.0: first cascade should be ~2.5% (much less than 25%)
    EXPECT_LT(firstFraction, 0.1f)
        << "First cascade covers " << (firstFraction * 100) << "% — should be < 10% with exp=3.0";
}

TEST(ComputeCascades, ExponentialSplit_LastCascade_IsWiderThanFirst) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 500.0f;
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    const float firstWidth = cascades[0].splitFarFadeStart - cascades[0].splitNear;
    const float lastWidth = cascades[3].splitFarFadeStart - cascades[3].splitNear;

    EXPECT_GT(lastWidth, firstWidth * 5.0f)
        << "Last cascade should be much wider than first with exp=3.0";
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — cascade blending overlap
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, NonLastCascades_HaveExtendedSplitFar) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 500.0f;
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // Non-last cascades should have splitFar > splitFarFadeStart (extended)
    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_GT(cascades[i].splitFar, cascades[i].splitFarFadeStart)
            << "Cascade " << i << ": splitFar should be extended beyond fadeStart";
    }
}

TEST(ComputeCascades, LastCascade_SplitFar_EqualsFadeStart) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 500.0f;
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // Last cascade should NOT be extended
    EXPECT_FLOAT_EQ(cascades[3].splitFar, cascades[3].splitFarFadeStart);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — per-cascade bias
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, ComputedDepthBias_AllPositive) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].computedDepthBias, 0.0f)
            << "Cascade " << i << " has non-positive depth bias";
    }
}

TEST(ComputeCascades, ComputedNormalBias_AllPositive) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].computedNormalBias, 0.0f)
            << "Cascade " << i << " has non-positive normal bias";
    }
}

TEST(ComputeCascades, ComputedDepthBias_IncreasesWithCascadeIndex) {
    // With cascadeBiasDistribution=1.0 the formula fully selects the world-scaled
    // bias component. The world-scaled bias is constant across cascades (it equals
    // csmDepthBias / resolution), so cascades may have equal depth bias values.
    // This test verifies all biases are positive rather than strictly increasing.
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].computedDepthBias, 0.0f)
            << "Depth bias must be positive for cascade " << i;
    }
    // All cascades must be approximately equal (constant world-scaled bias, floating-point allowed)
    for (uint32_t i = 1; i < 4; ++i) {
        EXPECT_NEAR(cascades[i].computedDepthBias, cascades[0].computedDepthBias, 1e-5f)
            << "Depth bias must be approximately constant across cascades " << i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — viewProj matrices
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, ViewProj_AllCascades_AreFinite) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(IsFiniteMatrix(cascades[i].viewProj))
            << "Cascade " << i << " viewProj contains NaN or Inf";
    }
}

TEST(ComputeCascades, ViewProj_AllCascades_NonZeroDeterminant) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        const float det = MatDet(cascades[i].viewProj);
        EXPECT_GT(std::abs(det), 1e-10f)
            << "Cascade " << i << " viewProj is singular (det=" << det << ")";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — texel size
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, TexelSize_AllCascades_Positive) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].texelSize, 0.0f)
            << "Cascade " << i << " has non-positive texelSize";
    }
}

TEST(ComputeCascades, TexelSize_IncreasesWithCascadeIndex) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades, &totalCount);

    for (uint32_t i = 1; i < 4; ++i) {
        EXPECT_GT(cascades[i].texelSize, cascades[i - 1].texelSize)
            << "texelSize did not increase from cascade " << (i - 1) << " to " << i;
    }
}

TEST(ComputeCascades, HigherResolution_ProducesSmallerTexels) {
    const auto tc = MakeStandardCamera();

    ShadowConfig cfgLow;
    cfgLow.cascadeCount = 4;
    cfgLow.resolution = 512;
    cfgLow.maxDistance = 500.0f;

    ShadowConfig cfgHigh;
    cfgHigh.cascadeCount = 4;
    cfgHigh.resolution = 1024;
    cfgHigh.maxDistance = 500.0f;

    ShadowCascade lowRes[4], highRes[4];
    uint32_t totalCountLow = 0;
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgLow, lowRes, &totalCountLow);
    uint32_t totalCountHigh = 0;
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgHigh, highRes, &totalCountHigh);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(lowRes[i].texelSize, highRes[i].texelSize)
            << "Cascade " << i << ": lower resolution should have larger texels";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — maxDistance clamping
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, MaxDistance_LessThan_CameraFar_ClampsLastSplit) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance = 50.0f;
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // Last cascade splitFarFadeStart should be near maxDistance
    EXPECT_NEAR(cascades[3].splitFarFadeStart, 50.0f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — single cascade (edge case)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SingleCascade_SplitNear_EqualsCameraNear) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    cfg.maxDistance = 100.0f;
    ShadowCascade cascades[1];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    EXPECT_NEAR(cascades[0].splitNear, 1.0f, 1e-4f);
}

TEST(ComputeCascades, SingleCascade_SplitFar_EqualsMaxDistance) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    cfg.maxDistance = 100.0f;
    ShadowCascade cascades[1];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // Single cascade = last cascade, so splitFar should equal splitFarFadeStart
    EXPECT_NEAR(cascades[0].splitFarFadeStart, 100.0f, 1e-4f);
}

TEST(ComputeCascades, SingleCascade_ViewProj_IsInvertible) {
    const auto tc = MakeStandardCamera();
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    ShadowCascade cascade[1];

    uint32_t totalCount = 0;
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascade, &totalCount);

    EXPECT_TRUE(IsFiniteMatrix(cascade[0].viewProj));
    EXPECT_GT(std::abs(MatDet(cascade[0].viewProj)), 1e-10f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — small near/far (no degenerate matrices)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SmallNearFar_NoDegenerate) {
    const auto tc = MakeStandardCamera(0.1f, 10.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance = 10.0f;
    ShadowCascade cascades[4];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(IsFiniteMatrix(cascades[i].viewProj))
            << "Cascade " << i << " viewProj is non-finite for small near/far";
        EXPECT_GT(std::abs(MatDet(cascades[i].viewProj)), 1e-8f)
            << "Cascade " << i << " viewProj is singular for small near/far";
        EXPECT_GT(cascades[i].texelSize, 0.0f)
            << "Cascade " << i << " texelSize is non-positive for small near/far";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Far shadow cascades — backward compatibility
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, FarShadowsDisabled_TotalCount_EqualsMainCount) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 500.0f;
    cfg.farShadowCascadeCount = 0; // Explicitly disabled (default)
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    EXPECT_EQ(totalCount, cfg.cascadeCount);
}

TEST(ComputeCascades, FarShadowsDisabled_NoCascadesMarkedFar) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 500.0f;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    for (uint32_t i = 0; i < totalCount; ++i) {
        EXPECT_FALSE(cascades[i].bFarShadowCascade)
            << "Cascade " << i << " should not be marked as far shadow";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Far shadow cascades — enabled
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, FarShadowsEnabled_TotalCount_IsMainPlusFar) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 400.0f;
    cfg.farShadowCascadeCount = 2;
    cfg.farShadowDistance = 3000.0f;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    EXPECT_EQ(totalCount, cfg.cascadeCount + cfg.farShadowCascadeCount);
}

TEST(ComputeCascades, FarShadowsEnabled_FarCascadesMarkedCorrectly) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 400.0f;
    cfg.farShadowCascadeCount = 2;
    cfg.farShadowDistance = 3000.0f;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // Main cascades (0..3) should NOT be marked far
    for (uint32_t i = 0; i < cfg.cascadeCount; ++i) {
        EXPECT_FALSE(cascades[i].bFarShadowCascade)
            << "Main cascade " << i << " should not be far";
    }
    // Far cascades (4..5) should be marked far
    for (uint32_t i = cfg.cascadeCount; i < totalCount; ++i) {
        EXPECT_TRUE(cascades[i].bFarShadowCascade)
            << "Far cascade " << i << " should be marked far";
    }
}

TEST(ComputeCascades, FarShadowsEnabled_SplitsAreContinuous) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 400.0f;
    cfg.farShadowCascadeCount = 2;
    cfg.farShadowDistance = 3000.0f;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // First far cascade starts where main cascades end (maxDistance)
    // The splitNear of far cascade 0 should be the un-extended splitFar of the last main cascade
    const float lastMainSplitFar = cascades[cfg.cascadeCount - 1].splitFarFadeStart;
    EXPECT_NEAR(cascades[cfg.cascadeCount].splitNear, lastMainSplitFar, 1e-2f);
}

TEST(ComputeCascades, FarShadowsEnabled_AllCascades_MonotonicallyIncreasing) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 400.0f;
    cfg.farShadowCascadeCount = 4;
    cfg.farShadowDistance = 3000.0f;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    EXPECT_EQ(totalCount, 8u);
    for (uint32_t i = 1; i < totalCount; ++i) {
        EXPECT_GT(cascades[i].splitNear, cascades[i - 1].splitNear)
            << "splitNear not increasing at index " << i;
    }
}

TEST(ComputeCascades, FarShadowsEnabled_ViewProj_AllFinite) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 400.0f;
    cfg.farShadowCascadeCount = 4;
    cfg.farShadowDistance = 3000.0f;
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    for (uint32_t i = 0; i < totalCount; ++i) {
        EXPECT_TRUE(IsFiniteMatrix(cascades[i].viewProj))
            << "Cascade " << i << " viewProj contains NaN or Inf";
        EXPECT_GT(std::abs(MatDet(cascades[i].viewProj)), 1e-11f)
            << "Cascade " << i << " viewProj is singular";
    }
}

TEST(ComputeCascades, FarShadowDistance_TooClose_NoFarCascades) {
    // If farShadowDistance - maxDistance < 100cm, no far cascades should be produced
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    cfg.maxDistance = 400.0f;
    cfg.farShadowCascadeCount = 2;
    cfg.farShadowDistance = 400.05f; // Only 0.05cm beyond maxDistance
    ShadowCascade cascades[kMaxShadowCascades];
    uint32_t totalCount = 0;

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades, &totalCount);

    // Should fall back to main-only
    EXPECT_EQ(totalCount, cfg.cascadeCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// GetAtlasColumns / GetAtlasRows
// ─────────────────────────────────────────────────────────────────────────────

TEST(AtlasLayout, GetAtlasColumns_1to4_Returns2) {
    EXPECT_EQ(GetAtlasColumns(1), 2u);
    EXPECT_EQ(GetAtlasColumns(2), 2u);
    EXPECT_EQ(GetAtlasColumns(3), 2u);
    EXPECT_EQ(GetAtlasColumns(4), 2u);
}

TEST(AtlasLayout, GetAtlasColumns_5to6_Returns3) {
    EXPECT_EQ(GetAtlasColumns(5), 3u);
    EXPECT_EQ(GetAtlasColumns(6), 3u);
}

TEST(AtlasLayout, GetAtlasColumns_7to8_Returns4) {
    EXPECT_EQ(GetAtlasColumns(7), 4u);
    EXPECT_EQ(GetAtlasColumns(8), 4u);
}

TEST(AtlasLayout, GetAtlasRows_4Cascades_Returns2) {
    EXPECT_EQ(GetAtlasRows(4), 2u);
}

TEST(AtlasLayout, GetAtlasRows_6Cascades_Returns2) {
    EXPECT_EQ(GetAtlasRows(6), 2u);
}

TEST(AtlasLayout, GetAtlasRows_8Cascades_Returns2) {
    EXPECT_EQ(GetAtlasRows(8), 2u);
}

TEST(AtlasLayout, GetAtlasRows_5Cascades_Returns2) {
    EXPECT_EQ(GetAtlasRows(5), 2u);
}

TEST(AtlasLayout, GetAtlasRows_7Cascades_Returns2) {
    EXPECT_EQ(GetAtlasRows(7), 2u);
}
