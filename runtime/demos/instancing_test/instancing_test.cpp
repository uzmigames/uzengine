/// Instancing Test -- 1000 cubes stress test for GPU instancing.
///
/// All 1000 cubes use the same mesh ("cube.uzmesh") and same material
/// (Stonebricks PBR textures). The InstanceBatcher groups them into a
/// single batch, producing 1 DrawIndexedInstanced call instead of 1000.
///
/// Scene setup only -- no per-frame animation. This keeps the test
/// focused on measuring instancing efficiency without update noise.

#include "instancing_test.h"

#include "log.h"
#include "input.h"
#include "scene_renderer.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "material/material_asset.h"
#include "sky_atmosphere_component.h"
#include "sky_light_component.h"
#include "height_fog_component.h"
#include "render_path.h"

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Spacing between cube centers in world units (cm).
static constexpr float kSpacing  = 250.f;

/// Uniform scale applied to each cube (keeps them smaller than the gap).
static constexpr float kCubeScale = 40.f;

/// Vertical offset so the grid floats above the ground plane (cm).
static constexpr float kGridLiftZ = 1200.f;

/// Shared Stonebricks PBR material for all cubes.
static Material s_stoneBrickMat;

/// Shared checker material for the ground plane.
static Material s_groundMat;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig InstancingTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 Instancing Test (1000 cubes)";
    cfg.width           = 1280;
    cfg.height          = 720;
    // Camera pulled back along -Y looking into the grid (Z-up world).
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = -1500.f;
    cfg.cameraPositionZ = 800.f;
    cfg.cameraYawDeg    = 90.f;
    cfg.cameraPitchDeg  = -5.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart -- Create scene entities
// ─────────────────────────────────────────────────────────────────────────────

void InstancingTest::OnStart() {
    auto& world = GetWorld();

    // ── Initialize shared materials ──────────────────────────────────────
    s_stoneBrickMat.albedoPath    = "Stonebricks_BaseColor.jpg";
    s_stoneBrickMat.normalPath    = "Stonebricks_Normal.jpg";
    s_stoneBrickMat.roughnessPath = "Stonebricks_Roughness.jpg";
    s_stoneBrickMat.metallicPath  = "Stonebricks_Metallic.jpg";
    s_stoneBrickMat.aoPath        = "Stonebricks_AO.jpg";

    s_groundMat.albedoPath = "checker.png";

    cubes_.resize(kCubeCount);

    // ── 1000 cubes in a 10x10x10 grid ──────────────────────────────────
    // Center the grid at (0, 0, kGridLiftZ) so the camera sees it head-on.
    const float halfExtent = (kGridSize - 1) * kSpacing * 0.5f;

    int idx = 0;
    for (int x = 0; x < kGridSize; ++x) {
        for (int y = 0; y < kGridSize; ++y) {
            for (int z = 0; z < kGridSize; ++z) {
                cubes_[idx] = world.CreateEntity();

                auto& scene = world.AddComponent<SceneComponent>(cubes_[idx]);
                scene.SetPosition({
                    -halfExtent + static_cast<float>(x) * kSpacing,
                    -halfExtent + static_cast<float>(y) * kSpacing,
                    -halfExtent + static_cast<float>(z) * kSpacing + kGridLiftZ
                });
                scene.SetScale({kCubeScale, kCubeScale, kCubeScale});

                auto& mesh = world.AddComponent<StaticMeshComponent>(cubes_[idx]);
                mesh.meshPath = "cube.uzmesh";
                mesh.SetMaterial(0, &s_stoneBrickMat);

                ++idx;
            }
        }
    }

    // ── Ground plane ──────────────────────────────────────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, -100.f});
        scene.SetScale({5000.f, 5000.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &s_groundMat);
    }

    // ── Directional sun light ─────────────────────────────────────────────
    // Direction derived from SceneComponent rotation, not a raw vector.
    // Aim the sun ~50 degrees down from horizontal, rotated slightly.
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetRotation(Quat::FromAxisAngle(Vec3{0.f, -0.9357f, 0.3528f}, 1.0404f));

        auto& light   = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color     = {3.0f, 2.85f, 2.7f};
        light.intensity = 1.0f;
        light.atmosphereSunLight = true;  // This light drives the sky atmosphere sun
    }

    // ── Sky atmosphere ────────────────────────────────────────────────────
    {
        Entity sky = world.CreateEntity();
        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
        atmo.sunIlluminance = {100.f, 100.f, 100.f};
    }

    // ── Sky light (IBL ambient fill) ─────────────────────────────────────
    {
        Entity skyLightEnt = world.CreateEntity();
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        sl.intensity    = 3.0f;   // Boost IBL ambient
        sl.minOcclusion = 0.15f;  // Prevent fully black shadows (15% minimum ambient)
    }

    // ── Height fog (ground haze) ──────────────────────────────────────────
    {
        Entity fog = world.CreateEntity();
        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        fogComp.density       = 0.02f;   
        fogComp.baseHeight    = -100.f;
        fogComp.heightFalloff = 0.1f;
        fogComp.maxOpacity    = 0.3f;
        fogComp.color         = {0.75f, 0.80f, 0.90f};
    }

    UZ_LOG(Demo, Info, "Instancing Test started -- {} cubes in {}x{}x{} grid + ground",
           kCubeCount, kGridSize, kGridSize, kGridSize);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate -- Per-frame logic
// ─────────────────────────────────────────────────────────────────────────────

void InstancingTest::OnUpdate(float dt) {
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

    // Static scene -- no per-frame animation.
    // GPU instancing efficiency is best measured on a stable workload.
}

} // namespace uz
