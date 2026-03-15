/// PBR Test — Application-based demo implementation.
///
/// Creates 5 PBR cubes with Stonebricks material, a ground plane,
/// a directional sun light, and 4 colored point lights that orbit the scene.
///
/// All rendering infrastructure (GBuffer, shadow atlas, render graph, CB uploads,
/// draw list construction) is handled by SceneRenderer. This file contains
/// only scene setup and per-frame game logic.

#include "pbr_test.h"

#include "log.h"
#include "input.h"
#include "scene_renderer.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "sky_atmosphere_component.h"
#include "height_fog_component.h"
#include "render_path.h"

#include <cmath>

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kPi = 3.14159265358979323846f;

/// Cube positions along the X axis, centered at origin.
static constexpr float kCubePositionsX[5] = {-4.f, -2.f, 0.f, 2.f, 4.f};
static constexpr float kCubeScale = 0.8f;

/// Point light orbital radius and height.
static constexpr float kLightOrbitRadius = 5.f;
static constexpr float kLightOrbitHeight = 2.5f;
static constexpr float kLightOrbitSpeed  = 0.5f;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig PBRTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 PBR Test";
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

void PBRTest::OnStart() {
    auto& world = GetWorld();

    // ── 5 PBR cubes with Stonebricks material ─────────────────────────────
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

    // ── Ground plane ──────────────────────────────────────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, -0.82f});
        scene.SetScale({15.f, 15.f, 0.02f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath   = "cube.uzmesh";
        mesh.albedoPath = "checker.png";
    }

    // ── Directional sun light ─────────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        // Rotate forward axis (1,0,0) toward (0.5, 0.3, 0.8).
        // axis = cross((1,0,0), normalized) = (0, -0.9357, 0.3528)
        // angle = acos(dot) ≈ 1.0404 rad
        scene.SetRotation(Quat::FromAxisAngle(Vec3{0.f, -0.9357f, 0.3528f}, 1.0404f));

        auto& light   = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color     = {3.0f, 2.85f, 2.7f};
        light.intensity = 1.0f;
    }

    // ── 4 colored point lights ────────────────────────────────────────────
    struct PointLightDef {
        Vec3  position;
        float range;
        Vec3  color;
    };
    static constexpr PointLightDef kPointLightDefs[4] = {
        {{-3.f, 2.f,  2.f}, 8.f,  {4.f, 0.2f, 0.2f}},
        {{ 0.f, 3.f, -2.f}, 10.f, {0.2f, 4.f, 0.2f}},
        {{ 3.f, 2.f,  2.f}, 8.f,  {0.2f, 0.2f, 4.f}},
        {{ 0.f, 1.f,  0.f}, 15.f, {3.f,  3.f,  3.f}},
    };

    for (int i = 0; i < 4; ++i) {
        pointLights_[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(pointLights_[i]);
        scene.SetPosition(kPointLightDefs[i].position);

        auto& light   = world.AddComponent<PointLightComponent>(pointLights_[i]);
        light.color     = kPointLightDefs[i].color;
        light.intensity = 1.0f;
        light.range     = kPointLightDefs[i].range;
    }

    // ── Sky atmosphere ────────────────────────────────────────────────────
    Entity sky = world.CreateEntity();
    {
        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
        atmo.sunIlluminance = {20.f, 20.f, 20.f};
        // Earth-like defaults — Rayleigh/Mie already set by struct defaults.
    }

    // ── Height fog (ground haze) ──────────────────────────────────────────
    Entity fog = world.CreateEntity();
    {
        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        fogComp.density      = 0.008f;
        fogComp.baseHeight   = -1.f;
        fogComp.heightFalloff = 0.3f;
        fogComp.maxOpacity   = 0.7f;
        fogComp.color        = {0.75f, 0.80f, 0.90f};
    }

    UZ_LOG(Demo, Info, "PBR Test started — {} cubes, 4 point lights + directional sun + sky",
           kCubeCount);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate — Per-frame game logic
// ─────────────────────────────────────────────────────────────────────────────

void PBRTest::OnUpdate(float dt) {
    totalTime_ += dt;

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

    // ── Orbit first 3 point lights ────────────────────────────────────────
    auto& world = GetWorld();
    for (int i = 0; i < 3; ++i) {
        if (!world.IsValid(pointLights_[i])) continue;

        const float angle = totalTime_ * kLightOrbitSpeed +
                            static_cast<float>(i) * (2.f * kPi / 3.f);
        const float x = kLightOrbitRadius * std::cos(angle);
        const float z = kLightOrbitRadius * std::sin(angle);

        auto& scene = world.GetComponent<SceneComponent>(pointLights_[i]);
        scene.SetPosition({x, kLightOrbitHeight, z});
    }
}

} // namespace uz
