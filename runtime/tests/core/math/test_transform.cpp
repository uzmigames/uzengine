#include "engine_math.h"

#include <gtest/gtest.h>

using namespace uz;

TEST(TransformTest, IdentityIsDefault) {
    Transform t;
    EXPECT_TRUE(t.position == Vec3::Zero());
    EXPECT_TRUE(t.rotation == Quat::Identity());
    EXPECT_TRUE(t.scale == Vec3::One());
}

TEST(TransformTest, ToMatrixIdentity) {
    Transform t;
    Mat4 m = t.ToMatrix();
    EXPECT_TRUE(m == Mat4::Identity());
}

TEST(TransformTest, TranslationOnly) {
    Transform t;
    t.position = Vec3(10.0f, 20.0f, 30.0f);
    Vec3 result = t.TransformPoint(Vec3::Zero());
    EXPECT_NEAR(result.X(), 10.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 20.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 30.0f, 1e-5f);
}

TEST(TransformTest, ScaleOnly) {
    Transform t;
    t.scale = Vec3(2.0f, 3.0f, 4.0f);
    Vec3 result = t.TransformPoint(Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_NEAR(result.X(), 2.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 3.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 4.0f, 1e-5f);
}

TEST(TransformTest, RotationOnly) {
    Transform t;
    t.rotation = Quat::FromAxisAngle(Vec3::UnitY(), XM_PIDIV2);
    Vec3 result = t.TransformPoint(Vec3(0.0f, 0.0f, 1.0f));
    EXPECT_NEAR(result.X(), 1.0f, 1e-4f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-4f);
    EXPECT_NEAR(result.Z(), 0.0f, 1e-4f);
}

TEST(TransformTest, TransformDirection) {
    Transform t;
    t.position = Vec3(100.0f, 200.0f, 300.0f);
    t.rotation = Quat::Identity();
    Vec3 dir = t.TransformDirection(Vec3(1.0f, 0.0f, 0.0f));
    // Direction ignores position
    EXPECT_NEAR(dir.X(), 1.0f, 1e-5f);
    EXPECT_NEAR(dir.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(dir.Z(), 0.0f, 1e-5f);
}

TEST(TransformTest, Inverse) {
    Transform t(Vec3(5.0f, 10.0f, 15.0f), Quat::FromAxisAngle(Vec3::UnitY(), 1.0f),
                Vec3(2.0f, 2.0f, 2.0f));
    Transform inv = t.Inverse();
    Vec3 original(1.0f, 2.0f, 3.0f);
    Vec3 transformed = t.TransformPoint(original);
    Vec3 restored = inv.TransformPoint(transformed);
    EXPECT_NEAR(restored.X(), original.X(), 1e-3f);
    EXPECT_NEAR(restored.Y(), original.Y(), 1e-3f);
    EXPECT_NEAR(restored.Z(), original.Z(), 1e-3f);
}

TEST(TransformTest, Lerp) {
    Transform a(Vec3::Zero(), Quat::Identity(), Vec3::One());
    Transform b(Vec3(10.0f, 0.0f, 0.0f), Quat::Identity(), Vec3(3.0f, 3.0f, 3.0f));
    Transform mid = Transform::Lerp(a, b, 0.5f);
    EXPECT_NEAR(mid.position.X(), 5.0f, 1e-5f);
    EXPECT_NEAR(mid.scale.X(), 2.0f, 1e-5f);
}
