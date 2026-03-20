/// Cornell Box Demo — GI Color Bleeding Test
///
/// Classic Cornell box: white room with a red left wall, green right wall,
/// and a white diffuse sphere in the center. With functioning GI, the sphere
/// should show red color bleeding on its left side and green on its right.

#include "cornell_box.h"

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

UZ_DECLARE_LOG_CATEGORY(CornellBox);
UZ_DEFINE_LOG_CATEGORY(CornellBox, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig CornellBoxDemo::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 Cornell Box GI Test";
    cfg.width           = 1920;
    cfg.height          = 1080;
    // Camera positioned outside the open face looking into the box.
    // Box center is at (0, 0, 250). Camera looks along +Y into the box.
    cfg.cameraPositionX =   0.f;
    cfg.cameraPositionY = -500.f;
    cfg.cameraPositionZ = 100.f;
    cfg.cameraYawDeg    = -270.f;   // Facing +Y direction
    cfg.cameraPitchDeg  =   0.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void CornellBoxDemo::OnStart() {
    auto& world = GetWorld();

    // ── Generate procedural sphere mesh if not cached ──────────────────────
    {
        const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";
        const std::filesystem::path spherePath = assetsDir / "sphere.uzmesh";
        if (!uz::FileExists(spherePath.string().c_str())) {
            const Mesh sphere = uz::GenerateSphereMesh(64, 32);
            if (uz::SaveMesh(spherePath.string(), sphere)) {
                UZ_LOG(CornellBox, Info, "Generated sphere.uzmesh: v={}, i={}",
                       sphere.GetVertexCount(), sphere.GetIndexCount());
            }
        }
    }

    // ── Material setup (solid colors, no textures) ─────────────────────────
    // With empty texture paths, the renderer uses fallback textures:
    // - Albedo fallback: white (1,1,1) -> baseColorTint IS the final color
    // - Normal fallback: flat blue (0.5, 0.5, 1.0)
    // - Roughness fallback: white (1.0)
    // - Metallic fallback: white (1.0) -> we set metallicMultiplier to 0

    // White material: diffuse, non-metallic, rough
    whiteMat_.baseColorTintR      = 0.9f;
    whiteMat_.baseColorTintG      = 0.9f;
    whiteMat_.baseColorTintB      = 0.9f;
    whiteMat_.roughnessMultiplier = 0.9f;
    whiteMat_.metallicMultiplier  = 0.0f;

    // Red material: strong red, non-metallic, rough
    redMat_.baseColorTintR      = 0.8f;
    redMat_.baseColorTintG      = 0.05f;
    redMat_.baseColorTintB      = 0.05f;
    redMat_.roughnessMultiplier = 0.9f;
    redMat_.metallicMultiplier  = 0.0f;

    // Green material: strong green, non-metallic, rough
    greenMat_.baseColorTintR      = 0.05f;
    greenMat_.baseColorTintG      = 0.8f;
    greenMat_.baseColorTintB      = 0.05f;
    greenMat_.roughnessMultiplier = 0.9f;
    greenMat_.metallicMultiplier  = 0.0f;

    // ── Box geometry ───────────────────────────────────────────────────────
    // The box is 500cm wide (X), 500cm deep (Y), 500cm tall (Z).
    // Centered at X=0, Y=0, with floor at Z=0 and ceiling at Z=500.
    // The open face is at Y < 0 (camera side).
    //
    // Each wall is a cube.uzmesh scaled flat along the thin axis.
    // cube.uzmesh is a unit cube centered at origin, so scaling by
    // (500, 500, 10) creates a 500x500x10 slab.

    // ── Floor (Z=0, white) ─────────────────────────────────────────────────
    floor_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(floor_);
        scene.SetPosition({0.f, 0.f, -5.f});
        scene.SetScale({500.f, 500.f, 10.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(floor_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &whiteMat_);
    }

    // ── Ceiling (Z=500, white) ─────────────────────────────────────────────
    ceiling_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ceiling_);
        scene.SetPosition({0.f, 0.f, 505.f});
        scene.SetScale({500.f, 500.f, 10.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ceiling_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &whiteMat_);
    }

    // ── Back wall (Y=250, white) ───────────────────────────────────────────
    backWall_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(backWall_);
        scene.SetPosition({0.f, 255.f, 250.f});
        scene.SetScale({500.f, 10.f, 500.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(backWall_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &whiteMat_);
    }

    // ── Left wall (X=-250, red) ────────────────────────────────────────────
    leftWall_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(leftWall_);
        scene.SetPosition({-255.f, 0.f, 250.f});
        scene.SetScale({10.f, 500.f, 500.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(leftWall_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &redMat_);
    }

    // ── Right wall (X=250, green) ──────────────────────────────────────────
    rightWall_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(rightWall_);
        scene.SetPosition({255.f, 0.f, 250.f});
        scene.SetScale({10.f, 500.f, 500.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(rightWall_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &greenMat_);
    }

    // ── Center sphere (white diffuse) ──────────────────────────────────────
    sphere_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sphere_);
        scene.SetPosition({0.f, 0.f, 100.f});
        scene.SetScale({80.f, 80.f, 80.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(sphere_);
        mesh.meshPath = "sphere.uzmesh";
        mesh.SetMaterial(0, &whiteMat_);
    }

    // ── Directional sun light (above-right) ────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetPosition({0.f, 0.f, 920.f});
        constexpr float kDegToRad = 3.14159265f / 180.f;
        // Sun angled down from above-right: pitch=50 deg down, yaw=-30 deg
        // This lights the interior through the open face and ceiling area.
        const float pitch = 50.0f * kDegToRad;
        const float yaw   = -30.0f * kDegToRad;
        scene.SetRotation(Quat::FromEuler(pitch, yaw, 0.f));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color              = {1.0f, 1.0f, 1.0f};
        light.intensity          = 10.0f;
        light.atmosphereSunLight = true;
        /// @source UE5 DirectionalLightComponent.cpp:1024 — LightSourceAngle = 0.5357 (Earth's sun)
        light.lightSourceAngle   = 0.5357f;
        light.useTemperature     = false;
    }

    // ── Sky atmosphere ─────────────────────────────────────────────────────
    Entity sky = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sky);
        scene.SetPosition({0.f, 0.f, 1020.f});
        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
        atmo.bottomRadiusKm          = 6360.0f;
        atmo.atmosphereHeightKm      = 60.0f;
        atmo.multiScatteringFactor   = 1.0f;
        atmo.rayleighScatteringScale = 1.0f;
        atmo.rayleighScaleHeightKm   = 8.0f;
        atmo.mieScatteringScale      = 1.0f;
        atmo.mieAbsorptionScale      = 1.0f;
        atmo.mieAnisotropy           = 0.8f;
        atmo.mieScaleHeightKm        = 1.2f;
        atmo.sunIlluminance          = {10.0f, 10.0f, 10.0f};
        atmo.skyLuminanceFactor      = {1.0f, 1.0f, 1.0f};
        /// @source UE5 SkyAtmosphereComponent.cpp:103 — FColor(170,170,170) ~ 0.4 linear
        atmo.groundAlbedo            = {0.4f, 0.4f, 0.4f};
        atmo.heightFogContribution   = 1.0f;
    }

    // ── Sky light ──────────────────────────────────────────────────────────
    Entity skyLightEnt = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 690.f});
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        /// @source UE5 SkyLightComponent.cpp:285-333
        sl.intensity                 = 1.0f;
        sl.indirectLightingIntensity = 1.0f;
        sl.lightColor                = {1.0f, 1.0f, 1.0f};
        sl.realTimeCapture           = false;
        sl.sourceType                = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution         = 128;
        sl.lowerHemisphereIsBlack    = true;
        sl.lowerHemisphereColor      = {0.0f, 0.0f, 0.0f};
        sl.minOcclusion              = 0.0f;
    }

    // ── Exponential Height Fog (minimal, mostly to have atmosphere fog) ────
    Entity fog = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(fog);
        scene.SetPosition({0.f, 0.f, -5000.f});
        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        fogComp.density          = 0.0436f;
        fogComp.heightFalloff    = 0.2f;
        fogComp.baseHeight       = -5000.f;
        fogComp.maxOpacity       = 1.0f;
        fogComp.startDistance    = 0.f;
        fogComp.fogCutoffDistance = 0.f;
        fogComp.color            = {0.0f, 0.0f, 0.0f};
        fogComp.directionalInscatteringColor          = {0.0f, 0.0f, 0.0f};
        fogComp.directionalInscatteringExponent        = 4.0f;
        fogComp.directionalInscatteringStartDistance   = 10000.f;
    }

    GetRenderer().SetScreenPercentage(100.0f);

    UZ_LOG(CornellBox, Info,
           "Cornell Box demo started — 5 walls + sphere + sun + sky, "
           "screen percentage={}%",
           GetRenderer().GetScreenPercentage());
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStop
// ─────────────────────────────────────────────────────────────────────────────

void CornellBoxDemo::OnStop() {
    // No GPU-baked textures to destroy; all materials use solid colors
    // with the renderer's built-in fallback textures.
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate
// ─────────────────────────────────────────────────────────────────────────────

void CornellBoxDemo::OnUpdate(float dt) {
    totalTime_ += dt;

    auto& input    = GetInput();
    auto& renderer = GetRenderer();

    if (input.IsKeyJustPressed(Key::F1)) {
        renderer.SetRenderPath(RenderPath::Deferred);
        UZ_LOG(CornellBox, Info, "Render path: Deferred");
    }
    if (input.IsKeyJustPressed(Key::F2)) {
        renderer.SetRenderPath(RenderPath::Forward);
        UZ_LOG(CornellBox, Info, "Render path: Forward");
    }
}

} // namespace uz
