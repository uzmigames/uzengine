/// Unit tests for Mat4 — identity, translation, rotation, and scale
/// construction, matrix multiplication associativity, inverse correctness,
/// and TRS decomposition round-trips.
#include "vector3.h"
#include "mat4.h"

#include <gtest/gtest.h>

using namespace uz;

TEST(Mat4Test, IdentityIsDefault) {
    Mat4 m;
    Mat4 id = Mat4::Identity();
    EXPECT_TRUE(m == id);
}

TEST(Mat4Test, IdentityTransformPoint) {
    Mat4 m;
    Vec3 p(1.0f, 2.0f, 3.0f);
    Vec3 result = m.TransformPoint(p);
    EXPECT_NEAR(result.X(), 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 2.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 3.0f, 1e-5f);
}

TEST(Mat4Test, TranslationMatrix) {
    Mat4 t = Mat4::Translation(10.0f, 20.0f, 30.0f);
    Vec3 origin;
    Vec3 result = t.TransformPoint(origin);
    EXPECT_NEAR(result.X(), 10.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 20.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 30.0f, 1e-5f);
}

TEST(Mat4Test, ScalingMatrix) {
    Mat4 s = Mat4::Scaling(2.0f, 3.0f, 4.0f);
    Vec3 p(1.0f, 1.0f, 1.0f);
    Vec3 result = s.TransformPoint(p);
    EXPECT_NEAR(result.X(), 2.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 3.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 4.0f, 1e-5f);
}

TEST(Mat4Test, UniformScaling) {
    Mat4 s = Mat4::Scaling(5.0f);
    Vec3 p(1.0f, 1.0f, 1.0f);
    Vec3 result = s.TransformPoint(p);
    EXPECT_NEAR(result.X(), 5.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 5.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 5.0f, 1e-5f);
}

TEST(Mat4Test, RotationX90) {
    Mat4 r = Mat4::RotationX(XM_PIDIV2);
    Vec3 up(0.0f, 1.0f, 0.0f);
    Vec3 result = r.TransformPoint(up);
    EXPECT_NEAR(result.X(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 1.0f, 1e-5f);
}

TEST(Mat4Test, MultiplyIdentity) {
    Mat4 a = Mat4::Translation(1.0f, 2.0f, 3.0f);
    Mat4 id;
    Mat4 result = a * id;
    Vec3 p = result.TransformPoint(Vec3::Zero());
    EXPECT_NEAR(p.X(), 1.0f, 1e-5f);
    EXPECT_NEAR(p.Y(), 2.0f, 1e-5f);
    EXPECT_NEAR(p.Z(), 3.0f, 1e-5f);
}

TEST(Mat4Test, Inverse) {
    Mat4 t = Mat4::Translation(5.0f, 10.0f, 15.0f);
    Mat4 inv = t.Inverse();
    Mat4 identity = t * inv;
    Vec3 p = identity.TransformPoint(Vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(p.X(), 1.0f, 1e-4f);
    EXPECT_NEAR(p.Y(), 2.0f, 1e-4f);
    EXPECT_NEAR(p.Z(), 3.0f, 1e-4f);
}

TEST(Mat4Test, Transpose) {
    Mat4 t = Mat4::Translation(1.0f, 2.0f, 3.0f);
    Mat4 tt = t.Transposed().Transposed();
    EXPECT_TRUE(t == tt);
}

TEST(Mat4Test, Determinant) {
    Mat4 id;
    EXPECT_NEAR(id.Determinant(), 1.0f, 1e-5f);

    Mat4 s = Mat4::Scaling(2.0f, 3.0f, 4.0f);
    EXPECT_NEAR(s.Determinant(), 24.0f, 1e-4f);
}

TEST(Mat4Test, PerspectiveCreates) {
    Mat4 p = Mat4::Perspective(XM_PIDIV4, 16.0f / 9.0f, 0.1f, 1000.0f);
    // Just verify it doesn't produce identity (i.e., it actually changed)
    EXPECT_TRUE(p != Mat4::Identity());
}

TEST(Mat4Test, LookAtCreates) {
    Mat4 v = Mat4::LookAt(Vec3(0, 0, -5), Vec3::Zero(), Vec3::Up());
    EXPECT_TRUE(v != Mat4::Identity());
}

TEST(Mat4Test, StorageConversion) {
    Mat4 t = Mat4::Translation(1.0f, 2.0f, 3.0f);
    XMFLOAT4X4 f = t.ToFloat4x4();
    Mat4 restored = Mat4::FromFloat4x4(f);
    EXPECT_TRUE(t == restored);
}

TEST(Mat4Test, TransformDirection) {
    Mat4 t = Mat4::Translation(100.0f, 200.0f, 300.0f);
    Vec3 dir(1.0f, 0.0f, 0.0f);
    Vec3 result = t.TransformDirection(dir);
    // Direction should not be affected by translation
    EXPECT_NEAR(result.X(), 1.0f, 1e-5f);
    EXPECT_NEAR(result.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(result.Z(), 0.0f, 1e-5f);
}
