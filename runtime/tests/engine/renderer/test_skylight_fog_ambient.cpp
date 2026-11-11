/// Unit tests for phase3a-3.51.0-skylight-fog-ambient-fix (CPU-only, no GPU device).
///
/// - `HeightFogComponent::skyAtmosphereAmbientContributionColorScale` — default white, the
///   `sky_atmosphere_ambient_contribution_color_scale` serializer key in and out, a missing key, the
///   round trip, the scene loader's registry path
/// - the fog renderer — `AddHeightFogFromECS` uploads the AUTHORED scale into
///   `HeightFogCB::skyAtmContribColorScale` (it was hardcoded to white), the atmosphere's
///   `heightFogContribution` (0 without a SkyAtmosphereComponent) and the sky colour it was given
/// - the shipped height fog shader — the injection expression the GPU evaluates:
///   `InscatteringColor += skyAtmContribColorScale.xyz * heightFogContribution * float3(skyLightColor)`
///   (a contract on the embedded source, like test_sky_light_intensity.cpp)
/// - the sky-light ambient fallback (sky_light_fallback.h) — `IsSkySHZero`, `SkyIBLReady`,
///   `SkyLightFallbackAmbient`, the constants
/// - the ambient defaults stay zero (`LightFrameData`, `SceneLighting`): the invented
///   {0.08, 0.09, 0.12} removed in e2571a66 does not come back
/// - `HeightFogCB` — the injection block's offsets the HLSL copy declares

#include "fog_renderer.h"
#include "height_fog_component.h"
#include "sky_atmosphere_component.h"
#include "sky_light_fallback.h"
#include "light_renderer.h"
#include "scene_lighting.h"
#include "component_serializer.h"
#include "constant_buffers.h"
#include "camera.h"
#include "world.h"
#include "scene_loader.h"
#include "pipeline_library.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "engine_shaders.h"
#include "mock_rhi_device.h"

#include <json.hpp>
#include <entt/entt.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <regex>
#include <string>
#include <vector>

using namespace uz;

namespace {

entt::entity ToEntt(Entity e) { return static_cast<entt::entity>(static_cast<uint32_t>(e.id)); }
std::vector<uint8_t> ToBytes(const std::string& s) { return {s.begin(), s.end()}; }

constexpr const char* kFogKey = "height_fog";
constexpr const char* kScaleKey = "sky_atmosphere_ambient_contribution_color_scale";

/// Registers the height fog pass for a HeightFogComponent with `scale` authored, with or without a
/// SkyAtmosphereComponent (heightFogContribution `contribution`), and returns the HeightFogCB it
/// uploaded (read back through the ring's bump contract: the probe allocation precedes the pass's).
HeightFogCB UploadedHeightFogCB(const Vec3& scale, bool withAtmosphere, float contribution,
                                float skyR, float skyG, float skyB) {
    uz::test::MockRHIDevice device;
    rg::RgResourcePool pool(&device);
    rg::RgBuilder rgb(&device, &pool);
    CBRingAllocator ring;
    EXPECT_TRUE(ring.Init(&device, 1, 16 * 1024));
    ring.BeginFrame(0);
    PipelineLibrary pipelines;
    World world;
    const Entity e = world.CreateEntity();
    auto& fog = world.AddComponent<HeightFogComponent>(e);
    fog.enabled = true;
    fog.density = 0.02f;
    fog.skyAtmosphereAmbientContributionColorScale = scale;
    if (withAtmosphere) {
        auto& atmo = world.AddComponent<SkyAtmosphereComponent>(world.CreateEntity());
        atmo.heightFogContribution = contribution;
    }
    Camera camera;
    camera.SetPosition({0.0f, 0.0f, 150.0f});
    const rg::RgResourceId hdr = rgb.ImportTexture("HDR", RHITextureHandle{1, 1}, RHIResourceState::RenderTarget);
    const rg::RgResourceId depth = rgb.ImportTexture("Depth", RHITextureHandle{2, 1}, RHIResourceState::ShaderResource);
    const CBAllocation probe = ring.Allocate(sizeof(HeightFogCB));
    AddHeightFogFromECS(rgb, hdr, depth, CBAllocation{}, &world, camera, &pipelines, ring,
                        5u, RHITextureHandle{2, 1}, true, 1920.f, 1080.f, skyR, skyG, skyB, 0.0f);
    EXPECT_EQ(rgb.GetPassCount(), 1u);
    HeightFogCB cb{};
    std::memcpy(&cb, static_cast<const uint8_t*>(probe.cpuPtr) + sizeof(HeightFogCB), sizeof(HeightFogCB));
    ring.Shutdown();
    return cb;
}

SHCoefficients ZeroSH() {
    SHCoefficients sh{};
    for (Vec4& c : sh.coeffs) c = {0.0f, 0.0f, 0.0f, 0.0f};
    return sh;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogComponent: the scale property and its serializer key
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogSkyScale, DefaultIsWhite) {
    const HeightFogComponent c{};
    EXPECT_FLOAT_EQ(c.skyAtmosphereAmbientContributionColorScale.x, 1.0f);
    EXPECT_FLOAT_EQ(c.skyAtmosphereAmbientContributionColorScale.y, 1.0f);
    EXPECT_FLOAT_EQ(c.skyAtmosphereAmbientContributionColorScale.z, 1.0f);
}

TEST(HeightFogSkyScale, DeserializeReadsTheKey) {
    World world;
    const Entity e = world.CreateEntity();
    const nlohmann::json j = {{kScaleKey, {0.5, 0.25, 2.0}}, {"density", 0.03}};
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kFogKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<HeightFogComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.x, 0.5f);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.y, 0.25f);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.z, 2.0f);
    EXPECT_FLOAT_EQ(c->density, 0.03f);
}

TEST(HeightFogSkyScale, MissingKeyKeepsWhite) {
    World world;
    const Entity e = world.CreateEntity();
    const nlohmann::json j = {{"density", 0.03}};
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kFogKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<HeightFogComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.x, 1.0f);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.y, 1.0f);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.z, 1.0f);
}

TEST(HeightFogSkyScale, SerializeWritesTheKey) {
    World world;
    const Entity e = world.CreateEntity();
    world.AddComponent<HeightFogComponent>(e).skyAtmosphereAmbientContributionColorScale = {0.1f, 0.2f, 0.3f};
    nlohmann::json j;
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Serialize(kFogKey, world, e, &j));
    ASSERT_TRUE(j.contains(kScaleKey));
    ASSERT_TRUE(j[kScaleKey].is_array());
    ASSERT_EQ(j[kScaleKey].size(), 3u);
    EXPECT_FLOAT_EQ(j[kScaleKey][0].get<float>(), 0.1f);
    EXPECT_FLOAT_EQ(j[kScaleKey][1].get<float>(), 0.2f);
    EXPECT_FLOAT_EQ(j[kScaleKey][2].get<float>(), 0.3f);
}

TEST(HeightFogSkyScale, RoundTripPreservesTheScale) {
    World world;
    const Entity src = world.CreateEntity();
    world.AddComponent<HeightFogComponent>(src).skyAtmosphereAmbientContributionColorScale = {0.75f, 0.5f, 0.0f};
    nlohmann::json j;
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Serialize(kFogKey, world, src, &j));
    const Entity dst = world.CreateEntity();
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kFogKey, world, dst, &j));
    const auto* c = world.GetRegistry().try_get<HeightFogComponent>(ToEntt(dst));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.x, 0.75f);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.y, 0.5f);
    EXPECT_FLOAT_EQ(c->skyAtmosphereAmbientContributionColorScale.z, 0.0f);
}

/// The scene loader reaches the component through the registry (no hardcoded parser).
TEST(HeightFogSkyScale, SceneLoaderReadsTheKey) {
    World world;
    const std::string scene = R"({"entities":[{"name":"Fog","components":{"height_fog":{
        "color":[0,0,0],"sky_atmosphere_ambient_contribution_color_scale":[1.0,0.3,0.3]}}}]})";
    const auto result = LoadSceneEx(world, ToBytes(scene), "skylight_fog_ambient_test.uzscene");
    ASSERT_TRUE(result.ok()) << result.message;
    auto view = world.GetRegistry().view<HeightFogComponent>();
    ASSERT_EQ(view.size(), 1u);
    const auto& c = view.get<HeightFogComponent>(*view.begin());
    EXPECT_FLOAT_EQ(c.skyAtmosphereAmbientContributionColorScale.x, 1.0f);
    EXPECT_FLOAT_EQ(c.skyAtmosphereAmbientContributionColorScale.y, 0.3f);
    EXPECT_FLOAT_EQ(c.skyAtmosphereAmbientContributionColorScale.z, 0.3f);
    EXPECT_FLOAT_EQ(c.color.x, 0.0f);
    EXPECT_FLOAT_EQ(c.density, HeightFogComponent{}.density);   // untouched default
}

// ─────────────────────────────────────────────────────────────────────────────
// Fog renderer: the authored scale and the injection inputs reach the CB
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogSkyInjectionCB, AuthoredScaleReachesTheCB) {
    const HeightFogCB cb = UploadedHeightFogCB({0.25f, 0.5f, 2.0f}, true, 1.0f, 0.1f, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.x, 0.25f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.y, 0.5f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.z, 2.0f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.w, 0.0f);
}

TEST(HeightFogSkyInjectionCB, DefaultScaleIsWhiteAsBefore) {
    const HeightFogCB cb = UploadedHeightFogCB(HeightFogComponent{}.skyAtmosphereAmbientContributionColorScale,
                                               true, 1.0f, 0.1f, 0.2f, 0.3f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.x, 1.0f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.y, 1.0f);
    EXPECT_FLOAT_EQ(cb.skyAtmContribColorScale.z, 1.0f);
}

TEST(HeightFogSkyInjectionCB, ContributionFollowsTheAtmosphere) {
    EXPECT_FLOAT_EQ(UploadedHeightFogCB({1, 1, 1}, true, 0.4f, 0.f, 0.f, 0.f).heightFogContribution, 0.4f);
    EXPECT_FLOAT_EQ(UploadedHeightFogCB({1, 1, 1}, true, 1.0f, 0.f, 0.f, 0.f).heightFogContribution, 1.0f);
    // No SkyAtmosphereComponent: no sky contribution (UE5's SkyAtmosphereHeightFogContribution = 0).
    EXPECT_FLOAT_EQ(UploadedHeightFogCB({1, 1, 1}, false, 1.0f, 0.f, 0.f, 0.f).heightFogContribution, 0.0f);
}

TEST(HeightFogSkyInjectionCB, SkyColourPassesThrough) {
    const HeightFogCB cb = UploadedHeightFogCB({1, 1, 1}, true, 1.0f, 1.1683f, 1.1214f, 1.0291f);
    EXPECT_FLOAT_EQ(cb.skyLightColorR, 1.1683f);
    EXPECT_FLOAT_EQ(cb.skyLightColorG, 1.1214f);
    EXPECT_FLOAT_EQ(cb.skyLightColorB, 1.0291f);
}

// ─────────────────────────────────────────────────────────────────────────────
// The shipped height fog shader: the injection expression (machine-consumed)
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogSkyInjectionShader, AddsScaleTimesContributionTimesSkyColour) {
    const std::string source(EngineShaders::GetSource(EngineShader::HeightFogPS));
    const std::string code = std::regex_replace(source, std::regex(R"(//[^\r\n]*|/\*[\s\S]*?\*/)"), " ");
    // UE5 HeightFogCommon.ush:174-180: SkyAmbient = scale.rgb * HeightFogContribution * skyLightColour;
    // Inscattering += SkyAmbient. The CB fields are the ones fog_renderer.cpp fills.
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(float3\s+InscatteringColor\s*=\s*fogColor\.xyz\s*;)")));
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(InscatteringColor\s*\+=\s*skyAtmContribColorScale\.xyz\s*\*\s*heightFogContribution\s*\*\s*)"
        R"(float3\s*\(\s*skyLightColorR\s*,\s*skyLightColorG\s*,\s*skyLightColorB\s*\)\s*;)")));
    // The sun term keeps the atmosphere contribution too (HeightFogCommon.ush:319).
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(dirColor\s*\+=\s*heightFogContribution\s*\*\s*float3\s*\(\s*sunIllumPostTransR\s*,\s*sunIllumPostTransG\s*,\s*sunIllumPostTransB\s*\)\s*;)")));
}

// ─────────────────────────────────────────────────────────────────────────────
// Sky-light ambient fallback (sky_light_fallback.h)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyLightFallback, ZeroSHIsDetected) {
    EXPECT_TRUE(IsSkySHZero(ZeroSH()));
    SHCoefficients sh = ZeroSH();
    sh.coeffs[6].w = 1.0f;          // the packed Y22 row's constant
    EXPECT_FALSE(IsSkySHZero(sh));
    sh = ZeroSH();
    sh.coeffs[2].w = 1.0291f;       // B channel L0
    EXPECT_FALSE(IsSkySHZero(sh));
}

TEST(SkyLightFallback, ReadyWhenTheSHOrTheIrradianceCubemapExists) {
    EXPECT_FALSE(SkyIBLReady(ZeroSH(), UINT32_MAX));
    EXPECT_TRUE(SkyIBLReady(ZeroSH(), 24910u));       // HDRI / baked cubemap without SH
    SHCoefficients sh = ZeroSH();
    sh.coeffs[0].w = 1.1683f;
    EXPECT_TRUE(SkyIBLReady(sh, UINT32_MAX));
    EXPECT_TRUE(SkyIBLReady(sh, 24910u));
}

TEST(SkyLightFallback, AmbientIsLightColourTimesIntensityTimesOneTenth) {
    EXPECT_FLOAT_EQ(kSkyLightFallbackIrradiance, 0.1f);
    const Vec3 a = SkyLightFallbackAmbient({0.85f, 0.9f, 1.0f}, 1.0f);   // the demo scene's sky light
    EXPECT_FLOAT_EQ(a.x, 0.085f);
    EXPECT_FLOAT_EQ(a.y, 0.09f);
    EXPECT_FLOAT_EQ(a.z, 0.1f);
    const Vec3 b = SkyLightFallbackAmbient({1.0f, 1.0f, 1.0f}, 2.5f);
    EXPECT_FLOAT_EQ(b.x, 0.25f);
    EXPECT_FLOAT_EQ(b.z, 0.25f);
    const Vec3 z = SkyLightFallbackAmbient({0.85f, 0.9f, 1.0f}, 0.0f);
    EXPECT_FLOAT_EQ(z.x, 0.0f);
    EXPECT_FLOAT_EQ(z.y, 0.0f);
    EXPECT_FLOAT_EQ(z.z, 0.0f);
}

TEST(SkyLightFallback, LateFallbackIsReportedAfterFrameTen) {
    EXPECT_EQ(kSkyLightFallbackReportFrame, 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// The ambient defaults stay zero (4.1 / 4.4): no invented constant
// ─────────────────────────────────────────────────────────────────────────────

TEST(AmbientDefaults, LightFrameDataAndSceneLightingCarryNoAmbient) {
    const LightFrameData frame{};
    EXPECT_FLOAT_EQ(frame.ambientColor.x, 0.0f);
    EXPECT_FLOAT_EQ(frame.ambientColor.y, 0.0f);
    EXPECT_FLOAT_EQ(frame.ambientColor.z, 0.0f);
    const SceneLighting lighting{};
    EXPECT_FLOAT_EQ(lighting.ambientColor.x, 0.0f);
    EXPECT_FLOAT_EQ(lighting.ambientColor.y, 0.0f);
    EXPECT_FLOAT_EQ(lighting.ambientColor.z, 0.0f);
    const PerFrameCB cb{};
    EXPECT_FLOAT_EQ(cb.skyLightMinAmbient, 0.0f);
    EXPECT_FLOAT_EQ(cb.skyLightIntensity, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HeightFogCB: the injection block the HLSL copy declares
// ─────────────────────────────────────────────────────────────────────────────

TEST(HeightFogCBLayout, SkyInjectionBlockOffsets) {
    EXPECT_EQ(offsetof(HeightFogCB, skyAtmContribColorScale), 96u);
    EXPECT_EQ(offsetof(HeightFogCB, heightFogContribution), 112u);
    EXPECT_EQ(offsetof(HeightFogCB, skyLightColorR), 116u);
    EXPECT_EQ(offsetof(HeightFogCB, skyLightColorG), 120u);
    EXPECT_EQ(offsetof(HeightFogCB, skyLightColorB), 124u);
    EXPECT_EQ(offsetof(HeightFogCB, sunIllumPostTransR), 128u);
    EXPECT_EQ(sizeof(HeightFogCB), 256u);
}
