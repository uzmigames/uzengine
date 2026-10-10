/// Unit tests for phase 8.14.0 — VFX trail ribbon morphology.
///
/// Validates the curve sampling helper, the IsIdentity short-circuit, and
/// the twist-rotation math via a CPU mirror.
///
/// @ref engine/vfx/trails/trail_mesh_builder.h
/// @ref docs/engine/25-vfx/31-ribbon-morphology.md

#include <gtest/gtest.h>

#include "engine/vfx/trails/trail_mesh_builder.h"

#include <cmath>

using namespace uz;

namespace {

// Build a linear curve from (0, startVal) to (1, endVal).
VfxTangentCurve LinearCurve(float startVal, float endVal) {
    VfxTangentCurve c;
    c.AddLinearKey(0.0f, startVal);
    c.AddLinearKey(1.0f, endVal);
    return c;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxSampleMorphologyCurve — empty-curve default
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailMorphology, EmptyCurveReturnsDefault) {
    VfxTangentCurve empty;
    EXPECT_FLOAT_EQ(VfxSampleMorphologyCurve(empty, 0.5f, 7.25f), 7.25f);
}

TEST(VfxTrailMorphology, EmptyCurveDefaultZero) {
    VfxTangentCurve empty;
    EXPECT_FLOAT_EQ(VfxSampleMorphologyCurve(empty, 0.9f, 0.0f), 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Sampling a real curve
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailMorphology, LinearCurveSamplesLinearly) {
    auto curve = LinearCurve(0.0f, 1.0f);
    EXPECT_NEAR(VfxSampleMorphologyCurve(curve, 0.0f,  0.0f), 0.0f, 1e-5f);
    EXPECT_NEAR(VfxSampleMorphologyCurve(curve, 0.5f,  0.0f), 0.5f, 1e-5f);
    EXPECT_NEAR(VfxSampleMorphologyCurve(curve, 1.0f,  0.0f), 1.0f, 1e-5f);
}

TEST(VfxTrailMorphology, SamplerClampsT) {
    auto curve = LinearCurve(2.0f, 3.0f);
    // Out-of-range t values clamp to [0, 1] so the curve always evaluates
    // within its authored domain.
    EXPECT_NEAR(VfxSampleMorphologyCurve(curve, -5.0f, 0.0f), 2.0f, 1e-5f);
    EXPECT_NEAR(VfxSampleMorphologyCurve(curve,  5.0f, 0.0f), 3.0f, 1e-5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxTrailMorphology::IsIdentity
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTrailMorphology, DefaultConstructedIsIdentity) {
    VfxTrailMorphology m;
    EXPECT_TRUE(m.IsIdentity());
}

TEST(VfxTrailMorphology, AnyCurveBreaksIdentity) {
    VfxTrailMorphology m;
    m.widthCurve.AddLinearKey(0.5f, 2.0f);
    EXPECT_FALSE(m.IsIdentity());

    VfxTrailMorphology n;
    n.twistCurve.AddLinearKey(0.5f, 1.5f);
    EXPECT_FALSE(n.IsIdentity());

    VfxTrailMorphology o;
    o.opacityCurve.AddLinearKey(0.5f, 0.5f);
    EXPECT_FALSE(o.IsIdentity());
}

// ─────────────────────────────────────────────────────────────────────────────
// Twist rotation math (CPU mirror of the builder's rotation step)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

struct Vec3f { float x = 0.0f, y = 0.0f, z = 0.0f; };

/// Replicates the builder's in-place twist rotation so tests can verify
/// the math without building a full mesh. Keeps a one-line hand-off to
/// the production helper if it's ever extracted.
void RotateBasisAroundTangent(float theta, Vec3f& right, Vec3f& up) {
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const Vec3f r = right, u = up;
    right = { r.x*c + u.x*s, r.y*c + u.y*s, r.z*c + u.z*s };
    up    = { u.x*c - r.x*s, u.y*c - r.y*s, u.z*c - r.z*s };
}

float Dot(const Vec3f& a, const Vec3f& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
float Length(const Vec3f& v) { return std::sqrt(Dot(v, v)); }

} // namespace

TEST(VfxTrailMorphology, TwistZeroPreservesBasis) {
    Vec3f right{1, 0, 0}, up{0, 1, 0};
    RotateBasisAroundTangent(0.0f, right, up);
    EXPECT_FLOAT_EQ(right.x, 1.0f);
    EXPECT_FLOAT_EQ(up.y,    1.0f);
}

TEST(VfxTrailMorphology, TwistNinetyRotatesRightToUp) {
    Vec3f right{1, 0, 0}, up{0, 1, 0};
    const float theta = 3.14159265358979f * 0.5f; // 90°
    RotateBasisAroundTangent(theta, right, up);
    // right ended up aligned with original up
    EXPECT_NEAR(right.x, 0.0f, 1e-5f);
    EXPECT_NEAR(right.y, 1.0f, 1e-5f);
    // up ended up aligned with negative original right
    EXPECT_NEAR(up.x, -1.0f, 1e-5f);
    EXPECT_NEAR(up.y,  0.0f, 1e-5f);
}

TEST(VfxTrailMorphology, TwistPreservesOrthogonalityAndLength) {
    Vec3f right{1, 0, 0}, up{0, 1, 0};
    RotateBasisAroundTangent(0.7f, right, up);
    EXPECT_NEAR(Dot(right, up), 0.0f, 1e-5f);
    EXPECT_NEAR(Length(right), 1.0f, 1e-5f);
    EXPECT_NEAR(Length(up),    1.0f, 1e-5f);
}

TEST(VfxTrailMorphology, TwistFullCircleReturnsOriginal) {
    Vec3f right{1, 0, 0}, up{0, 1, 0};
    const float theta = 2.0f * 3.14159265358979f;
    RotateBasisAroundTangent(theta, right, up);
    EXPECT_NEAR(right.x, 1.0f, 1e-4f);
    EXPECT_NEAR(up.y,    1.0f, 1e-4f);
}
