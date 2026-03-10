#include "uz_math.h"

#include <gtest/gtest.h>

using namespace uz;

TEST(QuatTest, IdentityIsDefault) {
    Quat q;
    EXPECT_FLOAT_EQ(q.X(), 0.0f);
    EXPECT_FLOAT_EQ(q.Y(), 0.0f);
    EXPECT_FLOAT_EQ(q.Z(), 0.0f);
    EXPECT_FLOAT_EQ(q.W(), 1.0f);
}

TEST(QuatTest, IdentityRotatesNothing) {
    Quat q;
    Vec3 v(1.0f, 2.0f, 3.0f);
    Vec3 result = q.RotateVector(v);
    EXPECT_NEAR(result.X(), 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 3.0f, 1e-5f);
}

TEST(QuatTest, RotateVector90AroundY) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitY(), XM_PIDIV2);
    Vec3 forward(0.0f, 0.0f, 1.0f);
    Vec3 result = q.RotateVector(forward);
    EXPECT_NEAR(result.X(), 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 0.0f, 1e-4f);
}

TEST(QuatTest, Multiply) {
    Quat a = Quat::FromAxisAngle(Vec3::UnitY(), XM_PIDIV2);
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), XM_PIDIV2);
    Quat combined = a * b;
    // Two 90-degree rotations = 180-degree rotation
    Vec3 forward(0.0f, 0.0f, 1.0f);
    Vec3 result = combined.RotateVector(forward);
    EXPECT_NEAR(result.X(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Z(), -1.0f, 1e-4f);
}

TEST(QuatTest, Inverse) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitY(), 1.0f);
    Quat inv = q.Inverse();
    Quat identity = q * inv;
    EXPECT_NEAR(identity.W(), 1.0f, 1e-5f);
    EXPECT_NEAR(identity.X(), 0.0f, 1e-5f);
    EXPECT_NEAR(identity.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(identity.Z(), 0.0f, 1e-5f);
}

TEST(QuatTest, Slerp) {
    Quat a = Quat::Identity();
    Quat b = Quat::FromAxisAngle(Vec3::UnitY(), XM_PIDIV2); // 90 degrees
    Quat mid = Quat::Slerp(a, b, 0.5f);
    // Should be 45-degree rotation around Y
    Vec3 forward(0.0f, 0.0f, 1.0f);
    Vec3 result = mid.RotateVector(forward);
    // At 45 degrees: z=cos(45)=0.707, x=sin(45)=0.707
    EXPECT_NEAR(result.X(), 0.707f, 0.01f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Z(), 0.707f, 0.01f);
}

TEST(QuatTest, FromEuler) {
    Quat q = Quat::FromEuler(0.0f, XM_PIDIV2, 0.0f); // 90 yaw
    Vec3 forward(0.0f, 0.0f, 1.0f);
    Vec3 result = q.RotateVector(forward);
    EXPECT_NEAR(result.X(), 1.0f, 1e-4f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Z(), 0.0f, 1e-4f);
}

TEST(QuatTest, ToMatrix) {
    Quat q = Quat::FromAxisAngle(Vec3::UnitZ(), XM_PIDIV2);
    Mat4 m = q.ToMatrix();
    Vec3 right(1.0f, 0.0f, 0.0f);
    Vec3 result = m.TransformPoint(right);
    EXPECT_NEAR(result.X(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 1.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 0.0f, 1e-5f);
}

TEST(QuatTest, FromMatrix) {
    Quat original = Quat::FromAxisAngle(Vec3::UnitY(), 1.0f);
    Mat4 m = original.ToMatrix();
    Quat restored = Quat::FromMatrix(m);
    EXPECT_NEAR(original.X(), restored.X(), 1e-5f);
    EXPECT_NEAR(original.Y(), restored.Y(), 1e-5f);
    EXPECT_NEAR(original.Z(), restored.Z(), 1e-5f);
    EXPECT_NEAR(original.W(), restored.W(), 1e-5f);
}

TEST(QuatTest, Length) {
    Quat q = Quat::Identity();
    EXPECT_NEAR(q.Length(), 1.0f, 1e-5f);
}

TEST(QuatTest, Normalize) {
    Quat q(1.0f, 2.0f, 3.0f, 4.0f);
    Quat n = q.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, 1e-5f);
}
