/// PBR Test — Application-based demo implementation.
///
/// Creates 5 PBR spheres with Stonebricks material, a tiled ground plane,
/// a directional sun light, and 4 colored point lights that orbit the scene.
/// Procedural sphere and plane meshes are generated on first run and cached
/// as .uzmesh files in the assets directory.
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
#include "mesh.h"
#include "paths.h"
#include "file_system.h"

#include <cmath>
#include <filesystem>

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
    // Camera in front of sphere on X axis, looking back toward origin.
    // Sphere at (0,0,100). Camera at (400,0,150) looking toward (-X) = yaw 180°.
    cfg.cameraPositionX = 400.f;
    cfg.cameraPositionY = 0.f;
    cfg.cameraPositionZ = 150.f;
    cfg.cameraYawDeg    = 180.f;   // look toward -X (back toward origin/sphere)
    cfg.cameraPitchDeg  = -5.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart — Create scene entities
// ─────────────────────────────────────────────────────────────────────────────

void PBRTest::OnStart() {
    auto& world = GetWorld();

    // ── Generate procedural meshes into the assets directory ──────────────
    // These are written once and cached on disk. The VFS root is <exe>/assets/,
    // so we write directly there so the renderer can load them by short name.
    {
        const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";

        // sphere.uzmesh — 32 slices × 16 stacks UV sphere, radius=1 (scaled by SceneComponent)
        const std::filesystem::path spherePath = assetsDir / "sphere.uzmesh";
        if (!uz::FileExists(spherePath.string().c_str())) {
            const Mesh sphere = uz::GenerateSphereMesh(32, 16);
            if (uz::SaveMesh(spherePath.string(), sphere)) {
                UZ_LOG(Demo, Info,
                       "Generated sphere.uzmesh: vertices={}, indices={}",
                       sphere.GetVertexCount(), sphere.GetIndexCount());
            }
        }

        // plane_tiled.uzmesh — flat quad with UV range [0, 8] for checker tiling
        const std::filesystem::path planePath = assetsDir / "plane_tiled.uzmesh";
        if (!uz::FileExists(planePath.string().c_str())) {
            const Mesh plane = uz::GeneratePlaneMesh(8.0f);
            if (uz::SaveMesh(planePath.string(), plane)) {
                UZ_LOG(Demo, Info,
                       "Generated plane_tiled.uzmesh: vertices={}, indices={}",
                       plane.GetVertexCount(), plane.GetIndexCount());
            }
        }
    }

    // ── Single PBR sphere — for shadow debug ────────────────────────────
    cubes_[0] = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(cubes_[0]);
        scene.SetPosition({0.f, 0.f, 100.f});     // Center, 100cm above ground
        scene.SetScale({100.f, 100.f, 100.f});     // Radius = 100cm (1m sphere)

        auto& mesh = world.AddComponent<StaticMeshComponent>(cubes_[0]);
        mesh.meshPath      = "sphere.uzmesh";
        mesh.albedoPath    = "Stonebricks_BaseColor.jpg";
        mesh.normalPath    = "Stonebricks_Normal.jpg";
        mesh.roughnessPath = "Stonebricks_Roughness.jpg";
        mesh.metallicPath  = "Stonebricks_Metallic.jpg";
        mesh.aoPath        = "Stonebricks_AO.jpg";
    }

    // ── Ground plane (cube.uzmesh flattened) ────────────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
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
        // Sun at 45° elevation, pointing south-east — clear diagonal shadow on ground
        constexpr float kD2R = 3.14159265f / 180.f;
        float pitch = 45.0f * kD2R;    // 45° down from horizon
        float yaw   = -45.0f * kD2R;   // South-east
        float roll  = 0.0f * kD2R;
        scene.SetRotation(Quat::FromEuler(pitch, yaw, roll));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color              = {1.0f, 1.0f, 1.0f};
        light.intensity          = 10.0f;          /// @source UE5 DirectionalLightComponent.cpp:1043
        light.atmosphereSunLight = true;
        light.lightSourceAngle   = 0.5357f;        /// @source UE5 DirectionalLightComponent.h:138 — Earth sun angular diameter
        light.useTemperature     = false;           /// @source UE5 LightComponent.h:61 — disabled by default
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
        atmo.rayleighScatteringScale = 1.0f;     // UE5: raw values already include scale
        atmo.rayleighScaleHeightKm   = 8.0f;
        atmo.mieScatteringScale      = 1.0f;
        atmo.mieAbsorptionScale      = 1.0f;
        atmo.mieAnisotropy           = 0.8f;
        atmo.mieScaleHeightKm        = 1.2f;
        // Sun illuminance = DirectionalLight.Intensity * Color.
        /// @source UE5 — SunIlluminance derived from DirectionalLight (10.0 * white)
        atmo.sunIlluminance          = {10.0f, 10.0f, 10.0f};
        atmo.skyLuminanceFactor       = {1.0f, 1.0f, 1.0f};
        /// @source UE5 SkyAtmosphereComponent.cpp:104 — GroundAlbedo = FColor(170,170,170)
        atmo.groundAlbedo            = {0.667f, 0.667f, 0.667f};
        /// @source UE5 SkyAtmosphereComponent.cpp:129 — HeightFogContribution = 1.0
        atmo.heightFogContribution   = 1.0f;
    }

    // ── Sky light (UE5 Basic Scene EXACT values) ─────────────────────────
    Entity skyLightEnt = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 690.f});

        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        sl.intensity                  = 1.0f;       /// @source UE5 SkyLightComponent.cpp:1195
        sl.indirectLightingIntensity  = 1.0f;       /// @source UE5 SkyLightComponent.cpp:1196
        sl.lightColor                 = {1.0f, 1.0f, 1.0f};
        sl.realTimeCapture            = false;       /// @source UE5 SkyLightComponent.cpp:1213
        sl.sourceType                 = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution          = 128;         /// @source UE5 SkyLightComponent.cpp:1210
        sl.lowerHemisphereIsBlack     = true;        /// @source UE5 SkyLightComponent.cpp:1199
        sl.minOcclusion               = 0.0f;        /// @source UE5 SkyLightComponent.cpp:1202
    }

    // ── Exponential Height Fog ─────────────────────────────────────────
    /// @source UE5 ExponentialHeightFogComponent.cpp — default parameter values
    /// UE5 defaults: density=0.02, heightFalloff=0.2, startDistance=0.
    /// Base height at ground level (0 cm) so fog is visible in the scene.
    /// Directional inscattering adds sun-colored glow toward the light.
    Entity fog = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(fog);
        scene.SetPosition({0.f, 0.f, 0.f});

        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        fogComp.density                           = 0.02f;      /// @source UE5 ExponentialHeightFogComponent.cpp:1250
        fogComp.heightFalloff                     = 0.2f;       /// @source UE5 ExponentialHeightFogComponent.cpp:1251
        fogComp.baseHeight                        = 0.f;        // Ground level
        fogComp.maxOpacity                        = 1.0f;       /// @source UE5 ExponentialHeightFogComponent.cpp:1253
        fogComp.startDistance                     = 0.f;        /// @source UE5 ExponentialHeightFogComponent.cpp:1254
        fogComp.fogCutoffDistance                 = 0.f;        /// @source UE5 ExponentialHeightFogComponent.cpp:1256
        /// @source UE5 ExponentialHeightFogComponent.cpp:64-103 — EXACT defaults
        fogComp.color                             = {0.0f, 0.0f, 0.0f};    /// FogInscatteringLuminance = Black (0,0,0)
        fogComp.directionalInscatteringColor      = {0.0f, 0.0f, 0.0f};    /// DirectionalInscatteringLuminance = Black (0,0,0)
        fogComp.directionalInscatteringExponent   = 4.0f;       /// @source UE5 line 93
        fogComp.directionalInscatteringStartDistance = 10000.f;  /// @source UE5 line 94
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
