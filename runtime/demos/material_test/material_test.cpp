/// Material Test — 5 blend-mode objects + ground + sky + lights.
///
/// Showcases all material blend modes in a single scene:
/// 1. Emissive sphere  — bright cyan glow (intensity=10, drives bloom).
/// 2. Masked sphere    — alpha-tested with opacity=0.5, twoSided.
/// 3. Translucent      — blue glass (opacity=0.3, Translucent blend).
/// 4. Additive         — hologram glow (Additive blend, bright tint).
/// 5. Opaque reference — standard PBR dielectric (roughness=0.3).

#include "material_test.h"

#include "log.h"
#include "input.h"
#include "world.h"
#include "static_mesh_component.h"
#include "scene_component.h"
#include "light_component.h"
#include "sky_atmosphere_component.h"
#include "sky_light_component.h"
#include "height_fog_component.h"
#include "reflection_probe_component.h"
#include "render_path.h"
#include "scene_renderer.h"

#include <cmath>

UZ_DECLARE_LOG_CATEGORY(MaterialDemo);
UZ_DEFINE_LOG_CATEGORY(MaterialDemo, Info);

namespace uz {

static constexpr float kPi = 3.14159265358979323846f;

// Sphere spacing along X axis (cm).
static constexpr float kSphereSpacing = 300.f;

// Sphere radius (cm) — large enough to clearly see material differences.
static constexpr float kSphereRadius = 100.f;

// Number of test spheres in the row.
static constexpr int kTestObjectCount = 5;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig MaterialTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 Material Test";
    cfg.width           = 1280;
    cfg.height          = 720;
    // Camera looking at the row of spheres from the front.
    cfg.cameraPositionX = 0.f;
    cfg.cameraPositionY = -700.f;
    cfg.cameraPositionZ = 200.f;
    cfg.cameraYawDeg    = 90.f;   // Facing +Y (toward the spheres)
    cfg.cameraPitchDeg  = -5.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void MaterialTest::OnStart() {
    auto& world = GetWorld();

    // ── Configure materials ──────────────────────────────────────────────

    // 1. Emissive — bright cyan neon glow that drives bloom.
    emissiveMat_.emissiveColorR   = 0.1f;
    emissiveMat_.emissiveColorG   = 0.8f;
    emissiveMat_.emissiveColorB   = 1.0f;
    emissiveMat_.emissiveIntensity = 10.0f;
    emissiveMat_.baseColorTintR   = 0.02f;
    emissiveMat_.baseColorTintG   = 0.15f;
    emissiveMat_.baseColorTintB   = 0.2f;
    emissiveMat_.roughnessMultiplier = 0.8f;
    emissiveMat_.metallicMultiplier  = 0.0f;

    // 2. Masked — alpha-tested geometry (fence-like effect).
    //    Without a spatial opacity texture, the entire surface clips uniformly.
    //    This demonstrates the blend mode configuration and pipeline routing.
    //    A real use case would supply an opacity texture with holes (chain-link, foliage).
    maskedMat_.blendMode         = EBlendMode::Masked;
    maskedMat_.alphaTest         = true;
    maskedMat_.twoSided          = true;
    maskedMat_.opacity           = 0.6f;
    maskedMat_.alphaClipThreshold = 0.5f;
    maskedMat_.baseColorTintR    = 0.8f;
    maskedMat_.baseColorTintG    = 0.6f;
    maskedMat_.baseColorTintB    = 0.2f;
    maskedMat_.roughnessMultiplier = 0.9f;
    maskedMat_.metallicMultiplier  = 1.0f;

    // 3. Translucent — blue glass. Forward-rendered with alpha blending.
    translucentMat_.blendMode         = EBlendMode::Translucent;
    translucentMat_.opacity           = 0.3f;
    translucentMat_.baseColorTintR    = 0.2f;
    translucentMat_.baseColorTintG    = 0.4f;
    translucentMat_.baseColorTintB    = 0.8f;
    translucentMat_.roughnessMultiplier = 0.1f;
    translucentMat_.metallicMultiplier  = 0.0f;
    translucentMat_.specular          = 0.5f;

    // 4. Additive — bright hologram overlay. Forward-rendered with (One, One) blending.
    additiveMat_.blendMode         = EBlendMode::Additive;
    additiveMat_.opacity           = 0.5f;
    additiveMat_.baseColorTintR    = 0.3f;
    additiveMat_.baseColorTintG    = 1.0f;
    additiveMat_.baseColorTintB    = 0.5f;
    additiveMat_.emissiveColorR    = 0.2f;
    additiveMat_.emissiveColorG    = 0.8f;
    additiveMat_.emissiveColorB    = 0.4f;
    additiveMat_.emissiveIntensity = 3.0f;
    additiveMat_.roughnessMultiplier = 0.5f;
    additiveMat_.metallicMultiplier  = 0.0f;

    // 5. Opaque reference — standard PBR white dielectric.
    opaqueMat_.baseColorTintR    = 0.9f;
    opaqueMat_.baseColorTintG    = 0.9f;
    opaqueMat_.baseColorTintB    = 0.9f;
    opaqueMat_.roughnessMultiplier = 0.3f;
    opaqueMat_.metallicMultiplier  = 0.0f;
    opaqueMat_.specular          = 0.5f;

    // Ground — neutral gray, slightly rough.
    groundMat_.baseColorTintR    = 0.4f;
    groundMat_.baseColorTintG    = 0.4f;
    groundMat_.baseColorTintB    = 0.4f;
    groundMat_.roughnessMultiplier = 0.8f;
    groundMat_.metallicMultiplier  = 0.0f;

    // ── Create test spheres arranged in a row ────────────────────────────
    // All spheres sit ON the ground plane at Z = kSphereRadius.
    // Row centered at X=0, objects evenly spaced along X.

    Material* materials[kTestObjectCount] = {
        &emissiveMat_, &maskedMat_, &translucentMat_, &additiveMat_, &opaqueMat_
    };

    Entity* entities[kTestObjectCount] = {
        &emissiveSphere_, &maskedPlane_, &translucentSphere_, &additiveSphere_, &opaqueSphere_
    };

    const float rowStartX = -static_cast<float>(kTestObjectCount - 1) * kSphereSpacing * 0.5f;

    for (int i = 0; i < kTestObjectCount; ++i) {
        *entities[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(*entities[i]);
        const float x = rowStartX + static_cast<float>(i) * kSphereSpacing;
        scene.SetPosition({x, 0.f, kSphereRadius});
        scene.SetScale({kSphereRadius, kSphereRadius, kSphereRadius});

        auto& mesh = world.AddComponent<StaticMeshComponent>(*entities[i]);
        mesh.meshPath = "builtin:sphere";
        mesh.SetMaterial(0, materials[i]);
    }

    // ── Ground plane (flattened cube) ─────────────────────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({1200.f, 1200.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = "builtin:cube";
        mesh.SetMaterial(0, &groundMat_);
    }

    // ── Directional sun light ─────────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetPosition({0.f, 0.f, 920.f});
        constexpr float kD2R = kPi / 180.f;
        const float pitch = 45.0f * kD2R;
        const float yaw   = -45.0f * kD2R;
        scene.SetRotation(Quat::FromEuler(pitch, yaw, 0.f));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color              = {1.0f, 1.0f, 1.0f};
        light.intensity          = 10.0f;
        light.atmosphereSunLight = true;
        light.lightSourceAngle   = 0.5357f;
        light.useTemperature     = false;
    }

    // ── Sky atmosphere ────────────────────────────────────────────────────
    {
        auto sky = world.CreateEntity();
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
        atmo.groundAlbedo            = {0.4f, 0.4f, 0.4f};
        atmo.heightFogContribution   = 1.0f;
    }

    // ── Sky light ─────────────────────────────────────────────────────────
    {
        auto skyLightEnt = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 690.f});
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        sl.intensity                  = 0.8f;
        sl.indirectLightingIntensity  = 0.8f;
        sl.lightColor                 = {0.85f, 0.9f, 1.0f};
        sl.realTimeCapture            = false;
        sl.sourceType                 = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution          = 128;
        sl.lowerHemisphereIsBlack     = false;
        sl.lowerHemisphereColor       = {0.15f, 0.16f, 0.20f};
        sl.minOcclusion               = 0.0f;
    }

    // ── Reflection probe ──────────────────────────────────────────────────
    {
        auto probeEnt = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(probeEnt);
        scene.SetPosition({0.f, 0.f, 150.f});
        auto& probe = world.AddComponent<ReflectionProbeComponent>(probeEnt);
        probe.influenceRadius = 2000.0f;
        probe.blendDistance   = 500.0f;
        probe.resolution      = 128;
        probe.priority         = 0;
    }

    // ── Height fog ────────────────────────────────────────────────────────
    {
        auto fog = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(fog);
        scene.SetPosition({0.f, 0.f, -5000.f});
        world.AddComponent<HeightFogComponent>(fog);
    }

    UZ_LOG(MaterialDemo, Info,
           "Material Test started — 5 blend-mode spheres: "
           "Emissive, Masked, Translucent, Additive, Opaque");
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStop
// ─────────────────────────────────────────────────────────────────────────────

void MaterialTest::OnStop() {
    // No GPU resources owned outside the renderer — nothing to release.
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate
// ─────────────────────────────────────────────────────────────────────────────

void MaterialTest::OnUpdate(float dt) {
    totalTime_ += dt;

    auto& world = GetWorld();

    // Slowly rotate the emissive and additive spheres for visual interest.
    constexpr float kRotationSpeed = 30.0f; // degrees per second
    constexpr float kD2R = kPi / 180.f;

    if (world.IsValid(emissiveSphere_)) {
        auto& scene = world.GetComponent<SceneComponent>(emissiveSphere_);
        const float yaw = totalTime_ * kRotationSpeed * kD2R;
        scene.SetRotation(Quat::FromEuler(0.f, yaw, 0.f));
    }

    if (world.IsValid(additiveSphere_)) {
        auto& scene = world.GetComponent<SceneComponent>(additiveSphere_);
        // Rotate opposite direction for variety.
        const float yaw = -totalTime_ * kRotationSpeed * 0.7f * kD2R;
        scene.SetRotation(Quat::FromEuler(0.f, yaw, 0.f));
    }

    // Pulse the additive hologram emissive intensity for a flickering effect.
    const float pulse = 2.0f + 1.0f * std::sin(totalTime_ * 4.0f);
    additiveMat_.emissiveIntensity = pulse;
}

} // namespace uz
