/// Unit tests for Quat — identity, axis-angle construction, Hamilton product,
/// slerp/lerp interpolation, to-matrix conversion, and normalisation.
#include "quaternion.h"
#include "mat4.h"

#include <gtest/gtest.h>

using namespace uz;
using namespace DirectX;

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
    // UE5-parity convention: pitch=Y-rotation, yaw=Z-rotation, roll=X-rotation.
    // Noon sun: pitch=-90° elevates the forward vector [1,0,0] to [0,0,1] (zenith).
    Quat q = Quat::FromEuler(-XM_PIDIV2, 0.0f, 0.0f); // pitch=-90° (noon elevation)
    Vec3 fwd(1.0f, 0.0f, 0.0f);                        // Vec3::Forward()
    Vec3 result = q.RotateVector(fwd);
    EXPECT_NEAR(result.X(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Z(), 1.0f, 1e-4f);

    // Yaw=90° (Z-rotation) rotates forward [1,0,0] to North [0,1,0].
    Quat qYaw = Quat::FromEuler(0.0f, XM_PIDIV2, 0.0f);
    Vec3 resultYaw = qYaw.RotateVector(fwd);
    EXPECT_NEAR(resultYaw.X(), 0.0f, 1e-4f);
    EXPECT_NEAR(resultYaw.Y(), 1.0f, 1e-4f);
    EXPECT_NEAR(resultYaw.Z(), 0.0f, 1e-4f);
}

TEST(QuatTest, FromEulerCombinedPitchYawPreservesElevation) {
    const Quat q = Quat::FromEuler(XMConvertToRadians(-56.0f), XMConvertToRadians(79.0f), 0.0f);
    const Vec3 direction = q * Vec3::Forward();
    EXPECT_NEAR(direction.x, 0.106699036f, 1e-5f);
    EXPECT_NEAR(direction.y, 0.548918955f, 1e-5f);
    EXPECT_NEAR(direction.z, 0.829037573f, 1e-5f);
}

TEST(QuatTest, FromEulerZenithRemainsUpAfterYaw) {
    const Vec3 direction = Quat::FromEuler(-XM_PIDIV2, XM_PIDIV2, 0.0f) * Vec3::Forward();
    EXPECT_NEAR(direction.x, 0.0f, 1e-5f);
    EXPECT_NEAR(direction.y, 0.0f, 1e-5f);
    EXPECT_NEAR(direction.z, 1.0f, 1e-5f);
}

TEST(QuatTest, FromEulerCombinedAxesRoundTrip) {
    const Vec3 angles[] = {{-0.977384381f, 1.378810109f, 0.0f},
                           {0.37f, -0.82f, 0.51f}, {-0.6f, 0.4f, -0.3f}};
    for (const Vec3& expected : angles) {
        const Vec3 actual = Quat::FromEuler(expected.x, expected.y, expected.z).get_euler();
        EXPECT_NEAR(actual.x, expected.x, 1e-5f);
        EXPECT_NEAR(actual.y, expected.y, 1e-5f);
        EXPECT_NEAR(actual.z, expected.z, 1e-5f);
    }
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
