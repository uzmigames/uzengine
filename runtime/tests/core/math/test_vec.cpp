#include "uz_math.h"

#include <gtest/gtest.h>

using namespace uz;

// ── Vec2 ─────────────────────────────────────────────────────────────────────

TEST(Vec2Test, DefaultConstructorIsZero) {
    Vec2 v;
    EXPECT_FLOAT_EQ(v.X(), 0.0f);
    EXPECT_FLOAT_EQ(v.Y(), 0.0f);
}

TEST(Vec2Test, ComponentConstructor) {
    Vec2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.X(), 3.0f);
    EXPECT_FLOAT_EQ(v.Y(), 4.0f);
}

TEST(Vec2Test, Arithmetic) {
    Vec2 a(1.0f, 2.0f);
    Vec2 b(3.0f, 4.0f);
    Vec2 sum = a + b;
    EXPECT_FLOAT_EQ(sum.X(), 4.0f);
    EXPECT_FLOAT_EQ(sum.Y(), 6.0f);

    Vec2 diff = b - a;
    EXPECT_FLOAT_EQ(diff.X(), 2.0f);
    EXPECT_FLOAT_EQ(diff.Y(), 2.0f);

    Vec2 scaled = a * 2.0f;
    EXPECT_FLOAT_EQ(scaled.X(), 2.0f);
    EXPECT_FLOAT_EQ(scaled.Y(), 4.0f);
}

TEST(Vec2Test, LengthAndNormalize) {
    Vec2 v(3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.Length(), 5.0f);
    EXPECT_FLOAT_EQ(v.LengthSq(), 25.0f);

    Vec2 n = v.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, 1e-5f);
    EXPECT_NEAR(n.X(), 0.6f, 1e-5f);
    EXPECT_NEAR(n.Y(), 0.8f, 1e-5f);
}

TEST(Vec2Test, Dot) {
    Vec2 a(1.0f, 0.0f);
    Vec2 b(0.0f, 1.0f);
    EXPECT_FLOAT_EQ(a.Dot(b), 0.0f);
    EXPECT_FLOAT_EQ(a.Dot(a), 1.0f);
}

TEST(Vec2Test, Equality) {
    Vec2 a(1.0f, 2.0f);
    Vec2 b(1.0f, 2.0f);
    Vec2 c(1.0f, 3.0f);
    EXPECT_TRUE(a == b);
    EXPECT_TRUE(a != c);
}

TEST(Vec2Test, Lerp) {
    Vec2 a(0.0f, 0.0f);
    Vec2 b(10.0f, 10.0f);
    Vec2 mid = Vec2::Lerp(a, b, 0.5f);
    EXPECT_FLOAT_EQ(mid.X(), 5.0f);
    EXPECT_FLOAT_EQ(mid.Y(), 5.0f);
}

TEST(Vec2Test, Negation) {
    Vec2 v(1.0f, -2.0f);
    Vec2 neg = -v;
    EXPECT_FLOAT_EQ(neg.X(), -1.0f);
    EXPECT_FLOAT_EQ(neg.Y(), 2.0f);
}

TEST(Vec2Test, StorageConversion) {
    Vec2 v(3.0f, 7.0f);
    XMFLOAT2 f = v.ToFloat2();
    EXPECT_FLOAT_EQ(f.x, 3.0f);
    EXPECT_FLOAT_EQ(f.y, 7.0f);
    Vec2 restored = Vec2::FromFloat2(f);
    EXPECT_TRUE(restored == v);
}

// ── Vec3 ─────────────────────────────────────────────────────────────────────

TEST(Vec3Test, DefaultConstructorIsZero) {
    Vec3 v;
    EXPECT_FLOAT_EQ(v.X(), 0.0f);
    EXPECT_FLOAT_EQ(v.Y(), 0.0f);
    EXPECT_FLOAT_EQ(v.Z(), 0.0f);
}

TEST(Vec3Test, ComponentConstructor) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    EXPECT_FLOAT_EQ(v.X(), 1.0f);
    EXPECT_FLOAT_EQ(v.Y(), 2.0f);
    EXPECT_FLOAT_EQ(v.Z(), 3.0f);
}

TEST(Vec3Test, Arithmetic) {
    Vec3 a(1.0f, 2.0f, 3.0f);
    Vec3 b(4.0f, 5.0f, 6.0f);
    Vec3 sum = a + b;
    EXPECT_FLOAT_EQ(sum.X(), 5.0f);
    EXPECT_FLOAT_EQ(sum.Y(), 7.0f);
    EXPECT_FLOAT_EQ(sum.Z(), 9.0f);

    Vec3 scaled = a * 3.0f;
    EXPECT_FLOAT_EQ(scaled.X(), 3.0f);
    EXPECT_FLOAT_EQ(scaled.Y(), 6.0f);
    EXPECT_FLOAT_EQ(scaled.Z(), 9.0f);

    Vec3 commutative = 3.0f * a;
    EXPECT_TRUE(commutative == scaled);
}

TEST(Vec3Test, CrossProduct) {
    Vec3 x = Vec3::UnitX();
    Vec3 y = Vec3::UnitY();
    Vec3 z = x.Cross(y);
    EXPECT_NEAR(z.X(), 0.0f, 1e-5f);
    EXPECT_NEAR(z.Y(), 0.0f, 1e-5f);
    EXPECT_NEAR(z.Z(), 1.0f, 1e-5f);
}

TEST(Vec3Test, LengthAndNormalize) {
    Vec3 v(1.0f, 2.0f, 2.0f);
    EXPECT_FLOAT_EQ(v.Length(), 3.0f);

    Vec3 n = v.Normalized();
    EXPECT_NEAR(n.Length(), 1.0f, 1e-5f);
}

TEST(Vec3Test, DotProduct) {
    Vec3 a(1.0f, 0.0f, 0.0f);
    Vec3 b(0.0f, 1.0f, 0.0f);
    EXPECT_FLOAT_EQ(a.Dot(b), 0.0f);
    EXPECT_FLOAT_EQ(a.Dot(a), 1.0f);
}

TEST(Vec3Test, MinMax) {
    Vec3 a(1.0f, 5.0f, 3.0f);
    Vec3 b(4.0f, 2.0f, 6.0f);
    Vec3 mn = Vec3::Min(a, b);
    Vec3 mx = Vec3::Max(a, b);
    EXPECT_FLOAT_EQ(mn.X(), 1.0f);
    EXPECT_FLOAT_EQ(mn.Y(), 2.0f);
    EXPECT_FLOAT_EQ(mn.Z(), 3.0f);
    EXPECT_FLOAT_EQ(mx.X(), 4.0f);
    EXPECT_FLOAT_EQ(mx.Y(), 5.0f);
    EXPECT_FLOAT_EQ(mx.Z(), 6.0f);
}

TEST(Vec3Test, NamedDirections) {
    EXPECT_TRUE(Vec3::Up() == Vec3::UnitY());
    EXPECT_TRUE(Vec3::Forward() == Vec3::UnitZ());
    EXPECT_TRUE(Vec3::Right() == Vec3::UnitX());
}

TEST(Vec3Test, StorageConversion) {
    Vec3 v(1.0f, 2.0f, 3.0f);
    XMFLOAT3 f = v.ToFloat3();
    EXPECT_FLOAT_EQ(f.x, 1.0f);
    EXPECT_FLOAT_EQ(f.y, 2.0f);
    EXPECT_FLOAT_EQ(f.z, 3.0f);
    Vec3 restored = Vec3::FromFloat3(f);
    EXPECT_TRUE(restored == v);
}

// ── Vec4 ─────────────────────────────────────────────────────────────────────

TEST(Vec4Test, DefaultConstructorIsZero) {
    Vec4 v;
    EXPECT_FLOAT_EQ(v.X(), 0.0f);
    EXPECT_FLOAT_EQ(v.Y(), 0.0f);
    EXPECT_FLOAT_EQ(v.Z(), 0.0f);
    EXPECT_FLOAT_EQ(v.W(), 0.0f);
}

TEST(Vec4Test, ComponentConstructor) {
    Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(v.X(), 1.0f);
    EXPECT_FLOAT_EQ(v.Y(), 2.0f);
    EXPECT_FLOAT_EQ(v.Z(), 3.0f);
    EXPECT_FLOAT_EQ(v.W(), 4.0f);
}

TEST(Vec4Test, FromVec3AndW) {
    Vec3 xyz(1.0f, 2.0f, 3.0f);
    Vec4 v(xyz, 4.0f);
    EXPECT_FLOAT_EQ(v.X(), 1.0f);
    EXPECT_FLOAT_EQ(v.Y(), 2.0f);
    EXPECT_FLOAT_EQ(v.Z(), 3.0f);
    EXPECT_FLOAT_EQ(v.W(), 4.0f);
}

TEST(Vec4Test, XYZExtraction) {
    Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
    Vec3 xyz = v.XYZ();
    EXPECT_FLOAT_EQ(xyz.X(), 1.0f);
    EXPECT_FLOAT_EQ(xyz.Y(), 2.0f);
    EXPECT_FLOAT_EQ(xyz.Z(), 3.0f);
}

TEST(Vec4Test, Arithmetic) {
    Vec4 a(1.0f, 2.0f, 3.0f, 4.0f);
    Vec4 b(5.0f, 6.0f, 7.0f, 8.0f);
    Vec4 sum = a + b;
    EXPECT_FLOAT_EQ(sum.X(), 6.0f);
    EXPECT_FLOAT_EQ(sum.Y(), 8.0f);
    EXPECT_FLOAT_EQ(sum.Z(), 10.0f);
    EXPECT_FLOAT_EQ(sum.W(), 12.0f);
}

TEST(Vec4Test, LengthAndDot) {
    Vec4 v(1.0f, 0.0f, 0.0f, 0.0f);
    EXPECT_FLOAT_EQ(v.Length(), 1.0f);
    EXPECT_FLOAT_EQ(v.Dot(v), 1.0f);
}
