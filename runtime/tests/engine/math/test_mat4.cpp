/// Unit tests for Mat4 — identity, translation, rotation, and scale
/// construction, matrix multiplication associativity, inverse correctness,
/// TRS decomposition round-trips, and orthographic projection variants
/// (standard-Z and reversed-Z shadow consistency).
#include "vector3.h"
#include "vector4.h"
#include "mat4.h"

#include <gtest/gtest.h>

using namespace uz;
using namespace DirectX;

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

TEST(Mat4PerspectiveReversedZ, ClipWEqualsDepth) {
    const Mat4 projection = Mat4::Perspective(XM_PIDIV2, 2.0f, 0.25f, 100.0f);
    for (float depth : {0.25f, 1.0f, 2.0f, 10.0f, 100.0f}) {
        SCOPED_TRACE(depth);
        const Vec4 clip = projection * Vec4(0.5f, -0.25f, depth, 1.0f);
        EXPECT_NEAR(clip.w, depth, 1e-6f);
    }
}

TEST(Mat4PerspectiveReversedZ, NearAndFarMapToOneAndZero) {
    const Mat4 projection = Mat4::Perspective(XM_PIDIV2, 2.0f, 0.25f, 100.0f);
    const Vec4 nearClip = projection * Vec4(0.0f, 0.0f, 0.25f, 1.0f);
    const Vec4 farClip = projection * Vec4(0.0f, 0.0f, 100.0f, 1.0f);
    ASSERT_GT(nearClip.w, 0.0f);
    ASSERT_GT(farClip.w, 0.0f);
    EXPECT_NEAR(nearClip.z / nearClip.w, 1.0f, 1e-6f);
    EXPECT_NEAR(farClip.z / farClip.w, 0.0f, 1e-6f);
}

TEST(Mat4PerspectiveReversedZ, ScreenCoordinatesScaleWithInverseDepth) {
    // A 90-degree vertical FOV and aspect 2 give unit vertical and half horizontal scale.
    const Mat4 projection = Mat4::Perspective(XM_PIDIV2, 2.0f, 0.25f, 100.0f);
    float previousDepth = 1.0f;
    for (float depth : {1.0f, 2.0f, 4.0f, 16.0f}) {
        SCOPED_TRACE(depth);
        const Vec4 clip = projection * Vec4(1.0f, -0.5f, depth, 1.0f);
        ASSERT_GT(clip.w, 0.0f);
        EXPECT_NEAR(clip.x / clip.w, 0.5f / depth, 1e-6f);
        EXPECT_NEAR(clip.y / clip.w, -0.5f / depth, 1e-6f);
        const float ndcDepth = clip.z / clip.w;
        EXPECT_GT(ndcDepth, 0.0f);
        EXPECT_LT(ndcDepth, previousDepth);
        previousDepth = ndcDepth;
    }
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

// ─────────────────────────────────────────────────────────────────────────────
// Orthographic projection helpers
//
// Both orthographic variants are tested by projecting homogeneous points
// Vec4(0, 0, z, 1) through the matrix and checking the resulting NDC z
// (= result.z / result.w).  For orthographic projections w stays 1.0 so
// NDC z == result.z.
//
// Convention summary:
//   Orthographic()          — standard-Z  : near → 0.0,  far → 1.0
//   OrthographicReversedZ() — reversed-Z  : near → 1.0,  far → 0.0
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Projects a depth value through a Mat4 and returns NDC z (= out.z / out.w).
/// Inputs x=0, y=0, w=1 so only the Z row matters; orthographic w remains 1.
float ProjectDepth(const Mat4& mat, float world_z) {
    Vec4 clip = mat * Vec4(0.0f, 0.0f, world_z, 1.0f);
    return clip.z / clip.w;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Mat4::Orthographic — standard-Z (near→0, far→1)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4OrthoStandardZ, NearPlane_MapsToZero) {
    // Standard-Z: near plane must project to NDC z = 0.0 exactly.
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 100.0f;
    Mat4 proj = Mat4::Orthographic(10.0f, 10.0f, kNear, kFar);

    float ndcZ = ProjectDepth(proj, kNear);

    EXPECT_NEAR(ndcZ, 0.0f, 1e-5f)
        << "Orthographic near plane (" << kNear << ") must map to NDC z = 0.0";
}

TEST(Mat4OrthoStandardZ, FarPlane_MapsToOne) {
    // Standard-Z: far plane must project to NDC z = 1.0 exactly.
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 100.0f;
    Mat4 proj = Mat4::Orthographic(10.0f, 10.0f, kNear, kFar);

    float ndcZ = ProjectDepth(proj, kFar);

    EXPECT_NEAR(ndcZ, 1.0f, 1e-5f)
        << "Orthographic far plane (" << kFar << ") must map to NDC z = 1.0";
}

TEST(Mat4OrthoStandardZ, MidRange_MapsToHalf) {
    // Standard-Z: a point exactly halfway between near and far must map to 0.5.
    constexpr float kNear = 0.0f;
    constexpr float kFar  = 100.0f;
    const float kMid = (kNear + kFar) * 0.5f;  // 50.0
    Mat4 proj = Mat4::Orthographic(10.0f, 10.0f, kNear, kFar);

    float ndcZ = ProjectDepth(proj, kMid);

    EXPECT_NEAR(ndcZ, 0.5f, 1e-5f)
        << "Orthographic midpoint (" << kMid << ") must map to NDC z = 0.5";
}

TEST(Mat4OrthoStandardZ, W_ComponentRemainsOne_NoPerspectiveDivide) {
    // Orthographic projection must not affect the w component — it stays 1.
    Mat4 proj = Mat4::Orthographic(20.0f, 20.0f, 1.0f, 1000.0f);
    Vec4 clip = proj * Vec4(0.0f, 0.0f, 50.0f, 1.0f);

    EXPECT_NEAR(clip.w, 1.0f, 1e-5f)
        << "Orthographic w output must remain 1.0 (no perspective divide)";
}

TEST(Mat4OrthoStandardZ, XY_ScaledByWidthHeight) {
    // Orthographic X maps to [-1,1] over width: a point at x=width/2 → NDC x=1.
    constexpr float kWidth  = 20.0f;
    constexpr float kHeight = 10.0f;
    Mat4 proj = Mat4::Orthographic(kWidth, kHeight, 1.0f, 100.0f);

    // X = half-width should project to NDC x = 1.0
    Vec4 clip_x = proj * Vec4(kWidth * 0.5f, 0.0f, 1.0f, 1.0f);
    EXPECT_NEAR(clip_x.x / clip_x.w, 1.0f, 1e-5f)
        << "Orthographic X: half-width should map to NDC x = 1.0";

    // Y = half-height should project to NDC y = 1.0
    Vec4 clip_y = proj * Vec4(0.0f, kHeight * 0.5f, 1.0f, 1.0f);
    EXPECT_NEAR(clip_y.y / clip_y.w, 1.0f, 1e-5f)
        << "Orthographic Y: half-height should map to NDC y = 1.0";
}

TEST(Mat4OrthoStandardZ, ZMonotonicity_IncreasingDepthIncreasesNdcZ) {
    // For standard-Z the depth ordering must be monotonically increasing
    // so that a point closer to near has smaller NDC z than one closer to far.
    Mat4 proj = Mat4::Orthographic(10.0f, 10.0f, 1.0f, 1000.0f);
    float z_near_side = ProjectDepth(proj, 10.0f);
    float z_far_side  = ProjectDepth(proj, 500.0f);

    EXPECT_LT(z_near_side, z_far_side)
        << "Standard-Z: a point closer to near must have smaller NDC z "
           "than a point closer to far";
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat4::OrthographicReversedZ — reversed-Z (near→1, far→0)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4OrthoReversedZ, NearPlane_MapsToOne) {
    // Reversed-Z: near plane must project to NDC z = 1.0 exactly.
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 100.0f;
    Mat4 proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, kNear, kFar);

    float ndcZ = ProjectDepth(proj, kNear);

    EXPECT_NEAR(ndcZ, 1.0f, 1e-5f)
        << "OrthographicReversedZ near plane (" << kNear << ") must map to NDC z = 1.0";
}

TEST(Mat4OrthoReversedZ, FarPlane_MapsToZero) {
    // Reversed-Z: far plane must project to NDC z = 0.0 exactly.
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 100.0f;
    Mat4 proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, kNear, kFar);

    float ndcZ = ProjectDepth(proj, kFar);

    EXPECT_NEAR(ndcZ, 0.0f, 1e-5f)
        << "OrthographicReversedZ far plane (" << kFar << ") must map to NDC z = 0.0";
}

TEST(Mat4OrthoReversedZ, MidRange_MapsToHalf) {
    // Reversed-Z: a point exactly halfway between near and far must map to 0.5.
    constexpr float kNear = 0.0f;
    constexpr float kFar  = 100.0f;
    const float kMid = (kNear + kFar) * 0.5f;  // 50.0
    Mat4 proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, kNear, kFar);

    float ndcZ = ProjectDepth(proj, kMid);

    EXPECT_NEAR(ndcZ, 0.5f, 1e-5f)
        << "OrthographicReversedZ midpoint (" << kMid << ") must map to NDC z = 0.5";
}

TEST(Mat4OrthoReversedZ, W_ComponentRemainsOne_NoPerspectiveDivide) {
    // Orthographic projection must not affect the w component — it stays 1.
    Mat4 proj = Mat4::OrthographicReversedZ(20.0f, 20.0f, 1.0f, 1000.0f);
    Vec4 clip = proj * Vec4(0.0f, 0.0f, 50.0f, 1.0f);

    EXPECT_NEAR(clip.w, 1.0f, 1e-5f)
        << "OrthographicReversedZ w output must remain 1.0 (no perspective divide)";
}

TEST(Mat4OrthoReversedZ, ZMonotonicity_IncreasingDepthDecreasesNdcZ) {
    // For reversed-Z the depth ordering is inverted: a point closer to near
    // has larger NDC z than one closer to far.
    Mat4 proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, 1.0f, 1000.0f);
    float z_near_side = ProjectDepth(proj, 10.0f);
    float z_far_side  = ProjectDepth(proj, 500.0f);

    EXPECT_GT(z_near_side, z_far_side)
        << "Reversed-Z: a point closer to near must have LARGER NDC z "
           "than a point closer to far";
}

TEST(Mat4OrthoReversedZ, NdcZRange_StrictlyInZeroToOne) {
    // Every depth in [near, far] must produce NDC z in [0, 1] for reversed-Z.
    constexpr float kNear = 1.0f;
    constexpr float kFar  = 500.0f;
    Mat4 proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, kNear, kFar);

    // Sample 10 evenly-spaced depths across the frustum.
    const int kSamples = 10;
    for (int i = 0; i <= kSamples; ++i) {
        const float t    = static_cast<float>(i) / static_cast<float>(kSamples);
        const float worldZ = kNear + t * (kFar - kNear);
        const float ndcZ   = ProjectDepth(proj, worldZ);

        EXPECT_GE(ndcZ, 0.0f - 1e-5f)
            << "Reversed-Z NDC z must be >= 0 for world z = " << worldZ;
        EXPECT_LE(ndcZ, 1.0f + 1e-5f)
            << "Reversed-Z NDC z must be <= 1 for world z = " << worldZ;
    }
}

TEST(Mat4OrthoReversedZ, XY_ScaledByWidthHeight) {
    // XY mapping must be identical to standard orthographic — only Z changes.
    constexpr float kWidth  = 20.0f;
    constexpr float kHeight = 10.0f;
    Mat4 proj = Mat4::OrthographicReversedZ(kWidth, kHeight, 1.0f, 100.0f);

    // X = half-width should project to NDC x = 1.0
    Vec4 clip_x = proj * Vec4(kWidth * 0.5f, 0.0f, 1.0f, 1.0f);
    EXPECT_NEAR(clip_x.x / clip_x.w, 1.0f, 1e-5f)
        << "OrthographicReversedZ X: half-width should map to NDC x = 1.0";

    // Y = half-height should project to NDC y = 1.0
    Vec4 clip_y = proj * Vec4(0.0f, kHeight * 0.5f, 1.0f, 1.0f);
    EXPECT_NEAR(clip_y.y / clip_y.w, 1.0f, 1e-5f)
        << "OrthographicReversedZ Y: half-height should map to NDC y = 1.0";
}

// ─────────────────────────────────────────────────────────────────────────────
// Cross-variant consistency checks
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4OrthoConvention, ReversedZ_IsComplementOfStandardZ_AtEveryDepth) {
    // For the same frustum, standard-Z NDC z + reversed-Z NDC z == 1.0 at
    // every depth (the two conventions are exact bitwise complements when the
    // near offset is 0, and linear complements otherwise — verified here as
    // the near-boundary matching convention).
    //
    // Only valid when near=0 and the linear range spans [0, far].
    constexpr float kNear = 0.0f;
    constexpr float kFar  = 200.0f;
    Mat4 std_proj = Mat4::Orthographic(10.0f, 10.0f, kNear, kFar);
    Mat4 rev_proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, kNear, kFar);

    const int kSamples = 5;
    for (int i = 0; i <= kSamples; ++i) {
        const float t      = static_cast<float>(i) / static_cast<float>(kSamples);
        const float worldZ = kNear + t * (kFar - kNear);
        const float std_z  = ProjectDepth(std_proj, worldZ);
        const float rev_z  = ProjectDepth(rev_proj, worldZ);

        EXPECT_NEAR(std_z + rev_z, 1.0f, 1e-5f)
            << "Standard-Z (" << std_z << ") + ReversedZ (" << rev_z
            << ") must sum to 1.0 for world z = " << worldZ;
    }
}

TEST(Mat4OrthoConvention, NearAndFarMappings_AreExactlySwapped) {
    // ReversedZ(near) == StandardZ(far) and ReversedZ(far) == StandardZ(near).
    constexpr float kNear = 1.0f;
    constexpr float kFar  = 500.0f;
    Mat4 std_proj = Mat4::Orthographic(10.0f, 10.0f, kNear, kFar);
    Mat4 rev_proj = Mat4::OrthographicReversedZ(10.0f, 10.0f, kNear, kFar);

    const float std_near = ProjectDepth(std_proj, kNear);  // must == 0
    const float std_far  = ProjectDepth(std_proj, kFar);   // must == 1
    const float rev_near = ProjectDepth(rev_proj, kNear);  // must == 1
    const float rev_far  = ProjectDepth(rev_proj, kFar);   // must == 0

    EXPECT_NEAR(rev_near, std_far, 1e-5f)
        << "ReversedZ(near) must equal StandardZ(far)";
    EXPECT_NEAR(rev_far, std_near, 1e-5f)
        << "ReversedZ(far) must equal StandardZ(near)";
}

TEST(Mat4OrthoConvention, LargeDepthRange_NoNumericalDrift) {
    // A very wide depth range (common for shadow maps) should not cause
    // the near/far boundary values to drift significantly from 0 or 1.
    constexpr float kNear = 0.1f;
    constexpr float kFar  = 100000.0f;
    Mat4 rev_proj = Mat4::OrthographicReversedZ(10000.0f, 10000.0f, kNear, kFar);

    EXPECT_NEAR(ProjectDepth(rev_proj, kNear), 1.0f, 1e-4f)
        << "Reversed-Z near boundary must be 1.0 even with large depth range";
    EXPECT_NEAR(ProjectDepth(rev_proj, kFar), 0.0f, 1e-4f)
        << "Reversed-Z far boundary must be 0.0 even with large depth range";
}
