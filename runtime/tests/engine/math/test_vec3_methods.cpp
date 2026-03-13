/// Unit tests for Vec3 new methods — distance_to, angle_to, signed_angle_to,
/// slerp, cubic_interpolate, slide, bounce, reflect, abs, floor, ceil, round,
/// clamp, snap, sign, is_zero_approx, is_equal_approx, octahedron encode/decode,
/// get_any_perpendicular, rotated.
#include "vector3.h"
#include "vector2.h"

#include <gtest/gtest.h>

using namespace uz;

static constexpr float kEps  = 1e-4f;
static constexpr float kEps6 = 1e-6f;

// ── distance_to ──────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, DistanceTo_KnownPoints_ReturnsCorrectLength) {
    Vec3 a(0.0f, 0.0f, 0.0f);
    Vec3 b(1.0f, 2.0f, 2.0f);
    // sqrt(1+4+4) = 3
    EXPECT_NEAR(a.distance_to(b), 3.0f, kEps);
}

TEST(Vec3MethodsTest, DistanceTo_SamePoint_ReturnsZero) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_NEAR(v.distance_to(v), 0.0f, kEps6);
}

TEST(Vec3MethodsTest, DistanceTo_IsSymmetric) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(-1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(a.distance_to(b), b.distance_to(a), kEps6);
}

TEST(Vec3MethodsTest, DistanceSquaredTo_EqualsDistanceSquared) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    float d  = a.distance_to(b);
    float d2 = a.distance_squared_to(b);
    EXPECT_NEAR(d2, d * d, kEps);
}

// ── angle_to ─────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, AngleTo_PerpendicularVectors_IsHalfPi) {
    Vec3 x = Vec3::UnitX();
    Vec3 y = Vec3::UnitY();
    EXPECT_NEAR(x.angle_to(y), Math::PI / 2.0f, kEps);
}

TEST(Vec3MethodsTest, AngleTo_ParallelVectors_IsZero) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_NEAR(v.angle_to(v), 0.0f, 1e-3f); // acos near 1.0 has reduced precision
}

TEST(Vec3MethodsTest, AngleTo_AntiparallelVectors_IsPi) {
    Vec3 v(1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(v.angle_to(-v), Math::PI, kEps);
}

TEST(Vec3MethodsTest, AngleTo_ResultAlwaysNonNegative) {
    Vec3 a = Vec3::UnitX();
    Vec3 b = Vec3::UnitY();
    EXPECT_GE(a.angle_to(b), 0.0f);
    EXPECT_GE(b.angle_to(a), 0.0f);
}

// ── signed_angle_to ──────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, SignedAngleTo_XToY_AroundZ_IsPositive) {
    Vec3 x = Vec3::UnitX();
    Vec3 y = Vec3::UnitY();
    Vec3 z = Vec3::UnitZ();
    float angle = x.signed_angle_to(y, z);
    EXPECT_NEAR(angle, Math::PI / 2.0f, kEps);
}

TEST(Vec3MethodsTest, SignedAngleTo_YToX_AroundZ_IsNegative) {
    Vec3 x = Vec3::UnitX();
    Vec3 y = Vec3::UnitY();
    Vec3 z = Vec3::UnitZ();
    float angle = y.signed_angle_to(x, z);
    EXPECT_NEAR(angle, -Math::PI / 2.0f, kEps);
}

TEST(Vec3MethodsTest, SignedAngleTo_SameVector_IsZero) {
    Vec3 v = Vec3::UnitX();
    float angle = v.signed_angle_to(v, Vec3::UnitZ());
    EXPECT_NEAR(angle, 0.0f, kEps);
}

// ── rotated ──────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Rotated_XAxisAroundZ_90Degrees_BecomesY) {
    Vec3 v(1.0f, 0.0f, 0.0f);
    Vec3 r = v.rotated(Vec3::UnitZ(), Math::PI / 2.0f);
    EXPECT_NEAR(r.x, 0.0f, kEps);
    EXPECT_NEAR(r.y, 1.0f, kEps);
    EXPECT_NEAR(r.z, 0.0f, kEps);
}

TEST(Vec3MethodsTest, Rotated_ByZero_ReturnsOriginal) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    Vec3 r = v.rotated(Vec3::UnitY(), 0.0f);
    EXPECT_NEAR(r.x, v.x, kEps);
    EXPECT_NEAR(r.y, v.y, kEps);
    EXPECT_NEAR(r.z, v.z, kEps);
}

TEST(Vec3MethodsTest, Rotated_PreservesLength) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    Vec3 r = v.rotated(Vec3::UnitZ().Normalized(), 1.23f);
    EXPECT_NEAR(r.length(), v.length(), kEps);
}

// ── slerp ────────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Slerp_AtZero_ReturnsThis) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);
    Vec3 r = a.slerp(b, 0.0f);
    EXPECT_NEAR(r.x, a.x, kEps);
    EXPECT_NEAR(r.y, a.y, kEps);
    EXPECT_NEAR(r.z, a.z, kEps);
}

TEST(Vec3MethodsTest, Slerp_AtOne_ReturnsOther) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);
    Vec3 r = a.slerp(b, 1.0f);
    EXPECT_NEAR(r.x, b.x, kEps);
    EXPECT_NEAR(r.y, b.y, kEps);
    EXPECT_NEAR(r.z, b.z, kEps);
}

TEST(Vec3MethodsTest, Slerp_SameVector_ReturnsUnchanged) {
    Vec3 v(0.0f, 1.0f, 0.0f);
    Vec3 r = v.slerp(v, 0.5f);
    EXPECT_NEAR(r.x, v.x, kEps);
    EXPECT_NEAR(r.y, v.y, kEps);
    EXPECT_NEAR(r.z, v.z, kEps);
}

TEST(Vec3MethodsTest, Slerp_AtHalf_HasAngleHalved) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);
    Vec3 r = a.slerp(b, 0.5f);
    // angle between a and r should be pi/4
    EXPECT_NEAR(a.angle_to(r), Math::PI / 4.0f, kEps);
}

// ── cubic_interpolate ────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, CubicInterpolate_AtZero_ReturnsThis) {
    Vec3 a(0.0f, 0.0f, 0.0f);
    Vec3 b(6.0f, 0.0f, 0.0f);
    Vec3 pre(-1.0f, 0.0f, 0.0f);
    Vec3 post(7.0f, 0.0f, 0.0f);
    Vec3 r = a.cubic_interpolate(b, pre, post, 0.0f);
    EXPECT_NEAR(r.x, a.x, kEps);
    EXPECT_NEAR(r.y, a.y, kEps);
    EXPECT_NEAR(r.z, a.z, kEps);
}

TEST(Vec3MethodsTest, CubicInterpolate_AtOne_ReturnsB) {
    Vec3 a(0.0f, 0.0f, 0.0f);
    Vec3 b(6.0f, 0.0f, 0.0f);
    Vec3 pre(-1.0f, 0.0f, 0.0f);
    Vec3 post(7.0f, 0.0f, 0.0f);
    Vec3 r = a.cubic_interpolate(b, pre, post, 1.0f);
    EXPECT_NEAR(r.x, b.x, kEps);
    EXPECT_NEAR(r.y, b.y, kEps);
    EXPECT_NEAR(r.z, b.z, kEps);
}

// ── slide ────────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Slide_DirectlyIntoWall_BecomesZero) {
    Vec3 motion(1.0f, 0.0f, 0.0f);
    Vec3 normal(1.0f, 0.0f, 0.0f);
    Vec3 slid = motion.slide(normal);
    EXPECT_NEAR(slid.x, 0.0f, kEps);
    EXPECT_NEAR(slid.y, 0.0f, kEps);
    EXPECT_NEAR(slid.z, 0.0f, kEps);
}

TEST(Vec3MethodsTest, Slide_TangentialMotion_Unchanged) {
    Vec3 motion(0.0f, 1.0f, 0.0f);
    Vec3 normal(1.0f, 0.0f, 0.0f);
    Vec3 slid = motion.slide(normal);
    EXPECT_NEAR(slid.x, 0.0f, kEps);
    EXPECT_NEAR(slid.y, 1.0f, kEps);
    EXPECT_NEAR(slid.z, 0.0f, kEps);
}

TEST(Vec3MethodsTest, Slide_Diagonal_RemovesNormalComponent) {
    Vec3 motion(1.0f, 1.0f, 0.0f);
    Vec3 normal(0.0f, 1.0f, 0.0f);
    Vec3 slid = motion.slide(normal);
    // slide = (1,1,0) - (0,1,0)*dot((1,1,0),(0,1,0)) = (1,1,0) - (0,1,0) = (1,0,0)
    EXPECT_NEAR(slid.x, 1.0f, kEps);
    EXPECT_NEAR(slid.y, 0.0f, kEps);
    EXPECT_NEAR(slid.z, 0.0f, kEps);
}

// ── reflect ──────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Reflect_DownwardOnHorizontalPlane) {
    // reflect(n) = 2*n*dot(n) - *this (Godot convention, NOT physics convention)
    Vec3 v(0.0f, -1.0f, 0.0f);
    Vec3 normal(0.0f, 1.0f, 0.0f);
    Vec3 r = v.reflect(normal);
    EXPECT_NEAR(r.x, 0.0f, kEps);
    EXPECT_NEAR(r.y, -1.0f, kEps); // same direction — not physics-style flip
    EXPECT_NEAR(r.z, 0.0f, kEps);
}

TEST(Vec3MethodsTest, Reflect_PreservesLength) {
    Vec3 v(1.0f, -2.0f, 3.0f);
    Vec3 normal(0.0f, 1.0f, 0.0f);
    Vec3 r = v.reflect(normal);
    EXPECT_NEAR(r.length(), v.length(), kEps);
}

TEST(Vec3MethodsTest, Reflect_Diagonal_CorrectMirror) {
    Vec3 v(1.0f, -1.0f, 0.0f);
    Vec3 normal(0.0f, 1.0f, 0.0f);
    // reflect = normal*(2*dot(v, normal)) - v
    // dot((1,-1,0),(0,1,0)) = -1
    // reflect = (0,1,0)*2*(-1) - (1,-1,0) = (0,-2,0) - (1,-1,0) = (-1,-1,0)
    Vec3 r = v.reflect(normal);
    EXPECT_NEAR(r.x, -1.0f, kEps);
    EXPECT_NEAR(r.y, -1.0f, kEps);
    EXPECT_NEAR(r.z,  0.0f, kEps);
}

// ── bounce ───────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Bounce_IsNegativeReflect) {
    Vec3 v(1.0f, -1.0f, 0.5f);
    Vec3 normal(0.0f, 1.0f, 0.0f);
    Vec3 bounced   = v.bounce(normal);
    Vec3 reflected = v.reflect(normal);
    EXPECT_NEAR(bounced.x, -reflected.x, kEps);
    EXPECT_NEAR(bounced.y, -reflected.y, kEps);
    EXPECT_NEAR(bounced.z, -reflected.z, kEps);
}

// ── abs ──────────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Abs_AllNegative_BecomesPositive) {
    Vec3 v(-1.0f, -2.0f, -3.0f);
    Vec3 a = v.abs();
    EXPECT_FLOAT_EQ(a.x, 1.0f);
    EXPECT_FLOAT_EQ(a.y, 2.0f);
    EXPECT_FLOAT_EQ(a.z, 3.0f);
}

TEST(Vec3MethodsTest, Abs_MixedSigns_AllPositive) {
    Vec3 v(-1.0f, 2.0f, -3.0f);
    Vec3 a = v.abs();
    EXPECT_FLOAT_EQ(a.x, 1.0f);
    EXPECT_FLOAT_EQ(a.y, 2.0f);
    EXPECT_FLOAT_EQ(a.z, 3.0f);
}

// ── floor / ceil / round ─────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Floor_FractionalPositive) {
    Vec3 v(1.9f, 2.1f, 3.5f);
    Vec3 f = v.floor();
    EXPECT_FLOAT_EQ(f.x, 1.0f);
    EXPECT_FLOAT_EQ(f.y, 2.0f);
    EXPECT_FLOAT_EQ(f.z, 3.0f);
}

TEST(Vec3MethodsTest, Floor_FractionalNegative) {
    Vec3 v(-1.1f, -2.9f, -3.5f);
    Vec3 f = v.floor();
    EXPECT_FLOAT_EQ(f.x, -2.0f);
    EXPECT_FLOAT_EQ(f.y, -3.0f);
    EXPECT_FLOAT_EQ(f.z, -4.0f);
}

TEST(Vec3MethodsTest, Ceil_FractionalPositive) {
    Vec3 v(1.1f, 2.9f, 3.0f);
    Vec3 c = v.ceil();
    EXPECT_FLOAT_EQ(c.x, 2.0f);
    EXPECT_FLOAT_EQ(c.y, 3.0f);
    EXPECT_FLOAT_EQ(c.z, 3.0f);
}

TEST(Vec3MethodsTest, Round_HalfUp) {
    Vec3 v(1.5f, 2.5f, 3.4f);
    Vec3 r = v.round();
    EXPECT_FLOAT_EQ(r.x, 2.0f);
    EXPECT_FLOAT_EQ(r.y, 3.0f);
    EXPECT_FLOAT_EQ(r.z, 3.0f);
}

// ── sign ─────────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Sign_AllPositive) {
    Vec3 v(5.0f, 3.0f, 0.1f);
    Vec3 s = v.sign();
    EXPECT_FLOAT_EQ(s.x, 1.0f);
    EXPECT_FLOAT_EQ(s.y, 1.0f);
    EXPECT_FLOAT_EQ(s.z, 1.0f);
}

TEST(Vec3MethodsTest, Sign_AllNegative) {
    Vec3 v(-5.0f, -3.0f, -0.1f);
    Vec3 s = v.sign();
    EXPECT_FLOAT_EQ(s.x, -1.0f);
    EXPECT_FLOAT_EQ(s.y, -1.0f);
    EXPECT_FLOAT_EQ(s.z, -1.0f);
}

TEST(Vec3MethodsTest, Sign_AllZero_ReturnsZero) {
    Vec3 v(0.0f, 0.0f, 0.0f);
    Vec3 s = v.sign();
    EXPECT_FLOAT_EQ(s.x, 0.0f);
    EXPECT_FLOAT_EQ(s.y, 0.0f);
    EXPECT_FLOAT_EQ(s.z, 0.0f);
}

// ── clamp ────────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Clamp_AllInRange_Unchanged) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    Vec3 r = v.clamp(Vec3(0.0f, 0.0f, 0.0f), Vec3(5.0f, 5.0f, 5.0f));
    EXPECT_FLOAT_EQ(r.x, 1.0f);
    EXPECT_FLOAT_EQ(r.y, 2.0f);
    EXPECT_FLOAT_EQ(r.z, 3.0f);
}

TEST(Vec3MethodsTest, Clamp_BelowMin_ClampedToMin) {
    Vec3 v(-10.0f, -20.0f, -30.0f);
    Vec3 r = v.clamp(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(r.x, 0.0f);
    EXPECT_FLOAT_EQ(r.y, 0.0f);
    EXPECT_FLOAT_EQ(r.z, 0.0f);
}

TEST(Vec3MethodsTest, Clamp_AboveMax_ClampedToMax) {
    Vec3 v(100.0f, 200.0f, 300.0f);
    Vec3 r = v.clamp(Vec3(0.0f, 0.0f, 0.0f), Vec3(5.0f, 10.0f, 15.0f));
    EXPECT_FLOAT_EQ(r.x, 5.0f);
    EXPECT_FLOAT_EQ(r.y, 10.0f);
    EXPECT_FLOAT_EQ(r.z, 15.0f);
}

// ── snap ─────────────────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, Snap_SnapsToNearestGrid) {
    Vec3 v(2.7f, 4.4f, 1.5f);
    Vec3 s = v.snap(Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_FLOAT_EQ(s.x, 3.0f);
    EXPECT_FLOAT_EQ(s.y, 4.0f);
    EXPECT_FLOAT_EQ(s.z, 2.0f);
}

TEST(Vec3MethodsTest, Snap_ZeroStep_ReturnsOriginal) {
    Vec3 v(2.7f, 4.4f, 1.5f);
    Vec3 s = v.snap(Vec3(0.0f, 0.0f, 0.0f));
    EXPECT_FLOAT_EQ(s.x, v.x);
    EXPECT_FLOAT_EQ(s.y, v.y);
    EXPECT_FLOAT_EQ(s.z, v.z);
}

// ── is_zero_approx ───────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, IsZeroApprox_ZeroVector_ReturnsTrue) {
    EXPECT_TRUE(Vec3(0.0f, 0.0f, 0.0f).is_zero_approx());
}

TEST(Vec3MethodsTest, IsZeroApprox_SmallValues_ReturnsTrue) {
    EXPECT_TRUE(Vec3(1e-7f, 1e-7f, 1e-7f).is_zero_approx());
}

TEST(Vec3MethodsTest, IsZeroApprox_PartialNonZero_ReturnsFalse) {
    EXPECT_FALSE(Vec3(0.0f, 1.0f, 0.0f).is_zero_approx());
}

// ── is_equal_approx ──────────────────────────────────────────────────────────

TEST(Vec3MethodsTest, IsEqualApprox_IdenticalVectors_ReturnsTrue) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_TRUE(v.is_equal_approx(v));
}

TEST(Vec3MethodsTest, IsEqualApprox_TinyDifference_ReturnsTrue) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(1.0f + 1e-7f, 2.0f, 3.0f);
    EXPECT_TRUE(a.is_equal_approx(b));
}

TEST(Vec3MethodsTest, IsEqualApprox_LargeDifference_ReturnsFalse) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(2.0f, 2.0f, 3.0f);
    EXPECT_FALSE(a.is_equal_approx(b));
}

// ── get_any_perpendicular ────────────────────────────────────────────────────

TEST(Vec3MethodsTest, GetAnyPerpendicular_XAxis_ResultIsOrthogonal) {
    Vec3 v = Vec3::UnitX();
    Vec3 perp = v.get_any_perpendicular();
    EXPECT_NEAR(v.dot(perp), 0.0f, kEps);
    EXPECT_GT(perp.length(), kEps);
}

TEST(Vec3MethodsTest, GetAnyPerpendicular_YAxis_ResultIsOrthogonal) {
    Vec3 v = Vec3::UnitY();
    Vec3 perp = v.get_any_perpendicular();
    EXPECT_NEAR(v.dot(perp), 0.0f, kEps);
    EXPECT_GT(perp.length(), kEps);
}

TEST(Vec3MethodsTest, GetAnyPerpendicular_ZAxis_ResultIsOrthogonal) {
    Vec3 v = Vec3::UnitZ();
    Vec3 perp = v.get_any_perpendicular();
    EXPECT_NEAR(v.dot(perp), 0.0f, kEps);
    EXPECT_GT(perp.length(), kEps);
}

TEST(Vec3MethodsTest, GetAnyPerpendicular_DiagonalVector_ResultIsOrthogonal) {
    Vec3 v(1.0f, 1.0f, 0.0f);
    Vec3 perp = v.get_any_perpendicular();
    EXPECT_NEAR(v.dot(perp), 0.0f, kEps);
}

// ── octahedron_encode / decode ───────────────────────────────────────────────

TEST(Vec3MethodsTest, OctahedronEncode_XAxis_DecodesBack) {
    Vec3 n = Vec3::UnitX();
    Vec2 enc = n.octahedron_encode();
    Vec3 dec = Vec3::octahedron_decode(enc);
    EXPECT_NEAR(dec.x, n.x, kEps);
    EXPECT_NEAR(dec.y, n.y, kEps);
    EXPECT_NEAR(dec.z, n.z, kEps);
}

TEST(Vec3MethodsTest, OctahedronEncode_YAxis_DecodesBack) {
    Vec3 n = Vec3::UnitY();
    Vec2 enc = n.octahedron_encode();
    Vec3 dec = Vec3::octahedron_decode(enc);
    EXPECT_NEAR(dec.x, n.x, kEps);
    EXPECT_NEAR(dec.y, n.y, kEps);
    EXPECT_NEAR(dec.z, n.z, kEps);
}

TEST(Vec3MethodsTest, OctahedronEncode_ZAxis_DecodesBack) {
    Vec3 n = Vec3::UnitZ();
    Vec2 enc = n.octahedron_encode();
    Vec3 dec = Vec3::octahedron_decode(enc);
    EXPECT_NEAR(dec.x, n.x, kEps);
    EXPECT_NEAR(dec.y, n.y, kEps);
    EXPECT_NEAR(dec.z, n.z, kEps);
}

TEST(Vec3MethodsTest, OctahedronEncode_NegativeZAxis_DecodesBack) {
    Vec3 n = -Vec3::UnitZ();
    Vec2 enc = n.octahedron_encode();
    Vec3 dec = Vec3::octahedron_decode(enc);
    EXPECT_NEAR(dec.x, n.x, kEps);
    EXPECT_NEAR(dec.y, n.y, kEps);
    EXPECT_NEAR(dec.z, n.z, kEps);
}

TEST(Vec3MethodsTest, OctahedronEncode_EncodedRangeIsZeroToOne) {
    Vec3 n = Vec3(1.0f, 1.0f, 1.0f).Normalized();
    Vec2 enc = n.octahedron_encode();
    EXPECT_GE(enc.x, 0.0f);
    EXPECT_LE(enc.x, 1.0f);
    EXPECT_GE(enc.y, 0.0f);
    EXPECT_LE(enc.y, 1.0f);
}

TEST(Vec3MethodsTest, OctahedronEncode_DecodedIsUnitLength) {
    Vec3 n = Vec3(0.5f, -0.5f, 0.707f).Normalized();
    Vec2 enc = n.octahedron_encode();
    Vec3 dec = Vec3::octahedron_decode(enc);
    EXPECT_NEAR(dec.length(), 1.0f, kEps);
}
