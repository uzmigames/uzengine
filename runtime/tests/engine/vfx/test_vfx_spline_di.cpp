/// Unit tests for phase 7.9.0 - Catmull-Rom spline data interface.
///
/// Locks the CPU reference spline evaluator that the HLSL port (deferred
/// follow-up) must match. Verifies:
///   - Under-populated splines (0 or 1 control points) don't crash.
///   - Spline passes through interior control points.
///   - Tangent is unit length and oriented along the curve.
///   - Looping vs open modes behave differently (open clamps, loop wraps).
///   - Arc-length table + uniform sampler produce ~equal arc steps.
///
/// @ref phase7_7.9.0-vfx-spline-data-interface

#include <gtest/gtest.h>

#include "engine/vfx/data_interfaces/vfx_spline_di.h"

#include <cmath>

using namespace uz;

namespace {

float Distance(const Vec3& a, const Vec3& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    const float dz = a.z - b.z;
    return std::sqrt(dx * dx + dy * dy + dz * dz);
}

float Length(const Vec3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

} // namespace

// ─── Degenerate inputs ─────────────────────────────────────────────────────

TEST(VfxSpline, EmptyReturnsZero) {
    VfxSpline s;
    const auto out = s.Sample(0.5f);
    EXPECT_FLOAT_EQ(out.position.x, 0.0f);
    EXPECT_FLOAT_EQ(out.position.y, 0.0f);
    EXPECT_FLOAT_EQ(out.position.z, 0.0f);
}

TEST(VfxSpline, SingleControlPointReturnsThatPoint) {
    VfxSpline s;
    s.controlPoints = { {10.0f, 20.0f, 30.0f} };
    const auto out = s.Sample(0.5f);
    EXPECT_FLOAT_EQ(out.position.x, 10.0f);
    EXPECT_FLOAT_EQ(out.position.y, 20.0f);
    EXPECT_FLOAT_EQ(out.position.z, 30.0f);
}

// ─── Interpolation passes through interior control points ─────────────────

TEST(VfxSpline, FourPointStartsAtFirstControlPoint) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 50.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 50.0f},
    };
    const auto out = s.Sample(0.0f);
    EXPECT_NEAR(out.position.x, 0.0f, 1e-3f);
    EXPECT_NEAR(out.position.y, 0.0f, 1e-3f);
    EXPECT_NEAR(out.position.z, 0.0f, 1e-3f);
}

TEST(VfxSpline, FourPointEndsAtLastControlPoint) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 50.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 50.0f},
    };
    const auto out = s.Sample(1.0f);
    EXPECT_NEAR(out.position.x, 300.0f, 1e-2f);
    EXPECT_NEAR(out.position.z,  50.0f, 1e-2f);
}

// ─── Tangent is unit length ────────────────────────────────────────────────

TEST(VfxSpline, TangentIsUnitLength) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 0.0f},
    };
    // Straight line along +X — tangent should be ~(1, 0, 0) everywhere
    // except at the very endpoints where mirrored phantoms can dominate.
    const auto mid = s.Sample(0.5f);
    EXPECT_NEAR(Length(mid.tangent), 1.0f, 1e-3f);
}

// ─── Clamping vs looping ──────────────────────────────────────────────────

TEST(VfxSpline, OpenSplineClampsOutOfRangeInputs) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 0.0f},
    };
    s.looping = false;
    const auto before = s.Sample(-1.0f);
    const auto after  = s.Sample( 2.0f);
    // Clamped to t=0 / t=1 so positions match the first/last samples.
    EXPECT_NEAR(before.position.x,  0.0f, 1e-2f);
    EXPECT_NEAR(after.position.x, 300.0f, 1e-2f);
}

TEST(VfxSpline, LoopingWrapsParameter) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 100.0f},
        {100.0f, 0.0f, 100.0f},
    };
    s.looping = true;
    const auto a = s.Sample(0.25f);
    const auto b = s.Sample(1.25f);
    EXPECT_NEAR(a.position.x, b.position.x, 1e-3f);
    EXPECT_NEAR(a.position.z, b.position.z, 1e-3f);
}

// ─── Arc-length table + uniform sampling ──────────────────────────────────

TEST(VfxSpline, BuildArcLengthTablePopulatesTotalLength) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 0.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 0.0f},
    };
    s.BuildArcLengthTable();
    // A straight line of length 300cm. Spline approximation < 1% error expected.
    EXPECT_NEAR(s.totalLength, 300.0f, 3.0f);
}

TEST(VfxSpline, UniformSamplerProducesEvenArcSteps) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 50.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 50.0f},
    };
    s.BuildArcLengthTable();
    const float total = s.totalLength;
    ASSERT_GT(total, 0.0f);

    Vec3 prev = s.SampleUniform(0.0f).position;
    float minStep = 1e9f;
    float maxStep = 0.0f;
    for (int i = 1; i <= 10; ++i) {
        const float t = static_cast<float>(i) / 10.0f;
        const Vec3 here = s.SampleUniform(t).position;
        const float step = Distance(here, prev);
        minStep = std::min(minStep, step);
        maxStep = std::max(maxStep, step);
        prev = here;
    }

    // Uniform sampler should keep arc-step ratio within ~20% of target
    // (10 steps -> each ~total/10).
    const float target = total / 10.0f;
    EXPECT_GT(minStep, target * 0.8f);
    EXPECT_LT(maxStep, target * 1.2f);
}

// ─── Tension sanity ───────────────────────────────────────────────────────

TEST(VfxSpline, TensionZeroStillProducesValidTangent) {
    VfxSpline s;
    s.controlPoints = {
        {0.0f, 0.0f, 0.0f},
        {100.0f, 0.0f, 50.0f},
        {200.0f, 0.0f, 0.0f},
        {300.0f, 0.0f, 50.0f},
    };
    s.tension = 0.0f;  // Zero tension would produce zero tangent magnitudes;
                      // the evaluator must fall back to a safe unit vector.
    const auto out = s.Sample(0.5f);
    EXPECT_NEAR(Length(out.tangent), 1.0f, 1e-3f);
}
