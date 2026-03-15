/// Camera Test — Application-based demo implementation.
///
/// Renders a cube with white fallback textures and a directional sun.
/// All rendering infrastructure (GBuffer, shadow atlas, render graph,
/// CB uploads, draw list) is handled by SceneRenderer.

#include "camera_test.h"

#include "log.h"
#include "input.h"
#include "scene_renderer.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "render_path.h"

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig CameraTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 Camera Test";
    cfg.width           = 1280;
    cfg.height          = 720;
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = 5.f;
    cfg.cameraPositionZ = 2.f;
    cfg.cameraYawDeg    = -90.f;
    cfg.cameraPitchDeg  = -20.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart — Create scene entities
// ─────────────────────────────────────────────────────────────────────────────

void CameraTest::OnStart() {
    auto& world = GetWorld();

    // ── Cube ─────────────────────────────────────────────────────────────
    cube_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(cube_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({1.f, 1.f, 1.f});

        auto& mesh    = world.AddComponent<StaticMeshComponent>(cube_);
        mesh.meshPath = "cube.uzmesh";
        // No texture paths — white/flat fallbacks applied by SceneRenderer.
    }

    // ── Directional sun light ────────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetRotation(Quat::FromAxisAngle(Vec3{0.f, -0.9357f, 0.3528f}, 1.0404f));

        auto& light   = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color     = {3.0f, 3.0f, 3.0f};
        light.intensity = 1.0f;
    }

    UZ_LOG(Demo, Info, "Camera Test started — cube + directional sun");
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — Per-frame logic
// ─────────────────────────────────────────────────────────────────────────────

void CameraTest::OnUpdate(float dt) {
    (void)dt;

    auto& input    = GetInput();
    auto& renderer = GetRenderer();

    if (input.IsKeyJustPressed(Key::F1)) {
        renderer.SetRenderPath(RenderPath::Deferred);
        UZ_LOG(Demo, Info, "Render path: Deferred");
    }
    if (input.IsKeyJustPressed(Key::F2)) {
        renderer.SetRenderPath(RenderPath::Forward);
        UZ_LOG(Demo, Info, "Render path: Forward");
    }
}

} // namespace uz
