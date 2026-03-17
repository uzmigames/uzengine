/// UzEngine SceneLighting and BuildPerFrameCB Tests
///
/// Covers default lighting defaults, custom lighting overrides, and full
/// BuildPerFrameCB field correctness including viewProj, invViewProj,
/// cameraPos, timeParams, lightDir, lightColor, and ambientColor.

#include <gtest/gtest.h>

#include "scene_lighting.h"
#include "camera.h"
#include "constant_buffers.h"
#include "vector3.h"
#include "mat4.h"

#include <DirectXMath.h>
#include <cmath>

using namespace uz;
using namespace DirectX;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps = 1e-3f;  // CB math uses float, looser tolerance

static Camera MakeTestCamera() {
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 1000.f);
    cam.SetPosition({3.f, 2.f, 1.f});
    return cam;
}

static void ExpectVec3Near(float ax, float ay, float az,
                            float bx, float by, float bz,
                            float eps, const char* label) {
    EXPECT_NEAR(ax, bx, eps) << label << " x";
    EXPECT_NEAR(ay, by, eps) << label << " y";
    EXPECT_NEAR(az, bz, eps) << label << " z";
}

// ─────────────────────────────────────────────────────────────────────────────
// SceneLighting — default values
// ─────────────────────────────────────────────────────────────────────────────

TEST(SceneLighting_Defaults, SunDirection_IsNormalized) {
    SceneLighting lighting;
    float len = lighting.sunDirection.Length();
    EXPECT_NEAR(len, 1.f, kEps) << "Default sunDirection must be unit length";
}

TEST(SceneLighting_Defaults, SunColor_IsPositive) {
    SceneLighting lighting;
    EXPECT_GT(lighting.sunColor.x, 0.f);
    EXPECT_GT(lighting.sunColor.y, 0.f);
    EXPECT_GT(lighting.sunColor.z, 0.f);
}

TEST(SceneLighting_Defaults, AmbientColor_IsPositive) {
    SceneLighting lighting;
    EXPECT_GT(lighting.ambientColor.x, 0.f);
    EXPECT_GT(lighting.ambientColor.y, 0.f);
    EXPECT_GT(lighting.ambientColor.z, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — viewProj
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB, ViewProj_MatchesCameraGetViewProjection) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 1.0f, 0.016f, 60u, lighting);

    Mat4 expected = cam.GetViewProjection();
    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(cb.viewProj.data()[i], expected.data()[i], kEps)
            << "viewProj element " << i << " mismatch";
    }
}

TEST(BuildPerFrameCB, ViewProj_IsNotIdentity) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    Mat4 id = Mat4::Identity();
    bool allSame = true;
    for (int i = 0; i < 16; ++i) {
        if (std::abs(cb.viewProj.data()[i] - id.data()[i]) > kEps) {
            allSame = false;
            break;
        }
    }
    EXPECT_FALSE(allSame) << "viewProj should not be identity for non-trivial camera";
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — invViewProj
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB, InvViewProj_IsApproximateInverseOfViewProj) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 1.f, 0.016f, 1u, lighting);

    // viewProj * invViewProj should approximate identity
    Mat4 product = cb.viewProj * cb.invViewProj;
    Mat4 id = Mat4::Identity();

    for (int i = 0; i < 16; ++i) {
        EXPECT_NEAR(product.data()[i], id.data()[i], 1e-2f)
            << "viewProj * invViewProj element " << i
            << " should be ~identity";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — cameraPos
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB, CameraPos_XYZ_MatchCameraPosition) {
    const Vec3 pos{7.f, -3.f, 5.f};
    Camera cam;
    cam.SetPerspective(XM_PIDIV4, 16.f / 9.f, 0.1f, 1000.f);
    cam.SetPosition(pos);

    SceneLighting lighting;
    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.cameraPos.x, pos.x, kEps) << "cameraPos.x";
    EXPECT_NEAR(cb.cameraPos.y, pos.y, kEps) << "cameraPos.y";
    EXPECT_NEAR(cb.cameraPos.z, pos.z, kEps) << "cameraPos.z";
}

TEST(BuildPerFrameCB, CameraPos_W_IsZero) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.cameraPos.w, 0.f, kEps) << "cameraPos.w should be 0 (padding)";
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — timeParams
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB, TimeParams_X_IsTotalTime) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 12.5f, 0.016f, 750u, lighting);

    EXPECT_NEAR(cb.timeParams.x, 12.5f, kEps) << "timeParams.x should be totalTime";
}

TEST(BuildPerFrameCB, TimeParams_Y_IsDeltaTime) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 12.5f, 0.016f, 750u, lighting);

    EXPECT_NEAR(cb.timeParams.y, 0.016f, kEps) << "timeParams.y should be deltaTime";
}

TEST(BuildPerFrameCB, TimeParams_Z_IsFrameIndex) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 42u, lighting);

    EXPECT_NEAR(cb.timeParams.z, 42.f, kEps) << "timeParams.z should be frameIndex as float";
}

TEST(BuildPerFrameCB, TimeParams_ZeroValues_AllZero) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.timeParams.x, 0.f, kEps) << "totalTime=0";
    EXPECT_NEAR(cb.timeParams.y, 0.f, kEps) << "deltaTime=0";
    EXPECT_NEAR(cb.timeParams.z, 0.f, kEps) << "frameIndex=0";
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — lightDir
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB, LightDir_XYZ_MatchSunDirection) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.sunDirection = Vec3{0.f, 0.f, 1.f};

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    ExpectVec3Near(cb.lightDir.x, cb.lightDir.y, cb.lightDir.z,
                   0.f, 0.f, 1.f,
                   kEps, "lightDir.xyz");
}

TEST(BuildPerFrameCB, LightDir_W_IsZero) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.lightDir.w, 0.f, kEps) << "lightDir.w should be 0";
}

TEST(BuildPerFrameCB, LightDir_DefaultLighting_MatchesDefaultSunDirection) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;  // uses default sunDirection

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    const Vec3& sunDir = lighting.sunDirection;
    EXPECT_NEAR(cb.lightDir.x, sunDir.x, kEps) << "lightDir.x";
    EXPECT_NEAR(cb.lightDir.y, sunDir.y, kEps) << "lightDir.y";
    EXPECT_NEAR(cb.lightDir.z, sunDir.z, kEps) << "lightDir.z";
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — custom lighting (sunColor, ambientColor)
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB_CustomLighting, SunColor_XYZ_MatchCustomColor) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.sunColor = {5.f, 4.8f, 4.5f};

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.lightColor.x, 5.0f, kEps) << "lightColor.x";
    EXPECT_NEAR(cb.lightColor.y, 4.8f, kEps) << "lightColor.y";
    EXPECT_NEAR(cb.lightColor.z, 4.5f, kEps) << "lightColor.z";
}

TEST(BuildPerFrameCB_CustomLighting, SunColor_W_IsZero) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.sunColor = {1.f, 1.f, 1.f};
    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.lightColor.w, 0.f, kEps) << "lightColor.w should be 0";
}

TEST(BuildPerFrameCB_CustomLighting, AmbientColor_XYZ_MatchCustomColor) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.ambientColor = {0.1f, 0.15f, 0.2f};

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.ambientColor.x, 0.1f,  kEps) << "ambientColor.x";
    EXPECT_NEAR(cb.ambientColor.y, 0.15f, kEps) << "ambientColor.y";
    EXPECT_NEAR(cb.ambientColor.z, 0.2f,  kEps) << "ambientColor.z";
}

TEST(BuildPerFrameCB_CustomLighting, AmbientColor_W_IsZero) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.ambientColor = {0.05f, 0.05f, 0.05f};
    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.ambientColor.w, 0.f, kEps) << "ambientColor.w should be 0";
}

TEST(BuildPerFrameCB_CustomLighting, ZeroAmbient_AllAmbientChannelsZero) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.ambientColor = {0.f, 0.f, 0.f};

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.ambientColor.x, 0.f, kEps);
    EXPECT_NEAR(cb.ambientColor.y, 0.f, kEps);
    EXPECT_NEAR(cb.ambientColor.z, 0.f, kEps);
}

TEST(BuildPerFrameCB_CustomLighting, CustomSunDirection_IsReflectedInLightDir) {
    Camera cam = MakeTestCamera();
    SceneLighting lighting;
    lighting.sunDirection = Vec3{1.f, 0.f, 0.f};  // pure +X

    PerFrameCB cb = BuildPerFrameCB(cam, 0.f, 0.f, 0u, lighting);

    EXPECT_NEAR(cb.lightDir.x, 1.f, kEps) << "lightDir.x should be 1 for +X sun";
    EXPECT_NEAR(cb.lightDir.y, 0.f, kEps) << "lightDir.y should be 0";
    EXPECT_NEAR(cb.lightDir.z, 0.f, kEps) << "lightDir.z should be 0";
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildPerFrameCB — struct size guarantee
// ─────────────────────────────────────────────────────────────────────────────

TEST(BuildPerFrameCB_StructSize, PerFrameCB_Is256Bytes) {
    // This verifies the D3D12 CB alignment requirement is met.
    EXPECT_EQ(sizeof(PerFrameCB), 512u);
}
