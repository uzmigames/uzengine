/// FBX Test — Load FBX character, apply PBR textures, render.

#include "fbx_test.h"

#include "log.h"
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

#include <filesystem>

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig FBXTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 FBX Test";
    cfg.width           = 1920;
    cfg.height          = 1080;
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = -200.f;
    cfg.cameraPositionZ = 100.f;
    cfg.cameraYawDeg    = 0.f;
    cfg.cameraPitchDeg  = 0.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void FBXTest::OnStart() {
    auto& world = GetWorld();
    const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";

    // ── Load FBX mesh via runtime API (auto-caches as .uzmesh) ───────────
    const std::string fbxPath = (assetsDir / "MushroomMonster_Low.fbx").string();
    Mesh characterMesh = uz::LoadFbx(fbxPath);

    if (!characterMesh.IsValid()) {
        UZ_LOG(Demo, Error, "Failed to load FBX: {}", fbxPath);
        return;
    }

    UZ_LOG(Demo, Info, "Character mesh: {} verts, {} indices",
           characterMesh.GetVertexCount(), characterMesh.GetIndexCount());

    // ── Character material (PBR textures) ────────────────────────────────
    characterMat_.albedoPath    = "textures/MushroomMonster_Low_MushroomMesh2_1_BaseColor.1001.png";
    characterMat_.normalPath    = "textures/MushroomMonster_Low_MushroomMesh2_1_Normal.1001.png";
    characterMat_.roughnessPath = "textures/MushroomMonster_Low_MushroomMesh2_1_OcclusionRoughnessMetallic.1001.png";
    characterMat_.metallicPath  = "textures/MushroomMonster_Low_MushroomMesh2_1_OcclusionRoughnessMetallic.1001.png";
    characterMat_.aoPath        = "textures/MushroomMonster_Low_MushroomMesh2_1_OcclusionRoughnessMetallic.1001.png";

    // ── Create character entity ──────────────────────────────────────────
    // FBX from Mixamo uses meters — scale 100x to centimeters (UzEngine units)
    character_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(character_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({100.f, 100.f, 100.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(character_);
        mesh.meshPath = (assetsDir / "MushroomMonster_Low.uzmesh").string();
        mesh.SetMaterial(0, &characterMat_);
    }

    // ── Ground plane ─────────────────────────────────────────────────────
    {
        const std::filesystem::path planePath = assetsDir / "plane.uzmesh";
        if (!uz::FileExists(planePath.string().c_str())) {
            const Mesh plane = uz::GeneratePlaneMesh(8.0f);
            uz::SaveMesh(planePath.string(), plane);
        }

        ground_ = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({10000.f, 10000.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = planePath.string();
    }

    // ── Sun (directional light) ──────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetPosition({0.f, 0.f, 920.f});
        constexpr float kD2R = 3.14159265f / 180.f;
        scene.SetRotation(Quat::FromEuler(45.0f * kD2R, -45.0f * kD2R, 0.f));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.intensity = 10.f;
        light.color     = {1.f, 0.95f, 0.85f};
    }

    // ── Sky atmosphere ───────────────────────────────────────────────────
    {
        Entity sky = world.CreateEntity();
        world.AddComponent<SceneComponent>(sky);
        world.AddComponent<SkyAtmosphereComponent>(sky);
    }

    // ── Sky light (IBL) ──────────────────────────────────────────────────
    {
        Entity skyLight = world.CreateEntity();
        world.AddComponent<SceneComponent>(skyLight);
        world.AddComponent<SkyLightComponent>(skyLight);
    }

    // ── Height fog ───────────────────────────────────────────────────────
    {
        Entity fog = world.CreateEntity();
        world.AddComponent<SceneComponent>(fog);
        world.AddComponent<HeightFogComponent>(fog);
    }

    UZ_LOG(Demo, Info, "FBX test scene ready");
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate
// ─────────────────────────────────────────────────────────────────────────────

void FBXTest::OnUpdate(float dt) {
    totalTime_ += dt;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStop
// ─────────────────────────────────────────────────────────────────────────────

void FBXTest::OnStop() {
    UZ_LOG(Demo, Info, "FBX test shutdown");
}

} // namespace uz
