/// Unit tests for Quat new methods — log, exp, angle_to, get_euler,
/// get_axis, get_angle, spherical_cubic_interpolate.
#include "quaternion.h"
#include "mat4.h"

#include <gtest/gtest.h>

using namespace uz;

static constexpr float kEps  = 1e-4f;
static constexpr float kEps3 = 1e-3f;

// ── log / exp (round-trip) ───────────────────────────────────────────────────

TEST(QuatMethodsTest, Log_IdentityQuaternion_ReturnsZeroQuat) {
    Quat id = Quat::Identity();
    Quat logq = id.log();
    // log(identity) = (0, 0, 0, 0)
    EXPECT_NEAR(logq.x, 0.0f, kEps);
    EXPECT_NEAR(logq.y, 0.0f, kEps);
    EXPECT_NEAR(logq.z, 0.0f, kEps);
    EXPECT_NEAR(logq.w, 0.0f, kEps);
}

TEST(QuatMethodsTest, Exp_ZeroQuat_ReturnsIdentity) {
    Quat zero(0.0f, 0.0f, 0.0f, 0.0f);
    Quat expq = zero.exp();
    EXPECT_NEAR(expq.x, 0.0f, kEps);
    EXPECT_NEAR(expq.y, 0.0f, kEps);
    EXPECT_NEAR(expq.z, 0.0f, kEps);
    EXPECT_NEAR(expq.w, 1.0f, kEps);
}

TEST(QuatMethodsTest, LogThenExp_RoundTrip_RecreatesOriginal) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitY(), 1.2f);
    Quat roundtrip = q.log().exp();
    // Round-trip should recover q (or -q which represents same rotation)
    bool positive = std::abs(roundtrip.x - q.x) < kEps3 &&
                    std::abs(roundtrip.y - q.y) < kEps3 &&
                    std::abs(roundtrip.z - q.z) < kEps3 &&
                    std::abs(roundtrip.w - q.w) < kEps3;
    bool negative = std::abs(roundtrip.x + q.x) < kEps3 &&
                    std::abs(roundtrip.y + q.y) < kEps3 &&
                    std::abs(roundtrip.z + q.z) < kEps3 &&
                    std::abs(roundtrip.w + q.w) < kEps3;
    EXPECT_TRUE(positive || negative)
        << "log->exp round-trip failed for axis-angle rotation";
}

TEST(QuatMethodsTest, Log_SmallRotation_WComponentIsZero) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitZ(), 0.1f);
    Quat logq = q.log();
    // log is a pure quaternion — w component should be zero
    EXPECT_NEAR(logq.w, 0.0f, kEps);
}

TEST(QuatMethodsTest, Exp_ReturnedQuaternion_IsUnitLength) {
    Quat pure(0.3f, 0.4f, 0.0f, 0.0f);
    Quat expq = pure.exp();
    EXPECT_NEAR(expq.length(), 1.0f, kEps);
}

// ── angle_to ─────────────────────────────────────────────────────────────────

TEST(QuatMethodsTest, AngleTo_SameQuaternion_IsZero) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitX(), 1.5f);
    EXPECT_NEAR(q.angle_to(q), 0.0f, kEps);
}

TEST(QuatMethodsTest, AngleTo_IdentityTo90DegRotation_IsHalfPi) {
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), Math::PI / 2.0f);
    EXPECT_NEAR(a.angle_to(b), Math::PI / 2.0f, kEps);
}

TEST(QuatMethodsTest, AngleTo_IdentityTo180DegRotation_IsPi) {
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), Math::PI);
    EXPECT_NEAR(a.angle_to(b), Math::PI, kEps);
}

TEST(QuatMethodsTest, AngleTo_IsSymmetric) {
    Quat a = Quat::FromAxisAngle(Vec3::UnitX(), 0.8f);
    Quat b = Quat::FromAxisAngle(Vec3::UnitZ(), 1.2f);
    EXPECT_NEAR(a.angle_to(b), b.angle_to(a), kEps);
}

TEST(QuatMethodsTest, AngleTo_ResultAlwaysNonNegative) {
    Quat a = Quat::FromAxisAngle(Vec3::UnitY(), 2.0f);
    Quat b = Quat::FromAxisAngle(Vec3::UnitX(), -1.0f);
    EXPECT_GE(a.angle_to(b), 0.0f);
}

// ── get_euler ────────────────────────────────────────────────────────────────

TEST(QuatMethodsTest, GetEuler_IdentityQuaternion_ReturnsZero) {
    Quat q = Quat::Identity();
    Vec3 euler = q.get_euler();
    EXPECT_NEAR(euler.x, 0.0f, kEps);
    EXPECT_NEAR(euler.y, 0.0f, kEps);
    EXPECT_NEAR(euler.z, 0.0f, kEps);
}

TEST(QuatMethodsTest, GetEuler_PureYawRotation_ReturnsYawOnly) {
    float yaw = Math::PI / 4.0f;  // 45 degrees around Y
    Quat q = Quat::FromEuler(0.0f, yaw, 0.0f);
    Vec3 euler = q.get_euler();
    EXPECT_NEAR(euler.y, yaw, kEps);
    EXPECT_NEAR(euler.x, 0.0f, kEps);
    EXPECT_NEAR(euler.z, 0.0f, kEps);
}

TEST(QuatMethodsTest, GetEuler_PurePitchRotation_ReturnsPitchOnly) {
    float pitch = Math::PI / 6.0f;  // 30 degrees around X
    Quat q = Quat::FromEuler(pitch, 0.0f, 0.0f);
    Vec3 euler = q.get_euler();
    EXPECT_NEAR(euler.x, pitch, kEps);
    EXPECT_NEAR(euler.y, 0.0f, kEps);
    EXPECT_NEAR(euler.z, 0.0f, kEps);
}

// ── get_axis ─────────────────────────────────────────────────────────────────

TEST(QuatMethodsTest, GetAxis_AxisAngleRoundTrip_RecreatesAxis) {
    Vec3 axis = Vec3(0.0f, 1.0f, 0.0f);
    float angle = 1.5f;
    Quat q = Quat::FromAxisAngle(axis, angle);
    Vec3 extracted = q.get_axis();
    EXPECT_NEAR(extracted.x, axis.x, kEps);
    EXPECT_NEAR(extracted.y, axis.y, kEps);
    EXPECT_NEAR(extracted.z, axis.z, kEps);
}

TEST(QuatMethodsTest, GetAxis_IdentityQuaternion_ReturnsDefaultAxis) {
    Quat q = Quat::Identity();
    Vec3 axis = q.get_axis();
    // Identity has no meaningful axis — should return a valid unit vector
    EXPECT_NEAR(axis.length(), 1.0f, kEps);
}

TEST(QuatMethodsTest, GetAxis_NegativeAxisAngle_NormalizesAxis) {
    Vec3 axis = Vec3(0.0f, 0.0f, -1.0f);
    Quat q = Quat::FromAxisAngle(axis, 1.0f);
    Vec3 extracted = q.get_axis();
    EXPECT_NEAR(extracted.length(), 1.0f, kEps);
}

// ── get_angle ────────────────────────────────────────────────────────────────

TEST(QuatMethodsTest, GetAngle_IdentityQuaternion_ReturnsZero) {
    Quat q = Quat::Identity();
    EXPECT_NEAR(q.get_angle(), 0.0f, kEps);
}

TEST(QuatMethodsTest, GetAngle_HalfPiRotation_ReturnsHalfPi) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitY(), Math::PI / 2.0f);
    EXPECT_NEAR(q.get_angle(), Math::PI / 2.0f, kEps);
}

TEST(QuatMethodsTest, GetAngle_FullPiRotation_ReturnsPi) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitZ(), Math::PI);
    // get_angle returns value in [0, 2*pi]; pi rotation is pi
    EXPECT_NEAR(q.get_angle(), Math::PI, kEps);
}

TEST(QuatMethodsTest, GetAngle_ArbitraryRotation_MatchesAxisAngleInput) {
    float input_angle = 1.23f;
    Quat q = Quat::FromAxisAngle(Vec3::UnitX(), input_angle);
    EXPECT_NEAR(q.get_angle(), input_angle, kEps);
}

// ── spherical_cubic_interpolate ──────────────────────────────────────────────

TEST(QuatMethodsTest, SphericalCubicInterpolate_AtZero_ReturnsThis) {
    Quat a = Quat::FromAxisAngle(Vec3::UnitY(), 0.0f);
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), 1.5f);
    Quat pre  = Quat::FromAxisAngle(Vec3::UnitY(), -0.5f);
    Quat post = Quat::FromAxisAngle(Vec3::UnitY(), 2.0f);
    Quat r = a.spherical_cubic_interpolate(b, pre, post, 0.0f);
    EXPECT_NEAR(r.x, a.x, kEps3);
    EXPECT_NEAR(r.y, a.y, kEps3);
    EXPECT_NEAR(r.z, a.z, kEps3);
    EXPECT_NEAR(r.w, a.w, kEps3);
}

TEST(QuatMethodsTest, SphericalCubicInterpolate_AtOne_ReturnsB) {
    Quat a = Quat::FromAxisAngle(Vec3::UnitY(), 0.0f);
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), 1.5f);
    Quat pre  = Quat::FromAxisAngle(Vec3::UnitY(), -0.5f);
    Quat post = Quat::FromAxisAngle(Vec3::UnitY(), 2.0f);
    Quat r = a.spherical_cubic_interpolate(b, pre, post, 1.0f);
    bool positive = std::abs(r.x - b.x) < kEps3 && std::abs(r.y - b.y) < kEps3 &&
                    std::abs(r.z - b.z) < kEps3 && std::abs(r.w - b.w) < kEps3;
    bool negative = std::abs(r.x + b.x) < kEps3 && std::abs(r.y + b.y) < kEps3 &&
                    std::abs(r.z + b.z) < kEps3 && std::abs(r.w + b.w) < kEps3;
    EXPECT_TRUE(positive || negative)
        << "spherical_cubic_interpolate at t=1 should return b (or -b)";
}

TEST(QuatMethodsTest, SphericalCubicInterpolate_ResultIsUnitLength) {
    Quat a = Quat::FromAxisAngle(Vec3::UnitX(), 0.3f);
    Quat b = Quat::FromAxisAngle(Vec3::UnitZ(), 1.2f);
    Quat pre  = Quat::FromAxisAngle(Vec3::UnitY(), -0.2f);
    Quat post = Quat::FromAxisAngle(Vec3::UnitY(), 1.8f);
    for (float t : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        Quat r = a.spherical_cubic_interpolate(b, pre, post, t);
        EXPECT_NEAR(r.length(), 1.0f, kEps3) << "at t=" << t;
    }
}

TEST(QuatMethodsTest, SphericalCubicInterpolate_SameQuats_ReturnsUnchanged) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitY(), 0.8f);
    Quat r = q.spherical_cubic_interpolate(q, q, q, 0.5f);
    bool positive = std::abs(r.x - q.x) < kEps3 && std::abs(r.w - q.w) < kEps3;
    bool negative = std::abs(r.x + q.x) < kEps3 && std::abs(r.w + q.w) < kEps3;
    EXPECT_TRUE(positive || negative)
        << "interpolating between identical quaternions should return the same quaternion";
}
