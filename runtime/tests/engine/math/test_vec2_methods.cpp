/// Unit tests for Vec2 new methods — distance_to, angle_to, slerp,
/// cubic_interpolate, slide, bounce, reflect, abs, floor, ceil, round,
/// clamp, snap, sign, is_zero_approx, is_equal_approx, rotated.
#include "vector2.h"

#include <gtest/gtest.h>

using namespace uz;

static constexpr float kEps  = 1e-4f;
static constexpr float kEps6 = 1e-6f;

// ── distance_to ──────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, DistanceTo_KnownPoints_ReturnsCorrectLength) {
    Vec2 a(0.0f, 0.0f);
    Vec2 b(3.0f, 4.0f);
    EXPECT_NEAR(a.distance_to(b), 5.0f, kEps);
}

TEST(Vec2MethodsTest, DistanceTo_SamePoint_ReturnsZero) {
    Vec2 v(7.0f, -3.0f);
    EXPECT_NEAR(v.distance_to(v), 0.0f, kEps6);
}

TEST(Vec2MethodsTest, DistanceTo_NegativeComponents_IsPositive) {
    Vec2 a(-1.0f, -1.0f);
    Vec2 b(1.0f, 1.0f);
    // distance = sqrt(4 + 4) = 2*sqrt(2)
    EXPECT_NEAR(a.distance_to(b), 2.0f * Math::SQRT2, kEps);
}

TEST(Vec2MethodsTest, DistanceSquaredTo_MatchesDistanceSquared) {
    Vec2 a(1.0f, 2.0f);
    Vec2 b(4.0f, 6.0f);
    float d  = a.distance_to(b);
    float d2 = a.distance_squared_to(b);
    EXPECT_NEAR(d2, d * d, kEps);
}

// ── angle_to ─────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, AngleTo_RightToUp_IsPositiveQuarterPi) {
    Vec2 right(1.0f, 0.0f);
    Vec2 up(0.0f, 1.0f);
    // Counter-clockwise from +X to +Y in standard 2D is +pi/2
    EXPECT_NEAR(right.angle_to(up), Math::PI / 2.0f, kEps);
}

TEST(Vec2MethodsTest, AngleTo_UpToRight_IsNegativeQuarterPi) {
    Vec2 right(1.0f, 0.0f);
    Vec2 up(0.0f, 1.0f);
    EXPECT_NEAR(up.angle_to(right), -Math::PI / 2.0f, kEps);
}

TEST(Vec2MethodsTest, AngleTo_SameVector_IsZero) {
    Vec2 v(1.0f, 0.5f);
    EXPECT_NEAR(v.angle_to(v), 0.0f, kEps);
}

// ── rotated ──────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Rotated_XAxisByHalfPi_BecomesYAxis) {
    Vec2 v(1.0f, 0.0f);
    Vec2 r = v.rotated(Math::PI / 2.0f);
    EXPECT_NEAR(r.x, 0.0f, kEps);
    EXPECT_NEAR(r.y, 1.0f, kEps);
}

TEST(Vec2MethodsTest, Rotated_ByZero_IsIdentity) {
    Vec2 v(3.0f, 4.0f);
    Vec2 r = v.rotated(0.0f);
    EXPECT_NEAR(r.x, v.x, kEps);
    EXPECT_NEAR(r.y, v.y, kEps);
}

TEST(Vec2MethodsTest, Rotated_ByFullCircle_IsIdentity) {
    Vec2 v(2.0f, 1.0f);
    Vec2 r = v.rotated(Math::TAU);
    EXPECT_NEAR(r.x, v.x, kEps);
    EXPECT_NEAR(r.y, v.y, kEps);
}

TEST(Vec2MethodsTest, Rotated_PreservesLength) {
    Vec2 v(3.0f, 4.0f);
    Vec2 r = v.rotated(1.2f);
    EXPECT_NEAR(r.length(), v.length(), kEps);
}

// ── slerp ────────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Slerp_AtZero_ReturnsThis) {
    Vec2 a(1.0f, 0.0f);
    Vec2 b(0.0f, 1.0f);
    Vec2 r = a.slerp(b, 0.0f);
    EXPECT_NEAR(r.x, a.x, kEps);
    EXPECT_NEAR(r.y, a.y, kEps);
}

TEST(Vec2MethodsTest, Slerp_AtOne_ReturnsOther) {
    Vec2 a(1.0f, 0.0f);
    Vec2 b(0.0f, 1.0f);
    Vec2 r = a.slerp(b, 1.0f);
    EXPECT_NEAR(r.x, b.x, kEps);
    EXPECT_NEAR(r.y, b.y, kEps);
}

TEST(Vec2MethodsTest, Slerp_SameVector_ReturnsUnchanged) {
    Vec2 v(1.0f, 0.0f);
    Vec2 r = v.slerp(v, 0.5f);
    EXPECT_NEAR(r.x, v.x, kEps);
    EXPECT_NEAR(r.y, v.y, kEps);
}

TEST(Vec2MethodsTest, Slerp_AtHalf_PreservesApproxLength) {
    Vec2 a(2.0f, 0.0f);
    Vec2 b(0.0f, 2.0f);
    Vec2 r = a.slerp(b, 0.5f);
    // Both have length 2, so midpoint should be near length 2
    EXPECT_NEAR(r.length(), 2.0f, kEps);
}

// ── cubic_interpolate ────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, CubicInterpolate_AtZero_ReturnsThis) {
    Vec2 a(0.0f, 0.0f);
    Vec2 b(4.0f, 0.0f);
    Vec2 pre_a(-1.0f, 0.0f);
    Vec2 post_b(5.0f, 0.0f);
    Vec2 r = a.cubic_interpolate(b, pre_a, post_b, 0.0f);
    EXPECT_NEAR(r.x, a.x, kEps);
    EXPECT_NEAR(r.y, a.y, kEps);
}

TEST(Vec2MethodsTest, CubicInterpolate_AtOne_ReturnsB) {
    Vec2 a(0.0f, 0.0f);
    Vec2 b(4.0f, 0.0f);
    Vec2 pre_a(-1.0f, 0.0f);
    Vec2 post_b(5.0f, 0.0f);
    Vec2 r = a.cubic_interpolate(b, pre_a, post_b, 1.0f);
    EXPECT_NEAR(r.x, b.x, kEps);
    EXPECT_NEAR(r.y, b.y, kEps);
}

// ── slide ────────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Slide_AgainstNormal_RemovesNormalComponent) {
    // Motion straight into wall: direction (1, 0), normal (1, 0) — result zero
    Vec2 motion(1.0f, 0.0f);
    Vec2 normal(1.0f, 0.0f);
    Vec2 slid = motion.slide(normal);
    EXPECT_NEAR(slid.x, 0.0f, kEps);
    EXPECT_NEAR(slid.y, 0.0f, kEps);
}

TEST(Vec2MethodsTest, Slide_ParallelToNormal_ReturnsFullVector) {
    // Motion purely tangential to wall normal — slide returns original
    Vec2 motion(0.0f, 1.0f);
    Vec2 normal(1.0f, 0.0f);  // wall normal is horizontal
    Vec2 slid = motion.slide(normal);
    EXPECT_NEAR(slid.x, 0.0f, kEps);
    EXPECT_NEAR(slid.y, 1.0f, kEps);
}

TEST(Vec2MethodsTest, Slide_DiagonalMotion_RemovesNormalComponent) {
    Vec2 motion(1.0f, 1.0f);
    Vec2 normal(1.0f, 0.0f);
    Vec2 slid = motion.slide(normal);
    // slide = motion - normal * dot(motion, normal) = (1,1) - (1,0)*1 = (0,1)
    EXPECT_NEAR(slid.x, 0.0f, kEps);
    EXPECT_NEAR(slid.y, 1.0f, kEps);
}

// ── reflect ──────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Reflect_DownwardOnHorizontalSurface) {
    // reflect(n) = 2*n*dot(n,v) - v (Godot convention)
    Vec2 incoming(0.0f, -1.0f);
    Vec2 normal(0.0f, 1.0f);
    Vec2 reflected = incoming.reflect(normal);
    // 2*(0,1)*(-1) - (0,-1) = (0,-2) - (0,-1) = (0,-1)
    EXPECT_NEAR(reflected.x, 0.0f, kEps);
    EXPECT_NEAR(reflected.y, -1.0f, kEps);
}

TEST(Vec2MethodsTest, Reflect_PreservesLength) {
    Vec2 v(3.0f, 4.0f);
    Vec2 normal(1.0f, 0.0f);
    Vec2 reflected = v.reflect(normal);
    EXPECT_NEAR(reflected.length(), v.length(), kEps);
}

// ── bounce ───────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Bounce_IsNegativeReflect) {
    Vec2 v(1.0f, -1.0f);
    Vec2 normal(0.0f, 1.0f);
    Vec2 bounced  = v.bounce(normal);
    Vec2 reflected = v.reflect(normal);
    EXPECT_NEAR(bounced.x, -reflected.x, kEps);
    EXPECT_NEAR(bounced.y, -reflected.y, kEps);
}

// ── abs ──────────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Abs_NegativeComponents_BecomesPositive) {
    Vec2 v(-3.5f, -7.0f);
    Vec2 a = v.abs();
    EXPECT_FLOAT_EQ(a.x, 3.5f);
    EXPECT_FLOAT_EQ(a.y, 7.0f);
}

TEST(Vec2MethodsTest, Abs_PositiveVector_IsUnchanged) {
    Vec2 v(1.0f, 2.0f);
    Vec2 a = v.abs();
    EXPECT_FLOAT_EQ(a.x, 1.0f);
    EXPECT_FLOAT_EQ(a.y, 2.0f);
}

TEST(Vec2MethodsTest, Abs_MixedSigns) {
    Vec2 v(-1.0f, 2.0f);
    Vec2 a = v.abs();
    EXPECT_FLOAT_EQ(a.x, 1.0f);
    EXPECT_FLOAT_EQ(a.y, 2.0f);
}

// ── floor / ceil / round ─────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Floor_PositiveFractional) {
    Vec2 v(1.9f, 2.1f);
    Vec2 f = v.floor();
    EXPECT_FLOAT_EQ(f.x, 1.0f);
    EXPECT_FLOAT_EQ(f.y, 2.0f);
}

TEST(Vec2MethodsTest, Floor_NegativeFractional) {
    Vec2 v(-1.1f, -2.9f);
    Vec2 f = v.floor();
    EXPECT_FLOAT_EQ(f.x, -2.0f);
    EXPECT_FLOAT_EQ(f.y, -3.0f);
}

TEST(Vec2MethodsTest, Ceil_PositiveFractional) {
    Vec2 v(1.1f, 2.9f);
    Vec2 c = v.ceil();
    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
}

TEST(Vec2MethodsTest, Ceil_NegativeFractional) {
    Vec2 v(-1.9f, -2.1f);
    Vec2 c = v.ceil();
    EXPECT_FLOAT_EQ(c.x, -1.0f);
    EXPECT_FLOAT_EQ(c.y, -2.0f);
}

TEST(Vec2MethodsTest, Round_HalfRoundsAwayFromZero) {
    Vec2 v(1.5f, 2.5f);
    Vec2 r = v.round();
    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 3.0f);
}

TEST(Vec2MethodsTest, Round_BelowHalf_RoundsDown) {
    Vec2 v(1.4f, 2.4f);
    Vec2 r = v.round();
    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.y, 2.0f);
}

// ── sign ─────────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Sign_PositiveComponents_ReturnsOne) {
    Vec2 v(5.0f, 3.0f);
    Vec2 s = v.sign();
    EXPECT_FLOAT_EQ(s.x, 1.0f);
    EXPECT_FLOAT_EQ(s.y, 1.0f);
}

TEST(Vec2MethodsTest, Sign_NegativeComponents_ReturnsMinusOne) {
    Vec2 v(-5.0f, -3.0f);
    Vec2 s = v.sign();
    EXPECT_FLOAT_EQ(s.x, -1.0f);
    EXPECT_FLOAT_EQ(s.y, -1.0f);
}

TEST(Vec2MethodsTest, Sign_ZeroComponents_ReturnsZero) {
    Vec2 v(0.0f, 0.0f);
    Vec2 s = v.sign();
    EXPECT_FLOAT_EQ(s.x, 0.0f);
    EXPECT_FLOAT_EQ(s.y, 0.0f);
}

// ── clamp ────────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Clamp_ValueInRange_Unchanged) {
    Vec2 v(1.0f, 2.0f);
    Vec2 result = v.clamp(Vec2(0.0f, 0.0f), Vec2(5.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
}

TEST(Vec2MethodsTest, Clamp_ValueBelowMin_ClampedToMin) {
    Vec2 v(-10.0f, -10.0f);
    Vec2 result = v.clamp(Vec2(0.0f, 0.0f), Vec2(5.0f, 5.0f));
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 0.0f);
}

TEST(Vec2MethodsTest, Clamp_ValueAboveMax_ClampedToMax) {
    Vec2 v(100.0f, 200.0f);
    Vec2 result = v.clamp(Vec2(0.0f, 0.0f), Vec2(5.0f, 10.0f));
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 10.0f);
}

// ── snap ─────────────────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, Snap_SnapsToNearestGridMultiple) {
    Vec2 v(3.7f, 5.3f);
    Vec2 s = v.snap(Vec2(2.0f, 2.0f));
    EXPECT_FLOAT_EQ(s.x, 4.0f);
    EXPECT_FLOAT_EQ(s.y, 6.0f);
}

TEST(Vec2MethodsTest, Snap_ExactMultiple_Unchanged) {
    Vec2 v(4.0f, 6.0f);
    Vec2 s = v.snap(Vec2(2.0f, 2.0f));
    EXPECT_FLOAT_EQ(s.x, 4.0f);
    EXPECT_FLOAT_EQ(s.y, 6.0f);
}

TEST(Vec2MethodsTest, Snap_ZeroStep_ReturnsOriginal) {
    Vec2 v(3.7f, 5.3f);
    Vec2 s = v.snap(Vec2(0.0f, 0.0f));
    EXPECT_FLOAT_EQ(s.x, v.x);
    EXPECT_FLOAT_EQ(s.y, v.y);
}

// ── is_zero_approx ───────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, IsZeroApprox_ExactlyZero_ReturnsTrue) {
    Vec2 v(0.0f, 0.0f);
    EXPECT_TRUE(v.is_zero_approx());
}

TEST(Vec2MethodsTest, IsZeroApprox_SmallValue_ReturnsTrue) {
    Vec2 v(1e-7f, 1e-7f);
    EXPECT_TRUE(v.is_zero_approx());
}

TEST(Vec2MethodsTest, IsZeroApprox_LargeValue_ReturnsFalse) {
    Vec2 v(0.01f, 0.0f);
    EXPECT_FALSE(v.is_zero_approx());
}

TEST(Vec2MethodsTest, IsZeroApprox_PartialZero_ReturnsFalse) {
    Vec2 v(0.0f, 1.0f);
    EXPECT_FALSE(v.is_zero_approx());
}

// ── is_equal_approx ──────────────────────────────────────────────────────────

TEST(Vec2MethodsTest, IsEqualApprox_SameVector_ReturnsTrue) {
    Vec2 v(1.0f, 2.0f);
    EXPECT_TRUE(v.is_equal_approx(v));
}

TEST(Vec2MethodsTest, IsEqualApprox_TinyDifference_ReturnsTrue) {
    Vec2 a(1.0f, 2.0f);
    Vec2 b(1.0f + 1e-7f, 2.0f + 1e-7f);
    EXPECT_TRUE(a.is_equal_approx(b));
}

TEST(Vec2MethodsTest, IsEqualApprox_LargeDifference_ReturnsFalse) {
    Vec2 a(1.0f, 2.0f);
    Vec2 b(1.1f, 2.0f);
    EXPECT_FALSE(a.is_equal_approx(b));
}
