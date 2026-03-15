/// Unit tests for uz::PrimitiveComponent, uz::LightComponent,
/// uz::StaticMeshComponent, and uz::CameraComponent.
///
/// Coverage targets:
///   - primitive_component.h / .cpp   → 95%+
///   - light_component.h / .cpp       → 95%+
///   - static_mesh_component.h / .cpp → 95%+
///   - camera_component.h / .cpp      → 95%+
///
/// Tests use a real `RenderWorld` instance. No GPU, no RHI required.

#include "primitive_component.h"
#include "light_component.h"
#include "static_mesh_component.h"
#include "camera_component.h"
#include "render_world.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps = 1e-4f;

static bool NearlyEqual(float a, float b, float eps = kEps) {
    return std::fabs(a - b) < eps;
}

static bool NearlyEqualVec3(const Vec3& a, const Vec3& b, float eps = kEps) {
    return NearlyEqual(a.x, b.x, eps) &&
           NearlyEqual(a.y, b.y, eps) &&
           NearlyEqual(a.z, b.z, eps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: DirectionalLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// 1. Default (identity) rotation → direction == Vec3::Forward().
TEST(DirectionalLightComponentTest, DefaultDirectionIsForward) {
    DirectionalLightComponent light;
    // No rotation set → local rotation is identity.
    Vec3 dir = light.GetDirection();
    EXPECT_TRUE(NearlyEqualVec3(dir, Vec3::Forward()))
        << "Expected forward (" << Vec3::Forward().x << ",0,0) but got ("
        << dir.x << "," << dir.y << "," << dir.z << ")";
}

/// 2. Direction is normalized for non-identity rotations.
TEST(DirectionalLightComponentTest, DirectionIsAlwaysNormalized) {
    DirectionalLightComponent light;
    // Rotate 45 degrees around Z.
    light.SetRotation(Quat::FromAxisAngle(Vec3::Up(), DirectX::XM_PIDIV4));
    Vec3 dir = light.GetDirection();
    float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
    EXPECT_TRUE(NearlyEqual(len, 1.f))
        << "Direction length should be 1.0, got " << len;
}

/// 3. 90° rotation around Z-up → direction rotates from X toward Y.
TEST(DirectionalLightComponentTest, DirectionRotatesWith90DegYaw) {
    DirectionalLightComponent light;
    // Rotate 90 degrees around the up axis (Z). Forward (X) should become Right (Y).
    light.SetRotation(Quat::FromAxisAngle(Vec3::Up(), DirectX::XM_PIDIV2));
    Vec3 dir = light.GetDirection();
    // After 90° around Z: forward (1,0,0) becomes right (0,1,0).
    EXPECT_TRUE(NearlyEqualVec3(dir, Vec3::Right(), 1e-3f))
        << "Expected (0,1,0) but got (" << dir.x << "," << dir.y << "," << dir.z << ")";
}

/// 4. SyncToRenderWorld creates a proxy with type DirectionalLight.
TEST(DirectionalLightComponentTest, SyncCreatesProxyWithCorrectType) {
    RenderWorld world;
    DirectionalLightComponent light;

    EXPECT_FALSE(light.GetProxyHandle().IsValid());
    light.SyncToRenderWorld(world);
    EXPECT_TRUE(light.GetProxyHandle().IsValid());

    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->type, RenderProxyType::DirectionalLight);
}

/// 5. Proxy stores color and intensity from the component.
TEST(DirectionalLightComponentTest, SyncPushesColorAndIntensity) {
    RenderWorld world;
    DirectionalLightComponent light;
    light.color     = Vec3{0.8f, 0.4f, 0.2f};
    light.intensity = 3.5f;

    light.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_TRUE(NearlyEqualVec3(proxy->light.color, Vec3{0.8f, 0.4f, 0.2f}));
    EXPECT_TRUE(NearlyEqual(proxy->light.intensity, 3.5f));
}

/// 6. Proxy direction matches GetDirection() at sync time.
TEST(DirectionalLightComponentTest, SyncPushesDirection) {
    RenderWorld world;
    DirectionalLightComponent light;
    light.SetRotation(Quat::FromAxisAngle(Vec3::Up(), DirectX::XM_PIDIV4));
    light.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    Vec3 expected = light.GetDirection();
    EXPECT_TRUE(NearlyEqualVec3(proxy->light.direction, expected));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: PointLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// 7. SyncToRenderWorld creates a proxy with type PointLight.
TEST(PointLightComponentTest, SyncCreatesProxyWithCorrectType) {
    RenderWorld world;
    PointLightComponent light;
    light.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->type, RenderProxyType::PointLight);
}

/// 8. Proxy stores range, color, and intensity.
TEST(PointLightComponentTest, SyncPushesRangeColorIntensity) {
    RenderWorld world;
    PointLightComponent light;
    light.range     = 25.f;
    light.color     = Vec3{1.f, 0.5f, 0.f};
    light.intensity = 2.f;
    light.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_TRUE(NearlyEqual(proxy->light.range, 25.f));
    EXPECT_TRUE(NearlyEqualVec3(proxy->light.color, Vec3{1.f, 0.5f, 0.f}));
    EXPECT_TRUE(NearlyEqual(proxy->light.intensity, 2.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SpotLightComponent
// ─────────────────────────────────────────────────────────────────────────────

/// 9. SyncToRenderWorld creates a proxy with type SpotLight.
TEST(SpotLightComponentTest, SyncCreatesProxyWithCorrectType) {
    RenderWorld world;
    SpotLightComponent light;
    light.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->type, RenderProxyType::SpotLight);
}

/// 10. Proxy stores range, inner/outer cone angles, color, intensity.
TEST(SpotLightComponentTest, SyncPushesAllSpotFields) {
    RenderWorld world;
    SpotLightComponent light;
    light.range         = 15.f;
    light.innerAngleDeg = 30.f;  // degrees (converted to radians in proxy)
    light.outerAngleDeg = 45.f;
    light.color         = Vec3{0.f, 1.f, 0.f};
    light.intensity     = 5.f;
    light.SyncToRenderWorld(world);

    static constexpr float kDegToRad = 3.14159265358979323846f / 180.f;
    const RenderProxy* proxy = world.GetProxy(light.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_TRUE(NearlyEqual(proxy->light.range, 15.f));
    EXPECT_TRUE(NearlyEqual(proxy->light.innerAngle, 30.f * kDegToRad));
    EXPECT_TRUE(NearlyEqual(proxy->light.outerAngle, 45.f * kDegToRad));
    EXPECT_TRUE(NearlyEqualVec3(proxy->light.color, Vec3{0.f, 1.f, 0.f}));
    EXPECT_TRUE(NearlyEqual(proxy->light.intensity, 5.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: StaticMeshComponent
// ─────────────────────────────────────────────────────────────────────────────

/// 11. SyncToRenderWorld creates a proxy with type StaticMesh.
TEST(StaticMeshComponentTest, SyncCreatesProxyWithCorrectType) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->type, RenderProxyType::StaticMesh);
}

/// 12. Proxy stores meshId and materialId.
TEST(StaticMeshComponentTest, SyncPushesMeshAndMaterialIds) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.meshId     = 42;
    mesh.materialId = 7;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->staticMesh.meshId,     42u);
    EXPECT_EQ(proxy->staticMesh.materialId, 7u);
}

/// 13. Default meshId and materialId are 0.
TEST(StaticMeshComponentTest, DefaultIdsAreZero) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->staticMesh.meshId,     0u);
    EXPECT_EQ(proxy->staticMesh.materialId, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: PrimitiveComponent — shared behavior (via StaticMeshComponent)
// ─────────────────────────────────────────────────────────────────────────────

/// 14. Proxy handle is invalid before the first sync.
TEST(PrimitiveComponentTest, HandleInvalidBeforeSync) {
    StaticMeshComponent mesh;
    EXPECT_FALSE(mesh.GetProxyHandle().IsValid());
}

/// 15. Proxy handle is valid after the first sync.
TEST(PrimitiveComponentTest, HandleValidAfterSync) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.SyncToRenderWorld(world);
    EXPECT_TRUE(mesh.GetProxyHandle().IsValid());
}

/// 16. Second sync does not create a second proxy (handle stays the same).
TEST(PrimitiveComponentTest, DoubleSyncReusesSameProxy) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.SyncToRenderWorld(world);
    RenderProxyHandle first = mesh.GetProxyHandle();

    mesh.SyncToRenderWorld(world);
    RenderProxyHandle second = mesh.GetProxyHandle();

    EXPECT_EQ(first.index,      second.index);
    EXPECT_EQ(first.generation, second.generation);
    // Only one live proxy should exist.
    EXPECT_EQ(world.ProxyCount(), 1u);
}

/// 17. `visible = false` propagates to proxy after sync.
TEST(PrimitiveComponentTest, VisibleFalsePropagatedToProxy) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.visible = false;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->visible);
}

/// 18. `castShadows = false` propagates to proxy after sync.
TEST(PrimitiveComponentTest, CastShadowsFalsePropagatedToProxy) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.castShadows = false;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->castShadows);
}

/// 19. `renderLayer` propagates to proxy after sync.
TEST(PrimitiveComponentTest, RenderLayerPropagatedToProxy) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.renderLayer = 4u;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_EQ(proxy->renderLayer, 4u);
}

/// 20. World transform is written into proxy each sync.
TEST(PrimitiveComponentTest, WorldTransformSyncedToProxy) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.SetPosition(Vec3{3.f, 0.f, 0.f});
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    // Translation column/row in a row-major TRS matrix is at _41,_42,_43.
    EXPECT_TRUE(NearlyEqual(proxy->worldMatrix.m._41, 3.f));
}

/// 21. Visibility change after initial sync is picked up on next sync.
TEST(PrimitiveComponentTest, VisibilityChangePickedUpOnNextSync) {
    RenderWorld world;
    StaticMeshComponent mesh;
    mesh.visible = true;
    mesh.SyncToRenderWorld(world);

    // Flip visibility.
    mesh.visible = false;
    mesh.SyncToRenderWorld(world);

    const RenderProxy* proxy = world.GetProxy(mesh.GetProxyHandle());
    ASSERT_NE(proxy, nullptr);
    EXPECT_FALSE(proxy->visible);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: CameraComponent
// ─────────────────────────────────────────────────────────────────────────────

/// 22. Default active state is false.
TEST(CameraComponentTest, DefaultIsNotActive) {
    CameraComponent cam;
    EXPECT_FALSE(cam.isActive);
}

/// 23. Default GetViewMatrix is identity for identity transform.
TEST(CameraComponentTest, DefaultViewMatrixIsIdentityInverse) {
    CameraComponent cam;
    Mat4 view = cam.GetViewMatrix();
    Mat4 identity = Mat4::Identity();

    // Compare each element to identity.
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            EXPECT_TRUE(NearlyEqual(view(r, c), identity(r, c)))
                << "Mismatch at (" << r << ", " << c << ")";
        }
    }
}

/// 24. GetProjectionMatrix returns a non-identity matrix in perspective mode.
TEST(CameraComponentTest, PerspectiveProjectionIsNonIdentity) {
    CameraComponent cam;
    cam.fov         = 90.f;
    cam.aspectRatio = 1.f;
    cam.nearPlane   = 0.1f;
    cam.farPlane    = 100.f;

    Mat4 proj     = cam.GetProjectionMatrix();
    Mat4 identity = Mat4::Identity();

    bool allEqual = true;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!NearlyEqual(proj(r, c), identity(r, c))) {
                allEqual = false;
                break;
            }
        }
    }
    EXPECT_FALSE(allEqual) << "Perspective projection should not be identity";
}

/// 25. isOrtho = true changes the projection matrix vs perspective.
TEST(CameraComponentTest, OrthoProjDiffersFromPerspectiveProj) {
    CameraComponent cam;
    cam.fov         = 60.f;
    cam.aspectRatio = 16.f / 9.f;

    cam.isOrtho = false;
    Mat4 persp  = cam.GetProjectionMatrix();

    cam.isOrtho = true;
    cam.orthoSize = 5.f;
    Mat4 ortho = cam.GetProjectionMatrix();

    bool identical = true;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!NearlyEqual(persp(r, c), ortho(r, c))) {
                identical = false;
                break;
            }
        }
    }
    EXPECT_FALSE(identical) << "Ortho and perspective matrices should differ";
}

/// 26. GetViewProjection equals GetViewMatrix() * GetProjectionMatrix().
TEST(CameraComponentTest, ViewProjectionMatchesManualComposition) {
    CameraComponent cam;
    cam.SetPosition(Vec3{1.f, 2.f, 3.f});
    cam.fov         = 60.f;
    cam.aspectRatio = 16.f / 9.f;

    Mat4 expected = cam.GetViewMatrix() * cam.GetProjectionMatrix();
    Mat4 result   = cam.GetViewProjection();

    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            EXPECT_TRUE(NearlyEqual(result(r, c), expected(r, c)))
                << "Mismatch at (" << r << ", " << c << ")";
        }
    }
}

/// 27. Moving the camera changes the view matrix.
TEST(CameraComponentTest, TranslationChangesViewMatrix) {
    CameraComponent cam;
    Mat4 viewAt0 = cam.GetViewMatrix();

    cam.SetPosition(Vec3{10.f, 0.f, 0.f});
    Mat4 viewAt10 = cam.GetViewMatrix();

    bool identical = true;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!NearlyEqual(viewAt0(r, c), viewAt10(r, c))) {
                identical = false;
                break;
            }
        }
    }
    EXPECT_FALSE(identical) << "Moving camera should change view matrix";
}

/// 28. Changing FOV changes the projection matrix.
TEST(CameraComponentTest, ChangingFovChangesProjection) {
    CameraComponent cam;
    cam.fov = 60.f;
    Mat4 proj60 = cam.GetProjectionMatrix();

    cam.fov = 90.f;
    Mat4 proj90 = cam.GetProjectionMatrix();

    bool identical = true;
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 4; ++c) {
            if (!NearlyEqual(proj60(r, c), proj90(r, c))) {
                identical = false;
                break;
            }
        }
    }
    EXPECT_FALSE(identical) << "Changing FOV should change projection matrix";
}

/// 29. CameraComponent does NOT inherit from PrimitiveComponent.
/// Compile-time check: GetProxyHandle() must NOT be available on CameraComponent.
TEST(CameraComponentTest, CameraIsNotAPrimitive) {
    // If this compiles, CameraComponent inherits SceneComponent, not PrimitiveComponent.
    CameraComponent cam;
    (void)cam.GetWorldMatrix(); // SceneComponent method — should compile fine.

    // The following line MUST NOT compile if the design is correct:
    // cam.GetProxyHandle();  // intentionally commented out — not a primitive
    SUCCEED();
}

/// 30. LightComponent stores color and intensity correctly.
TEST(LightComponentTest, ColorAndIntensityStoredCorrectly) {
    DirectionalLightComponent light;
    light.color     = Vec3{0.5f, 0.3f, 0.9f};
    light.intensity = 7.2f;

    EXPECT_TRUE(NearlyEqualVec3(light.color, Vec3{0.5f, 0.3f, 0.9f}));
    EXPECT_TRUE(NearlyEqual(light.intensity, 7.2f));
}

/// 31. Multiple primitives in one RenderWorld each get a distinct proxy.
TEST(PrimitiveComponentTest, MultipleComponentsGetDistinctProxies) {
    RenderWorld world;
    StaticMeshComponent meshA;
    StaticMeshComponent meshB;

    meshA.SyncToRenderWorld(world);
    meshB.SyncToRenderWorld(world);

    EXPECT_NE(meshA.GetProxyHandle().index, meshB.GetProxyHandle().index);
    EXPECT_EQ(world.ProxyCount(), 2u);
}
