/// Textured Quad — Application-based demo implementation.
///
/// Creates a scaled cube as a ground plane with checker.png albedo,
/// viewed from above. SceneRenderer handles all rendering.

#include "textured_quad.h"

#include "log.h"
#include "input.h"
#include "scene_renderer.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "material/material_asset.h"
#include "render_path.h"

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

/// Shared checker material for the ground plane.
static Material s_groundMat;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig TexturedQuad::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 Textured Quad";
    cfg.width           = 1280;
    cfg.height          = 720;
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = 0.f;
    cfg.cameraPositionZ = 800.f;
    cfg.cameraYawDeg    = -90.f;
    cfg.cameraPitchDeg  = -90.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart — Create scene entities
// ─────────────────────────────────────────────────────────────────────────────

void TexturedQuad::OnStart() {
    auto& world = GetWorld();

    // ── Ground plane (flat cube with checker texture) ────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({1000.f, 1000.f, 1.f});

        s_groundMat.albedoPath = "checker.png";

        auto& mesh      = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath    = "cube.uzmesh";
        mesh.SetMaterial(0, &s_groundMat);
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

    UZ_LOG(Demo, Info, "Textured Quad started — checker ground plane + directional sun");
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — Per-frame logic
// ─────────────────────────────────────────────────────────────────────────────

void TexturedQuad::OnUpdate(float dt) {
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
