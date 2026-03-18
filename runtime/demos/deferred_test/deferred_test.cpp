/// Deferred Test — Application-based demo implementation.
///
/// Creates 5 PBR cubes with Stonebricks textures and a directional sun.
/// Debug view modes (keys 1-9) visualize G-Buffer channels via SceneRenderer.

#include "deferred_test.h"

#include "log.h"
#include "input.h"
#include "scene_renderer.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "material/material_asset.h"
#include "render_path.h"
#include "debug_view_mode.h"

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Cube positions along the X axis, centered at origin (cm).
static constexpr float kCubePositionsX[5] = {-400.f, -200.f, 0.f, 200.f, 400.f};
static constexpr float kCubeScale = 80.f;

/// Shared Stonebricks PBR material for all cubes.
static Material s_stoneBrickMat;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig DeferredTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 Deferred Test";
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

void DeferredTest::OnStart() {
    auto& world = GetWorld();

    // ── Initialize shared Stonebricks material ──────────────────────────
    s_stoneBrickMat.albedoPath    = "Stonebricks_BaseColor.jpg";
    s_stoneBrickMat.normalPath    = "Stonebricks_Normal.jpg";
    s_stoneBrickMat.roughnessPath = "Stonebricks_Roughness.jpg";
    s_stoneBrickMat.metallicPath  = "Stonebricks_Metallic.jpg";
    s_stoneBrickMat.aoPath        = "Stonebricks_AO.jpg";

    // ── 5 PBR cubes with Stonebricks material ───────────────────────────
    for (int i = 0; i < kCubeCount; ++i) {
        cubes_[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(cubes_[i]);
        scene.SetPosition({kCubePositionsX[i], 0.f, 0.f});
        scene.SetScale({kCubeScale, kCubeScale, kCubeScale});

        auto& mesh = world.AddComponent<StaticMeshComponent>(cubes_[i]);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &s_stoneBrickMat);
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

    UZ_LOG(Demo, Info, "Deferred Test started — {} cubes + directional sun", kCubeCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — Per-frame logic (render path + debug view mode toggles)
// ─────────────────────────────────────────────────────────────────────────────

void DeferredTest::OnUpdate(float dt) {
    (void)dt;

    auto& input    = GetInput();
    auto& renderer = GetRenderer();

    // F1/F2: render path toggle
    if (input.IsKeyJustPressed(Key::F1)) {
        renderer.SetRenderPath(RenderPath::Deferred);
        UZ_LOG(Demo, Info, "Render path: Deferred");
    }
    if (input.IsKeyJustPressed(Key::F2)) {
        renderer.SetRenderPath(RenderPath::Forward);
        UZ_LOG(Demo, Info, "Render path: Forward");
    }

    // 0: Lit (default), 1-9: debug view modes
    if (input.IsKeyJustPressed(Key::Num0)) {
        renderer.SetDebugViewMode(DebugViewMode::Lit);
        UZ_LOG(Demo, Info, "Debug view: Lit");
    }
    if (input.IsKeyJustPressed(Key::Num1)) {
        renderer.SetDebugViewMode(DebugViewMode::Albedo);
        UZ_LOG(Demo, Info, "Debug view: Albedo");
    }
    if (input.IsKeyJustPressed(Key::Num2)) {
        renderer.SetDebugViewMode(DebugViewMode::WorldNormal);
        UZ_LOG(Demo, Info, "Debug view: WorldNormal");
    }
    if (input.IsKeyJustPressed(Key::Num3)) {
        renderer.SetDebugViewMode(DebugViewMode::Metallic);
        UZ_LOG(Demo, Info, "Debug view: Metallic");
    }
    if (input.IsKeyJustPressed(Key::Num4)) {
        renderer.SetDebugViewMode(DebugViewMode::Roughness);
        UZ_LOG(Demo, Info, "Debug view: Roughness");
    }
    if (input.IsKeyJustPressed(Key::Num5)) {
        renderer.SetDebugViewMode(DebugViewMode::AO);
        UZ_LOG(Demo, Info, "Debug view: AO");
    }
    if (input.IsKeyJustPressed(Key::Num6)) {
        renderer.SetDebugViewMode(DebugViewMode::Depth);
        UZ_LOG(Demo, Info, "Debug view: Depth");
    }
    if (input.IsKeyJustPressed(Key::Num7)) {
        renderer.SetDebugViewMode(DebugViewMode::WorldPosition);
        UZ_LOG(Demo, Info, "Debug view: WorldPosition");
    }
    if (input.IsKeyJustPressed(Key::Num8)) {
        renderer.SetDebugViewMode(DebugViewMode::Lighting);
        UZ_LOG(Demo, Info, "Debug view: Lighting");
    }
    if (input.IsKeyJustPressed(Key::Num9)) {
        renderer.SetDebugViewMode(DebugViewMode::Shadows);
        UZ_LOG(Demo, Info, "Debug view: Shadows");
    }
}

} // namespace uz
