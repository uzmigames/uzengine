/// Unit tests for phase 8.11.0 — VFX billboard facing modes (5 and 6).
///
/// Mirrors the HLSL basis-vector math for modes PlaneAxis (5) and
/// WorldZAligned (6) in C++ so the geometric invariants can be checked
/// without a GPU:
///
///   - right ⊥ up  (always)
///   - right ⊥ planeNormal          (mode 5)
///   - up    lies in the plane       (mode 5)
///   - up == world Z                 (mode 6)
///   - right lies in XY plane        (mode 6)
///   - right ⊥ (camera - particle)XY (mode 6)
///   - degenerate camera placement falls back to a stable axis (both modes)
///
/// @ref runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl
/// @ref docs/engine/25-vfx/28-billboard-facing-modes.md

#include <gtest/gtest.h>

#include <cmath>

namespace {

struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;

    Vec3 operator-(const Vec3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3 operator+(const Vec3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3 operator*(float s) const       { return {x*s, y*s, z*s}; }
};

Vec3 Cross(const Vec3& a, const Vec3& b) {
    return { a.y*b.z - a.z*b.y, a.z*b.x - a.x*b.z, a.x*b.y - a.y*b.x };
}
float Dot(const Vec3& a, const Vec3& b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
float Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }
Vec3  Normalize(const Vec3& v) {
    float l = Length(v);
    return (l > 1e-6f) ? Vec3{v.x/l, v.y/l, v.z/l} : Vec3{0,0,0};
}

// ─── CPU mirror of BasisPlaneAxis ────────────────────────────────────────────
void BasisPlaneAxisCPU(const Vec3& particlePos, const Vec3& cameraPos,
                        const Vec3& planeNormal,
                        Vec3& rightOut, Vec3& upOut) {
    Vec3 toCamera = cameraPos - particlePos;
    float dotN    = Dot(toCamera, planeNormal);
    Vec3 inPlane  = { toCamera.x - dotN * planeNormal.x,
                      toCamera.y - dotN * planeNormal.y,
                      toCamera.z - dotN * planeNormal.z };

    float inPlaneLen = Length(inPlane);
    if (inPlaneLen < 0.001f) {
        Vec3 alt = (std::fabs(planeNormal.x) < 0.99f) ? Vec3{1,0,0} : Vec3{0,1,0};
        rightOut = Normalize(Cross(planeNormal, alt));
    } else {
        Vec3 ip = { inPlane.x / inPlaneLen, inPlane.y / inPlaneLen, inPlane.z / inPlaneLen };
        rightOut = Normalize(Cross(planeNormal, ip));
    }
    upOut = Cross(rightOut, planeNormal);
}

// ─── CPU mirror of BasisWorldZAligned ────────────────────────────────────────
void BasisWorldZAlignedCPU(const Vec3& particlePos, const Vec3& cameraPos,
                            Vec3& rightOut, Vec3& upOut) {
    float dx = cameraPos.x - particlePos.x;
    float dy = cameraPos.y - particlePos.y;
    float len2D = std::sqrt(dx*dx + dy*dy);
    if (len2D < 0.001f) {
        rightOut = Vec3{1,0,0};
    } else {
        float hx = dx / len2D, hy = dy / len2D;
        rightOut = Vec3{-hy, hx, 0.0f};
    }
    upOut = Vec3{0,0,1};
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Mode 5: PlaneAxis
// ─────────────────────────────────────────────────────────────────────────────

TEST(BillboardFacingMode5_PlaneAxis, RightPerpendicularToPlaneNormal) {
    Vec3 particle{0,0,0}, camera{5, 3, 2};
    Vec3 planeNormal{0, 0, 1};
    Vec3 right, up;
    BasisPlaneAxisCPU(particle, camera, planeNormal, right, up);
    EXPECT_NEAR(Dot(right, planeNormal), 0.0f, 1e-5f);
}

TEST(BillboardFacingMode5_PlaneAxis, UpLiesInThePlane) {
    Vec3 particle{0,0,0}, camera{5, 3, 2};
    Vec3 planeNormal{0, 0, 1};
    Vec3 right, up;
    BasisPlaneAxisCPU(particle, camera, planeNormal, right, up);
    // up lies in plane ⇔ up ⊥ planeNormal
    EXPECT_NEAR(Dot(up, planeNormal), 0.0f, 1e-5f);
}

TEST(BillboardFacingMode5_PlaneAxis, RightAndUpOrthogonal) {
    Vec3 particle{0,0,0}, camera{5, 3, 2};
    Vec3 planeNormal = Normalize(Vec3{0, 1, 0}); // wall-decal example
    Vec3 right, up;
    BasisPlaneAxisCPU(particle, camera, planeNormal, right, up);
    EXPECT_NEAR(Dot(right, up), 0.0f, 1e-5f);
}

TEST(BillboardFacingMode5_PlaneAxis, CameraOnNormalAxis_UsesFallbackAxis) {
    // Camera directly above particle along +Z: inPlane is zero.
    Vec3 particle{0,0,0}, camera{0, 0, 10};
    Vec3 planeNormal{0, 0, 1};
    Vec3 right, up;
    BasisPlaneAxisCPU(particle, camera, planeNormal, right, up);
    // Fallback must still produce unit-length, perpendicular axes.
    EXPECT_NEAR(Length(right), 1.0f, 1e-4f);
    EXPECT_NEAR(Dot(right, planeNormal), 0.0f, 1e-4f);
    EXPECT_NEAR(Dot(right, up), 0.0f, 1e-4f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode 6: WorldZAligned
// ─────────────────────────────────────────────────────────────────────────────

TEST(BillboardFacingMode6_WorldZAligned, UpIsWorldZ) {
    Vec3 particle{0,0,0}, camera{5, 2, 3};
    Vec3 right, up;
    BasisWorldZAlignedCPU(particle, camera, right, up);
    EXPECT_FLOAT_EQ(up.x, 0.0f);
    EXPECT_FLOAT_EQ(up.y, 0.0f);
    EXPECT_FLOAT_EQ(up.z, 1.0f);
}

TEST(BillboardFacingMode6_WorldZAligned, RightLiesInHorizontalPlane) {
    Vec3 particle{0,0,0}, camera{5, 2, 10};
    Vec3 right, up;
    BasisWorldZAlignedCPU(particle, camera, right, up);
    EXPECT_FLOAT_EQ(right.z, 0.0f);
}

TEST(BillboardFacingMode6_WorldZAligned, RightIsPerpToHorizontalCamera) {
    Vec3 particle{0,0,0}, camera{5, 3, 2};
    Vec3 right, up;
    BasisWorldZAlignedCPU(particle, camera, right, up);
    // Horizontal camera direction.
    Vec3 horiz = {camera.x - particle.x, camera.y - particle.y, 0.0f};
    float lenH = Length(horiz);
    ASSERT_GT(lenH, 1e-3f);
    horiz = { horiz.x / lenH, horiz.y / lenH, 0.0f };
    EXPECT_NEAR(Dot(right, horiz), 0.0f, 1e-5f);
}

TEST(BillboardFacingMode6_WorldZAligned, RightAndUpOrthogonal) {
    Vec3 particle{0,0,0}, camera{5, 3, 2};
    Vec3 right, up;
    BasisWorldZAlignedCPU(particle, camera, right, up);
    EXPECT_NEAR(Dot(right, up), 0.0f, 1e-5f);
}

TEST(BillboardFacingMode6_WorldZAligned, CameraDirectlyOverhead_UsesXFallback) {
    Vec3 particle{0,0,0}, camera{0, 0, 10};
    Vec3 right, up;
    BasisWorldZAlignedCPU(particle, camera, right, up);
    EXPECT_FLOAT_EQ(right.x, 1.0f);
    EXPECT_FLOAT_EQ(right.y, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode invariants across both new modes
// ─────────────────────────────────────────────────────────────────────────────

TEST(BillboardFacingModes, UnitLengthAxes) {
    Vec3 particle{1,1,1}, camera{7, 5, 4};

    Vec3 right5, up5;
    BasisPlaneAxisCPU(particle, camera, Vec3{0,0,1}, right5, up5);
    EXPECT_NEAR(Length(right5), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(up5),    1.0f, 1e-4f);

    Vec3 right6, up6;
    BasisWorldZAlignedCPU(particle, camera, right6, up6);
    EXPECT_NEAR(Length(right6), 1.0f, 1e-4f);
    EXPECT_NEAR(Length(up6),    1.0f, 1e-4f);
}
