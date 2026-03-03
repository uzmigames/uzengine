// test_math.cpp — 100% coverage of math.h (Vec2, Vec3, Vec4, Quat, Mat4 + free functions)

#include <gtest/gtest.h>
#include "engine/core/math.h"

#include <cmath>
#include <cstddef>

using namespace uz;

// ============================================================================
// Floating-point tolerance for EXPECT_NEAR
// ============================================================================

static constexpr float kTol = 1e-5f;

// ============================================================================
// Constants
// ============================================================================

TEST(MathConstants, Pi_MatchesExpectedValue) {
    EXPECT_NEAR(kPi, 3.14159265f, 1e-7f);
}

TEST(MathConstants, TwoPi_IsDoublePi) {
    EXPECT_NEAR(kTwoPi, 2.0f * kPi, kTol);
}

TEST(MathConstants, HalfPi_IsHalfOfPi) {
    EXPECT_NEAR(kHalfPi, kPi / 2.0f, kTol);
}

TEST(MathConstants, Epsilon_IsSmall) {
    EXPECT_NEAR(kEpsilon, 1e-6f, 1e-10f);
}

TEST(MathRadians, DegreesToRadians_90Degrees) {
    EXPECT_NEAR(radians(90.0f), kHalfPi, kTol);
}

TEST(MathRadians, DegreesToRadians_180Degrees) {
    EXPECT_NEAR(radians(180.0f), kPi, kTol);
}

TEST(MathDegrees, RadiansToDegrees_PiIsOneEighty) {
    EXPECT_NEAR(degrees(kPi), 180.0f, kTol);
}

TEST(MathDegrees, RoundTrip_DegToRadToDeg) {
    EXPECT_NEAR(degrees(radians(45.0f)), 45.0f, kTol);
}

// ============================================================================
// Vec2 — size and fields
// ============================================================================

TEST(Vec2Size, SizeIs8Bytes_AlignIs8) {
    static_assert(sizeof(Vec2) == 8, "Vec2 must be 8 bytes");
    static_assert(alignof(Vec2) == 8, "Vec2 must be 8-byte aligned");
    EXPECT_EQ(sizeof(Vec2), 8u);
}

TEST(Vec2Default, FieldsAreAccessible) {
    Vec2 v{3.0f, 4.0f};

    EXPECT_EQ(v.x, 3.0f);
    EXPECT_EQ(v.y, 4.0f);
}

TEST(Vec2Ops, Add) {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};
    Vec2 c = a + b;

    EXPECT_EQ(c.x, 4.0f);
    EXPECT_EQ(c.y, 6.0f);
}

TEST(Vec2Ops, Subtract) {
    Vec2 a{5.0f, 3.0f};
    Vec2 b{2.0f, 1.0f};
    Vec2 c = a - b;

    EXPECT_EQ(c.x, 3.0f);
    EXPECT_EQ(c.y, 2.0f);
}

TEST(Vec2Ops, ScalarMultiply) {
    Vec2 a{2.0f, 3.0f};
    Vec2 c = a * 4.0f;

    EXPECT_EQ(c.x, 8.0f);
    EXPECT_EQ(c.y, 12.0f);
}

TEST(Vec2Ops, Negate) {
    Vec2 a{1.0f, -2.0f};
    Vec2 c = -a;

    EXPECT_EQ(c.x, -1.0f);
    EXPECT_EQ(c.y,  2.0f);
}

TEST(Vec2Equality, EqualVecs_EqTrue) {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{1.0f, 2.0f};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a != b);
}

TEST(Vec2Equality, DifferentVecs_NeqTrue) {
    Vec2 a{1.0f, 2.0f};
    Vec2 b{1.0f, 3.0f};
    EXPECT_NE(a, b);
}

TEST(Vec2Dot, PerpendicularVectors_DotIsZero) {
    EXPECT_NEAR(vec2_dot({1.0f, 0.0f}, {0.0f, 1.0f}), 0.0f, kTol);
}

TEST(Vec2Dot, ParallelVectors_DotIsProduct) {
    EXPECT_NEAR(vec2_dot({3.0f, 0.0f}, {3.0f, 0.0f}), 9.0f, kTol);
}

TEST(Vec2Len, Len_3_4_Is5) {
    EXPECT_NEAR(vec2_len({3.0f, 4.0f}), 5.0f, kTol);
}

TEST(Vec2Normalize, ResultHasUnitLength) {
    Vec2 n = vec2_normalize({3.0f, 4.0f});
    float len = std::sqrt(n.x * n.x + n.y * n.y);

    EXPECT_NEAR(len, 1.0f, kTol);
    EXPECT_NEAR(n.x, 0.6f, kTol);
    EXPECT_NEAR(n.y, 0.8f, kTol);
}

// ============================================================================
// Vec3 — size and static_assert
// ============================================================================

TEST(Vec3Size, SizeIs16Bytes) {
    EXPECT_EQ(sizeof(Vec3), 16u);
    EXPECT_EQ(alignof(Vec3), 16u);
}

TEST(Vec3Fields, XYZAndPadAccessible) {
    Vec3 v{1.0f, 2.0f, 3.0f, 0.0f};

    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.y, 2.0f);
    EXPECT_EQ(v.z, 3.0f);
    EXPECT_EQ(v._pad, 0.0f);
}

TEST(Vec3Ops, Add_PadIsZero) {
    Vec3 a{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 b{4.0f, 5.0f, 6.0f, 0.0f};
    Vec3 c = a + b;

    EXPECT_EQ(c.x, 5.0f);
    EXPECT_EQ(c.y, 7.0f);
    EXPECT_EQ(c.z, 9.0f);
    EXPECT_EQ(c._pad, 0.0f);
}

TEST(Vec3Ops, Subtract) {
    Vec3 a{5.0f, 3.0f, 1.0f, 0.0f};
    Vec3 b{2.0f, 1.0f, 0.0f, 0.0f};
    Vec3 c = a - b;

    EXPECT_NEAR(c.x, 3.0f, kTol);
    EXPECT_NEAR(c.y, 2.0f, kTol);
    EXPECT_NEAR(c.z, 1.0f, kTol);
}

TEST(Vec3Ops, ScalarMultiply) {
    Vec3 a{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 c = a * 3.0f;

    EXPECT_NEAR(c.x, 3.0f, kTol);
    EXPECT_NEAR(c.y, 6.0f, kTol);
    EXPECT_NEAR(c.z, 9.0f, kTol);
    EXPECT_EQ(c._pad, 0.0f);
}

TEST(Vec3Ops, Negate) {
    Vec3 a{1.0f, -2.0f, 3.0f, 0.0f};
    Vec3 c = -a;

    EXPECT_EQ(c.x, -1.0f);
    EXPECT_EQ(c.y,  2.0f);
    EXPECT_EQ(c.z, -3.0f);
}

TEST(Vec3Equality, EqualVecs_EqTrue) {
    Vec3 a{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 b{1.0f, 2.0f, 3.0f, 0.0f};
    EXPECT_EQ(a, b);
}

TEST(Vec3Equality, DifferentZ_NeqTrue) {
    Vec3 a{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 b{1.0f, 2.0f, 4.0f, 0.0f};
    EXPECT_NE(a, b);
}

TEST(Vec3Dot, PerpendicularVectors_DotIsZero) {
    Vec3 x{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 y{0.0f, 1.0f, 0.0f, 0.0f};

    EXPECT_NEAR(vec3_dot(x, y), 0.0f, kTol);
}

TEST(Vec3Dot, ParallelUnitVectors_DotIsOne) {
    Vec3 x{1.0f, 0.0f, 0.0f, 0.0f};

    EXPECT_NEAR(vec3_dot(x, x), 1.0f, kTol);
}

TEST(Vec3Dot, ArbitraryVectors_MatchManualCalculation) {
    Vec3 a{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 b{4.0f, 5.0f, 6.0f, 0.0f};
    // dot = 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    EXPECT_NEAR(vec3_dot(a, b), 32.0f, kTol);
}

TEST(Vec3Len, ThreeFourZero_IsFive) {
    EXPECT_NEAR(vec3_len({3.0f, 4.0f, 0.0f, 0.0f}), 5.0f, kTol);
}

TEST(Vec3Len, UnitX_IsOne) {
    EXPECT_NEAR(vec3_len({1.0f, 0.0f, 0.0f, 0.0f}), 1.0f, kTol);
}

TEST(Vec3Normalize, ResultHasUnitLength) {
    Vec3 n = vec3_normalize({3.0f, 4.0f, 0.0f, 0.0f});

    EXPECT_NEAR(std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z), 1.0f, kTol);
    EXPECT_NEAR(n.x, 0.6f, kTol);
    EXPECT_NEAR(n.y, 0.8f, kTol);
    EXPECT_NEAR(n.z, 0.0f, kTol);
}

TEST(Vec3Normalize, ArbitraryVector_UnitLength) {
    Vec3 n = vec3_normalize({1.0f, 1.0f, 1.0f, 0.0f});
    float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z);

    EXPECT_NEAR(len, 1.0f, kTol);
}

TEST(Vec3Cross, XCrossY_IsPositiveZ) {
    Vec3 x{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 y{0.0f, 1.0f, 0.0f, 0.0f};
    Vec3 c = vec3_cross(x, y);

    EXPECT_NEAR(c.x, 0.0f, kTol);
    EXPECT_NEAR(c.y, 0.0f, kTol);
    EXPECT_NEAR(c.z, 1.0f, kTol);
    EXPECT_EQ(c._pad, 0.0f);
}

TEST(Vec3Cross, YCrossX_IsNegativeZ) {
    Vec3 x{1.0f, 0.0f, 0.0f, 0.0f};
    Vec3 y{0.0f, 1.0f, 0.0f, 0.0f};
    Vec3 c = vec3_cross(y, x);

    EXPECT_NEAR(c.z, -1.0f, kTol);
}

TEST(Vec3Cross, ParallelVectors_CrossIsZero) {
    Vec3 a{2.0f, 0.0f, 0.0f, 0.0f};
    Vec3 b{5.0f, 0.0f, 0.0f, 0.0f};
    Vec3 c = vec3_cross(a, b);

    EXPECT_NEAR(c.x, 0.0f, kTol);
    EXPECT_NEAR(c.y, 0.0f, kTol);
    EXPECT_NEAR(c.z, 0.0f, kTol);
}

TEST(Vec3Lerp, AtT0_ReturnsA) {
    Vec3 a{0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 b{1.0f, 1.0f, 1.0f, 0.0f};
    Vec3 r = vec3_lerp(a, b, 0.0f);

    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
}

TEST(Vec3Lerp, AtT1_ReturnsB) {
    Vec3 a{0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 b{1.0f, 1.0f, 1.0f, 0.0f};
    Vec3 r = vec3_lerp(a, b, 1.0f);

    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 1.0f, kTol);
    EXPECT_NEAR(r.z, 1.0f, kTol);
}

TEST(Vec3Lerp, AtT05_ReturnsMidpoint) {
    Vec3 a{0.0f, 0.0f, 0.0f, 0.0f};
    Vec3 b{1.0f, 1.0f, 1.0f, 0.0f};
    Vec3 r = vec3_lerp(a, b, 0.5f);

    EXPECT_NEAR(r.x, 0.5f, kTol);
    EXPECT_NEAR(r.y, 0.5f, kTol);
    EXPECT_NEAR(r.z, 0.5f, kTol);
    EXPECT_EQ(r._pad, 0.0f);
}

// ============================================================================
// Vec4 — size and fields
// ============================================================================

TEST(Vec4Size, SizeIs16Bytes) {
    EXPECT_EQ(sizeof(Vec4), 16u);
    EXPECT_EQ(alignof(Vec4), 16u);
}

TEST(Vec4Fields, XYZWAccessible) {
    Vec4 v{1.0f, 2.0f, 3.0f, 4.0f};

    EXPECT_EQ(v.x, 1.0f);
    EXPECT_EQ(v.y, 2.0f);
    EXPECT_EQ(v.z, 3.0f);
    EXPECT_EQ(v.w, 4.0f);
}

TEST(Vec4Ops, Add) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{5.0f, 6.0f, 7.0f, 8.0f};
    Vec4 c = a + b;

    EXPECT_EQ(c.x, 6.0f);
    EXPECT_EQ(c.y, 8.0f);
    EXPECT_EQ(c.z, 10.0f);
    EXPECT_EQ(c.w, 12.0f);
}

TEST(Vec4Ops, Subtract) {
    Vec4 a{5.0f, 4.0f, 3.0f, 2.0f};
    Vec4 b{1.0f, 1.0f, 1.0f, 1.0f};
    Vec4 c = a - b;

    EXPECT_EQ(c.x, 4.0f);
    EXPECT_EQ(c.y, 3.0f);
    EXPECT_EQ(c.z, 2.0f);
    EXPECT_EQ(c.w, 1.0f);
}

TEST(Vec4Ops, ScalarMultiply) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 c = a * 2.0f;

    EXPECT_EQ(c.x, 2.0f);
    EXPECT_EQ(c.y, 4.0f);
    EXPECT_EQ(c.z, 6.0f);
    EXPECT_EQ(c.w, 8.0f);
}

TEST(Vec4Ops, Negate) {
    Vec4 a{1.0f, -2.0f, 3.0f, -4.0f};
    Vec4 c = -a;

    EXPECT_EQ(c.x, -1.0f);
    EXPECT_EQ(c.y,  2.0f);
    EXPECT_EQ(c.z, -3.0f);
    EXPECT_EQ(c.w,  4.0f);
}

TEST(Vec4Equality, EqualVecs_Eq) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{1.0f, 2.0f, 3.0f, 4.0f};
    EXPECT_EQ(a, b);
}

TEST(Vec4Equality, DifferentW_Neq) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{1.0f, 2.0f, 3.0f, 5.0f};
    EXPECT_NE(a, b);
}

TEST(Vec4Dot, UnitVectors) {
    Vec4 a{1.0f, 0.0f, 0.0f, 0.0f};
    Vec4 b{0.0f, 1.0f, 0.0f, 0.0f};

    EXPECT_NEAR(vec4_dot(a, b), 0.0f, kTol);
    EXPECT_NEAR(vec4_dot(a, a), 1.0f, kTol);
}

TEST(Vec4Dot, ArbitraryVectors) {
    Vec4 a{1.0f, 2.0f, 3.0f, 4.0f};
    Vec4 b{4.0f, 3.0f, 2.0f, 1.0f};
    // 1*4 + 2*3 + 3*2 + 4*1 = 4 + 6 + 6 + 4 = 20
    EXPECT_NEAR(vec4_dot(a, b), 20.0f, kTol);
}

// ============================================================================
// Quat — size and identity
// ============================================================================

TEST(QuatSize, SizeIs16Bytes) {
    EXPECT_EQ(sizeof(Quat), 16u);
    EXPECT_EQ(alignof(Quat), 16u);
}

TEST(QuatIdentity, XYZW_Is_0001) {
    Quat q = quat_identity();

    EXPECT_EQ(q.x, 0.0f);
    EXPECT_EQ(q.y, 0.0f);
    EXPECT_EQ(q.z, 0.0f);
    EXPECT_EQ(q.w, 1.0f);
}

TEST(QuatEquality, SameComponents_EqTrue) {
    Quat a{0.0f, 0.0f, 0.0f, 1.0f};
    Quat b{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_EQ(a, b);
}

TEST(QuatEquality, DifferentW_NeqTrue) {
    Quat a{0.0f, 0.0f, 0.0f, 1.0f};
    Quat b{0.0f, 0.0f, 0.0f, 0.5f};
    EXPECT_NE(a, b);
}

// ============================================================================
// quat_from_axis_angle
// ============================================================================

TEST(QuatFromAxisAngle, Ninety_DegreesAroundZ) {
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat q = quat_from_axis_angle(z, kHalfPi); // 90°

    // sin(45°) ≈ 0.7071, cos(45°) ≈ 0.7071
    EXPECT_NEAR(q.x, 0.0f,    kTol);
    EXPECT_NEAR(q.y, 0.0f,    kTol);
    EXPECT_NEAR(q.z, 0.7071f, 1e-4f);
    EXPECT_NEAR(q.w, 0.7071f, 1e-4f);
}

TEST(QuatFromAxisAngle, ZeroAngle_IsIdentity) {
    Vec3 axis{1.0f, 0.0f, 0.0f, 0.0f};
    Quat q = quat_from_axis_angle(axis, 0.0f);

    EXPECT_NEAR(q.x, 0.0f, kTol);
    EXPECT_NEAR(q.y, 0.0f, kTol);
    EXPECT_NEAR(q.z, 0.0f, kTol);
    EXPECT_NEAR(q.w, 1.0f, kTol);
}

TEST(QuatFromAxisAngle, HalfTurnAroundX_PureRotation) {
    Vec3 x{1.0f, 0.0f, 0.0f, 0.0f};
    Quat q = quat_from_axis_angle(x, kPi); // 180°

    // sin(90°) = 1, cos(90°) = 0
    EXPECT_NEAR(q.x, 1.0f, kTol);
    EXPECT_NEAR(q.y, 0.0f, kTol);
    EXPECT_NEAR(q.z, 0.0f, kTol);
    EXPECT_NEAR(q.w, 0.0f, kTol);
}

// ============================================================================
// quat_mul
// ============================================================================

TEST(QuatMul, IdentityTimesIdentity_IsIdentity) {
    Quat id  = quat_identity();
    Quat res = quat_mul(id, id);

    EXPECT_NEAR(res.x, 0.0f, kTol);
    EXPECT_NEAR(res.y, 0.0f, kTol);
    EXPECT_NEAR(res.z, 0.0f, kTol);
    EXPECT_NEAR(res.w, 1.0f, kTol);
}

TEST(QuatMul, IdentityTimesQ_IsQ) {
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat q   = quat_from_axis_angle(z, kHalfPi);
    Quat res = quat_mul(quat_identity(), q);

    EXPECT_NEAR(res.x, q.x, kTol);
    EXPECT_NEAR(res.y, q.y, kTol);
    EXPECT_NEAR(res.z, q.z, kTol);
    EXPECT_NEAR(res.w, q.w, kTol);
}

TEST(QuatMul, TwoNinety_DegreeTurns_AroundZ_Is180Degrees) {
    // 90° around Z * 90° around Z = 180° around Z
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat q90  = quat_from_axis_angle(z, kHalfPi);
    Quat q180 = quat_mul(q90, q90);

    // 180° around Z: x=0, y=0, z=sin(90°)=1, w=cos(90°)=0
    EXPECT_NEAR(q180.x, 0.0f, kTol);
    EXPECT_NEAR(q180.y, 0.0f, kTol);
    EXPECT_NEAR(q180.z, 1.0f, kTol);
    EXPECT_NEAR(std::abs(q180.w), 0.0f, kTol);
}

// ============================================================================
// quat_normalize
// ============================================================================

TEST(QuatNormalize, NonUnitQuat_ResultHasLengthOne) {
    Quat q{2.0f, 0.0f, 0.0f, 0.0f};
    Quat n = quat_normalize(q);
    float len = std::sqrt(n.x*n.x + n.y*n.y + n.z*n.z + n.w*n.w);

    EXPECT_NEAR(len, 1.0f, kTol);
    EXPECT_NEAR(n.x, 1.0f, kTol);
}

TEST(QuatNormalize, AlreadyUnitQuat_RemainsUnit) {
    Quat q = quat_identity();
    Quat n = quat_normalize(q);

    EXPECT_NEAR(n.w, 1.0f, kTol);
}

// ============================================================================
// quat_slerp
// ============================================================================

TEST(QuatSlerp, AtT0_ReturnsA) {
    Quat a = quat_identity();
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat b = quat_from_axis_angle(z, kHalfPi);

    Quat r = quat_slerp(a, b, 0.0f);

    EXPECT_NEAR(r.x, a.x, kTol);
    EXPECT_NEAR(r.y, a.y, kTol);
    EXPECT_NEAR(r.z, a.z, kTol);
    EXPECT_NEAR(r.w, a.w, kTol);
}

TEST(QuatSlerp, AtT1_ReturnsB) {
    Quat a = quat_identity();
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat b = quat_from_axis_angle(z, kHalfPi);

    Quat r = quat_slerp(a, b, 1.0f);

    EXPECT_NEAR(r.x, b.x, kTol);
    EXPECT_NEAR(r.y, b.y, kTol);
    EXPECT_NEAR(r.z, b.z, kTol);
    EXPECT_NEAR(r.w, b.w, 1e-4f);
}

TEST(QuatSlerp, NearlyIdenticalQuats_UsesLinearPath_ResultIsUnit) {
    // When dot > 0.9995, slerp uses linear interpolation.
    Quat a = quat_identity();
    // Very small rotation — dot(a,b) will be > 0.9995.
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat b = quat_from_axis_angle(z, 0.001f);

    Quat r = quat_slerp(a, b, 0.5f);
    float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);

    EXPECT_NEAR(len, 1.0f, kTol);
}

TEST(QuatSlerp, NegativeDot_TakesShortestPath) {
    // If dot < 0, slerp should negate b to take the shorter arc.
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat a =  quat_from_axis_angle(z, kHalfPi);
    Quat b = {-a.x, -a.y, -a.z, -a.w}; // same rotation, negated

    // Should not crash and result should be unit length.
    Quat r = quat_slerp(a, b, 0.5f);
    float len = std::sqrt(r.x*r.x + r.y*r.y + r.z*r.z + r.w*r.w);

    EXPECT_NEAR(len, 1.0f, kTol);
}

// ============================================================================
// quat_to_mat4 / Mat4::rotate
// ============================================================================

TEST(QuatToMat4, IdentityQuat_ProducesIdentityMatrix) {
    Mat4 m = quat_to_mat4(quat_identity());

    // Column-major: m[col][row]
    EXPECT_NEAR(m.m[0][0], 1.0f, kTol);
    EXPECT_NEAR(m.m[1][1], 1.0f, kTol);
    EXPECT_NEAR(m.m[2][2], 1.0f, kTol);
    EXPECT_NEAR(m.m[3][3], 1.0f, kTol);

    // Off-diagonals in the rotation part must be zero.
    EXPECT_NEAR(m.m[0][1], 0.0f, kTol);
    EXPECT_NEAR(m.m[1][0], 0.0f, kTol);
    EXPECT_NEAR(m.m[0][2], 0.0f, kTol);
    EXPECT_NEAR(m.m[2][0], 0.0f, kTol);
    EXPECT_NEAR(m.m[1][2], 0.0f, kTol);
    EXPECT_NEAR(m.m[2][1], 0.0f, kTol);
}

TEST(QuatToMat4, NinetyDegreesAroundZ_RotatesXtoY) {
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat q = quat_from_axis_angle(z, kHalfPi);
    Mat4 m = quat_to_mat4(q);

    // Rotating X axis (1,0,0) by 90° around Z should give approx (0,1,0).
    // m[0] is the transformed X column.
    EXPECT_NEAR(m.m[0][0],  0.0f, kTol); // new X.x ≈ 0
    EXPECT_NEAR(m.m[0][1],  1.0f, kTol); // new X.y ≈ 1
    EXPECT_NEAR(m.m[0][2],  0.0f, kTol); // new X.z ≈ 0
}

// ============================================================================
// quat_rotate_vec3
// ============================================================================

TEST(QuatRotateVec3, IdentityQuat_VectorUnchanged) {
    Vec3 v{1.0f, 2.0f, 3.0f, 0.0f};
    Vec3 r = quat_rotate_vec3(quat_identity(), v);

    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 2.0f, kTol);
    EXPECT_NEAR(r.z, 3.0f, kTol);
}

TEST(QuatRotateVec3, NinetyDegreesAroundZ_XBecomesY) {
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat q = quat_from_axis_angle(z, kHalfPi);
    Vec3 x{1.0f, 0.0f, 0.0f, 0.0f};

    Vec3 r = quat_rotate_vec3(q, x);

    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 1.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
}

TEST(QuatRotateVec3, NinetyDegreesAroundX_YBecomesNegZ) {
    Vec3 xaxis{1.0f, 0.0f, 0.0f, 0.0f};
    Quat q = quat_from_axis_angle(xaxis, kHalfPi);
    Vec3 y{0.0f, 1.0f, 0.0f, 0.0f};

    Vec3 r = quat_rotate_vec3(q, y);

    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, -1.0f, kTol); // right-handed: Y rotated by +90° around X = -Z
}

// ============================================================================
// Mat4 — size and static_assert
// ============================================================================

TEST(Mat4Size, SizeIs64Bytes) {
    EXPECT_EQ(sizeof(Mat4), 64u);
    EXPECT_EQ(alignof(Mat4), 16u);
}

// ============================================================================
// Mat4::identity
// ============================================================================

TEST(Mat4Identity, IdentityTimesIdentity_IsIdentity) {
    Mat4 id  = Mat4::identity();
    Mat4 res = id * id;

    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            float expected = (col == row) ? 1.0f : 0.0f;
            EXPECT_NEAR(res.m[col][row], expected, kTol)
                << "Identity*Identity mismatch at m[" << col << "][" << row << "]";
        }
    }
}

// ============================================================================
// Mat4::translate
// ============================================================================

TEST(Mat4Translate, TranslationStoredInColumn3) {
    Mat4 m = Mat4::translate({5.0f, 10.0f, 15.0f, 0.0f});

    // Column-major: m[3][0] = tx, m[3][1] = ty, m[3][2] = tz
    EXPECT_EQ(m.m[3][0], 5.0f);
    EXPECT_EQ(m.m[3][1], 10.0f);
    EXPECT_EQ(m.m[3][2], 15.0f);
    EXPECT_EQ(m.m[3][3], 1.0f);
}

TEST(Mat4Translate, TransformsPoint_AddsTranslation) {
    Mat4 t = Mat4::translate({1.0f, 2.0f, 3.0f, 0.0f});
    Vec3 p{0.0f, 0.0f, 0.0f, 0.0f};

    Vec3 r = vec3_transform(p, t);

    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 2.0f, kTol);
    EXPECT_NEAR(r.z, 3.0f, kTol);
}

TEST(Mat4Translate, TransformsNonOriginPoint) {
    Mat4 t = Mat4::translate({5.0f, 0.0f, 0.0f, 0.0f});
    Vec3 p{1.0f, 0.0f, 0.0f, 0.0f};

    Vec3 r = vec3_transform(p, t);

    EXPECT_NEAR(r.x, 6.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
}

// ============================================================================
// Mat4::scale
// ============================================================================

TEST(Mat4Scale, DiagonalContainsScaleValues) {
    Mat4 m = Mat4::scale({2.0f, 3.0f, 4.0f, 0.0f});

    EXPECT_EQ(m.m[0][0], 2.0f);
    EXPECT_EQ(m.m[1][1], 3.0f);
    EXPECT_EQ(m.m[2][2], 4.0f);
    EXPECT_EQ(m.m[3][3], 1.0f);

    // Off-diagonals in scale cols must be zero.
    EXPECT_EQ(m.m[0][1], 0.0f);
    EXPECT_EQ(m.m[1][0], 0.0f);
}

TEST(Mat4Scale, UniformScale_EnlargesPoint) {
    Mat4 s = Mat4::scale({3.0f, 3.0f, 3.0f, 0.0f});
    Vec3 p{1.0f, 1.0f, 1.0f, 0.0f};

    Vec3 r = vec3_transform(p, s);

    EXPECT_NEAR(r.x, 3.0f, kTol);
    EXPECT_NEAR(r.y, 3.0f, kTol);
    EXPECT_NEAR(r.z, 3.0f, kTol);
}

// ============================================================================
// Mat4 multiplication
// ============================================================================

TEST(Mat4Mul, TwoKnownMatrices_MatchManualResult) {
    // Translate(1,0,0) * Translate(2,0,0) should equal Translate(3,0,0).
    // Because column-major: T1*T2 means apply T2 first, then T1.
    // But for translation matrices:
    //   translate(a) * translate(b) = translate(a+b)
    Mat4 t1 = Mat4::translate({1.0f, 0.0f, 0.0f, 0.0f});
    Mat4 t2 = Mat4::translate({2.0f, 0.0f, 0.0f, 0.0f});
    Mat4 r  = t1 * t2;

    // The combined translation in column 3 row 0.
    EXPECT_NEAR(r.m[3][0], 3.0f, kTol);
    EXPECT_NEAR(r.m[3][1], 0.0f, kTol);
    EXPECT_NEAR(r.m[3][2], 0.0f, kTol);
}

TEST(Mat4Mul, ScaleThenTranslate_CorrectResult) {
    // scale(2) * translate(1,0,0): apply translate first, then scale.
    Mat4 s = Mat4::scale({2.0f, 2.0f, 2.0f, 0.0f});
    Mat4 t = Mat4::translate({1.0f, 0.0f, 0.0f, 0.0f});
    Mat4 r = s * t;

    // Transform (0,0,0): translate → (1,0,0) → scale → (2,0,0)
    Vec3 result = vec3_transform({0.0f, 0.0f, 0.0f, 0.0f}, r);

    EXPECT_NEAR(result.x, 2.0f, kTol);
    EXPECT_NEAR(result.y, 0.0f, kTol);
    EXPECT_NEAR(result.z, 0.0f, kTol);
}

// ============================================================================
// Mat4::perspective
// ============================================================================

TEST(Mat4Perspective, ProjectionMatrix_NonZeroDepthEntries) {
    Mat4 p = Mat4::perspective(kHalfPi, 16.0f / 9.0f, 0.1f, 1000.0f);

    // For right-handed, depth [0..1]: m[2][2] and m[3][2] must be non-zero.
    EXPECT_NE(p.m[2][2], 0.0f);
    EXPECT_NE(p.m[3][2], 0.0f);

    // m[2][3] must be -1 (the homogeneous divide row).
    EXPECT_NEAR(p.m[2][3], -1.0f, kTol);

    // m[3][3] must be 0 (perspective projection).
    EXPECT_NEAR(p.m[3][3], 0.0f, kTol);
}

TEST(Mat4Perspective, FovY_90deg_Aspect1_X_Equals_Y_Scale) {
    // At 90° fov and aspect=1.0, x and y scale factors should be equal.
    Mat4 p = Mat4::perspective(kHalfPi, 1.0f, 0.1f, 100.0f);

    EXPECT_NEAR(p.m[0][0], p.m[1][1], kTol);
}

// ============================================================================
// Mat4::look_at
// ============================================================================

TEST(Mat4LookAt, LookingAlongNegZ_ForwardIsNegZ) {
    // eye at (0,0,-5) looking at origin with up=(0,1,0).
    // In this engine: f = normalize(target - eye) = normalize((0,0,0)-(0,0,-5)) = (0,0,1)
    // The third column of look_at contains -f components in the z row.
    Mat4 m = Mat4::look_at(
        {0.0f, 0.0f, -5.0f, 0.0f},
        {0.0f, 0.0f,  0.0f, 0.0f},
        {0.0f, 1.0f,  0.0f, 0.0f}
    );

    // m[2][2] = -f.z where f=(0,0,1), so m[2][2] = -1
    EXPECT_NEAR(m.m[2][2], -1.0f, kTol);

    // Right direction (X) must be (1,0,0) for standard up=(0,1,0).
    EXPECT_NEAR(m.m[0][0], 1.0f, kTol);
    EXPECT_NEAR(m.m[0][1], 0.0f, kTol);
    EXPECT_NEAR(m.m[0][2], 0.0f, kTol);
}

TEST(Mat4LookAt, LastColumnIsHomogeneous1) {
    Mat4 m = Mat4::look_at(
        {0.0f, 0.0f, -5.0f, 0.0f},
        {0.0f, 0.0f,  0.0f, 0.0f},
        {0.0f, 1.0f,  0.0f, 0.0f}
    );

    EXPECT_NEAR(m.m[3][3], 1.0f, kTol);
}

// ============================================================================
// vec3_transform
// ============================================================================

TEST(Vec3Transform, TranslationMatrix_AddsOffset) {
    Vec3 result = vec3_transform(
        {1.0f, 0.0f, 0.0f, 0.0f},
        Mat4::translate({5.0f, 0.0f, 0.0f, 0.0f})
    );

    EXPECT_NEAR(result.x, 6.0f, kTol);
    EXPECT_NEAR(result.y, 0.0f, kTol);
    EXPECT_NEAR(result.z, 0.0f, kTol);
}

TEST(Vec3Transform, IdentityMatrix_VectorUnchanged) {
    Vec3 v{3.0f, 4.0f, 5.0f, 0.0f};

    Vec3 r = vec3_transform(v, Mat4::identity());

    EXPECT_NEAR(r.x, 3.0f, kTol);
    EXPECT_NEAR(r.y, 4.0f, kTol);
    EXPECT_NEAR(r.z, 5.0f, kTol);
}

// ============================================================================
// vec3_transform_dir
// ============================================================================

TEST(Vec3TransformDir, TranslationMatrix_NoTranslationApplied) {
    Vec3 dir{1.0f, 0.0f, 0.0f, 0.0f};
    Mat4 t = Mat4::translate({5.0f, 10.0f, 15.0f, 0.0f});

    Vec3 r = vec3_transform_dir(dir, t);

    // Direction must not be translated.
    EXPECT_NEAR(r.x, 1.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
}

TEST(Vec3TransformDir, ScaleMatrix_ScalesDirection) {
    Vec3 dir{1.0f, 0.0f, 0.0f, 0.0f};
    Mat4 s = Mat4::scale({3.0f, 2.0f, 1.0f, 0.0f});

    Vec3 r = vec3_transform_dir(dir, s);

    EXPECT_NEAR(r.x, 3.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 0.0f, kTol);
}

TEST(Vec3TransformDir, IdentityMatrix_DirectionUnchanged) {
    Vec3 dir{0.0f, 0.0f, 1.0f, 0.0f};

    Vec3 r = vec3_transform_dir(dir, Mat4::identity());

    EXPECT_NEAR(r.x, 0.0f, kTol);
    EXPECT_NEAR(r.y, 0.0f, kTol);
    EXPECT_NEAR(r.z, 1.0f, kTol);
}

// ============================================================================
// Mat4::rotate — from quaternion
// ============================================================================

TEST(Mat4Rotate, IdentityQuat_ProducesIdentityRotation) {
    Mat4 m = Mat4::rotate(quat_identity());

    EXPECT_NEAR(m.m[0][0], 1.0f, kTol);
    EXPECT_NEAR(m.m[1][1], 1.0f, kTol);
    EXPECT_NEAR(m.m[2][2], 1.0f, kTol);
    EXPECT_NEAR(m.m[3][3], 1.0f, kTol);
    EXPECT_NEAR(m.m[0][1], 0.0f, kTol);
    EXPECT_NEAR(m.m[0][2], 0.0f, kTol);
}

TEST(Mat4Rotate, Quaternion90AroundZ_MatchesExpectedMatrix) {
    Vec3 z{0.0f, 0.0f, 1.0f, 0.0f};
    Quat q = quat_from_axis_angle(z, kHalfPi);
    Mat4 m = Mat4::rotate(q);

    // Column 0 should be approx (0, 1, 0, 0) — the rotated X axis.
    EXPECT_NEAR(m.m[0][0],  0.0f, kTol);
    EXPECT_NEAR(m.m[0][1],  1.0f, kTol);
    EXPECT_NEAR(m.m[0][2],  0.0f, kTol);
    // Column 1 should be approx (-1, 0, 0, 0) — the rotated Y axis.
    EXPECT_NEAR(m.m[1][0], -1.0f, kTol);
    EXPECT_NEAR(m.m[1][1],  0.0f, kTol);
    EXPECT_NEAR(m.m[1][2],  0.0f, kTol);
}
