/// Unit tests for the Cascaded Shadow Map frustum-splitting system.
///
/// Validates ShadowConfig defaults (UE5 parity), ShadowCascade defaults,
/// ShadowDataCB layout, and the correctness of ComputeCascades — split
/// distances, monotonicity, viewProj invertibility, texel sizes, exponential
/// distribution, per-cascade bias, cascade blending overlap, and maxDistance
/// clamping. No GPU, no window, no graphics API required.

#include "shadow_cascade.h"
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
// ShadowConfig defaults — UE5 parity values
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

// ─────────────────────────────────────────────────────────────────────────────
// ShadowDataCB layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowDataCB, SizeIs512Bytes) {
    EXPECT_EQ(sizeof(ShadowDataCB), 512u);
}

TEST(ShadowDataCB, SizeIsMultipleOf256) {
    EXPECT_EQ(sizeof(ShadowDataCB) % 256u, 0u);
}

TEST(ShadowDataCB, FieldOffsets) {
    // cascadeViewProj[4] starts at 0 (4 * 64B = 256B).
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeViewProj), 0u);
    // cascadeSplits at 256
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeSplits), 256u);
    // cascadeFadeStarts at 272
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeFadeStarts), 272u);
    // cascadeDepthBias at 288
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeDepthBias), 288u);
    // cascadeNormalBias at 304
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeNormalBias), 304u);
    // shadowAtlasIndex at 320
    EXPECT_EQ(offsetof(ShadowDataCB, shadowAtlasIndex), 320u);
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeCount), 324u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowMapSize), 328u);
    EXPECT_EQ(offsetof(ShadowDataCB, receiverBias), 332u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowFadeStart), 336u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowFadeEnd), 340u);
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
// ComputeCascades — split distances (UE5 exponential accumulation)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SplitNear_FirstCascade_EqualsCameraNear) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[0].splitNear, 1.0f, 1e-4f);
}

TEST(ComputeCascades, SplitDistances_AreMonotonicallyIncreasing) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    // Last cascade should NOT be extended
    EXPECT_FLOAT_EQ(cascades[3].splitFar, cascades[3].splitFarFadeStart);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — per-cascade bias
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, ComputedDepthBias_AllPositive) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].computedDepthBias, 0.0f)
            << "Cascade " << i << " has non-positive depth bias";
    }
}

TEST(ComputeCascades, ComputedNormalBias_AllPositive) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].computedNormalBias, 0.0f)
            << "Cascade " << i << " has non-positive normal bias";
    }
}

TEST(ComputeCascades, ComputedDepthBias_IncreasesWithCascadeIndex) {
    // With cascadeBiasDistribution=1.0, far cascades get larger bias
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 1; i < 4; ++i) {
        EXPECT_GT(cascades[i].computedDepthBias, cascades[i - 1].computedDepthBias)
            << "Depth bias should increase from cascade " << (i - 1) << " to " << i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — viewProj matrices
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, ViewProj_AllCascades_AreFinite) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(IsFiniteMatrix(cascades[i].viewProj))
            << "Cascade " << i << " viewProj contains NaN or Inf";
    }
}

TEST(ComputeCascades, ViewProj_AllCascades_NonZeroDeterminant) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(cascades[i].texelSize, 0.0f)
            << "Cascade " << i << " has non-positive texelSize";
    }
}

TEST(ComputeCascades, TexelSize_IncreasesWithCascadeIndex) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

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
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgLow, lowRes);
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgHigh, highRes);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[0].splitNear, 1.0f, 1e-4f);
}

TEST(ComputeCascades, SingleCascade_SplitFar_EqualsMaxDistance) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    cfg.maxDistance = 100.0f;
    ShadowCascade cascades[1];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    // Single cascade = last cascade, so splitFar should equal splitFarFadeStart
    EXPECT_NEAR(cascades[0].splitFarFadeStart, 100.0f, 1e-4f);
}

TEST(ComputeCascades, SingleCascade_ViewProj_IsInvertible) {
    const auto tc = MakeStandardCamera();
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    ShadowCascade cascade[1];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascade);

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

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_TRUE(IsFiniteMatrix(cascades[i].viewProj))
            << "Cascade " << i << " viewProj is non-finite for small near/far";
        EXPECT_GT(std::abs(MatDet(cascades[i].viewProj)), 1e-8f)
            << "Cascade " << i << " viewProj is singular for small near/far";
        EXPECT_GT(cascades[i].texelSize, 0.0f)
            << "Cascade " << i << " texelSize is non-positive for small near/far";
    }
}
