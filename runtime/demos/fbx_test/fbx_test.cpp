/// FBX Test — Load FBX character, apply PBR textures, render.

#include "fbx_test.h"

#include "log.h"
#include "scene_renderer.h"
#include "material_baker.h"
#include "material_baker_gpu.h"
#include "shader_manager.h"
#include "shader_pipeline.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "sky_atmosphere_component.h"
#include "sky_light_component.h"
#include "height_fog_component.h"
#include "volumetric_clouds_component.h"
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
    cfg.cameraPositionY = 300.f;
    cfg.cameraPositionZ = 150.f;
    cfg.cameraYawDeg    = 270.f;
    cfg.cameraPitchDeg  = -15.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void FBXTest::OnStart() {
    auto& world = GetWorld();

    // ── Character material (PBR textures — VFS-relative paths) ───────────
    characterMat_.albedoPath = "assets/textures/MushroomMonster_Low_MushroomMesh_BaseColor.png";
    characterMat_.normalPath = "assets/textures/MushroomMonster_Low_MushroomMesh_Normal.png";
    characterMat_.ormPath    = "assets/textures/MushroomMonster_Low_MushroomMesh_ORM.png";
    characterMat_.useORM     = true;
    // ORM texture B channel has incorrect metallic (0.9+) for this organic mesh.
    // Override to 0 — mushroom is a dielectric organic material, not metal.
    characterMat_.metallicMultiplier = 0.0f;
    // Organic material: reduced dielectric specular to prevent IBL desaturation.
    // specular=0.25 → F0 = 0.08 * 0.25 = 0.02 (suitable for organic surfaces).
    characterMat_.specular = 0.25f;

    // ── Create character entity ──────────────────────────────────────────
    // meshPath uses .fbx — SceneRenderer detects it and routes through LoadFbx
    // which auto-caches as .uzmesh. Scale 100x: Mixamo meters → UzEngine cm.
    character_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(character_);
        scene.SetPosition({0.f, 0.f, 0.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(character_);
        mesh.meshPath = "assets/meshs/MushroomMonster_Low.uzmesh";
        mesh.SetMaterial(0, &characterMat_);
    }

    // ── Ground material (GPU-baked from mi_procgrid.uzmat) ─────────────
    {
        const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";
        const std::filesystem::path uzmatPath = assetsDir / "materials" / "mi_procgrid.uzmat";

        materialCache_.Init((assetsDir / "cache" / "materials").string());

        if (graphMaterial_.LoadFromFile(uzmatPath.string(), materialCache_)) {
            auto& renderer = GetRenderer();
            auto* device = renderer.GetDevice();
            auto* shaderMgr = renderer.GetShaderManager();

            if (device && shaderMgr) {
                auto& pipeline = shaderMgr->GetPipeline();
                const auto& hlsl = graphMaterial_.GetGeneratedHlsl();

                auto colorResult = MaterialBakerGpu::Bake(
                    device, pipeline, hlsl,
                    MaterialBakerGpu::OutputPin::BaseColor, 1024, 1024);
                if (colorResult.success) {
                    gpuBakedBaseColor_ = colorResult.texture;
                    groundMat_.albedoSrvIndex = colorResult.srvIndex;
                    groundMat_.gpuResourcesLoaded = true;
                } else {
                    groundMat_.albedoPath = "assets/textures/checker.png";
                }

                auto roughResult = MaterialBakerGpu::Bake(
                    device, pipeline, hlsl,
                    MaterialBakerGpu::OutputPin::Roughness, 512, 512);
                if (roughResult.success) {
                    gpuBakedRoughness_ = roughResult.texture;
                    groundMat_.roughnessSrvIndex = roughResult.srvIndex;
                }
            } else {
                groundMat_.albedoPath = "assets/textures/checker.png";
            }
        } else {
            groundMat_.albedoPath = "assets/textures/checker.png";
        }
    }

    // ── Ground plane ─────────────────────────────────────────────────────
    {
        ground_ = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, -1.f});
        scene.SetScale({2000.f, 2000.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = "builtin:cube";
        mesh.SetMaterial(0, &groundMat_);
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
        auto& sl = world.AddComponent<SkyLightComponent>(skyLight);
        sl.intensity = 1.5f;  // Stronger fill light for better material visibility
        sl.lowerHemisphereIsBlack = false;
        sl.lowerHemisphereColor = {0.2f, 0.22f, 0.28f}; // Blue-ish ground bounce
    }

    // ── Height fog ───────────────────────────────────────────────────────
    {
        Entity fog = world.CreateEntity();
        world.AddComponent<SceneComponent>(fog);
        world.AddComponent<HeightFogComponent>(fog);
    }

    // ── Volumetric Clouds ─────────────────────────────────────────────
#if 1 // DIAG: volumetric clouds
    {
        auto cloudEntity = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(cloudEntity);
        scene.SetPosition({0.f, 0.f, 0.f});
        auto& clouds = world.AddComponent<VolumetricCloudsComponent>(cloudEntity);
        clouds.coverage              = 0.4f;
        clouds.density               = 1.0f;
        clouds.layerBottomAltitudeKm = 7.0f;
        clouds.layerHeightKm         = 10.0f;
        clouds.enabled = true;
    }
#endif

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
    auto* device = GetRenderer().GetDevice();
    if (device) {
        if (gpuBakedBaseColor_.IsValid()) {
            device->DestroyTexture(gpuBakedBaseColor_);
            gpuBakedBaseColor_ = {};
        }
        if (gpuBakedRoughness_.IsValid()) {
            device->DestroyTexture(gpuBakedRoughness_);
            gpuBakedRoughness_ = {};
        }
    }
    UZ_LOG(Demo, Info, "FBX test shutdown");
}

} // namespace uz
