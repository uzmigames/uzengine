/// Comprehensive unit tests for Plane, Bounds3, and Frustum geometry primitives.
/// Covers: Plane signed distance, Bounds3 construction/contains/intersects/expand/merge/
/// center/extents/volume/validity, Frustum extraction from view-proj, contains/intersects
/// for points, boxes, and spheres.
#include "geometry.h"
#include "mat4.h"

#include <gtest/gtest.h>

using namespace uz;

static constexpr float kEps = 1e-4f;

// ─────────────────────────────────────────────────────────────────────────────
// Plane
// ─────────────────────────────────────────────────────────────────────────────

TEST(PlaneTest, Distance_PointAbove_IsPositive) {
    Plane p(0.0f, 1.0f, 0.0f, 0.0f);  // Y=0 plane, normal +Y
    EXPECT_NEAR(p.distance(Vec3(0.0f, 5.0f, 0.0f)), 5.0f, kEps);
}

TEST(PlaneTest, Distance_PointBelow_IsNegative) {
    Plane p(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(p.distance(Vec3(0.0f, -3.0f, 0.0f)), -3.0f, kEps);
}

TEST(PlaneTest, Distance_PointOnPlane_IsZero) {
    Plane p(0.0f, 1.0f, 0.0f, 0.0f);
    EXPECT_NEAR(p.distance(Vec3(100.0f, 0.0f, -50.0f)), 0.0f, kEps);
}

TEST(PlaneTest, Distance_OffsetPlane_CorrectSignedDistance) {
    // Plane y = 5 (i.e. y - 5 = 0): a=0, b=1, c=0, d=-5
    Plane p(0.0f, 1.0f, 0.0f, -5.0f);
    EXPECT_NEAR(p.distance(Vec3(0.0f, 7.0f, 0.0f)), 2.0f, kEps);
    EXPECT_NEAR(p.distance(Vec3(0.0f, 3.0f, 0.0f)), -2.0f, kEps);
}

TEST(PlaneTest, Distance_DiagonalNormal) {
    // Plane with normal (1, 0, 0) through x=3: d = -3
    Plane p(1.0f, 0.0f, 0.0f, -3.0f);
    EXPECT_NEAR(p.distance(Vec3(5.0f, 0.0f, 0.0f)), 2.0f, kEps);
    EXPECT_NEAR(p.distance(Vec3(1.0f, 0.0f, 0.0f)), -2.0f, kEps);
}

TEST(PlaneTest, AllAliases_ReturnSameValue) {
    Plane p(0.0f, 1.0f, 0.0f, 0.0f);
    Vec3 v(1.0f, 4.0f, 2.0f);
    EXPECT_FLOAT_EQ(p.distance(v), p.Distance(v));
    EXPECT_FLOAT_EQ(p.distance(v), p.DistanceToPoint(v));
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — construction
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, DefaultConstructor_IsInvalid) {
    Bounds3 b;
    EXPECT_FALSE(b.IsValid());
}

TEST(Bounds3Test2, ExplicitConstruction_IsValid) {
    Bounds3 b(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
    EXPECT_TRUE(b.IsValid());
}

TEST(Bounds3Test2, FromCenterExtents_CorrectMinMax) {
    Bounds3 b = Bounds3::FromCenterExtents(Vec3(2.0f, 3.0f, 4.0f), Vec3(1.0f, 2.0f, 0.5f));
    EXPECT_NEAR(b.min.x, 1.0f, kEps);
    EXPECT_NEAR(b.min.y, 1.0f, kEps);
    EXPECT_NEAR(b.min.z, 3.5f, kEps);
    EXPECT_NEAR(b.max.x, 3.0f, kEps);
    EXPECT_NEAR(b.max.y, 5.0f, kEps);
    EXPECT_NEAR(b.max.z, 4.5f, kEps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — contains
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, Contains_PointAtCenter_ReturnsTrue) {
    Bounds3 b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(b.contains(Vec3(0, 0, 0)));
}

TEST(Bounds3Test2, Contains_PointOnCorner_ReturnsTrue) {
    Bounds3 b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(b.contains(Vec3(1, 1, 1)));
    EXPECT_TRUE(b.contains(Vec3(-1, -1, -1)));
}

TEST(Bounds3Test2, Contains_PointOutside_ReturnsFalse) {
    Bounds3 b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_FALSE(b.contains(Vec3(2, 0, 0)));
    EXPECT_FALSE(b.contains(Vec3(0, -2, 0)));
    EXPECT_FALSE(b.contains(Vec3(0, 0, 1.0001f)));
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — intersects
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, Intersects_Overlapping_ReturnsTrue) {
    Bounds3 a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    Bounds3 b(Vec3(0, 0, 0), Vec3(2, 2, 2));
    EXPECT_TRUE(a.intersects(b));
    EXPECT_TRUE(b.intersects(a));
}

TEST(Bounds3Test2, Intersects_Separated_ReturnsFalse) {
    Bounds3 a(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    Bounds3 b(Vec3(5, 5, 5), Vec3(6, 6, 6));
    EXPECT_FALSE(a.intersects(b));
    EXPECT_FALSE(b.intersects(a));
}

TEST(Bounds3Test2, Intersects_Touching_ReturnsTrue) {
    // Boxes that share exactly one face
    Bounds3 a(Vec3(0, 0, 0), Vec3(1, 1, 1));
    Bounds3 b(Vec3(1, 0, 0), Vec3(2, 1, 1));
    EXPECT_TRUE(a.intersects(b));
}

TEST(Bounds3Test2, Intersects_SeparatedAlongX_ReturnsFalse) {
    Bounds3 a(Vec3(0, 0, 0), Vec3(1, 1, 1));
    Bounds3 b(Vec3(2, 0, 0), Vec3(3, 1, 1));
    EXPECT_FALSE(a.intersects(b));
}

TEST(Bounds3Test2, Intersects_SeparatedAlongY_ReturnsFalse) {
    Bounds3 a(Vec3(0, 0, 0), Vec3(1, 1, 1));
    Bounds3 b(Vec3(0, 2, 0), Vec3(1, 3, 1));
    EXPECT_FALSE(a.intersects(b));
}

TEST(Bounds3Test2, Intersects_SeparatedAlongZ_ReturnsFalse) {
    Bounds3 a(Vec3(0, 0, 0), Vec3(1, 1, 1));
    Bounds3 b(Vec3(0, 0, 2), Vec3(1, 1, 3));
    EXPECT_FALSE(a.intersects(b));
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — expand
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, Expand_Mutating_ExpandsMin) {
    Bounds3 b(Vec3(0, 0, 0), Vec3(1, 1, 1));
    b.Expand(Vec3(-5, 0, 0));
    EXPECT_FLOAT_EQ(b.min.x, -5.0f);
    EXPECT_FLOAT_EQ(b.max.x,  1.0f);
}

TEST(Bounds3Test2, Expand_Mutating_ExpandsMax) {
    Bounds3 b(Vec3(0, 0, 0), Vec3(1, 1, 1));
    b.Expand(Vec3(10, 10, 10));
    EXPECT_FLOAT_EQ(b.max.x, 10.0f);
    EXPECT_FLOAT_EQ(b.max.y, 10.0f);
    EXPECT_FLOAT_EQ(b.max.z, 10.0f);
}

TEST(Bounds3Test2, Expand_NonMutating_ReturnsEnlargedBounds) {
    Bounds3 b(Vec3(0, 0, 0), Vec3(1, 1, 1));
    Bounds3 result = b.expand(Vec3(-2, 3, -1));
    EXPECT_FLOAT_EQ(result.min.x, -2.0f);
    EXPECT_FLOAT_EQ(result.max.y,  3.0f);
    EXPECT_FLOAT_EQ(result.min.z, -1.0f);
    // Original unchanged
    EXPECT_FLOAT_EQ(b.min.x, 0.0f);
}

TEST(Bounds3Test2, Expand_DefaultBounds_BuildsCorrectly) {
    Bounds3 b;
    b.Expand(Vec3(1, 2, 3));
    b.Expand(Vec3(-1, -2, -3));
    EXPECT_FLOAT_EQ(b.min.x, -1.0f);
    EXPECT_FLOAT_EQ(b.max.x,  1.0f);
    EXPECT_FLOAT_EQ(b.min.y, -2.0f);
    EXPECT_FLOAT_EQ(b.max.y,  2.0f);
    EXPECT_TRUE(b.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — merge
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, Merge_TwoBoxes_ProducesUnion) {
    Bounds3 a(Vec3(-1, -1, -1), Vec3(0, 0, 0));
    Bounds3 b(Vec3(0, 0, 0), Vec3(3, 3, 3));
    Bounds3 m = a.Merge(b);
    EXPECT_FLOAT_EQ(m.min.x, -1.0f);
    EXPECT_FLOAT_EQ(m.max.x,  3.0f);
    EXPECT_FLOAT_EQ(m.min.y, -1.0f);
    EXPECT_FLOAT_EQ(m.max.y,  3.0f);
}

TEST(Bounds3Test2, Merge_IsCommutative) {
    Bounds3 a(Vec3(-2, -3, -1), Vec3(1, 2, 0));
    Bounds3 b(Vec3(0, 0, -4), Vec3(5, 5, 5));
    Bounds3 ab = a.Merge(b);
    Bounds3 ba = b.Merge(a);
    EXPECT_FLOAT_EQ(ab.min.x, ba.min.x);
    EXPECT_FLOAT_EQ(ab.max.x, ba.max.x);
    EXPECT_FLOAT_EQ(ab.min.y, ba.min.y);
    EXPECT_FLOAT_EQ(ab.max.z, ba.max.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — center, extents, size, volume
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, Center_SymmetricBox_IsOrigin) {
    Bounds3 b(Vec3(-3, -3, -3), Vec3(3, 3, 3));
    Vec3 c = b.Center();
    EXPECT_NEAR(c.x, 0.0f, kEps);
    EXPECT_NEAR(c.y, 0.0f, kEps);
    EXPECT_NEAR(c.z, 0.0f, kEps);
}

TEST(Bounds3Test2, Center_OffsetBox_IsCorrect) {
    Bounds3 b(Vec3(0, 0, 0), Vec3(4, 6, 2));
    Vec3 c = b.Center();
    EXPECT_NEAR(c.x, 2.0f, kEps);
    EXPECT_NEAR(c.y, 3.0f, kEps);
    EXPECT_NEAR(c.z, 1.0f, kEps);
}

TEST(Bounds3Test2, Extents_IsHalfSize) {
    Bounds3 b(Vec3(-2, -4, -6), Vec3(2, 4, 6));
    Vec3 e = b.Extents();
    EXPECT_NEAR(e.x, 2.0f, kEps);
    EXPECT_NEAR(e.y, 4.0f, kEps);
    EXPECT_NEAR(e.z, 6.0f, kEps);
}

TEST(Bounds3Test2, Size_IsMaxMinusMIn) {
    Bounds3 b(Vec3(1, 2, 3), Vec3(4, 7, 11));
    Vec3 s = b.Size();
    EXPECT_FLOAT_EQ(s.x, 3.0f);
    EXPECT_FLOAT_EQ(s.y, 5.0f);
    EXPECT_FLOAT_EQ(s.z, 8.0f);
}

TEST(Bounds3Test2, Volume_CorrectProduct) {
    Bounds3 b(Vec3(0, 0, 0), Vec3(2, 3, 4));
    EXPECT_NEAR(b.Volume(), 24.0f, kEps);
}

TEST(Bounds3Test2, Volume_UnitCube_IsOne) {
    Bounds3 b(Vec3(0, 0, 0), Vec3(1, 1, 1));
    EXPECT_NEAR(b.Volume(), 1.0f, kEps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Bounds3 — intersectsSphere
// ─────────────────────────────────────────────────────────────────────────────

TEST(Bounds3Test2, IntersectsSphere_CenterInside_ReturnsTrue) {
    Bounds3 b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(b.intersectsSphere(Vec3(0, 0, 0), 0.1f));
}

TEST(Bounds3Test2, IntersectsSphere_JustTouching_ReturnsTrue) {
    Bounds3 b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_TRUE(b.intersectsSphere(Vec3(2, 0, 0), 1.0f));
}

TEST(Bounds3Test2, IntersectsSphere_ClearlyOutside_ReturnsFalse) {
    Bounds3 b(Vec3(-1, -1, -1), Vec3(1, 1, 1));
    EXPECT_FALSE(b.intersectsSphere(Vec3(100, 0, 0), 1.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — extracted from perspective camera
// ─────────────────────────────────────────────────────────────────────────────

class FrustumTest2 : public ::testing::Test {
protected:
    Frustum frustum;

    void SetUp() override {
        // Camera at (0,0,-5) looking at origin, Y-up, 45-deg FOV, 100 unit depth
        Mat4 view = Mat4::LookAt(Vec3(0, 0, -5), Vec3(0, 0, 0), Vec3(0, 1, 0));
        Mat4 proj = Mat4::Perspective(XM_PIDIV4, 1.0f, 0.1f, 100.0f);
        frustum = Frustum::FromViewProj(proj * view);
    }
};

TEST_F(FrustumTest2, Contains_OriginInView_ReturnsTrue) {
    EXPECT_TRUE(frustum.contains(Vec3(0, 0, 0)));
}

TEST_F(FrustumTest2, Contains_PointBehindCamera_ReturnsFalse) {
    EXPECT_FALSE(frustum.contains(Vec3(0, 0, -200)));
}

TEST_F(FrustumTest2, Contains_PointFarAway_ReturnsFalse) {
    EXPECT_FALSE(frustum.contains(Vec3(1000, 0, 0)));
}

TEST_F(FrustumTest2, Contains_PointAlongViewAxis_ReturnsTrue) {
    // Point along the view direction (camera looks from -5 toward origin, so z=-2 is inside)
    EXPECT_TRUE(frustum.contains(Vec3(0.0f, 0.0f, -2.0f)));
}

TEST_F(FrustumTest2, Intersects_BoxAtOrigin_ReturnsTrue) {
    Bounds3 box(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
    EXPECT_TRUE(frustum.intersects(box));
}

TEST_F(FrustumTest2, Intersects_BoxFarBehindCamera_ReturnsFalse) {
    Bounds3 box(Vec3(-1, -1, -500), Vec3(1, 1, -400));
    EXPECT_FALSE(frustum.intersects(box));
}

TEST_F(FrustumTest2, Intersects_BoxVeryFarForward_ReturnsFalse) {
    Bounds3 box(Vec3(1000, 1000, 1000), Vec3(1001, 1001, 1001));
    EXPECT_FALSE(frustum.intersects(box));
}

TEST_F(FrustumTest2, Intersects_LargeBoxSpanningOrigin_ReturnsTrue) {
    Bounds3 box(Vec3(-10, -10, -10), Vec3(10, 10, 10));
    EXPECT_TRUE(frustum.intersects(box));
}

TEST_F(FrustumTest2, IntersectsSphere_AtOrigin_ReturnsTrue) {
    EXPECT_TRUE(frustum.intersectsSphere(Vec3(0, 0, 0), 1.0f));
}

TEST_F(FrustumTest2, IntersectsSphere_FarAway_ReturnsFalse) {
    EXPECT_FALSE(frustum.intersectsSphere(Vec3(1000, 1000, 1000), 1.0f));
}

TEST_F(FrustumTest2, IntersectsSphere_BehindCamera_ReturnsFalse) {
    EXPECT_FALSE(frustum.intersectsSphere(Vec3(0, 0, -200), 1.0f));
}

TEST_F(FrustumTest2, AllAliases_ReturnSameResult) {
    Vec3 p(0, 0, 0);
    Bounds3 box(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f));
    EXPECT_EQ(frustum.contains(p), frustum.Contains(p));
    EXPECT_EQ(frustum.contains(p), frustum.TestPoint(p));
    EXPECT_EQ(frustum.intersects(box), frustum.Intersects(box));
    EXPECT_EQ(frustum.intersects(box), frustum.TestBounds3(box));
}

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — orthographic projection
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrustumOrthoTest, FromOrthoProjection_PointInside_ReturnsTrue) {
    Mat4 view = Mat4::LookAt(Vec3(0, 0, -5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Mat4 proj = Mat4::Orthographic(10.0f, 10.0f, 0.1f, 100.0f);
    Frustum f = Frustum::FromViewProj(proj * view);
    EXPECT_TRUE(f.intersects(Bounds3(Vec3(-0.5f, -0.5f, -0.5f), Vec3(0.5f, 0.5f, 0.5f))));
}

TEST(FrustumOrthoTest, FromOrthoProjection_PointFarAway_ReturnsFalse) {
    Mat4 view = Mat4::LookAt(Vec3(0, 0, -5), Vec3(0, 0, 0), Vec3(0, 1, 0));
    Mat4 proj = Mat4::Orthographic(10.0f, 10.0f, 0.1f, 100.0f);
    Frustum f = Frustum::FromViewProj(proj * view);
    EXPECT_FALSE(f.intersects(Bounds3(Vec3(1000, 1000, 1000), Vec3(1001, 1001, 1001))));
}

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — explicit plane construction
// ─────────────────────────────────────────────────────────────────────────────

TEST(FrustumExplicitTest, ExplicitConstruction_PlanesFilled) {
    Plane near_p(0, 0, -1, 0);
    Plane far_p(0, 0, 1, -10);
    Plane left_p(1, 0, 0, 0);
    Plane right_p(-1, 0, 0, 0);
    Plane top_p(0, -1, 0, 0);
    Plane bot_p(0, 1, 0, 0);
    Frustum f(near_p, far_p, left_p, right_p, top_p, bot_p);
    // Verify plane data stored correctly
    EXPECT_FLOAT_EQ(f.planes[0].c, -1.0f);  // near z component
    EXPECT_FLOAT_EQ(f.planes[1].d, -10.0f); // far d component
}
