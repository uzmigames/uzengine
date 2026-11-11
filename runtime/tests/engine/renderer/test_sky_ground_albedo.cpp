/// phase3a-3.51.0 item 2.6 (2026-11-11): the sky cubemap capture renders the planet below the horizon
/// with SkyAtmosphereComponent::groundAlbedo (root constants) instead of a hardcoded 0.7, so the
/// component is the single source of the ground bounce every image-based term sees.
///
/// Pins (machine-consumed values only):
/// - the embedded sky_cubemap_cs.hlsl: the b0 cbuffer's fields in the order and count of
///   SkyCubemapCaptureRootConstants (the ground albedo being the last three), the ground term reading
///   the three root constants through the `precise` albedo / PI factor and the hoisted NdotL product
///   (the arithmetic shape that keeps the default capture bit-identical to the hardcoded-0.7 build),
///   no static GROUND_ALBEDO constant left
/// - the component default (0.7, the value the capture hardcoded before, so an unauthored scene captures
///   exactly as it did), the `ground_albedo` scene key, a missing key keeping the default
/// - the sky IBL bake signature counting the three albedo values (a runtime change re-captures)

#include "sky_cubemap_capture_pass.h"
#include "sky_atmosphere_component.h"
#include "atmosphere_renderer.h"
#include "component_serializer.h"
#include "engine_shaders.h"
#include "world.h"

#include <json.hpp>
#include <entt/entt.hpp>
#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <regex>
#include <string>
#include <vector>

using namespace uz;

namespace {

entt::entity ToEntt(Entity e) { return static_cast<entt::entity>(static_cast<uint32_t>(e.id)); }

constexpr const char* kSkyKey = "sky_atmosphere";

std::string CaptureShaderCode() {
    const std::string source(EngineShaders::GetSource(EngineShader::SkyCubemapCS));
    return std::regex_replace(source, std::regex(R"(//[^\r\n]*|/\*[\s\S]*?\*/)"), " ");
}

/// The declared field names of `cbuffer SkyCubemapParams : register(b0)`, in declaration order
/// (scalars only, as the shader declares them; a `float a, b, c;` row yields three names).
std::vector<std::string> CbufferFields(const std::string& code) {
    std::smatch block;
    if (!std::regex_search(code, block, std::regex(
            R"(cbuffer\s+SkyCubemapParams\s*:\s*register\s*\(\s*b0\s*\)\s*\{([^}]*)\})"))) {
        return {};
    }
    const std::string body = block[1];
    std::vector<std::string> names;
    const std::regex decl(R"((float|uint|int)\s+([^;]+);)");
    const std::regex ident(R"([A-Za-z_]\w*)");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), decl); it != std::sregex_iterator(); ++it) {
        const std::string list = (*it)[2];
        for (auto n = std::sregex_iterator(list.begin(), list.end(), ident); n != std::sregex_iterator(); ++n) {
            names.push_back(n->str());
        }
    }
    return names;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// The shipped capture shader (contract on the embedded source)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyCubemapGroundAlbedoShader, CbufferMatchesTheRootConstantsAndEndsWithTheGroundAlbedo) {
    const std::vector<std::string> fields = CbufferFields(CaptureShaderCode());
    const std::vector<std::string> expected = {
        "bottomRadiusKm", "topRadiusKm", "sunDirX", "sunDirY", "sunDirZ",
        "sunIllumR", "sunIllumG", "sunIllumB", "transmittanceLutIdx", "packedScatterScales",
        "scatteringLut3DIdx", "muSMin", "groundAlbedoR", "groundAlbedoG", "groundAlbedoB"};
    EXPECT_EQ(fields, expected);
    ASSERT_EQ(fields.size(), kSkyCubemapCaptureRootDwords);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, groundAlbedoR) / sizeof(uint32_t), 12u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, groundAlbedoG) / sizeof(uint32_t), 13u);
    EXPECT_EQ(offsetof(SkyCubemapCaptureRootConstants, groundAlbedoB) / sizeof(uint32_t), 14u);
    EXPECT_EQ(fields[12], "groundAlbedoR");
    EXPECT_EQ(fields[13], "groundAlbedoG");
    EXPECT_EQ(fields[14], "groundAlbedoB");
}

TEST(SkyCubemapGroundAlbedoShader, GroundTermReadsTheRootConstantsAndNoConstantIsLeft) {
    const std::string code = CaptureShaderCode();
    // The planet the march hits: Lambertian with the component's albedo (direct sun x transmittance).
    // kGround = albedo / PI is rounded once as a `precise` value (fl(0.7 * fl(1 / PI)) is exactly the
    // constant the pre-2026-11-11 shader folded) and the product is nk = NdotL * kGround, then
    // nk * sunIllum * gSunT. This shape is what makes the default capture bit-identical to that build
    // (0 of 524,288 env values differ; the plain `albedo * (1.0/PI) * NdotL * ...` chain and a max()
    // clamp around nk each moved texels by one f16 ulp), so it is pinned.
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(precise\s+float3\s+kGround\s*=\s*float3\s*\(\s*groundAlbedoR\s*,\s*groundAlbedoG\s*,\s*groundAlbedoB\s*\)\s*\*\s*\(\s*1\.0\s*/\s*PI\s*\)\s*;)")));
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(float3\s+nk\s*=\s*NdotL\s*\*\s*kGround\s*;)")));
    EXPECT_TRUE(std::regex_search(code, std::regex(
        R"(gRad\s*=\s*nk\s*\*\s*sunIllum\s*\*\s*gSunT\s*;)")));
    // The hardcoded 0.7 planet of the pre-2026-11-11 capture is gone.
    EXPECT_EQ(code.find("GROUND_ALBEDO"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// The component: the default and the scene key
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyGroundAlbedo, ComponentDefaultIsTheFormerHardcodedPlanet) {
    // 0.7 = the constant sky_cubemap_cs.hlsl carried until 2026-11-11: the default render must not move.
    const SkyAtmosphereComponent c{};
    EXPECT_FLOAT_EQ(c.groundAlbedo.x, 0.7f);
    EXPECT_FLOAT_EQ(c.groundAlbedo.y, 0.7f);
    EXPECT_FLOAT_EQ(c.groundAlbedo.z, 0.7f);
}

TEST(SkyGroundAlbedo, SceneKeyReachesTheComponent) {
    World world;
    const Entity e = world.CreateEntity();
    const nlohmann::json j = {{"ground_albedo", {0.1, 0.12, 0.08}}};
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSkyKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<SkyAtmosphereComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->groundAlbedo.x, 0.1f);
    EXPECT_FLOAT_EQ(c->groundAlbedo.y, 0.12f);
    EXPECT_FLOAT_EQ(c->groundAlbedo.z, 0.08f);
}

TEST(SkyGroundAlbedo, MissingSceneKeyKeepsTheDefault) {
    World world;
    const Entity e = world.CreateEntity();
    const nlohmann::json j = nlohmann::json::object();
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSkyKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<SkyAtmosphereComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    EXPECT_FLOAT_EQ(c->groundAlbedo.x, 0.7f);
    EXPECT_FLOAT_EQ(c->groundAlbedo.y, 0.7f);
    EXPECT_FLOAT_EQ(c->groundAlbedo.z, 0.7f);
}

// ─────────────────────────────────────────────────────────────────────────────
// The bake signature: sun direction (3) + sky sun colour (3) + angular radius (1) + ground albedo (3)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SkyGroundAlbedo, BakeSignatureCountsTheGroundAlbedo) {
    EXPECT_EQ(kSkyBakeSignatureValues, 10);
}
