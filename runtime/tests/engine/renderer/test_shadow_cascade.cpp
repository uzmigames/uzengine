/// Unit tests for the Cascaded Shadow Map frustum-splitting system.
///
/// Validates ShadowConfig defaults, ShadowCascade defaults, ShadowDataCB layout,
/// and the correctness of ComputeCascades — split distances, monotonicity,
/// contiguity, viewProj invertibility, texel sizes, lambda behaviour, and
/// maxDistance clamping.  No GPU, no window, no graphics API required.

#include "cascade.h"
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

/// Builds a standard camera + light setup used across most test cases.
///
/// Camera: 60° FOV, 1:1 aspect, near=0.1, far=1000.
/// Light: sun-like diagonal direction, normalised.
struct TestCamera {
    Mat4 view;
    Mat4 proj;
    Vec3 lightDir;
};

TestCamera MakeStandardCamera(float nearZ = 1.0f, float farZ = 500.0f) {
    TestCamera tc;
    // LookAt: eye offset from origin so the frustum is well-formed.
    tc.view = Mat4::LookAt(Vec3{0.0f, 5.0f, 20.0f}, Vec3{0.0f, 0.0f, 0.0f}, Vec3{0.0f, 1.0f, 0.0f});
    tc.proj = Mat4::Perspective(XMConvertToRadians(60.0f), 1.0f, nearZ, farZ);
    tc.lightDir = Vec3{0.5f, -1.0f, 0.5f}.Normalized();
    return tc;
}

/// Returns true when every component of the 4x4 matrix is finite (no NaN/Inf).
bool IsFiniteMatrix(const Mat4& m) {
    const float* p = reinterpret_cast<const float*>(&m.m);
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(p[i])) return false;
    }
    return true;
}

/// Computes the determinant of a 4x4 matrix via DirectXMath.
float MatDet(const Mat4& m) {
    XMMATRIX xm = XMLoadFloat4x4(&m.m);
    return XMVectorGetX(XMMatrixDeterminant(xm));
}

/// Returns a default ShadowConfig.
ShadowConfig DefaultConfig() {
    return ShadowConfig{};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// ShadowConfig defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(ShadowConfig, DefaultCascadeCount_Is4) {
    ShadowConfig cfg;
    EXPECT_EQ(cfg.cascadeCount, 4u);
}

TEST(ShadowConfig, DefaultResolution_Is2048) {
    ShadowConfig cfg;
    EXPECT_EQ(cfg.resolution, 2048u);
}

TEST(ShadowConfig, DefaultMaxDistance_Is100) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.maxDistance, 100.0f);
}

TEST(ShadowConfig, DefaultSplitLambda_Is0_75) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.splitLambda, 0.75f);
}

TEST(ShadowConfig, DefaultNormalBias_Is0_02) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.normalBias, 0.02f);
}

TEST(ShadowConfig, DefaultDepthBias_Is0_005) {
    ShadowConfig cfg;
    EXPECT_FLOAT_EQ(cfg.depthBias, 0.005f);
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

TEST(ShadowCascade, DefaultTexelSize_IsZero) {
    ShadowCascade cascade{};
    EXPECT_FLOAT_EQ(cascade.texelSize, 0.0f);
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
    // cascadeSplits follows immediately after (256B).
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeSplits), 256u);
    // shadowAtlasIndex follows Vec4 (16B) = offset 272.
    EXPECT_EQ(offsetof(ShadowDataCB, shadowAtlasIndex), 272u);
    EXPECT_EQ(offsetof(ShadowDataCB, cascadeCount),    276u);
    EXPECT_EQ(offsetof(ShadowDataCB, normalBias),      280u);
    EXPECT_EQ(offsetof(ShadowDataCB, depthBias),       284u);
    EXPECT_EQ(offsetof(ShadowDataCB, shadowMapSize),   288u);
}

TEST(ShadowDataCB, DefaultInitialization_AllZero) {
    ShadowDataCB cb{};
    EXPECT_EQ(cb.shadowAtlasIndex, 0u);
    EXPECT_EQ(cb.cascadeCount,     0u);
    EXPECT_FLOAT_EQ(cb.normalBias,    0.0f);
    EXPECT_FLOAT_EQ(cb.depthBias,     0.0f);
    EXPECT_FLOAT_EQ(cb.shadowMapSize, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — split distances
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SplitNear_FirstCascade_EqualsCameraNear) {
    const auto tc  = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();  // maxDistance = 100
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[0].splitNear, 1.0f, 1e-4f);
}

TEST(ComputeCascades, SplitFar_LastCascade_EqualsMin_CameraFar_MaxDistance) {
    // cameraFar=500, maxDistance=100 → last splitFar should be 100.
    const auto tc  = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg = DefaultConfig();  // maxDistance=100
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[3].splitFar, 100.0f, 1e-4f);
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

TEST(ComputeCascades, SplitDistances_AreContiguous) {
    // Each cascade's splitFar must equal the next cascade's splitNear.
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(cascades[i].splitFar, cascades[i + 1].splitNear, 1e-5f)
            << "Gap between cascade " << i << " and " << (i + 1);
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

TEST(ComputeCascades, ViewProj_AllCascades_NotIdentity) {
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    // Identity matrix has diagonal 1,1,1,1 and zeros elsewhere.
    // A valid light-space ortho matrix will differ substantially.
    for (uint32_t i = 0; i < 4; ++i) {
        const Mat4& vp = cascades[i].viewProj;
        // At least one off-diagonal must be non-trivial OR diagonal != identity.
        bool isIdentity = (std::abs(vp.m._11 - 1.0f) < 1e-4f &&
                           std::abs(vp.m._22 - 1.0f) < 1e-4f &&
                           std::abs(vp.m._33 - 1.0f) < 1e-4f &&
                           std::abs(vp.m._44 - 1.0f) < 1e-4f &&
                           std::abs(vp.m._12) < 1e-4f &&
                           std::abs(vp.m._13) < 1e-4f);
        EXPECT_FALSE(isIdentity) << "Cascade " << i << " viewProj is identity";
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
    // Far cascades cover a larger world area, so texels must be larger.
    const auto tc = MakeStandardCamera();
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, DefaultConfig(), cascades);

    for (uint32_t i = 1; i < 4; ++i) {
        EXPECT_GT(cascades[i].texelSize, cascades[i - 1].texelSize)
            << "texelSize did not increase from cascade " << (i - 1) << " to " << i;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — lambda parameter
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, Lambda0_ProducesLinearSplits) {
    const auto tc = MakeStandardCamera(1.0f, 100.0f);  // near=1, far matches maxDistance
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.resolution   = 2048;
    cfg.maxDistance  = 100.0f;
    cfg.splitLambda  = 0.0f;  // purely linear
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    // Linear splits: splits[i] = near + (far - near) * i/N
    // With near=1, far=100, N=4:
    //   splits[0]=1, splits[1]=25.75, splits[2]=50.5, splits[3]=75.25, splits[4]=100
    // Each interval width = (100-1)/4 = 24.75
    const float near = 1.0f;
    const float far  = 100.0f;
    const float expectedInterval = (far - near) / 4.0f;

    for (uint32_t i = 0; i < 4; ++i) {
        const float interval = cascades[i].splitFar - cascades[i].splitNear;
        EXPECT_NEAR(interval, expectedInterval, 0.01f)
            << "Cascade " << i << " interval not uniform for lambda=0";
    }
}

TEST(ComputeCascades, Lambda1_ProducesLogarithmicSplits) {
    // With lambda=1 the first cascade interval must be much smaller than the last.
    const auto tc = MakeStandardCamera(1.0f, 100.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.resolution   = 2048;
    cfg.maxDistance  = 100.0f;
    cfg.splitLambda  = 1.0f;  // purely logarithmic
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    const float firstInterval = cascades[0].splitFar - cascades[0].splitNear;
    const float lastInterval  = cascades[3].splitFar - cascades[3].splitNear;

    // Logarithmic: last interval must be substantially larger than first.
    EXPECT_GT(lastInterval, firstInterval * 2.0f)
        << "lambda=1 should produce much larger far intervals than near intervals";
}

TEST(ComputeCascades, Lambda0_Vs_Lambda1_FirstCascadeWidth_Differs) {
    // Linear first-cascade must be wider than logarithmic first-cascade.
    // (Logarithmic clusters more precision near the camera.)
    const auto tc = MakeStandardCamera(1.0f, 100.0f);
    ShadowConfig cfgLinear;
    cfgLinear.cascadeCount = 4;
    cfgLinear.maxDistance  = 100.0f;
    cfgLinear.splitLambda  = 0.0f;

    ShadowConfig cfgLog;
    cfgLog.cascadeCount = 4;
    cfgLog.maxDistance  = 100.0f;
    cfgLog.splitLambda  = 1.0f;

    ShadowCascade linear[4], log4[4];
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgLinear, linear);
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgLog,    log4);

    const float linearFirst = linear[0].splitFar - linear[0].splitNear;
    const float logFirst    = log4[0].splitFar   - log4[0].splitNear;

    EXPECT_GT(linearFirst, logFirst)
        << "Linear first cascade should be wider than logarithmic first cascade";
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — maxDistance clamping
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, MaxDistance_LessThan_CameraFar_ClampsLastSplit) {
    // cameraFar=500, maxDistance=50 → last splitFar == 50.
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance  = 50.0f;
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[3].splitFar, 50.0f, 1e-4f);
}

TEST(ComputeCascades, MaxDistance_GreaterThan_CameraFar_UsesCameraFar) {
    // cameraFar=200, maxDistance=1000 → last splitFar == 200.
    // Loose tolerance: near/far are extracted from the projection matrix via
    // floating-point division, so the recovered far may differ slightly.
    const auto tc = MakeStandardCamera(1.0f, 200.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance  = 1000.0f;
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[3].splitFar, 200.0f, 0.01f);
}

TEST(ComputeCascades, MaxDistance_EqualTo_CameraFar_IsExact) {
    const auto tc = MakeStandardCamera(1.0f, 300.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance  = 300.0f;
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[3].splitFar, 300.0f, 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — single cascade (edge case)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SingleCascade_SplitNear_EqualsCameraNear) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    cfg.maxDistance  = 100.0f;
    ShadowCascade cascades[1];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[0].splitNear, 1.0f, 1e-4f);
}

TEST(ComputeCascades, SingleCascade_SplitFar_EqualsMaxDistance) {
    const auto tc = MakeStandardCamera(1.0f, 500.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    cfg.maxDistance  = 100.0f;
    ShadowCascade cascades[1];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    EXPECT_NEAR(cascades[0].splitFar, 100.0f, 1e-4f);
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

TEST(ComputeCascades, SingleCascade_TexelSize_IsPositive) {
    const auto tc = MakeStandardCamera();
    ShadowConfig cfg;
    cfg.cascadeCount = 1;
    ShadowCascade cascade[1];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascade);

    EXPECT_GT(cascade[0].texelSize, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — small near/far (no degenerate matrices)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, SmallNearFar_NoDegenerate) {
    // near=0.1, far=10 — very tight frustum, should not blow up.
    const auto tc = MakeStandardCamera(0.1f, 10.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance  = 10.0f;
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

TEST(ComputeCascades, SmallNearFar_SplitsContiguous) {
    const auto tc = MakeStandardCamera(0.1f, 10.0f);
    ShadowConfig cfg;
    cfg.cascadeCount = 4;
    cfg.maxDistance  = 10.0f;
    ShadowCascade cascades[4];

    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfg, cascades);

    for (uint32_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(cascades[i].splitFar, cascades[i + 1].splitNear, 1e-5f)
            << "Non-contiguous cascades at " << i << "/" << (i + 1) << " for small far";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeCascades — texel size matches resolution
// ─────────────────────────────────────────────────────────────────────────────

TEST(ComputeCascades, HigherResolution_ProducesSmallerTexels) {
    // Same scene, double the resolution → half the texel size.
    const auto tc = MakeStandardCamera();

    ShadowConfig cfgLow;
    cfgLow.cascadeCount = 4;
    cfgLow.resolution   = 512;
    cfgLow.maxDistance  = 100.0f;

    ShadowConfig cfgHigh;
    cfgHigh.cascadeCount = 4;
    cfgHigh.resolution   = 1024;
    cfgHigh.maxDistance  = 100.0f;

    ShadowCascade lowRes[4], highRes[4];
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgLow,  lowRes);
    ComputeCascades(tc.view, tc.proj, tc.lightDir, cfgHigh, highRes);

    for (uint32_t i = 0; i < 4; ++i) {
        EXPECT_GT(lowRes[i].texelSize, highRes[i].texelSize)
            << "Cascade " << i << ": lower resolution should have larger texels";
    }
}
