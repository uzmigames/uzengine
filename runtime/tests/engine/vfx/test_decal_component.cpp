/// Unit tests for DecalComponent.
///
/// Covers: default field values, GetProjectionMatrix (determinant, center mapping),
/// Tick (age advancement, expiry, linear fade), FadeOut (immediate and timed),
/// MarkForDestruction, IsExpired, and DecalBlendModeToString for every enum value.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "engine/vfx/decals/decal_component.h"

#include <cmath>
#include <cstring>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the determinant of a 3x3 submatrix from the top-left corner of a Mat4.
/// Used to verify that the projection matrix is non-singular.
float Det3x3(const Mat4& m) {
    const float a = m.m._11, b = m.m._12, c = m.m._13;
    const float d = m.m._21, e = m.m._22, f = m.m._23;
    const float g = m.m._31, h = m.m._32, i = m.m._33;
    return a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
}

/// Transforms a Vec3 point through a Mat4 using homogeneous coordinates.
/// Returns the transformed x, y, z after division by w.
Vec3 TransformPoint(const Mat4& m, const Vec3& p) {
    const float x = m.m._11 * p.x + m.m._21 * p.y + m.m._31 * p.z + m.m._41;
    const float y = m.m._12 * p.x + m.m._22 * p.y + m.m._32 * p.z + m.m._42;
    const float z = m.m._13 * p.x + m.m._23 * p.y + m.m._33 * p.z + m.m._43;
    const float w = m.m._14 * p.x + m.m._24 * p.y + m.m._34 * p.z + m.m._44;
    const float invW = (std::fabs(w) > 1e-7f) ? 1.0f / w : 1.0f;
    return Vec3{x * invW, y * invW, z * invW};
}

} // anonymous namespace

// =============================================================================
// Default field values
// =============================================================================

TEST(DecalComponent, DefaultValues_HalfExtents_Are_1_1_0p5) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.halfExtents.x, 1.0f);
    EXPECT_FLOAT_EQ(d.halfExtents.y, 1.0f);
    EXPECT_FLOAT_EQ(d.halfExtents.z, 0.5f);
}

TEST(DecalComponent, DefaultValues_Position_IsZero) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.position.x, 0.0f);
    EXPECT_FLOAT_EQ(d.position.y, 0.0f);
    EXPECT_FLOAT_EQ(d.position.z, 0.0f);
}

TEST(DecalComponent, DefaultValues_Rotation_IsIdentity) {
    DecalComponent d;
    // Identity quaternion: (0, 0, 0, 1)
    EXPECT_FLOAT_EQ(d.rotation.x, 0.0f);
    EXPECT_FLOAT_EQ(d.rotation.y, 0.0f);
    EXPECT_FLOAT_EQ(d.rotation.z, 0.0f);
    EXPECT_FLOAT_EQ(d.rotation.w, 1.0f);
}

TEST(DecalComponent, DefaultValues_NormalIntensity_Is_0p8) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.normalIntensity, 0.8f);
}

TEST(DecalComponent, DefaultValues_RoughnessOverride_Is_NegativeOne) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.roughnessOverride, -1.0f);
}

TEST(DecalComponent, DefaultValues_BlendMode_IsOpaque) {
    DecalComponent d;
    EXPECT_EQ(d.blendMode, DecalBlendMode::Opaque);
}

TEST(DecalComponent, DefaultValues_Lifetime_Is_10) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.lifetime, 10.0f);
}

TEST(DecalComponent, DefaultValues_Age_IsZero) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.age, 0.0f);
}

TEST(DecalComponent, DefaultValues_FadeAlpha_IsOne) {
    DecalComponent d;
    EXPECT_FLOAT_EQ(d.fadeAlpha, 1.0f);
}

TEST(DecalComponent, DefaultValues_IsActive_IsTrue) {
    DecalComponent d;
    EXPECT_TRUE(d.isActive);
}

TEST(DecalComponent, DefaultValues_SortOrder_IsZero) {
    DecalComponent d;
    EXPECT_EQ(d.sortOrder, 0);
}

TEST(DecalComponent, DefaultValues_MaterialPath_IsEmpty) {
    DecalComponent d;
    EXPECT_TRUE(d.materialPath.empty());
}

// =============================================================================
// GetProjectionMatrix — determinant (non-singular)
// =============================================================================

TEST(DecalComponent, GetProjectionMatrix_IdentityDecal_DeterminantIsNonZero) {
    DecalComponent d;
    // Default: position=(0,0,0), rotation=identity, halfExtents=(1,1,0.5)
    const Mat4 proj = d.GetProjectionMatrix();
    const float det = Det3x3(proj);
    EXPECT_GT(std::fabs(det), 1e-5f)
        << "Projection matrix must be non-singular for default decal";
}

TEST(DecalComponent, GetProjectionMatrix_CustomHalfExtents_DeterminantIsNonZero) {
    DecalComponent d;
    d.halfExtents = Vec3{2.0f, 3.0f, 1.5f};
    const Mat4 proj = d.GetProjectionMatrix();
    const float det = Det3x3(proj);
    EXPECT_GT(std::fabs(det), 1e-5f)
        << "Projection matrix must be non-singular for any valid half-extents";
}

// =============================================================================
// GetProjectionMatrix — center position maps to origin
// =============================================================================

TEST(DecalComponent, GetProjectionMatrix_CenterPosition_MapsToOrigin) {
    DecalComponent d;
    d.position = Vec3{5.0f, 3.0f, -2.0f};
    d.halfExtents = Vec3{1.0f, 1.0f, 0.5f};
    // Identity rotation: no rotation
    d.rotation = Quat::Identity();

    const Mat4 proj = d.GetProjectionMatrix();

    // The decal's own center in world space should transform to (0,0,0)
    const Vec3 result = TransformPoint(proj, d.position);
    EXPECT_NEAR(result.x, 0.0f, 1e-4f)
        << "Center X must map to 0 after projection";
    EXPECT_NEAR(result.y, 0.0f, 1e-4f)
        << "Center Y must map to 0 after projection";
    EXPECT_NEAR(result.z, 0.0f, 1e-4f)
        << "Center Z must map to 0 after projection";
}

TEST(DecalComponent, GetProjectionMatrix_HalfExtentBoundary_MapsToOne) {
    // A point exactly at the +X boundary of the OBB (position.x + halfExtents.x)
    // should map to x=1 in decal-local normalized space.
    DecalComponent d;
    d.position = Vec3{0.0f, 0.0f, 0.0f};
    d.halfExtents = Vec3{2.0f, 2.0f, 1.0f};
    d.rotation = Quat::Identity();

    const Mat4 proj = d.GetProjectionMatrix();

    // Point at the +X boundary
    const Vec3 boundary{2.0f, 0.0f, 0.0f};
    const Vec3 result = TransformPoint(proj, boundary);
    EXPECT_NEAR(result.x, 1.0f, 1e-4f)
        << "+X boundary must map to x=1.0 in normalized decal space";
}

TEST(DecalComponent, GetProjectionMatrix_NegativeHalfExtentBoundary_MapsToNegativeOne) {
    DecalComponent d;
    d.position = Vec3{0.0f, 0.0f, 0.0f};
    d.halfExtents = Vec3{2.0f, 2.0f, 1.0f};
    d.rotation = Quat::Identity();

    const Mat4 proj = d.GetProjectionMatrix();

    // Point at the -X boundary
    const Vec3 boundary{-2.0f, 0.0f, 0.0f};
    const Vec3 result = TransformPoint(proj, boundary);
    EXPECT_NEAR(result.x, -1.0f, 1e-4f)
        << "-X boundary must map to x=-1.0 in normalized decal space";
}

// =============================================================================
// Tick — age advancement
// =============================================================================

TEST(DecalComponent, Tick_AdvancesAge_ByDt) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.Tick(0.016f);
    EXPECT_NEAR(d.age, 0.016f, 1e-5f);
}

TEST(DecalComponent, Tick_MultipleCalls_AccumulatesAge) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.Tick(0.1f);
    d.Tick(0.2f);
    d.Tick(0.3f);
    EXPECT_NEAR(d.age, 0.6f, 1e-5f);
}

TEST(DecalComponent, Tick_WhileInactive_DoesNotAdvanceAge) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.isActive = false;
    d.age = 2.0f;
    d.Tick(1.0f);
    EXPECT_NEAR(d.age, 2.0f, 1e-5f)
        << "Tick must be a no-op when isActive is false";
}

// =============================================================================
// Tick — linear fade alpha computation
// =============================================================================

TEST(DecalComponent, Tick_AtQuarterLifetime_FadeAlphaIsThreeQuarters) {
    DecalComponent d;
    d.lifetime = 8.0f;
    d.Tick(2.0f);
    // fadeAlpha = 1.0 - (2/8) = 0.75
    EXPECT_NEAR(d.fadeAlpha, 0.75f, 1e-4f);
    EXPECT_TRUE(d.isActive);
}

TEST(DecalComponent, Tick_AtHalfLifetime_FadeAlphaIsHalf) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.Tick(5.0f);
    // fadeAlpha = 1.0 - (5/10) = 0.5
    EXPECT_NEAR(d.fadeAlpha, 0.5f, 1e-4f);
    EXPECT_TRUE(d.isActive);
}

TEST(DecalComponent, Tick_JustBeforeExpiry_FadeAlphaIsNearZero) {
    DecalComponent d;
    d.lifetime = 4.0f;
    // Tick to age = 3.9 -- still active, very low fade
    d.Tick(3.9f);
    EXPECT_NEAR(d.fadeAlpha, 0.025f, 1e-4f);
    EXPECT_TRUE(d.isActive);
}

TEST(DecalComponent, Tick_AtExactLifetime_IsActiveBecomesFlase_FadeAlphaIsZero) {
    DecalComponent d;
    d.lifetime = 5.0f;
    d.Tick(5.0f);
    EXPECT_FALSE(d.isActive)
        << "isActive must be false when age reaches lifetime";
    EXPECT_FLOAT_EQ(d.fadeAlpha, 0.0f)
        << "fadeAlpha must be 0.0 when expired";
}

TEST(DecalComponent, Tick_BeyondLifetime_IsActiveBecomesFlase_FadeAlphaIsZero) {
    DecalComponent d;
    d.lifetime = 3.0f;
    d.Tick(10.0f);
    EXPECT_FALSE(d.isActive)
        << "isActive must be false when age exceeds lifetime";
    EXPECT_FLOAT_EQ(d.fadeAlpha, 0.0f)
        << "fadeAlpha must be 0.0 after expiry";
}

TEST(DecalComponent, Tick_AfterExpiry_FurtherTicksDoNotChangeAge) {
    // Once expired (isActive=false), subsequent Ticks are no-ops.
    DecalComponent d;
    d.lifetime = 2.0f;
    d.Tick(2.0f); // expires here
    ASSERT_FALSE(d.isActive);
    const float ageAfterExpiry = d.age;
    d.Tick(5.0f);
    EXPECT_NEAR(d.age, ageAfterExpiry, 1e-5f)
        << "age must not advance after the decal has expired";
}

// =============================================================================
// Tick — zero dt is a valid no-op
// =============================================================================

TEST(DecalComponent, Tick_ZeroDt_DoesNotChangeState) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.Tick(0.0f);
    EXPECT_FLOAT_EQ(d.age, 0.0f);
    EXPECT_FLOAT_EQ(d.fadeAlpha, 1.0f);
    EXPECT_TRUE(d.isActive);
}

// =============================================================================
// FadeOut
// =============================================================================

TEST(DecalComponent, FadeOut_PositiveDuration_SetsLifetimeToAgePlusDuration) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.age = 3.0f;
    d.FadeOut(2.0f);
    // lifetime should be reset to current age (3) + duration (2) = 5
    EXPECT_NEAR(d.lifetime, 5.0f, 1e-5f);
    EXPECT_TRUE(d.isActive)
        << "FadeOut with positive duration must not expire immediately";
}

TEST(DecalComponent, FadeOut_ZeroDuration_ExpiresImmediately) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.age = 3.0f;
    d.FadeOut(0.0f);
    EXPECT_FALSE(d.isActive)
        << "FadeOut(0) must expire the decal immediately";
    EXPECT_FLOAT_EQ(d.fadeAlpha, 0.0f)
        << "fadeAlpha must be 0 after immediate fade-out";
}

TEST(DecalComponent, FadeOut_NegativeDuration_ExpiresImmediately) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.age = 1.0f;
    d.FadeOut(-5.0f);
    EXPECT_FALSE(d.isActive)
        << "FadeOut(negative) must expire the decal immediately";
    EXPECT_FLOAT_EQ(d.fadeAlpha, 0.0f);
}

TEST(DecalComponent, FadeOut_ThenTick_FadesOverRemainingDuration) {
    DecalComponent d;
    d.lifetime = 20.0f;
    d.age = 5.0f;
    d.FadeOut(4.0f); // new lifetime = 5 + 4 = 9

    // After ticking 2 more seconds: age=7, fadeAlpha = 1 - (7/9)
    d.Tick(2.0f);
    const float expected = 1.0f - (7.0f / 9.0f);
    EXPECT_NEAR(d.fadeAlpha, expected, 1e-4f);
    EXPECT_TRUE(d.isActive);
}

TEST(DecalComponent, FadeOut_ThenTickFull_ExpiresAtNewLifetime) {
    DecalComponent d;
    d.lifetime = 20.0f;
    d.age = 0.0f;
    d.FadeOut(3.0f); // new lifetime = 0 + 3 = 3

    d.Tick(3.0f); // age reaches new lifetime
    EXPECT_FALSE(d.isActive);
    EXPECT_FLOAT_EQ(d.fadeAlpha, 0.0f);
}

// =============================================================================
// MarkForDestruction
// =============================================================================

TEST(DecalComponent, MarkForDestruction_SetsIsActiveToFalse) {
    DecalComponent d;
    d.MarkForDestruction();
    EXPECT_FALSE(d.isActive);
}

TEST(DecalComponent, MarkForDestruction_DoesNotChangeAge) {
    DecalComponent d;
    d.age = 3.5f;
    d.MarkForDestruction();
    EXPECT_FLOAT_EQ(d.age, 3.5f);
}

TEST(DecalComponent, MarkForDestruction_DoesNotChangeLifetime) {
    DecalComponent d;
    d.lifetime = 10.0f;
    d.MarkForDestruction();
    EXPECT_FLOAT_EQ(d.lifetime, 10.0f);
}

TEST(DecalComponent, MarkForDestruction_DoesNotChangeFadeAlpha) {
    DecalComponent d;
    d.fadeAlpha = 0.7f;
    d.MarkForDestruction();
    EXPECT_FLOAT_EQ(d.fadeAlpha, 0.7f)
        << "MarkForDestruction must not modify fadeAlpha";
}

// =============================================================================
// IsExpired
// =============================================================================

TEST(DecalComponent, IsExpired_ReturnsFalse_WhenAgeIsZeroAndLifetimeIsPositive) {
    DecalComponent d;
    d.age = 0.0f;
    d.lifetime = 10.0f;
    EXPECT_FALSE(d.IsExpired());
}

TEST(DecalComponent, IsExpired_ReturnsFalse_WhenAgeLessThanLifetime) {
    DecalComponent d;
    d.age = 4.9f;
    d.lifetime = 5.0f;
    EXPECT_FALSE(d.IsExpired());
}

TEST(DecalComponent, IsExpired_ReturnsTrue_WhenAgeEqualsLifetime) {
    DecalComponent d;
    d.age = 10.0f;
    d.lifetime = 10.0f;
    EXPECT_TRUE(d.IsExpired());
}

TEST(DecalComponent, IsExpired_ReturnsTrue_WhenAgeExceedsLifetime) {
    DecalComponent d;
    d.age = 15.0f;
    d.lifetime = 10.0f;
    EXPECT_TRUE(d.IsExpired());
}

TEST(DecalComponent, IsExpired_IsIndependentOfIsActive) {
    // IsExpired checks only age vs lifetime, not the isActive flag.
    DecalComponent d;
    d.age = 0.0f;
    d.lifetime = 5.0f;
    d.isActive = false;
    EXPECT_FALSE(d.IsExpired())
        << "IsExpired must be false if age < lifetime, even when not active";
}

// =============================================================================
// DecalBlendModeToString — all enum values and unknown
// =============================================================================

TEST(DecalBlendModeToString, Opaque_ReturnsCorrectString) {
    EXPECT_STREQ(DecalBlendModeToString(DecalBlendMode::Opaque), "Opaque");
}

TEST(DecalBlendModeToString, Additive_ReturnsCorrectString) {
    EXPECT_STREQ(DecalBlendModeToString(DecalBlendMode::Additive), "Additive");
}

TEST(DecalBlendModeToString, Multiply_ReturnsCorrectString) {
    EXPECT_STREQ(DecalBlendModeToString(DecalBlendMode::Multiply), "Multiply");
}

TEST(DecalBlendModeToString, Screen_ReturnsCorrectString) {
    EXPECT_STREQ(DecalBlendModeToString(DecalBlendMode::Screen), "Screen");
}

TEST(DecalBlendModeToString, UnknownValue_ReturnsUnknown) {
    const auto unknownMode = static_cast<DecalBlendMode>(255u);
    EXPECT_STREQ(DecalBlendModeToString(unknownMode), "Unknown");
}
