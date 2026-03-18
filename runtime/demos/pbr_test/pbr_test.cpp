/// PBR Test — 9 material spheres + ground + sky + lights.
///
/// Showcases diverse PBR materials: Gold, Concrete, Grass, Metal,
/// MetalCorroded, RattanWeave, Sand, StoneQuartzite, Stonebricks.
/// Two rows of spheres (5 back, 4 front) matching the PoC1 layout.

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
#include "material_baker.h"

#include <stb_image_write.h>

#include <cmath>
#include <filesystem>

UZ_DECLARE_LOG_CATEGORY(Demo);
UZ_DEFINE_LOG_CATEGORY(Demo, Info);

namespace uz {

static constexpr float kPi = 3.14159265358979323846f;
static constexpr float kLightOrbitRadius = 600.f;
static constexpr float kLightOrbitHeight = 300.f;
static constexpr float kLightOrbitSpeed  = 0.5f;

// ─────────────────────────────────────────────────────────────────────────────
// Configure
// ─────────────────────────────────────────────────────────────────────────────

ApplicationConfig PBRTest::Configure() {
    ApplicationConfig cfg{};
    cfg.title           = "UzEngine \xe2\x80\x94 PBR Test";
    cfg.width           = 2560;
    cfg.height          = 1440;
    // Camera elevated and angled to see both rows of spheres
    cfg.cameraPositionX = 700.f;
    cfg.cameraPositionY = 100.f;
    cfg.cameraPositionZ = 400.f;
    cfg.cameraYawDeg    = 195.f;  // slightly rotated to center on sphere array
    cfg.cameraPitchDeg  = -20.f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void PBRTest::OnStart() {
    auto& world = GetWorld();

    // ── Generate procedural meshes ───────────────────────────────────────
    {
        const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";

        const std::filesystem::path spherePath = assetsDir / "sphere.uzmesh";
        if (!uz::FileExists(spherePath.string().c_str())) {
            const Mesh sphere = uz::GenerateSphereMesh(64, 32); // High-poly for smooth silhouettes at 1440p
            if (uz::SaveMesh(spherePath.string(), sphere)) {
                UZ_LOG(Demo, Info, "Generated sphere.uzmesh: v={}, i={}",
                       sphere.GetVertexCount(), sphere.GetIndexCount());
            }
        }

        const std::filesystem::path planePath = assetsDir / "plane_tiled.uzmesh";
        if (!uz::FileExists(planePath.string().c_str())) {
            const Mesh plane = uz::GeneratePlaneMesh(8.0f);
            if (uz::SaveMesh(planePath.string(), plane)) {
                UZ_LOG(Demo, Info, "Generated plane_tiled.uzmesh: v={}, i={}",
                       plane.GetVertexCount(), plane.GetIndexCount());
            }
        }
    }

    // ── Initialize 9 diverse materials ───────────────────────────────────

    // 0: Gold (metallic)
    goldMat_.albedoPath    = "Poliigon_MetalGoldPaint_7253_BaseColor.jpg";
    goldMat_.normalPath    = "Poliigon_MetalGoldPaint_7253_Normal.png";
    goldMat_.roughnessPath = "Poliigon_MetalGoldPaint_7253_Roughness.jpg";
    goldMat_.metallicPath  = "Poliigon_MetalGoldPaint_7253_Metallic.jpg";
    goldMat_.aoPath        = "Poliigon_MetalGoldPaint_7253_AmbientOcclusion.jpg";

    // 1: Metal (galvanized steel)
    metalMat_.albedoPath    = "MetalGalvanizedSteelWorn001_COL_2K_METALNESS.jpg";
    metalMat_.normalPath    = "MetalGalvanizedSteelWorn001_NRM_2K_METALNESS.jpg";
    metalMat_.roughnessPath = "MetalGalvanizedSteelWorn001_ROUGHNESS_2K_METALNESS.jpg";
    metalMat_.metallicPath  = "MetalGalvanizedSteelWorn001_METALNESS_2K_METALNESS.jpg";

    // 2: RattanWeave (organic)
    rattanMat_.albedoPath    = "Poliigon_RattanWeave_6945_BaseColor.jpg";
    rattanMat_.normalPath    = "Poliigon_RattanWeave_6945_Normal.png";
    rattanMat_.roughnessPath = "Poliigon_RattanWeave_6945_Roughness.jpg";
    rattanMat_.metallicPath  = "Poliigon_RattanWeave_6945_Metallic.jpg";
    rattanMat_.aoPath        = "Poliigon_RattanWeave_6945_AmbientOcclusion.jpg";

    // 3: StoneQuartzite (white stone)
    quartziteMat_.albedoPath    = "Poliigon_StoneQuartzite_8060_BaseColor.jpg";
    quartziteMat_.normalPath    = "Poliigon_StoneQuartzite_8060_Normal.png";
    quartziteMat_.roughnessPath = "Poliigon_StoneQuartzite_8060_Roughness.jpg";
    quartziteMat_.metallicPath  = "Poliigon_StoneQuartzite_8060_Metallic.jpg";
    quartziteMat_.aoPath        = "Poliigon_StoneQuartzite_8060_AmbientOcclusion.jpg";

    // 4: Stonebricks (classic)
    stoneBrickMat_.albedoPath    = "Stonebricks_BaseColor.jpg";
    stoneBrickMat_.normalPath    = "Stonebricks_Normal.jpg";
    stoneBrickMat_.roughnessPath = "Stonebricks_Roughness.jpg";
    stoneBrickMat_.metallicPath  = "Stonebricks_Metallic.jpg";
    stoneBrickMat_.aoPath        = "Stonebricks_AO.jpg";
    stoneBrickMat_.uvScaleX      = 3.0f;
    stoneBrickMat_.uvScaleY      = 3.0f;

    // 5: Concrete
    concreteMat_.albedoPath    = "Poliigon_ConcreteWorn_8690_BaseColor.jpg";
    concreteMat_.normalPath    = "Poliigon_ConcreteWorn_8690_Normal.png";
    concreteMat_.roughnessPath = "Poliigon_ConcreteWorn_8690_Roughness.jpg";
    concreteMat_.metallicPath  = "Poliigon_ConcreteWorn_8690_Metallic.jpg";
    concreteMat_.aoPath        = "Poliigon_ConcreteWorn_8690_AmbientOcclusion.jpg";

    // 6: MetalCorroded
    metalCorrodedMat_.albedoPath    = "MetalCorrodedHeavy001_COL_2K_METALNESS.jpg";
    metalCorrodedMat_.normalPath    = "MetalCorrodedHeavy001_NRM_2K_METALNESS.jpg";
    metalCorrodedMat_.roughnessPath = "MetalCorrodedHeavy001_ROUGHNESS_2K_METALNESS.jpg";
    metalCorrodedMat_.metallicPath  = "MetalCorrodedHeavy001_METALNESS_2K_METALNESS.jpg";

    // 7: Grass
    grassMat_.albedoPath    = "Poliigon_GrassPatchyGround_4585_BaseColor.jpg";
    grassMat_.normalPath    = "Poliigon_GrassPatchyGround_4585_Normal.png";
    grassMat_.roughnessPath = "Poliigon_GrassPatchyGround_4585_Roughness.jpg";
    grassMat_.metallicPath  = "Poliigon_GrassPatchyGround_4585_Metallic.jpg";
    grassMat_.aoPath        = "Poliigon_GrassPatchyGround_4585_AmbientOcclusion.jpg";

    // 8: Sand (uses GLOSS map — invert for roughness if needed)
    sandMat_.albedoPath    = "GroundSand005_COL_2K.jpg";
    sandMat_.normalPath    = "GroundSand005_NRM_2K.jpg";
    sandMat_.aoPath        = "GroundSand005_AO_2K.jpg";
    // Sand has a GLOSS map — we'll use roughness=1.0 default (no roughness texture)

    // ── Ground material from graph-based .uzmat ───────────────────────
    //
    // Load the MI_ProcGrid material graph, compile it to HLSL (verifying
    // the full material pipeline), then bake BaseColor and Roughness to
    // textures on CPU. The baked textures are written as PNG files to the
    // assets directory and assigned to the ground material for rendering
    // via the standard texture-based path.

    {
        const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";
        const std::filesystem::path uzmatPath = assetsDir / "mi_procgrid.uzmat";

        materialCache_.Init((assetsDir / "cache" / "materials").string());

        if (graphMaterial_.LoadFromFile(uzmatPath.string(), materialCache_)) {
            UZ_LOG(Demo, Info, "Material graph '{}' compiled — HLSL:\n{}",
                   graphMaterial_.GetName(), graphMaterial_.GetGeneratedHlsl());

            // Bake BaseColor (1024x1024) and Roughness (512x512) from the graph
            static constexpr uint32_t kBakeColorSize     = 1024;
            static constexpr uint32_t kBakeRoughnessSize = 512;

            const auto& graph = graphMaterial_.GetGraph();

            BakeResult baseColorBake = MaterialBaker::BakeBaseColor(
                graph, kBakeColorSize, kBakeColorSize);
            BakeResult roughnessBake = MaterialBaker::BakeRoughness(
                graph, kBakeRoughnessSize, kBakeRoughnessSize);

            if (baseColorBake.success) {
                const std::string colorPath = (assetsDir / "mi_procgrid_basecolor.png").string();
                int wrote = stbi_write_png(
                    colorPath.c_str(),
                    static_cast<int>(baseColorBake.width),
                    static_cast<int>(baseColorBake.height),
                    4, baseColorBake.pixels.data(),
                    static_cast<int>(baseColorBake.width * 4));
                if (wrote) {
                    groundMat_.albedoPath = "mi_procgrid_basecolor.png";
                    UZ_LOG(Demo, Info, "Baked BaseColor to '{}' ({}x{})",
                           colorPath, baseColorBake.width, baseColorBake.height);
                } else {
                    UZ_LOG(Demo, Error, "Failed to write baked BaseColor PNG: {}", colorPath);
                    groundMat_.albedoPath = "checker.png";
                }
            } else {
                UZ_LOG(Demo, Warning, "BaseColor bake failed — using fallback checker.png");
                groundMat_.albedoPath = "checker.png";
            }

            if (roughnessBake.success) {
                const std::string roughPath = (assetsDir / "mi_procgrid_roughness.png").string();
                int wrote = stbi_write_png(
                    roughPath.c_str(),
                    static_cast<int>(roughnessBake.width),
                    static_cast<int>(roughnessBake.height),
                    4, roughnessBake.pixels.data(),
                    static_cast<int>(roughnessBake.width * 4));
                if (wrote) {
                    groundMat_.roughnessPath = "mi_procgrid_roughness.png";
                    UZ_LOG(Demo, Info, "Baked Roughness to '{}' ({}x{})",
                           roughPath, roughnessBake.width, roughnessBake.height);
                } else {
                    UZ_LOG(Demo, Error, "Failed to write baked Roughness PNG: {}", roughPath);
                }
            } else {
                UZ_LOG(Demo, Warning, "Roughness bake failed — no roughness texture for ground");
            }
        } else {
            UZ_LOG(Demo, Warning, "Failed to load mi_procgrid.uzmat — using fallback checker.png");
            groundMat_.albedoPath = "checker.png";
        }
    }

    // ── Material array for sphere assignment ─────────────────────────────
    Material* materials[kSphereCount] = {
        &goldMat_, &metalMat_, &rattanMat_, &quartziteMat_, &stoneBrickMat_,
        &concreteMat_, &metalCorrodedMat_, &grassMat_, &sandMat_
    };

    // ── Create 9 spheres in two rows (PoC1 layout) ─────────────────────
    // Back row (Y=-200): 5 smaller spheres, spaced 250cm apart
    // Front row (Y=200): 4 larger spheres, spaced 300cm apart
    // All spheres sit ON the ground (Z = radius)
    for (int i = 0; i < kSphereCount; ++i) {
        spheres_[i] = world.CreateEntity();

        auto& scene = world.AddComponent<SceneComponent>(spheres_[i]);
        auto& mesh  = world.AddComponent<StaticMeshComponent>(spheres_[i]);

        if (i < 5) {
            // Back row: 5 spheres, radius=80cm
            float x = -500.f + static_cast<float>(i) * 250.f;
            scene.SetPosition({x, -200.f, 80.f});
            scene.SetScale({80.f, 80.f, 80.f});
        } else {
            // Front row: 4 spheres, radius=120cm
            float x = -450.f + static_cast<float>(i - 5) * 300.f;
            scene.SetPosition({x, 200.f, 120.f});
            scene.SetScale({120.f, 120.f, 120.f});
        }

        mesh.meshPath = "sphere.uzmesh";
        mesh.SetMaterial(0, materials[i]);
    }

    // ── Ground plane ─────────────────────────────────────────────────────
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({1500.f, 1500.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = "cube.uzmesh";
        mesh.SetMaterial(0, &groundMat_);
    }

    // ── Directional sun light ────────────────────────────────────────────
    sun_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sun_);
        scene.SetPosition({0.f, 0.f, 920.f});
        constexpr float kD2R = 3.14159265f / 180.f;
        float pitch = 45.0f * kD2R;
        float yaw   = -45.0f * kD2R;
        scene.SetRotation(Quat::FromEuler(pitch, yaw, 0.f));

        auto& light = world.AddComponent<DirectionalLightComponent>(sun_);
        light.color              = {1.0f, 1.0f, 1.0f};
        light.intensity          = 10.0f;
        light.atmosphereSunLight = true;
        light.lightSourceAngle   = 2.0f;  // Wider than real sun (0.5357°) for softer artistic shadows
        light.useTemperature     = false;
    }

    // ── 4 colored point lights ───────────────────────────────────────────
    struct PointLightDef { Vec3 position; float range; Vec3 color; };
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
        auto& light = world.AddComponent<PointLightComponent>(pointLights_[i]);
        light.color     = kPointLightDefs[i].color;
        light.intensity = 1.0f;
        light.range     = kPointLightDefs[i].range;
    }

    // ── Sky atmosphere ───────────────────────────────────────────────────
    Entity sky = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(sky);
        scene.SetPosition({0.f, 0.f, 1020.f});
        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(sky);
        atmo.bottomRadiusKm         = 6360.0f;
        atmo.atmosphereHeightKm     = 60.0f;
        atmo.multiScatteringFactor  = 1.0f;
        atmo.rayleighScatteringScale = 1.0f;
        atmo.rayleighScaleHeightKm  = 8.0f;
        atmo.mieScatteringScale     = 1.0f;
        atmo.mieAbsorptionScale     = 1.0f;
        atmo.mieAnisotropy          = 0.8f;
        atmo.mieScaleHeightKm       = 1.2f;
        atmo.sunIlluminance         = {10.0f, 10.0f, 10.0f};
        atmo.skyLuminanceFactor     = {1.0f, 1.0f, 1.0f};
        atmo.groundAlbedo           = {0.667f, 0.667f, 0.667f};
        atmo.heightFogContribution  = 1.0f;
    }

    // ── Sky light ────────────────────────────────────────────────────────
    Entity skyLightEnt = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 690.f});
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        sl.intensity                 = 1.0f;       /// @source UE5 SkyLightComponent.cpp:1195
        sl.indirectLightingIntensity = 1.0f;       /// @source UE5 SkyLightComponent.cpp:1196
        sl.lightColor                = {0.85f, 0.9f, 1.0f}; // Slight blue tint from sky — fills shadows with sky color
        sl.realTimeCapture           = false;
        sl.sourceType                = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution         = 128;
        sl.lowerHemisphereIsBlack    = false;  // Allow ambient from below for better shadow fill
        sl.minOcclusion              = 0.15f;  // Prevents fully black shadows — UE5 default 0.0 but 0.15 gives better fill
    }

    // ── Exponential Height Fog ───────────────────────────────────────────
    Entity fog = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(fog);
        scene.SetPosition({0.f, 0.f, 0.f});
        auto& fogComp = world.AddComponent<HeightFogComponent>(fog);
        /// @source UE5 Basic.json export — ExponentialHeightFog_0
        ///   FogDensity=0.0436 (from Inspector screenshot)
        ///   FogHeightFalloff=0.2
        ///   Actor position Z=-5000 (baseHeight equivalent)
        ///   FogInscatteringColor=black
        ///   StartDistance=0
        /// With baseHeight=-5000 and falloff=0.2, density at ground (Z=0):
        ///   0.0436 * exp2(-0.2 * 5000) ≈ 0 — no visible fog on nearby geometry.
        ///   Fog only accumulates over very long horizontal rays (sky distance),
        ///   creating the smooth horizon haze gradient.
        fogComp.density         = 0.0436f;
        fogComp.heightFalloff   = 0.2f;
        fogComp.baseHeight      = -5000.f;
        fogComp.maxOpacity      = 1.0f;
        fogComp.startDistance    = 0.f;
        fogComp.fogCutoffDistance = 0.f;
        fogComp.color            = {0.0f, 0.0f, 0.0f};
        fogComp.directionalInscatteringColor      = {0.0f, 0.0f, 0.0f};
        fogComp.directionalInscatteringExponent   = 4.0f;
        fogComp.directionalInscatteringStartDistance = 10000.f;
    }

    // ── Screen percentage (render at lower internal resolution) ────────
    // 67% = render at ~960p for 1440p display, TAA upscales to native.
    // 100% = native resolution (no upscale, current default behavior).
    GetRenderer().SetScreenPercentage(67.f);

    UZ_LOG(Demo, Info, "PBR Test started — {} spheres (9 materials), 4 point lights + sun + sky, "
           "screen percentage={}%",
           kSphereCount, GetRenderer().GetScreenPercentage());
}

// ─────────────────────────────────────────────────────────────────────────────
// OnUpdate
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

    // ── Orbit first 3 point lights ───────────────────────────────────────
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
