/// Unit tests for geometric bounds — Bounds3 construction and expansion,
/// point/Bounds3 intersection, Plane signed-distance, and Frustum culling
/// against Bounds3 and sphere primitives.
#include "geometry.h"

#include <gtest/gtest.h>

using namespace uz;
using namespace DirectX;

// ── Bounds3 ─────────────────────────────────────────────────────────────────────

TEST(Bounds3Test, ContainsPoint) {
    Bounds3 box(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(box.Contains(Vec3(0, 0, 0)));
    EXPECT_TRUE(box.Contains(Vec3(1, 1, 1)));
    EXPECT_FALSE(box.Contains(Vec3(2, 0, 0)));
}

TEST(Bounds3Test, IntersectsOverlapping) {
    Bounds3 a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    Bounds3 b(Vec3(0, 0, 0), Vec3(2, 2, 2));
    EXPECT_TRUE(a.Intersects(b));
    EXPECT_TRUE(b.Intersects(a));
}

TEST(Bounds3Test, IntersectsSeparated) {
    Bounds3 a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    Bounds3 b(Vec3(5, 5, 5), Vec3(6, 6, 6));
    EXPECT_FALSE(a.Intersects(b));
}

TEST(Bounds3Test, Merge) {
    Bounds3 a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    Bounds3 b(Vec3(0, 0, 0), Vec3(3, 3, 3));
    Bounds3 merged = a.Merge(b);
    EXPECT_FLOAT_EQ(merged.min.X(), -1.0f);
    EXPECT_FLOAT_EQ(merged.max.X(), 3.0f);
}

TEST(Bounds3Test, CenterAndExtents) {
    Bounds3 box(Vec3(-2, -4, -6), Vec3(2, 4, 6));
    Vec3 center = box.Center();
    Vec3 extents = box.Extents();
    EXPECT_NEAR(center.X(), 0.0f, 1e-5f);
    EXPECT_NEAR(center.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(center.Z(), 0.0f, 1e-5f);
    EXPECT_NEAR(extents.X(), 2.0f, 1e-5f);
    EXPECT_NEAR(extents.Y(), 4.0f, 1e-5f);
    EXPECT_NEAR(extents.Z(), 6.0f, 1e-5f);
}

TEST(Bounds3Test, Expand) {
    Bounds3 box(Vec3(0, 0, 0), Vec3(1, 1, 1));
    box.Expand(Vec3(-5, 0, 0));
    EXPECT_FLOAT_EQ(box.min.X(), -5.0f);
    EXPECT_FLOAT_EQ(box.max.X(), 1.0f);
}

TEST(Bounds3Test, FromCenterExtents) {
    Bounds3 box = Bounds3::FromCenterExtents(Vec3(5, 5, 5), Vec3(2, 2, 2));
    EXPECT_FLOAT_EQ(box.min.X(), 3.0f);
    EXPECT_FLOAT_EQ(box.max.X(), 7.0f);
}

TEST(Bounds3Test, IsValid) {
    Bounds3 valid(Vec3(0, 0, 0), Vec3(1, 1, 1));
    EXPECT_TRUE(valid.IsValid());

    Bounds3 empty; // default: min=FLT_MAX, max=-FLT_MAX
    EXPECT_FALSE(empty.IsValid());
}

// ── Plane ────────────────────────────────────────────────────────────────────

TEST(PlaneTest, DistanceToPoint) {
    // XY plane at origin, normal pointing up (Y)
    Plane p(0, 1, 0, 0);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0, 5, 0)), 5.0f, 1e-5f);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0, -3, 0)), -3.0f, 1e-5f);
    EXPECT_NEAR(p.DistanceToPoint(Vec3(0, 0, 0)), 0.0f, 1e-5f);
}

// ── Frustum ──────────────────────────────────────────────────────────────────

class FrustumTest : public ::testing::Test {
  protected:
    Frustum frustum;

    void SetUp() override {
        // Mat4::LookAt wraps XMMatrixLookAtLH which uses Y-up convention.
        // Pass Y-up vector (0,1,0) directly, NOT Vec3::Up() which is Z-up (0,0,1).
        Mat4 view = Mat4::LookAt(Vec3(0, 0, -5), Vec3(0, 0, 0), Vec3(0, 1, 0));
        Mat4 proj = Mat4::Perspective(XM_PIDIV4, 1.0f, 0.1f, 100.0f);
        Mat4 vp = proj * view;
        frustum = Frustum::FromViewProj(vp);
    }
};

TEST_F(FrustumTest, PointInsideFrustum) {
    // Origin should be visible from camera at (0,0,-5) looking at origin
    EXPECT_TRUE(frustum.TestPoint(Vec3(0, 0, 0)));
}

TEST_F(FrustumTest, PointBehindCamera) {
    EXPECT_FALSE(frustum.TestPoint(Vec3(0, 0, -200)));
}

TEST_F(FrustumTest, Bounds3InsideFrustum) {
    Bounds3 box(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(frustum.TestBounds3(box));
}

TEST_F(FrustumTest, Bounds3OutsideFrustum) {
    Bounds3 box(Vec3(1000, 1000, 1000), Vec3(1001, 1001, 1001));
    EXPECT_FALSE(frustum.TestBounds3(box));
}

TEST_F(FrustumTest, SphereInsideFrustum) {
    EXPECT_TRUE(frustum.TestSphere(Vec3(0, 0, 0), 1.0f));
}

TEST_F(FrustumTest, SphereOutsideFrustum) {
    EXPECT_FALSE(frustum.TestSphere(Vec3(1000, 1000, 1000), 1.0f));
}
