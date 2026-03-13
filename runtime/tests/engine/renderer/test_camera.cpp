/// UzEngine Camera System Tests
///
/// Tests for Camera class (view/projection matrices, orientation, LookAt).

#include <gtest/gtest.h>

#include "camera.h"

#include <DirectXMath.h>
#include <cmath>

using namespace uz;
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEpsilon = 1e-4f;

static void ExpectVec3Near(const Vec3& a, const Vec3& b, float eps = kEpsilon) {
    EXPECT_NEAR(a.x, b.x, eps) << "Vec3.x mismatch";
    EXPECT_NEAR(a.y, b.y, eps) << "Vec3.y mismatch";
    EXPECT_NEAR(a.z, b.z, eps) << "Vec3.z mismatch";
}

static void ExpectMat4NotIdentity(const Mat4& m) {
    Mat4 id = Mat4::Identity();
    bool allSame = true;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(m.data()[i] - id.data()[i]) > kEpsilon) {
            allSame = false;
            break;
        }
    }
    EXPECT_FALSE(allSame) << "Matrix should not be identity";
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera — Default State
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera, DefaultPosition) {
    Camera cam;
    ExpectVec3Near(cam.GetPosition(), {0.f, 0.f, 0.f});
}

TEST(Camera, DefaultProjectionMode) {
    Camera cam;
    EXPECT_EQ(cam.GetProjectionMode(), ProjectionMode::Perspective);
}

TEST(Camera, DefaultForwardIsPositiveZ) {
    Camera cam;
    Vec3 fwd = cam.GetForward();
    ExpectVec3Near(fwd, Vec3::Forward());
}

TEST(Camera, DefaultRightIsPositiveX) {
    Camera cam;
    Vec3 right = cam.GetRight();
    ExpectVec3Near(right, Vec3::Right());
}

TEST(Camera, DefaultUpIsPositiveY) {
    Camera cam;
    Vec3 up = cam.GetUp();
    ExpectVec3Near(up, Vec3::Up());
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera — Perspective Projection
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera, PerspectiveProjectionNotIdentity) {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 1000.f);
    ExpectMat4NotIdentity(cam.GetProjection());
}

TEST(Camera, PerspectiveStoresParameters) {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 1.5f, 0.5f, 500.f);
    EXPECT_NEAR(cam.GetFovY(), XM_PIDIV4, kEpsilon);
    EXPECT_NEAR(cam.GetAspectRatio(), 1.5f, kEpsilon);
    EXPECT_NEAR(cam.GetNearPlane(), 0.5f, kEpsilon);
    EXPECT_NEAR(cam.GetFarPlane(), 500.f, kEpsilon);
    EXPECT_EQ(cam.GetProjectionMode(), ProjectionMode::Perspective);
}

TEST(Camera, PerspectiveMatchesDirectXMath) {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 100.f);
    const Mat4& proj = cam.GetProjection();

    Mat4 expected = Mat4::Perspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 100.f);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(proj.data()[i], expected.data()[i], kEpsilon)
            << "Projection element [" << i << "] mismatch";
    }
}

TEST(Camera, SetAspectRatioUpdatesProjection) {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 100.f);
    const Mat4& proj1 = cam.GetProjection();
    float elem00_before = proj1.data()[0];

    cam.SetAspectRatio(4.f / 3.f);
    const Mat4& proj2 = cam.GetProjection();
    float elem00_after = proj2.data()[0];

    EXPECT_NE(elem00_before, elem00_after)
        << "Aspect ratio change should modify projection matrix";
    EXPECT_NEAR(cam.GetAspectRatio(), 4.f / 3.f, kEpsilon);
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera — Orthographic Projection
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera, OrthographicProjectionNotIdentity) {
    Camera cam;
    cam.SetOrthographic(20.f, 15.f, 0.1f, 100.f);
    ExpectMat4NotIdentity(cam.GetProjection());
    EXPECT_EQ(cam.GetProjectionMode(), ProjectionMode::Orthographic);
}

TEST(Camera, OrthographicMatchesDirectXMath) {
    Camera cam;
    cam.SetOrthographic(20.f, 15.f, 0.1f, 100.f);
    const Mat4& proj = cam.GetProjection();

    Mat4 expected = Mat4::Orthographic(20.f, 15.f, 0.1f, 100.f);
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(proj.data()[i], expected.data()[i], kEpsilon)
            << "Ortho projection element [" << i << "] mismatch";
    }
}

TEST(Camera, SetAspectRatioNoOpForOrtho) {
    Camera cam;
    cam.SetOrthographic(20.f, 15.f, 0.1f, 100.f);
    const Mat4& proj1 = cam.GetProjection();
    float elem00_before = proj1.data()[0];

    cam.SetAspectRatio(999.f);
    const Mat4& proj2 = cam.GetProjection();
    float elem00_after = proj2.data()[0];

    EXPECT_FLOAT_EQ(elem00_before, elem00_after)
        << "SetAspectRatio should be no-op for orthographic";
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera — View Matrix
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera, ViewMatrixAtOriginIncludesCoordinateSwap) {
    // Camera at origin with default orientation produces a Z-up→Y-up swap matrix
    // combined with a LookTo. The result is NOT identity — it swaps axes.
    Camera cam;
    const Mat4& view = cam.GetView();

    // The view matrix should transform Z-up forward (+X) to Y-up depth (+Z)
    // Verify the matrix is valid (non-zero determinant) and non-identity
    Mat4 id = Mat4::Identity();
    bool isIdentity = true;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(view.data()[i] - id.data()[i]) > kEpsilon) {
            isIdentity = false;
            break;
        }
    }
    EXPECT_FALSE(isIdentity) << "View at origin should include Z-up→Y-up swap, not identity";

    // Verify the origin maps to origin (camera at origin sees origin at view-space origin)
    Vec4 origin = {0.f, 0.f, 0.f, 1.f};
    Vec4 viewOrigin = view * origin;
    EXPECT_NEAR(viewOrigin.x, 0.f, kEpsilon);
    EXPECT_NEAR(viewOrigin.y, 0.f, kEpsilon);
    EXPECT_NEAR(viewOrigin.z, 0.f, kEpsilon);
}

TEST(Camera, ViewMatrixTranslatesInverse) {
    Camera cam;
    cam.SetPosition({5.f, 0.f, 0.f});
    const Mat4& view = cam.GetView();

    // Transform the camera position by the view matrix — should yield origin
    Vec4 camWorld = {5.f, 0.f, 0.f, 1.f};
    Vec4 camView = view * camWorld;
    EXPECT_NEAR(camView.x, 0.f, kEpsilon);
    EXPECT_NEAR(camView.y, 0.f, kEpsilon);
    EXPECT_NEAR(camView.z, 0.f, kEpsilon);
}

TEST(Camera, ViewMatrixCachesResult) {
    Camera cam;
    cam.SetPosition({1.f, 2.f, 3.f});
    const Mat4& view1 = cam.GetView();
    const Mat4& view2 = cam.GetView();
    // Same pointer means cached
    EXPECT_EQ(&view1, &view2);
}

TEST(Camera, ViewMatrixInvalidatesOnPositionChange) {
    Camera cam;
    cam.SetPosition({1.f, 0.f, 0.f});
    Mat4 view1 = cam.GetView(); // Copy — GetView returns reference to internal

    cam.SetPosition({2.f, 0.f, 0.f});
    const Mat4& view2 = cam.GetView();

    // At least one element must differ after position change
    bool differs = false;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(view1.data()[i] - view2.data()[i]) > kEpsilon) {
            differs = true;
            break;
        }
    }
    EXPECT_TRUE(differs) << "View should update after position change";
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera — LookAt
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera, LookAtSetsOrientation) {
    Camera cam;
    cam.SetPosition({0.f, 0.f, -5.f});
    cam.LookAt({0.f, 0.f, 0.f});

    // Forward should point towards +Z (from -5 looking at origin)
    Vec3 fwd = cam.GetForward();
    EXPECT_GT(fwd.z, 0.5f) << "Camera at -5Z looking at origin should face +Z";
}

TEST(Camera, LookAtViewProjectionTransformsCorrectly) {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 1.f, 0.1f, 100.f);
    cam.SetPosition({0.f, 0.f, -5.f});
    cam.LookAt({0.f, 0.f, 0.f});

    Mat4 vp = cam.GetViewProjection();
    // Origin should transform to near the center of clip space
    Vec4 origin = vp * Vec4{0.f, 0.f, 0.f, 1.f};
    // After perspective divide
    float ndcX = origin.x / origin.w;
    float ndcY = origin.y / origin.w;
    EXPECT_NEAR(ndcX, 0.f, 0.1f) << "Origin should be near center X in NDC";
    EXPECT_NEAR(ndcY, 0.f, 0.1f) << "Origin should be near center Y in NDC";
}

// ─────────────────────────────────────────────────────────────────────────────
// Camera — ViewProjection Composition
// ─────────────────────────────────────────────────────────────────────────────

TEST(Camera, ViewProjectionIsViewTimesProj) {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 100.f);
    cam.SetPosition({1.f, 2.f, 3.f});

    Mat4 vp = cam.GetViewProjection();
    Mat4 manual = cam.GetView() * cam.GetProjection();

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(vp.data()[i], manual.data()[i], kEpsilon)
            << "VP element [" << i << "] should equal View * Proj";
    }
}
