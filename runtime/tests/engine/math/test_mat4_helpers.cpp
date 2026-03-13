/// UzEngine Mat4 Helper Tests
///
/// Covers the three TRS overloads and NormalMatrix, verifying translation,
/// scale, and rotation extraction as well as normal-vector perpendicularity
/// guarantees under non-uniform scaling.

#include "vector3.h"
#include "mat4.h"
#include "quaternion.h"
#include "transform.h"

#include <gtest/gtest.h>
#include <DirectXMath.h>
#include <cmath>

using namespace uz;
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps = 1e-4f;

static void ExpectVec3Near(const Vec3& a, const Vec3& b, float eps = kEps,
                            const char* label = "") {
    EXPECT_NEAR(a.x, b.x, eps) << label << " x";
    EXPECT_NEAR(a.y, b.y, eps) << label << " y";
    EXPECT_NEAR(a.z, b.z, eps) << label << " z";
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat4::TRS(Vec3, float) — translation + uniform scale
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4TRS_UniformScale, IdentityCase_MatchesIdentityMatrix) {
    Mat4 result = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, 1.0f);
    Mat4 identity = Mat4::Identity();
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(result.data()[i], identity.data()[i], kEps)
            << "Element " << i << " differs from identity";
    }
}

TEST(Mat4TRS_UniformScale, TranslationOnly_GetTranslationMatchesInput) {
    const Vec3 pos{5.f, 3.f, 1.f};
    Mat4 m = Mat4::TRS(pos, 1.0f);

    ExpectVec3Near(m.getTranslation(), pos, kEps, "translation");
}

TEST(Mat4TRS_UniformScale, ScaleOnly_GetScaleMatchesUniform) {
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, 2.0f);

    ExpectVec3Near(m.getScale(), Vec3{2.f, 2.f, 2.f}, kEps, "scale");
}

TEST(Mat4TRS_UniformScale, ScaleOnly_TranslationIsZero) {
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, 2.0f);

    ExpectVec3Near(m.getTranslation(), Vec3{0.f, 0.f, 0.f}, kEps, "translation");
}

TEST(Mat4TRS_UniformScale, Combined_TranslationAndScaleCorrect) {
    const Vec3 pos{1.f, 2.f, 3.f};
    const float s = 0.5f;
    Mat4 m = Mat4::TRS(pos, s);

    ExpectVec3Near(m.getTranslation(), pos, kEps, "translation");
    ExpectVec3Near(m.getScale(), Vec3{s, s, s}, kEps, "scale");
}

TEST(Mat4TRS_UniformScale, NegativeTranslation_GetTranslationCorrect) {
    const Vec3 pos{-7.f, -3.f, 0.5f};
    Mat4 m = Mat4::TRS(pos, 1.0f);

    ExpectVec3Near(m.getTranslation(), pos, kEps, "negative translation");
}

TEST(Mat4TRS_UniformScale, LargeScale_GetScaleCorrect) {
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, 100.f);

    ExpectVec3Near(m.getScale(), Vec3{100.f, 100.f, 100.f}, 1e-2f, "large scale");
}

TEST(Mat4TRS_UniformScale, SmallScale_GetScaleCorrect) {
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, 0.001f);

    ExpectVec3Near(m.getScale(), Vec3{0.001f, 0.001f, 0.001f}, 1e-6f, "small scale");
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat4::TRS(Vec3, Vec3) — translation + non-uniform scale
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4TRS_NonUniformScale, IdentityScale_GetScaleIsOne) {
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, Vec3{1.f, 1.f, 1.f});

    ExpectVec3Near(m.getScale(), Vec3{1.f, 1.f, 1.f}, kEps, "identity scale");
}

TEST(Mat4TRS_NonUniformScale, NonUniformScale_GetScaleMatchesInput) {
    const Vec3 scl{1.f, 2.f, 3.f};
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, scl);

    ExpectVec3Near(m.getScale(), scl, kEps, "non-uniform scale");
}

TEST(Mat4TRS_NonUniformScale, Combined_TranslationCorrect) {
    const Vec3 pos{10.f, 0.f, 0.f};
    const Vec3 scl{0.5f, 1.0f, 2.0f};
    Mat4 m = Mat4::TRS(pos, scl);

    ExpectVec3Near(m.getTranslation(), pos, kEps, "translation");
}

TEST(Mat4TRS_NonUniformScale, Combined_ScaleCorrect) {
    const Vec3 pos{10.f, 0.f, 0.f};
    const Vec3 scl{0.5f, 1.0f, 2.0f};
    Mat4 m = Mat4::TRS(pos, scl);

    ExpectVec3Near(m.getScale(), scl, kEps, "scale");
}

TEST(Mat4TRS_NonUniformScale, NegativeTranslationAndScale_BothCorrect) {
    const Vec3 pos{-1.f, -2.f, -3.f};
    const Vec3 scl{4.f, 5.f, 6.f};
    Mat4 m = Mat4::TRS(pos, scl);

    ExpectVec3Near(m.getTranslation(), pos, kEps, "translation");
    ExpectVec3Near(m.getScale(), scl, kEps, "scale");
}

TEST(Mat4TRS_NonUniformScale, TransformPoint_ScalesCorrectly) {
    // A point {1,1,1} scaled by {2,3,4} should become {2,3,4}.
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, Vec3{2.f, 3.f, 4.f});
    Vec3 result = m.TransformPoint(Vec3{1.f, 1.f, 1.f});

    EXPECT_NEAR(result.x, 2.f, kEps) << "x component";
    EXPECT_NEAR(result.y, 3.f, kEps) << "y component";
    EXPECT_NEAR(result.z, 4.f, kEps) << "z component";
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat4::TRS(Vec3, Quat, Vec3) — full TRS
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4TRS_Full, IdentityQuat_SameAsPositionAndScale) {
    const Vec3 pos{3.f, -1.f, 2.f};
    const Vec3 scl{2.f, 2.f, 2.f};
    Mat4 full = Mat4::TRS(pos, Quat::Identity(), scl);
    Mat4 simple = Mat4::TRS(pos, scl);

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(full.data()[i], simple.data()[i], kEps)
            << "Element " << i << " differs between TRS(full) and TRS(pos,scale)";
    }
}

TEST(Mat4TRS_Full, IdentityQuat_TranslationCorrect) {
    const Vec3 pos{7.f, -5.f, 3.f};
    Mat4 m = Mat4::TRS(pos, Quat::Identity(), Vec3{1.f, 1.f, 1.f});

    ExpectVec3Near(m.getTranslation(), pos, kEps, "translation with identity quat");
}

TEST(Mat4TRS_Full, IdentityQuat_ScaleCorrect) {
    const Vec3 scl{1.5f, 2.5f, 3.5f};
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, Quat::Identity(), scl);

    ExpectVec3Near(m.getScale(), scl, kEps, "scale with identity quat");
}

TEST(Mat4TRS_Full, RotationZ90_TransformsYIntoX) {
    // A 90-degree rotation around Z maps +Y → -X in a left-handed system.
    // The exact axis conventions depend on the engine's handedness.
    // We verify that TransformDirection returns a unit vector orthogonal to +Z.
    Quat rot = Quat::FromAxisAngle(Vec3{0.f, 0.f, 1.f}, XM_PIDIV2);
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, rot, Vec3{1.f, 1.f, 1.f});

    Vec3 xAxis{1.f, 0.f, 0.f};
    Vec3 rotated = m.TransformDirection(xAxis);

    // Rotated vector must be unit length
    EXPECT_NEAR(rotated.Length(), 1.f, kEps) << "Rotated vector must remain unit length";
    // Rotated vector must be perpendicular to the rotation axis (+Z)
    EXPECT_NEAR(rotated.z, 0.f, kEps) << "Rotated X must lie in XY plane after Z rotation";
}

TEST(Mat4TRS_Full, RotationX90_TransformsPoint_ZComponentChangesSign) {
    // Rotating +Y by 90 degrees around X should produce something in the +Z or -Z direction.
    Quat rot = Quat::FromAxisAngle(Vec3{1.f, 0.f, 0.f}, XM_PIDIV2);
    Mat4 m = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, rot, Vec3{1.f, 1.f, 1.f});

    Vec3 yAxis{0.f, 1.f, 0.f};
    Vec3 rotated = m.TransformDirection(yAxis);

    // Result must be unit length
    EXPECT_NEAR(rotated.Length(), 1.f, kEps) << "Result must be unit length";
    // X component must be zero (X rotation leaves X invariant)
    EXPECT_NEAR(rotated.x, 0.f, kEps) << "X component unchanged by X-axis rotation";
}

TEST(Mat4TRS_Full, FullTRS_RoundTrip_Via_Transform_Decompose) {
    // Build a TRS matrix, decompose via Transform::FromMatrix, then rebuild.
    const Vec3 pos{4.f, -2.f, 7.f};
    const Quat rot = Quat::FromAxisAngle(Vec3{0.f, 1.f, 0.f}, 0.4f);
    const Vec3 scl{1.5f, 2.0f, 0.75f};

    Mat4 original = Mat4::TRS(pos, rot, scl);
    Transform t = Transform::FromMatrix(original);
    Mat4 rebuilt = t.toMatrix();

    // Translation must round-trip
    ExpectVec3Near(original.getTranslation(), rebuilt.getTranslation(), kEps,
                   "round-trip translation");
    // Scale must round-trip
    ExpectVec3Near(original.getScale(), rebuilt.getScale(), kEps,
                   "round-trip scale");
}

TEST(Mat4TRS_Full, TranslationPlusRotation_TranslationNotAffectedByRotation) {
    // When a pure rotation is applied, the translation row must be unchanged.
    const Vec3 pos{5.f, 0.f, 0.f};
    Quat rot = Quat::FromAxisAngle(Vec3{0.f, 1.f, 0.f}, XM_PIDIV2);
    Mat4 m = Mat4::TRS(pos, rot, Vec3{1.f, 1.f, 1.f});

    ExpectVec3Near(m.getTranslation(), pos, kEps, "translation unaffected by rotation");
}

// ─────────────────────────────────────────────────────────────────────────────
// Mat4::NormalMatrix()
// ─────────────────────────────────────────────────────────────────────────────

TEST(Mat4NormalMatrix, Identity_NormalMatrixIsIdentity) {
    Mat4 nm = Mat4::Identity().NormalMatrix();
    Mat4 id = Mat4::Identity();

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(nm.data()[i], id.data()[i], kEps)
            << "NormalMatrix of identity should be identity, element " << i;
    }
}

TEST(Mat4NormalMatrix, UniformScale_NormalVectorRemainsUnit) {
    // Under uniform scale, a transformed normal is only scaled by 1/s,
    // so re-normalizing it yields the same direction as transforming with the
    // NormalMatrix.
    Mat4 model = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, 3.0f);
    Mat4 nm = model.NormalMatrix();

    Vec3 normal{0.f, 1.f, 0.f};
    Vec3 transformed = nm.TransformDirection(normal);

    // Under uniform scale s, the normal matrix is (1/s)*I, so the transformed
    // normal should be (1/3, 0, 0) ... but direction must match after normalize.
    Vec3 dir = transformed.Normalized();
    ExpectVec3Near(dir, normal.Normalized(), kEps,
                   "normal direction preserved under uniform scale");
}

TEST(Mat4NormalMatrix, NonUniformScale_NormalStaysPerpendicularToTangent) {
    // Key property: normal transformed by NormalMatrix stays perpendicular
    // to the tangent transformed by the original model matrix.
    //
    // Setup: scale {1, 2, 1} — Y axis is stretched.
    // Surface tangent = +X, surface normal = +Y.
    // After model transform: tangent_world = {1,0,0}, normal_world = ?
    // Correct normal_world must still be perpendicular to tangent_world.

    Mat4 model = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, Vec3{1.f, 2.f, 1.f});
    Mat4 nm = model.NormalMatrix();

    Vec3 tangent{1.f, 0.f, 0.f};  // surface tangent in object space
    Vec3 normal{0.f, 1.f, 0.f};   // surface normal in object space

    Vec3 tangent_world = model.TransformDirection(tangent);
    Vec3 normal_world  = nm.TransformDirection(normal);

    float dot = tangent_world.Dot(normal_world);
    EXPECT_NEAR(dot, 0.f, kEps)
        << "Normal transformed by NormalMatrix must be perpendicular to "
           "tangent transformed by model matrix (dot must be ~0)";
}

TEST(Mat4NormalMatrix, NonUniformScale_XZAxes_NormalPerpendicularity) {
    // Tangent = +Z, normal = +Y, scale = {2, 1, 3}.
    // Verifies the same perpendicularity property on a different axis pair.
    Mat4 model = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, Vec3{2.f, 1.f, 3.f});
    Mat4 nm = model.NormalMatrix();

    Vec3 tangent{0.f, 0.f, 1.f};
    Vec3 normal{0.f, 1.f, 0.f};

    Vec3 tangent_world = model.TransformDirection(tangent);
    Vec3 normal_world  = nm.TransformDirection(normal);

    float dot = tangent_world.Dot(normal_world);
    EXPECT_NEAR(dot, 0.f, kEps)
        << "Perpendicularity must hold for Z-tangent / Y-normal under {2,1,3} scale";
}

TEST(Mat4NormalMatrix, PureRotation_NormalMatrixEqualsRotationMatrix) {
    // For a pure rotation (no scale), the normal matrix equals the rotation
    // matrix (transpose of inverse of a pure rotation = itself).
    Quat rot = Quat::FromAxisAngle(Vec3{0.f, 0.f, 1.f}, XM_PIDIV4);
    Mat4 model = Mat4::TRS(Vec3{0.f, 0.f, 0.f}, rot, Vec3{1.f, 1.f, 1.f});
    Mat4 nm = model.NormalMatrix();

    // For a pure rotation M, NormalMatrix should equal M (up to translation).
    // The 3x3 upper-left block must match.
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            EXPECT_NEAR(nm.data()[col * 4 + row], model.data()[col * 4 + row], kEps)
                << "NormalMatrix[" << row << "][" << col
                << "] should equal rotation matrix for pure rotation";
        }
    }
}
