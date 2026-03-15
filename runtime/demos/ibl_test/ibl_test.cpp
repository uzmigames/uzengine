/// IBL Test — Application-based demo implementation.
///
/// Creates 5 PBR cubes with Stonebricks textures and a directional sun.
/// SceneRenderer handles IBL baking and ambient lighting automatically.

#include "ibl_test.h"

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
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Cube positions along the X axis, centered at origin (cm).
static constexpr float kCubePositionsX[5] = {-400.f, -200.f, 0.f, 200.f, 400.f};
static constexpr float kCubeScale = 80.f;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig IBLTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 IBL Test";
    cfg.width           = 1280;
    cfg.height          = 720;
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = 500.f;
    cfg.cameraPositionZ = 200.f;
    cfg.cameraYawDeg    = -90.f;
    cfg.cameraPitchDeg  = -20.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart — Create scene entities
// ─────────────────────────────────────────────────────────────────────────────

void IBLTest::OnStart() {
    auto& world = GetWorld();

    // ── 5 PBR cubes with Stonebricks material ───────────────────────────
    for (int i = 0; i < kCubeCount; ++i) {
        cubes_[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(cubes_[i]);
        scene.SetPosition({kCubePositionsX[i], 0.f, 0.f});
        scene.SetScale({kCubeScale, kCubeScale, kCubeScale});

        auto& mesh = world.AddComponent<StaticMeshComponent>(cubes_[i]);
        mesh.meshPath      = "cube.uzmesh";
        mesh.albedoPath    = "Stonebricks_BaseColor.jpg";
        mesh.normalPath    = "Stonebricks_Normal.jpg";
        mesh.roughnessPath = "Stonebricks_Roughness.jpg";
        mesh.metallicPath  = "Stonebricks_Metallic.jpg";
        mesh.aoPath        = "Stonebricks_AO.jpg";
    }

    // ── Directional sun light ────────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetRotation(Quat::FromAxisAngle(Vec3{0.f, -0.9357f, 0.3528f}, 1.0404f));

        auto& light   = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color     = {3.0f, 2.85f, 2.7f};
        light.intensity = 1.0f;
    }

    UZ_LOG(Demo, Info, "IBL Test started — {} cubes + directional sun", kCubeCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — Per-frame logic
// ─────────────────────────────────────────────────────────────────────────────

void IBLTest::OnUpdate(float dt) {
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
