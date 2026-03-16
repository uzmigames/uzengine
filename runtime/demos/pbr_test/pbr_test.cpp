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
        scene.SetPosition({kCubePositionsX[i], 0.f, 80.f});  // cubes on top of ground (Z=80cm)
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
        scene.SetPosition({0.f, 0.f, 0.f});      // Ground at Z=0 (same as UE5 fogBaseHeight)
        scene.SetScale({1500.f, 1500.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath   = "cube.uzmesh";
        mesh.albedoPath = "checker.png";
    }

    // ── Directional sun light (UE5 Basic Scene EXACT values) ────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetPosition({0.f, 0.f, 920.f});
        // UE5 rotation: (25.555, -60.779, -14.988) degrees
        // Convert Euler (pitch=25.555, yaw=-60.779, roll=-14.988) to quaternion
        constexpr float kD2R = 3.14159265f / 180.f;
        float pitch = 25.555f * kD2R;
        float yaw   = -60.779f * kD2R;
        float roll  = -14.988f * kD2R;
        scene.SetRotation(Quat::FromEuler(pitch, yaw, roll));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color              = {1.0f, 1.0f, 1.0f};
        light.intensity          = 8.0f;           // Higher than UE5 3.0 to dominate over ambient
        light.atmosphereSunLight = true;
        light.lightSourceAngle   = 0.7357f;        // UE5 Basic Scene: 0.7357
        light.useTemperature     = true;
        light.temperature        = 6500.f;
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

    // ── Sky atmosphere (UE5 Basic Scene EXACT values) ─────────────────────
    Entity sky = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sky);
        scene.SetPosition({0.f, 0.f, 1020.f});

        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
        atmo.bottomRadiusKm          = 6360.0f;
        atmo.atmosphereHeightKm      = 60.0f;
        atmo.multiScatteringFactor   = 1.0f;
        // Scattering scales boost the in-scattering color without changing
        // extinction. Higher values = more vivid sky; 1.0 = physically accurate.
        atmo.rayleighScatteringScale = 4.0f;     // boost blue sky color
        atmo.rayleighScaleHeightKm   = 8.0f;
        atmo.mieScatteringScale      = 1.0f;
        atmo.mieAbsorptionScale      = 1.0f;
        atmo.mieAnisotropy           = 0.8f;
        atmo.mieScaleHeightKm        = 1.2f;
        // Sun illuminance for atmosphere scattering (decoupled from PBR lighting).
        atmo.sunIlluminance          = {1.5f, 1.5f, 1.5f};
        // Suppress red/green to make blue more vivid after ACES tonemapping.
        atmo.skyLuminanceFactor       = {1.0f, 1.0f, 1.0f};
    }

    // ── Sky light (UE5 Basic Scene EXACT values) ─────────────────────────
    Entity skyLightEnt = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 690.f});

        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        sl.intensity                  = 2.0f;       // Moderate ambient fill
        sl.indirectLightingIntensity  = 1.0f;
        sl.lightColor                 = {1.0f, 1.0f, 1.0f};
        sl.realTimeCapture            = true;       // UE5: Real Time Capture = true
        sl.sourceType                 = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution          = 128;
        sl.lowerHemisphereIsBlack     = true;
        sl.minOcclusion               = 0.0f;
    }

    // ── Exponential Height Fog (UE5 Basic Scene EXACT values) ────────────
    Entity fog = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(fog);
        scene.SetPosition({-5600.f, -50.f, -6850.f});  // UE5 fog actor position

        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        fogComp.density                           = 0.0436f;    // UE5: 0.0436
        fogComp.heightFalloff                     = 0.2f;       // UE5: 0.2
        fogComp.baseHeight                        = -6850.f;    // fog at actor Z position
        fogComp.maxOpacity                        = 1.0f;
        fogComp.startDistance                     = 0.f;
        fogComp.color                             = {0.447f, 0.638f, 1.0f};  // UE5 FogInscatteringColor
        fogComp.directionalInscatteringExponent   = 4.0f;
        fogComp.directionalInscatteringStartDistance = 10000.f;
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
