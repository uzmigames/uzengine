#include "uz_geometry.h"
#include "uz_math.h"

#include <gtest/gtest.h>

using namespace uz;

// ── AABB ─────────────────────────────────────────────────────────────────────

TEST(AABBTest, ContainsPoint) {
    AABB box(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(box.Contains(Vec3(0, 0, 0)));
    EXPECT_TRUE(box.Contains(Vec3(1, 1, 1)));
    EXPECT_FALSE(box.Contains(Vec3(2, 0, 0)));
}

TEST(AABBTest, IntersectsOverlapping) {
    AABB a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    AABB b(Vec3(0, 0, 0), Vec3(2, 2, 2));
    EXPECT_TRUE(a.Intersects(b));
    EXPECT_TRUE(b.Intersects(a));
}

TEST(AABBTest, IntersectsSeparated) {
    AABB a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    AABB b(Vec3(5, 5, 5), Vec3(6, 6, 6));
    EXPECT_FALSE(a.Intersects(b));
}

TEST(AABBTest, Merge) {
    AABB a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    AABB b(Vec3(0, 0, 0), Vec3(3, 3, 3));
    AABB merged = a.Merge(b);
    EXPECT_FLOAT_EQ(merged.min.X(), -1.0f);
    EXPECT_FLOAT_EQ(merged.max.X(), 3.0f);
}

TEST(AABBTest, CenterAndExtents) {
    AABB box(Vec3(-2, -4, -6), Vec3(2, 4, 6));
    Vec3 center = box.Center();
    Vec3 extents = box.Extents();
    EXPECT_NEAR(center.X(), 0.0f, 1e-5f);
    EXPECT_NEAR(center.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(center.Z(), 0.0f, 1e-5f);
    EXPECT_NEAR(extents.X(), 2.0f, 1e-5f);
    EXPECT_NEAR(extents.Y(), 4.0f, 1e-5f);
    EXPECT_NEAR(extents.Z(), 6.0f, 1e-5f);
}

TEST(AABBTest, Expand) {
    AABB box(Vec3(0, 0, 0), Vec3(1, 1, 1));
    box.Expand(Vec3(-5, 0, 0));
    EXPECT_FLOAT_EQ(box.min.X(), -5.0f);
    EXPECT_FLOAT_EQ(box.max.X(), 1.0f);
}

TEST(AABBTest, FromCenterExtents) {
    AABB box = AABB::FromCenterExtents(Vec3(5, 5, 5), Vec3(2, 2, 2));
    EXPECT_FLOAT_EQ(box.min.X(), 3.0f);
    EXPECT_FLOAT_EQ(box.max.X(), 7.0f);
}

TEST(AABBTest, IsValid) {
    AABB valid(Vec3(0, 0, 0), Vec3(1, 1, 1));
    EXPECT_TRUE(valid.IsValid());

    AABB empty; // default: min=FLT_MAX, max=-FLT_MAX
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
        Mat4 view = Mat4::LookAt(Vec3(0, 0, -5), Vec3(0, 0, 0), Vec3::Up());
        Mat4 proj = Mat4::Perspective(XM_PIDIV4, 1.0f, 0.1f, 100.0f);
        Mat4 vp = proj * view; // Correct order: proj applied after view
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

TEST_F(FrustumTest, AABBInsideFrustum) {
    AABB box(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(frustum.TestAABB(box));
}

TEST_F(FrustumTest, AABBOutsideFrustum) {
    AABB box(Vec3(1000, 1000, 1000), Vec3(1001, 1001, 1001));
    EXPECT_FALSE(frustum.TestAABB(box));
}

TEST_F(FrustumTest, SphereInsideFrustum) {
    EXPECT_TRUE(frustum.TestSphere(Vec3(0, 0, 0), 1.0f));
}

TEST_F(FrustumTest, SphereOutsideFrustum) {
    EXPECT_FALSE(frustum.TestSphere(Vec3(1000, 1000, 1000), 1.0f));
}
