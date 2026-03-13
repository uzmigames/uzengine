/// UzEngine Camera Convenience Method Tests
///
/// Covers SetHorizontalFOV(), SetPerspectiveDegrees(), and SetLookFromAngles()
/// which are high-level wrappers over SetPerspective() / LookAt().

#include <gtest/gtest.h>

#include "camera.h"
#include "vector3.h"
#include "mat4.h"

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
// Camera::SetHorizontalFOV()
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera_SetHorizontalFOV, SetsProjectionMode_ToPerspective) {
    Camera cam;
    cam.SetHorizontalFOV(90.f, 16.f / 9.f, 0.1f, 1000.f);
    EXPECT_EQ(cam.GetProjectionMode(), ProjectionMode::Perspective);
}

TEST(Camera_SetHorizontalFOV, Aspect16by9_FOV90_FovYMatchesFormula) {
    // vFOV = 2 * atan(tan(hFOV/2) / aspect)
    const float hFovDeg  = 90.f;
    const float aspect   = 16.f / 9.f;
    const float hFovRad  = hFovDeg * XM_PI / 180.f;
    const float expected = 2.f * std::atan(std::tan(hFovRad * 0.5f) / aspect);

    Camera cam;
    cam.SetHorizontalFOV(hFovDeg, aspect, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetFovY(), expected, kEps)
        << "SetHorizontalFOV(90, 16/9) vFOV should match 2*atan(tan(45deg)/aspect)";
}

TEST(Camera_SetHorizontalFOV, Aspect1by1_FOV90_FovYIs90Degrees) {
    // With square aspect (1:1), hFOV == vFOV for any angle.
    const float hFovDeg = 90.f;
    const float aspect  = 1.f;
    const float hFovRad = hFovDeg * XM_PI / 180.f;
    const float expected = 2.f * std::atan(std::tan(hFovRad * 0.5f) / aspect);

    Camera cam;
    cam.SetHorizontalFOV(hFovDeg, aspect, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetFovY(), expected, kEps)
        << "For square aspect, hFOV == vFOV";
}

TEST(Camera_SetHorizontalFOV, WiderAspect_ProducesNarrowerVerticalFOV) {
    // For the same hFOV, a wider aspect produces a narrower vFOV.
    Camera camNarrow, camWide;
    camNarrow.SetHorizontalFOV(90.f, 1.f,        0.1f, 1000.f);
    camWide.SetHorizontalFOV(  90.f, 16.f / 9.f, 0.1f, 1000.f);

    EXPECT_LT(camWide.GetFovY(), camNarrow.GetFovY())
        << "Wider aspect should produce narrower vertical FOV";
}

TEST(Camera_SetHorizontalFOV, NearAndFarPlanes_AreStoredCorrectly) {
    Camera cam;
    cam.SetHorizontalFOV(90.f, 16.f / 9.f, 0.5f, 500.f);

    EXPECT_NEAR(cam.GetNearPlane(), 0.5f, kEps);
    EXPECT_NEAR(cam.GetFarPlane(),  500.f, kEps);
}

TEST(Camera_SetHorizontalFOV, AspectRatio_IsStoredCorrectly) {
    const float aspect = 16.f / 9.f;
    Camera cam;
    cam.SetHorizontalFOV(90.f, aspect, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetAspectRatio(), aspect, kEps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera::SetPerspectiveDegrees()
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera_SetPerspectiveDegrees, SetsProjectionMode_ToPerspective) {
    Camera cam;
    cam.SetPerspectiveDegrees(60.f, 16.f / 9.f, 0.1f, 1000.f);
    EXPECT_EQ(cam.GetProjectionMode(), ProjectionMode::Perspective);
}

TEST(Camera_SetPerspectiveDegrees, FOV60Degrees_GetFovYMatchesRadians) {
    const float vFovDeg = 60.f;
    const float expected = vFovDeg * XM_PI / 180.f;

    Camera cam;
    cam.SetPerspectiveDegrees(vFovDeg, 16.f / 9.f, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetFovY(), expected, kEps)
        << "SetPerspectiveDegrees(60) should give FovY = 60 * pi/180";
}

TEST(Camera_SetPerspectiveDegrees, FOV45Degrees_GetFovYMatchesRadians) {
    const float vFovDeg = 45.f;
    const float expected = vFovDeg * XM_PI / 180.f;

    Camera cam;
    cam.SetPerspectiveDegrees(vFovDeg, 16.f / 9.f, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetFovY(), expected, kEps);
}

TEST(Camera_SetPerspectiveDegrees, FOV90Degrees_GetFovYMatchesRadians) {
    const float vFovDeg = 90.f;
    const float expected = vFovDeg * XM_PI / 180.f;

    Camera cam;
    cam.SetPerspectiveDegrees(vFovDeg, 16.f / 9.f, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetFovY(), expected, kEps);
}

TEST(Camera_SetPerspectiveDegrees, NearAndFarPlanes_AreStoredCorrectly) {
    Camera cam;
    cam.SetPerspectiveDegrees(60.f, 16.f / 9.f, 0.01f, 2000.f);

    EXPECT_NEAR(cam.GetNearPlane(), 0.01f, kEps);
    EXPECT_NEAR(cam.GetFarPlane(),  2000.f, 0.01f);
}

TEST(Camera_SetPerspectiveDegrees, AspectRatio_IsStoredCorrectly) {
    const float aspect = 4.f / 3.f;
    Camera cam;
    cam.SetPerspectiveDegrees(60.f, aspect, 0.1f, 1000.f);

    EXPECT_NEAR(cam.GetAspectRatio(), aspect, kEps);
}

TEST(Camera_SetPerspectiveDegrees, ProjectionMatrix_IsNotIdentity) {
    Camera cam;
    cam.SetPerspectiveDegrees(60.f, 16.f / 9.f, 0.1f, 1000.f);

    Mat4 proj = cam.GetProjection();
    Mat4 id   = Mat4::Identity();
    bool allSame = true;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(proj.data()[i] - id.data()[i]) > kEps) {
            allSame = false;
            break;
        }
    }
    EXPECT_FALSE(allSame) << "Perspective projection should not be identity";
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera::SetLookFromAngles()
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera_SetLookFromAngles, SetsPosition_Correctly) {
    const Vec3 pos{0.f, 0.f, 5.f};
    Camera cam;
    cam.SetLookFromAngles(pos, -90.f, -20.f);

    ExpectVec3Near(cam.GetPosition(), pos, kEps, "position");
}

TEST(Camera_SetLookFromAngles, ZeroAngles_ForwardIsPositiveX) {
    // yaw=0, pitch=0 → forward = +X (engine's default forward direction)
    Camera cam;
    cam.SetLookFromAngles(Vec3{0.f, 0.f, 0.f}, 0.f, 0.f);

    Vec3 fwd = cam.GetForward();
    EXPECT_NEAR(fwd.Length(), 1.f, kEps) << "Forward must be unit length";

    // The forward vector must be +X or very close to it when angles are zero.
    EXPECT_GT(fwd.x, 0.9f)
        << "At yaw=0, pitch=0 the forward vector should point mostly along +X";
    EXPECT_NEAR(fwd.y, 0.f, 0.1f)
        << "At yaw=0, pitch=0 the forward vector should have no Y component";
}

TEST(Camera_SetLookFromAngles, ForwardIsUnitLength_AfterArbitraryAngles) {
    Camera cam;
    cam.SetLookFromAngles(Vec3{0.f, 0.f, 5.f}, -90.f, -20.f);

    Vec3 fwd = cam.GetForward();
    EXPECT_NEAR(fwd.Length(), 1.f, kEps) << "Forward vector must be unit length";
}

TEST(Camera_SetLookFromAngles, DifferentPositions_PositionStoredCorrectly) {
    const Vec3 positions[] = {
        {1.f, 2.f, 3.f},
        {-10.f, 5.f, 0.f},
        {0.f, 0.f, 0.f},
        {100.f, -50.f, 200.f},
    };

    for (const Vec3& pos : positions) {
        Camera cam;
        cam.SetLookFromAngles(pos, 0.f, 0.f);
        ExpectVec3Near(cam.GetPosition(), pos, kEps, "position round-trip");
    }
}

TEST(Camera_SetLookFromAngles, NegativePitch_ForwardPointsDownward) {
    // Negative pitch should tilt the camera downward (negative Z or Y component).
    Camera cam;
    cam.SetLookFromAngles(Vec3{0.f, 0.f, 0.f}, 0.f, -45.f);

    Vec3 fwd = cam.GetForward();
    // With pitch=-45, the forward vector should have a significant downward component.
    // In UzEngine's LH Z-up system, "down" is negative Z.
    EXPECT_NEAR(fwd.Length(), 1.f, kEps) << "Forward must be unit length";
    // The downward tilt should yield a negative Z component.
    EXPECT_LT(fwd.z, 0.f)
        << "Negative pitch should produce a forward vector with negative Z (downward tilt)";
}

TEST(Camera_SetLookFromAngles, PositivePitch_ForwardPointsUpward) {
    Camera cam;
    cam.SetLookFromAngles(Vec3{0.f, 0.f, 0.f}, 0.f, 45.f);

    Vec3 fwd = cam.GetForward();
    EXPECT_NEAR(fwd.Length(), 1.f, kEps) << "Forward must be unit length";
    EXPECT_GT(fwd.z, 0.f)
        << "Positive pitch should produce a forward vector with positive Z (upward tilt)";
}

TEST(Camera_SetLookFromAngles, ViewMatrixIsNotIdentity_AfterSettingAngles) {
    Camera cam;
    cam.SetLookFromAngles(Vec3{5.f, 0.f, 0.f}, 0.f, 0.f);

    Mat4 view = cam.GetView();
    Mat4 id   = Mat4::Identity();
    bool allSame = true;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(view.data()[i] - id.data()[i]) > kEps) {
            allSame = false;
            break;
        }
    }
    EXPECT_FALSE(allSame) << "View matrix must not be identity when camera is offset";
}
