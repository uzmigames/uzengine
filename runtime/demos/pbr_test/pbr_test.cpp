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
#include "sky_light_component.h"
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

/// Cube positions along the X axis, centered at origin (cm).
static constexpr float kCubePositionsX[5] = {-400.f, -200.f, 0.f, 200.f, 400.f};
static constexpr float kCubeScale = 80.f;

/// Point light orbital radius and height (cm).
static constexpr float kLightOrbitRadius = 500.f;
static constexpr float kLightOrbitHeight = 250.f;
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

void PBRTest::OnStart() {
    auto& world = GetWorld();

    // ── 5 PBR cubes with Stonebricks material ─────────────────────────────
    for (int i = 0; i < kCubeCount; ++i) {
        cubes_[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(cubes_[i]);
        scene.SetPosition({kCubePositionsX[i], 0.f, 0.f});  // cm
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
        scene.SetPosition({0.f, 0.f, -82.f});
        scene.SetScale({1500.f, 1500.f, 2.f});

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
        // UE5 defaults: Intensity=10 lux, white light, atmosphereSunLight=true
        light.color     = {1.0f, 1.0f, 1.0f};
        light.intensity = 10.0f;
        light.atmosphereSunLight = true;
        light.lightSourceAngle = 0.5357f;  // Earth sun angular diameter
    }

    // ── 4 colored point lights ────────────────────────────────────────────
    struct PointLightDef {
        Vec3  position;
        float range;
        Vec3  color;
    };
    static constexpr PointLightDef kPointLightDefs[4] = {
        {{-300.f, 200.f,  200.f}, 800.f,  {2.f, 0.1f, 0.1f}},
        {{   0.f, 300.f, -200.f}, 1000.f, {0.1f, 2.f, 0.1f}},
        {{ 300.f, 200.f,  200.f}, 800.f,  {0.1f, 0.1f, 2.f}},
        {{   0.f, 100.f,    0.f}, 1500.f, {1.5f, 1.5f, 1.5f}},
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

    // ── Sky atmosphere (UE5 exact defaults) ─────────────────────────────
    Entity sky = world.CreateEntity();
    {
        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
        // UE5: sun illuminance derived from DirectionalLight (intensity=10)
        // For sky scattering, use moderate value to get vivid blue sky
        atmo.sunIlluminance        = {6.f, 6.f, 6.f};
        // UE5 defaults (already set in struct, but explicit for clarity):
        atmo.bottomRadiusKm        = 6360.0f;
        atmo.atmosphereHeightKm    = 60.0f;
        atmo.rayleighScattering    = {5.802e-3f, 13.558e-3f, 33.1e-3f};
        atmo.rayleighScaleHeightKm = 8.0f;
        atmo.mieScattering         = 3.996e-3f;
        atmo.mieAnisotropy         = 0.8f;
        atmo.mieScaleHeightKm      = 1.2f;
        atmo.groundAlbedo          = {0.4f, 0.4f, 0.4f};  // UE5: FColor(170,170,170)
        atmo.multiScatteringFactor = 1.0f;
    }

    // ── Sky light (UE5 exact defaults) ───────────────────────────────────
    Entity skyLightEnt = world.CreateEntity();
    {
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        // UE5 defaults with boost for simpler IBL pipeline.
        sl.intensity                  = 1.0f;      // UE5 default
        sl.indirectLightingIntensity  = 1.0f;
        sl.lightColor                 = {1.0f, 1.0f, 1.0f};
        sl.lowerHemisphereIsBlack     = true;
        sl.lowerHemisphereColor       = {0.0f, 0.0f, 0.0f};
        sl.minOcclusion               = 0.0f;   // UE5 default
        sl.cubemapResolution          = 128;
    }

    // ── Exponential Height Fog (UE5 exact defaults) ──────────────────────
    Entity fog = world.CreateEntity();
    {
        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        // UE5: FogInscatteringColor = (0.447, 0.638, 1.0) — blue haze
        // UE5 cm-scale defaults.
        fogComp.color                             = {0.447f, 0.638f, 1.0f};
        fogComp.density                           = 0.0f;      // TEMP: disabled for testing
        fogComp.heightFalloff                     = 0.2f;
        fogComp.baseHeight                        = 0.f;
        fogComp.maxOpacity                        = 1.0f;
        fogComp.startDistance                     = 0.f;
        fogComp.directionalInscatteringColor      = {0.25f, 0.25f, 0.125f};
        fogComp.directionalInscatteringExponent   = 4.0f;
        fogComp.directionalInscatteringStartDistance = 10000.f;  // 10000cm = 100m (UE5 default)
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
