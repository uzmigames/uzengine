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
#include "volumetric_clouds_component.h"
#include "reflection_probe_component.h"
#include "render_path.h"
#include "mesh.h"
#include "paths.h"
#include "file_system.h"
#include "material_baker.h"
#include "material_baker_gpu.h"
#include "shader_manager.h"
#include "shader_pipeline.h"
#include "vfs.h"

#include <cmath>
#include <filesystem>
#include <fstream>

#include <json.hpp>

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
    cfg.width           = 1920;
    cfg.height          = 1080;
    // Camera looking at PBR spheres on ground plane
    cfg.cameraPositionX = -251.f;
    cfg.cameraPositionY = -833.f;
    cfg.cameraPositionZ = 222.f;
    cfg.cameraYawDeg    = 76.2f;
    cfg.cameraPitchDeg  = -12.0f;
    cfg.renderPath      = RenderPath::Deferred;
    return cfg;
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadPbrMaterial — simple .uzmat JSON loader
// ─────────────────────────────────────────────────────────────────────────────

/// Loads a simple PBR material from a .uzmat JSON file via VFS.
/// Populates the Material struct's texture paths and UV params.
/// Returns true on success, false if the file cannot be read or parsed.
static bool LoadPbrMaterial(Material& mat, IVFS& vfs, const std::string& vfsPath) {
    std::vector<uint8_t> data = vfs.Read(vfsPath);
    if (data.empty()) {
        UZ_LOG(Demo, Error, "LoadPbrMaterial: cannot read '{}' from VFS", vfsPath);
        return false;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(data.begin(), data.end());
    } catch (const nlohmann::json::exception& e) {
        UZ_LOG(Demo, Error, "LoadPbrMaterial: JSON parse error in '{}': {}", vfsPath, e.what());
        return false;
    }

    // Textures
    if (root.contains("textures")) {
        const auto& tex = root["textures"];
        if (tex.contains("albedo"))    mat.albedoPath    = tex["albedo"].get<std::string>();
        if (tex.contains("normal"))    mat.normalPath    = tex["normal"].get<std::string>();
        if (tex.contains("roughness")) mat.roughnessPath = tex["roughness"].get<std::string>();
        if (tex.contains("metallic"))  mat.metallicPath  = tex["metallic"].get<std::string>();
        if (tex.contains("ao"))        mat.aoPath        = tex["ao"].get<std::string>();
        if (tex.contains("emissive"))  mat.emissivePath  = tex["emissive"].get<std::string>();
    }

    // Params
    if (root.contains("params")) {
        const auto& params = root["params"];
        if (params.contains("uvScaleX"))            mat.uvScaleX            = params["uvScaleX"].get<float>();
        if (params.contains("uvScaleY"))            mat.uvScaleY            = params["uvScaleY"].get<float>();
        if (params.contains("uvOffsetX"))           mat.uvOffsetX           = params["uvOffsetX"].get<float>();
        if (params.contains("uvOffsetY"))           mat.uvOffsetY           = params["uvOffsetY"].get<float>();
        if (params.contains("roughnessMultiplier")) mat.roughnessMultiplier = params["roughnessMultiplier"].get<float>();
        if (params.contains("metallicMultiplier"))  mat.metallicMultiplier  = params["metallicMultiplier"].get<float>();
        if (params.contains("normalIntensity"))     mat.normalIntensity     = params["normalIntensity"].get<float>();
        if (params.contains("aoIntensity"))         mat.aoIntensity         = params["aoIntensity"].get<float>();
    }

    UZ_LOG(Demo, Info, "Loaded PBR material '{}': albedo='{}', normal='{}', rough='{}', metal='{}', ao='{}'",
           root.value("name", "unnamed"), mat.albedoPath, mat.normalPath,
           mat.roughnessPath, mat.metallicPath, mat.aoPath);

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStart
// ─────────────────────────────────────────────────────────────────────────────

void PBRTest::OnStart() {
    auto& world = GetWorld();
    const std::filesystem::path assetsDir = uz::GetExeDir() / "assets";
    const std::filesystem::path matsDir   = assetsDir / "materials";

    // ── Generate procedural meshes ───────────────────────────────────────
    {
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

    // ── Initialize 9 diverse materials from .uzmat files (loaded via VFS/pak) ──
    auto& vfs = GetVFS();
    LoadPbrMaterial(goldMat_,          vfs, "assets/materials/gold/gold.uzmat");
    LoadPbrMaterial(metalMat_,         vfs, "assets/materials/metal/metal.uzmat");
    LoadPbrMaterial(rattanMat_,        vfs, "assets/materials/rattan/rattan.uzmat");
    LoadPbrMaterial(quartziteMat_,     vfs, "assets/materials/quartzite/quartzite.uzmat");
    LoadPbrMaterial(stoneBrickMat_,    vfs, "assets/materials/stonebricks/stonebricks.uzmat");
    LoadPbrMaterial(concreteMat_,      vfs, "assets/materials/ground/concrete_worn.uzmat");
    LoadPbrMaterial(metalCorrodedMat_, vfs, "assets/materials/metal_corroded/metal_corroded.uzmat");
    LoadPbrMaterial(grassMat_,         vfs, "assets/materials/grass/grass.uzmat");
    LoadPbrMaterial(sandMat_,          vfs, "assets/materials/sand/sand.uzmat");

    // ── Unlit test material — flat red, no lighting response ──
    unlitMat_.shadingModel = EShadingModel::Unlit;
    unlitMat_.baseColorTintR = 1.0f;
    unlitMat_.baseColorTintG = 0.2f;
    unlitMat_.baseColorTintB = 0.1f;

    // ── Subsurface scattering test material — warm skin-like sphere ──
    sssMat_.shadingModel = EShadingModel::Subsurface;
    sssMat_.baseColorTintR = 0.9f;
    sssMat_.baseColorTintG = 0.7f;
    sssMat_.baseColorTintB = 0.6f;
    sssMat_.roughnessMultiplier = 0.8f;
    sssMat_.subsurfaceColorR = 1.0f;
    sssMat_.subsurfaceColorG = 0.2f;
    sssMat_.subsurfaceColorB = 0.1f;
    sssMat_.subsurfaceRadius = 1.2f;

    // ── Ground material from graph-based .uzmat ───────────────────────
    //
    // Load the MI_ProcGrid material graph, compile it to HLSL, then bake
    // BaseColor and Roughness to GPU textures via compute shader dispatch.
    // This replaces the ~8 second CPU bake + PNG round-trip with a single
    // GPU dispatch (~0.1ms). The baked textures are directly bound as SRVs,
    // bypassing the file-based texture loading path entirely.

    {
        const std::filesystem::path uzmatPath = matsDir / "mi_procgrid.uzmat";

        materialCache_.Init((assetsDir / "cache" / "materials").string());

        if (graphMaterial_.LoadFromFile(uzmatPath.string(), materialCache_)) {
            UZ_LOG(Demo, Info, "Material graph '{}' compiled — HLSL:\n{}",
                   graphMaterial_.GetName(), graphMaterial_.GetGeneratedHlsl());

            auto& renderer = GetRenderer();
            auto* device = renderer.GetDevice();
            auto* shaderMgr = renderer.GetShaderManager();

            if (device && shaderMgr) {
                auto& pipeline = shaderMgr->GetPipeline();
                const auto& hlsl = graphMaterial_.GetGeneratedHlsl();

                // GPU bake BaseColor (1024x1024)
                auto colorResult = MaterialBakerGpu::Bake(
                    device, pipeline, hlsl,
                    MaterialBakerGpu::OutputPin::BaseColor, 1024, 1024);

                if (colorResult.success) {
                    gpuBakedBaseColor_ = colorResult.texture;
                    groundMat_.albedoSrvIndex = colorResult.srvIndex;
                    groundMat_.gpuResourcesLoaded = true;
                    UZ_LOG(Demo, Info, "GPU-baked BaseColor (1024x1024, SRV={})",
                           colorResult.srvIndex);
                } else {
                    UZ_LOG(Demo, Warning,
                           "GPU BaseColor bake failed — using fallback checker.png");
                    groundMat_.albedoPath = "assets/textures/checker.png";
                }

                // GPU bake Roughness (512x512)
                auto roughResult = MaterialBakerGpu::Bake(
                    device, pipeline, hlsl,
                    MaterialBakerGpu::OutputPin::Roughness, 512, 512);

                if (roughResult.success) {
                    gpuBakedRoughness_ = roughResult.texture;
                    groundMat_.roughnessSrvIndex = roughResult.srvIndex;
                    UZ_LOG(Demo, Info, "GPU-baked Roughness (512x512, SRV={})",
                           roughResult.srvIndex);
                } else {
                    UZ_LOG(Demo, Warning,
                           "GPU Roughness bake failed — no roughness texture for ground");
                }
            } else {
                UZ_LOG(Demo, Warning,
                       "Device or ShaderManager unavailable — falling back to CPU bake");

                // CPU fallback path (MaterialBaker)
                static constexpr uint32_t kBakeColorSize     = 1024;
                static constexpr uint32_t kBakeRoughnessSize = 512;
                const auto& graph = graphMaterial_.GetGraph();

                BakeResult baseColorBake = MaterialBaker::BakeBaseColor(
                    graph, kBakeColorSize, kBakeColorSize);
                if (baseColorBake.success) {
                    groundMat_.albedoPath = "assets/textures/checker.png";
                    UZ_LOG(Demo, Info, "CPU-baked BaseColor ({}x{})",
                           baseColorBake.width, baseColorBake.height);
                } else {
                    groundMat_.albedoPath = "assets/textures/checker.png";
                }

                BakeResult roughnessBake = MaterialBaker::BakeRoughness(
                    graph, kBakeRoughnessSize, kBakeRoughnessSize);
                if (!roughnessBake.success) {
                    UZ_LOG(Demo, Warning,
                           "CPU Roughness bake failed — no roughness texture for ground");
                }
            }
        } else {
            UZ_LOG(Demo, Warning, "Failed to load mi_procgrid.uzmat — using fallback checker.png");
            groundMat_.albedoPath = "assets/textures/checker.png";
        }
    }

    // ── Material array for sphere assignment ─────────────────────────────
    Material* materials[kSphereCount] = {
        &goldMat_, &metalMat_, &rattanMat_, &quartziteMat_, &stoneBrickMat_,
        &concreteMat_, &metalCorrodedMat_, &grassMat_, &sandMat_, &unlitMat_, &sssMat_
    };

    // ── Create spheres ──
#if 1 // Enabled for AO/PBR visual testing
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
            // Front row: 6 spheres, radius=120cm (includes Unlit + SSS test spheres)
            float x = -750.f + static_cast<float>(i - 5) * 300.f;
            scene.SetPosition({x, 200.f, 120.f});
            scene.SetScale({120.f, 120.f, 120.f});
        }

        mesh.meshPath = "assets/meshes/sphere.uzmesh";
        mesh.SetMaterial(0, materials[i]);
    }
#endif

    // ── Ground plane ─────────────────────────────────────────────────────
#if 1 // Enabled for AO/PBR visual testing
    ground_ = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(ground_);
        scene.SetPosition({0.f, 0.f, 0.f});
        scene.SetScale({800.f, 800.f, 2.f});

        auto& mesh = world.AddComponent<StaticMeshComponent>(ground_);
        mesh.meshPath = "assets/meshes/cube.uzmesh";
        mesh.SetMaterial(0, &groundMat_);
    }
#endif

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
        /// @refDirectionalLightComponent.cpp:1024 — LightSourceAngle = 0.5357 (Earth's sun)
        light.lightSourceAngle   = 0.5357f;
        light.useTemperature     = false;
    }

    // ── 4 colored point lights ───────────────────────────────────────────
    /*struct PointLightDef { Vec3 position; float range; Vec3 color; };
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
    }*/

    // ── DIAGNOSTIC: Components gated by #if for horizon tint isolation ──
    // Change these to 1 to enable each component, rebuild, screenshot.

    // ── Sky atmosphere ───────────────────────────────────────────────────
#if 1 // DIAG: sky atmosphere
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
        /// @refSkyAtmosphereComponent.cpp:103 — FColor(170,170,170) = ~0.4 linear
        atmo.groundAlbedo           = {0.4f, 0.4f, 0.4f};
        atmo.heightFogContribution  = 1.0f;
    }
#endif

    // ── Sky light ────────────────────────────────────────────────────────
#if 1 // DIAG: sky light
    Entity skyLightEnt = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(skyLightEnt);
        scene.SetPosition({0.f, 0.f, 690.f});
        auto& sl = world.AddComponent<SkyLightComponent>(skyLightEnt);
        /// @refSkyLightComponent.cpp:285-333 — all defaults
        sl.intensity                 = 0.8f;       ///< Balanced ambient with contrast
        sl.indirectLightingIntensity = 0.8f;       ///< Indirect below direct for contrast
        sl.lightColor                = {0.85f, 0.9f, 1.0f}; ///< Slight cool tint for natural sky fill
        sl.realTimeCapture           = false;      ///< @refSkyLightComponent.cpp:322
        sl.sourceType                = ESkyLightSourceType::CapturedScene;
        sl.cubemapResolution         = 128;        ///< @refSkyLightComponent.cpp:311
        sl.lowerHemisphereIsBlack    = false;      ///< Allow ground bounce light for natural fill
        sl.lowerHemisphereColor      = {0.15f, 0.16f, 0.20f}; ///< Ground bounce fill light
        sl.minOcclusion              = 0.0f;       ///< Allow fully dark AO
    }
#endif

    // ── Reflection Probe ──────────────────────────────────────────────
    {
        auto probeEnt = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(probeEnt);
        scene.SetPosition({0.f, 0.f, 150.f}); // Center of sphere grid, slightly above floor
        auto& probe = world.AddComponent<ReflectionProbeComponent>(probeEnt);
        probe.influenceRadius = 2000.0f; // 20m — covers the entire demo scene
        probe.blendDistance   = 500.0f;  // 5m fade at edges
        probe.resolution      = 128;
        probe.priority         = 0;
    }

    // ── Exponential Height Fog ───────────────────────────────────────────
#if 1 // DIAG: fog
    Entity fog = world.CreateEntity();
    {
        auto& scene = world.AddComponent<SceneComponent>(fog);
        scene.SetPosition({0.f, 0.f, -5000.f});
        world.AddComponent<HeightFogComponent>(fog);
    }
#endif

    // ── Volumetric Clouds ─────────────────────────────────────────────
#if 1 // DIAG: volumetric clouds
    {
        auto cloudEntity = world.CreateEntity();
        auto& scene = world.AddComponent<SceneComponent>(cloudEntity);
        scene.SetPosition({0.f, 0.f, 0.f});
        auto& clouds = world.AddComponent<VolumetricCloudsComponent>(cloudEntity);
        clouds.coverage              = 0.5f;
        clouds.density               = 1.0f;
        clouds.layerBottomAltitudeKm = 6.0f;
        clouds.layerHeightKm         = 10.0f;
        clouds.enabled = true;
    }
#endif

    // ── Screen percentage (render at lower internal resolution) ────────
    // 67% = render at ~960p for 1440p display, TAA upscales to native.
    // 100% = native resolution (no upscale, current default behavior).
    GetRenderer().SetScreenPercentage(100.0f);

    UZ_LOG(Demo, Info, "PBR Test started — {} spheres (9 materials), 4 point lights + sun + sky, "
           "screen percentage={}%",
           kSphereCount, GetRenderer().GetScreenPercentage());
}

// ─────────────────────────────────────────────────────────────────────────────
// OnStop
// ─────────────────────────────────────────────────────────────────────────────

void PBRTest::OnStop() {
    auto& renderer = GetRenderer();
    auto* device = renderer.GetDevice();
    if (device) {
        if (gpuBakedBaseColor_.IsValid()) {
            device->DestroyTexture(gpuBakedBaseColor_);
            gpuBakedBaseColor_ = RHITextureHandle::Invalid();
        }
        if (gpuBakedRoughness_.IsValid()) {
            device->DestroyTexture(gpuBakedRoughness_);
            gpuBakedRoughness_ = RHITextureHandle::Invalid();
        }
    }
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
