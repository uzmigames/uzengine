/// HDRI Environment Test — .exr panorama as sky environment with IBL.
///
/// Loads a 4K equirectangular HDR panorama, converts it to a cubemap via
/// GPU compute, bakes irradiance and prefiltered specular cubemaps, then
/// renders PBR spheres with varying metallic/roughness to showcase the
/// HDRI environment reflections.

#include "hdri_test.h"

#include "log.h"
#include "input.h"
#include "scene_renderer.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "sky_light_component.h"
#include "texture_loader.h"
#include "vfs.h"

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

static constexpr float kPi = 3.14159265358979323846f;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig HDRITest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 HDRI Environment Test";
    cfg.width           = 1920;
    cfg.height          = 1080;
    // Camera position looking at the sphere array from a slightly elevated angle
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = -600.f;
    cfg.cameraPositionZ = 200.f;
    cfg.cameraYawDeg    = 90.f;
    cfg.cameraPitchDeg  = -10.0f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void HDRITest::OnStart() {
    auto& world = GetWorld();
    auto& renderer = GetRenderer();

    // ── Load HDRI panorama (.exr) via VFS ───────────────────────────────
    {
        auto& vfs = GetVFS();
        const auto exrData = vfs.Read("assets/grasslands_sunset_4k.exr");
        if (exrData.empty()) {
            UZ_LOG(Demo, Error, "Failed to read HDRI from VFS");
        } else {
            auto result = TextureLoader::LoadHDRFromMemory(
                renderer.GetDevice(), exrData.data(), exrData.size(),
                "exr", "grasslands_sunset_4k");

            if (result.handle.IsValid()) {
                hdriTexture_ = result.handle;
                UZ_LOG(Demo, Info, "HDRI loaded: {}x{}, SRV={}",
                       result.width, result.height, result.srvIndex);

                if (!renderer.SetHDRIEnvironment(result.srvIndex)) {
                    UZ_LOG(Demo, Error, "Failed to set HDRI as environment");
                }
            } else {
                UZ_LOG(Demo, Error, "Failed to decode HDRI from VFS data");
            }
        }
    }

    // ── Define materials with varying metallic/roughness ─────────────────
    //
    // 7 spheres showcasing different IBL reflection characteristics:
    // 1. Pure chrome (metallic=1, roughness=0.05) — sharp mirror reflections
    // 2. Brushed metal (metallic=1, roughness=0.3) — soft specular
    // 3. Rough metal (metallic=1, roughness=0.7) — very diffused reflections
    // 4. Gold (metallic=1, roughness=0.15) — warm metallic
    // 5. Smooth plastic (metallic=0, roughness=0.1) — dielectric, sharp IBL
    // 6. Rough plastic (metallic=0, roughness=0.6) — diffuse-dominant
    // 7. Half-and-half (metallic=0.5, roughness=0.3) — semi-metallic

    // Chrome — mirror-like
    chromeMatLow_.baseColorTintR = 0.95f;
    chromeMatLow_.baseColorTintG = 0.95f;
    chromeMatLow_.baseColorTintB = 0.95f;
    chromeMatLow_.metallicMultiplier = 1.0f;
    chromeMatLow_.roughnessMultiplier = 0.05f;

    // Brushed metal
    chromeMatMid_.baseColorTintR = 0.85f;
    chromeMatMid_.baseColorTintG = 0.85f;
    chromeMatMid_.baseColorTintB = 0.87f;
    chromeMatMid_.metallicMultiplier = 1.0f;
    chromeMatMid_.roughnessMultiplier = 0.3f;

    // Rough metal
    chromeMatHigh_.baseColorTintR = 0.7f;
    chromeMatHigh_.baseColorTintG = 0.72f;
    chromeMatHigh_.baseColorTintB = 0.73f;
    chromeMatHigh_.metallicMultiplier = 1.0f;
    chromeMatHigh_.roughnessMultiplier = 0.7f;

    // Gold
    metalMat_.baseColorTintR = 1.0f;
    metalMat_.baseColorTintG = 0.766f;
    metalMat_.baseColorTintB = 0.336f;
    metalMat_.metallicMultiplier = 1.0f;
    metalMat_.roughnessMultiplier = 0.15f;

    // Smooth plastic (dielectric)
    plasticMat_.baseColorTintR = 0.8f;
    plasticMat_.baseColorTintG = 0.1f;
    plasticMat_.baseColorTintB = 0.1f;
    plasticMat_.metallicMultiplier = 0.0f;
    plasticMat_.roughnessMultiplier = 0.1f;

    // Rough plastic
    roughMat_.baseColorTintR = 0.2f;
    roughMat_.baseColorTintG = 0.3f;
    roughMat_.baseColorTintB = 0.8f;
    roughMat_.metallicMultiplier = 0.0f;
    roughMat_.roughnessMultiplier = 0.6f;

    // Semi-metallic
    dielectricMat_.baseColorTintR = 0.5f;
    dielectricMat_.baseColorTintG = 0.9f;
    dielectricMat_.baseColorTintB = 0.5f;
    dielectricMat_.metallicMultiplier = 0.5f;
    dielectricMat_.roughnessMultiplier = 0.3f;

    Material* materials[kSphereCount] = {
        &chromeMatLow_, &chromeMatMid_, &chromeMatHigh_, &metalMat_,
        &plasticMat_, &roughMat_, &dielectricMat_
    };

    // ── Create spheres in a row ──────────────────────────────────────────
    for (int i = 0; i < kSphereCount; ++i) {
        spheres_[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(spheres_[i]);
        auto& mesh  = world.AddComponent<StaticMeshComponent>(spheres_[i]);

        // Single row, evenly spaced, centered at origin
        const float spacing = 250.f;
        const float totalWidth = static_cast<float>(kSphereCount - 1) * spacing;
        const float x = -totalWidth * 0.5f + static_cast<float>(i) * spacing;
        scene.SetPosition({x, 0.f, 100.f});
        scene.SetScale({100.f, 100.f, 100.f});

        mesh.meshPath = "builtin:sphere";
        mesh.SetMaterial(0, materials[i]);
    }

    // ── Ground plane (dark, slightly reflective) ─────────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({1500.f, 1500.f, 1.f});

        groundMat_.baseColorTintR = 0.15f;
        groundMat_.baseColorTintG = 0.15f;
        groundMat_.baseColorTintB = 0.15f;
        groundMat_.metallicMultiplier = 0.0f;
        groundMat_.roughnessMultiplier = 0.4f;

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = "builtin:cube";
        mesh.SetMaterial(0, &groundMat_);
    }

    // ── Directional sun light ────────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetPosition({0.f, 0.f, 920.f});
        constexpr float kD2R = kPi / 180.f;
        const float pitch = 35.0f * kD2R;
        const float yaw   = -60.0f * kD2R;
        scene.SetRotation(Quat::FromEuler(pitch, yaw, 0.f));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color              = {1.0f, 0.98f, 0.95f};
        light.intensity          = 8.0f;
        light.atmosphereSunLight = true;
        light.lightSourceAngle   = 0.5357f;
        light.useTemperature     = false;
    }

    // ── Sky light (IBL ambient) ──────────────────────────────────────────
    // The SkyLightComponent drives the IBL intensity. Even with an HDRI
    // environment, the sky light controls how much IBL affects surfaces.
    {
        auto skyLightEnt = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 500.f});
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        sl.intensity                 = 1.0f;
        sl.indirectLightingIntensity = 1.0f;
        sl.lightColor                = {1.0f, 1.0f, 1.0f};
        sl.realTimeCapture           = false;
        sl.sourceType                = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution         = 128;
        sl.lowerHemisphereIsBlack    = false;
        sl.lowerHemisphereColor      = {0.1f, 0.1f, 0.12f};
        sl.minOcclusion              = 0.0f;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate
// ─────────────────────────────────────────────────────────────────────────────

void HDRITest::OnUpdate(float dt) {
    totalTime_ += dt;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStop
// ─────────────────────────────────────────────────────────────────────────────

void HDRITest::OnStop() {
    auto& renderer = GetRenderer();
    auto* device = renderer.GetDevice();

    if (hdriTexture_.IsValid() && device) {
        device->DestroyTexture(hdriTexture_);
        hdriTexture_ = RHITextureHandle{};
    }
}

} // namespace uz
