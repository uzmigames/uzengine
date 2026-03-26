/// Unit tests for LightRenderModule.
///
/// Covers: default config values, ParseConfig from valid JSON, ParseConfig with
/// invalid radius/intensity/maxLights (wrong type, out-of-range), GetShaderCode
/// returns empty (no compute contribution), GetName returns "light_render",
/// GetReadbackBufferSize calculation (maxLights × sizeof(LightParticleData)),
/// ProcessReadback with valid data, NaN/Inf positions (should skip), null buffer,
/// zero aliveCount, cap at maxLights, LightParticleData size is 32 bytes, and
/// GetParameters returns an empty vector.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/light_render_module.h"

#include <json.hpp>
#include <cmath>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a ValidLightParticleData entry with finite position/color.
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a LightParticleData entry with finite, non-degenerate values.
LightParticleData MakeValidEntry(float x, float y, float z) {
    LightParticleData d{};
    d.position = Vec3{x, y, z};
    d._pad     = 0.0f;
    d.color    = Vec4{1.0f, 0.5f, 0.0f, 1.0f};
    return d;
}

/// Returns a LightParticleData entry with a NaN x-position.
LightParticleData MakeNaNEntry() {
    LightParticleData d{};
    d.position = Vec3{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
    d._pad     = 0.0f;
    d.color    = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    return d;
}

/// Returns a LightParticleData entry with an Inf y-position.
LightParticleData MakeInfEntry() {
    LightParticleData d{};
    d.position = Vec3{0.0f, std::numeric_limits<float>::infinity(), 0.0f};
    d._pad     = 0.0f;
    d.color    = Vec4{1.0f, 1.0f, 1.0f, 1.0f};
    return d;
}

} // anonymous namespace

// =============================================================================
// LightParticleData — size assertion (32 bytes, verified at test time too)
// =============================================================================

TEST(UZ_LightParticleData, SizeIs32Bytes) {
    EXPECT_EQ(sizeof(LightParticleData), 32u)
        << "LightParticleData must be exactly 32 bytes for GPU readback ABI";
}

TEST(UZ_LightParticleData, PositionOffset_IsZero) {
    LightParticleData d{};
    const auto* base = reinterpret_cast<const char*>(&d);
    const auto* pos  = reinterpret_cast<const char*>(&d.position);
    EXPECT_EQ(pos - base, 0)
        << "position must be at byte offset 0";
}

TEST(UZ_LightParticleData, ColorOffset_Is16Bytes) {
    LightParticleData d{};
    const auto* base  = reinterpret_cast<const char*>(&d);
    const auto* color = reinterpret_cast<const char*>(&d.color);
    EXPECT_EQ(color - base, 16)
        << "color must be at byte offset 16 (after 12-byte Vec3 + 4-byte pad)";
}

// =============================================================================
// Default config values
// =============================================================================

TEST(UZ_LightRenderModule, DefaultConfig_RadiusIsFive) {
    LightRenderModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().radius, 5.0f);
}

TEST(UZ_LightRenderModule, DefaultConfig_IntensityIsOne) {
    LightRenderModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().intensity, 1.0f);
}

TEST(UZ_LightRenderModule, DefaultConfig_CastShadowIsFalse) {
    LightRenderModule m;
    EXPECT_FALSE(m.GetConfig().castShadow);
}

TEST(UZ_LightRenderModule, DefaultConfig_MaxLightsIs64) {
    LightRenderModule m;
    EXPECT_EQ(m.GetConfig().maxLights, 64u);
}

// =============================================================================
// GetName
// =============================================================================

TEST(UZ_LightRenderModule, GetName_ReturnsLightRender) {
    LightRenderModule m;
    EXPECT_STREQ(m.GetName(), "light_render");
}

// =============================================================================
// GetShaderCode — empty (no compute contribution)
// =============================================================================

TEST(UZ_LightRenderModule, GetShaderCode_IsEmpty) {
    LightRenderModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_TRUE(code.empty())
        << "LightRenderModule must return empty HLSL (post-compute readback module)";
}

// =============================================================================
// GetParameters — empty vector
// =============================================================================

TEST(UZ_LightRenderModule, GetParameters_IsEmpty) {
    LightRenderModule m;
    const auto params = m.GetParameters();
    EXPECT_TRUE(params.empty())
        << "LightRenderModule exposes no parameters (configured via LightRenderModuleConfig)";
}

// =============================================================================
// Execute — no-op, must not crash
// =============================================================================

TEST(UZ_LightRenderModule, Execute_IsNoOp_DoesNotCrash) {
    LightRenderModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 50;
    ctx.dt = 0.016f;
    m.Execute(ctx);
    EXPECT_FLOAT_EQ(m.GetConfig().radius, 5.0f);
}

// =============================================================================
// GetReadbackBufferSize — maxLights × sizeof(LightParticleData)
// =============================================================================

TEST(UZ_LightRenderModule, GetReadbackBufferSize_DefaultConfig_Is64Times32) {
    LightRenderModule m;
    const uint32_t expected = 64u * static_cast<uint32_t>(sizeof(LightParticleData));
    EXPECT_EQ(m.GetReadbackBufferSize(), expected);
}

TEST(UZ_LightRenderModule, GetReadbackBufferSize_MaxLights1_Is32Bytes) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 1;
    LightRenderModule m(cfg);
    EXPECT_EQ(m.GetReadbackBufferSize(), 32u);
}

TEST(UZ_LightRenderModule, GetReadbackBufferSize_MaxLights256_Is8192Bytes) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 256;
    LightRenderModule m(cfg);
    EXPECT_EQ(m.GetReadbackBufferSize(), 256u * 32u);
}

TEST(UZ_LightRenderModule, GetReadbackBufferSize_IsMultipleOf32) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 10;
    LightRenderModule m(cfg);
    EXPECT_EQ(m.GetReadbackBufferSize() % 32u, 0u)
        << "Buffer size must be a multiple of 32 (sizeof LightParticleData)";
}

// =============================================================================
// ProcessReadback — null buffer
// =============================================================================

TEST(UZ_LightRenderModule_ProcessReadback, NullBuffer_ReturnsEmpty) {
    LightRenderModule m;
    const auto result = m.ProcessReadback(nullptr, 0u, 10u);
    EXPECT_TRUE(result.empty())
        << "Null readback buffer must yield an empty result vector";
}

TEST(UZ_LightRenderModule_ProcessReadback, NullBufferWithAlive_ReturnsEmpty) {
    LightRenderModule m;
    const auto result = m.ProcessReadback(nullptr, 2048u, 64u);
    EXPECT_TRUE(result.empty());
}

// =============================================================================
// ProcessReadback — zero aliveCount
// =============================================================================

TEST(UZ_LightRenderModule_ProcessReadback, ZeroAliveCount_ReturnsEmpty) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 4;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf(4, MakeValidEntry(1.0f, 2.0f, 3.0f));
    const auto result = m.ProcessReadback(
        buf.data(),
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData)),
        0u
    );
    EXPECT_TRUE(result.empty())
        << "Zero aliveCount must yield an empty result even with a valid buffer";
}

// =============================================================================
// ProcessReadback — valid data, all finite
// =============================================================================

TEST(UZ_LightRenderModule_ProcessReadback, ValidEntries_AllReturned) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 4;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf = {
        MakeValidEntry(1.0f, 0.0f, 0.0f),
        MakeValidEntry(0.0f, 2.0f, 0.0f),
        MakeValidEntry(0.0f, 0.0f, 3.0f),
    };
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 3u);
    EXPECT_EQ(result.size(), 3u)
        << "All 3 valid entries must be returned";
}

TEST(UZ_LightRenderModule_ProcessReadback, ValidEntry_PositionIsPreserved) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 2;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf = {
        MakeValidEntry(10.0f, 20.0f, 30.0f),
    };
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 1u);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NEAR(result[0].position.x, 10.0f, 1e-5f);
    EXPECT_NEAR(result[0].position.y, 20.0f, 1e-5f);
    EXPECT_NEAR(result[0].position.z, 30.0f, 1e-5f);
}

TEST(UZ_LightRenderModule_ProcessReadback, ValidEntry_ColorIsPreserved) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 2;
    LightRenderModule m(cfg);

    LightParticleData entry = MakeValidEntry(1.0f, 1.0f, 1.0f);
    entry.color = Vec4{0.8f, 0.2f, 0.4f, 0.9f};

    const uint32_t bufBytes = static_cast<uint32_t>(sizeof(LightParticleData));
    const auto result = m.ProcessReadback(&entry, bufBytes, 1u);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_NEAR(result[0].color.x, 0.8f, 1e-5f);
    EXPECT_NEAR(result[0].color.y, 0.2f, 1e-5f);
    EXPECT_NEAR(result[0].color.z, 0.4f, 1e-5f);
    EXPECT_NEAR(result[0].color.w, 0.9f, 1e-5f);
}

// =============================================================================
// ProcessReadback — NaN / Inf positions skipped
// =============================================================================

TEST(UZ_LightRenderModule_ProcessReadback, NaNPosition_EntryIsSkipped) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 4;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf = {
        MakeNaNEntry(),
        MakeValidEntry(5.0f, 5.0f, 5.0f),
    };
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 2u);
    ASSERT_EQ(result.size(), 1u)
        << "NaN entry must be skipped; only 1 valid entry should be returned";
    EXPECT_NEAR(result[0].position.x, 5.0f, 1e-5f);
}

TEST(UZ_LightRenderModule_ProcessReadback, InfPosition_EntryIsSkipped) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 4;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf = {
        MakeInfEntry(),
        MakeValidEntry(3.0f, 0.0f, 0.0f),
    };
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 2u);
    ASSERT_EQ(result.size(), 1u)
        << "Inf position entry must be skipped";
    EXPECT_NEAR(result[0].position.x, 3.0f, 1e-5f);
}

TEST(UZ_LightRenderModule_ProcessReadback, AllNaN_ReturnsEmpty) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 4;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf = {
        MakeNaNEntry(),
        MakeNaNEntry(),
        MakeNaNEntry(),
    };
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 3u);
    EXPECT_TRUE(result.empty())
        << "All NaN positions must all be skipped, yielding empty vector";
}

// =============================================================================
// ProcessReadback — caps at maxLights
// =============================================================================

TEST(UZ_LightRenderModule_ProcessReadback, AliveCountExceedsMaxLights_CappedAtMax) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 2;
    LightRenderModule m(cfg);

    // 5 valid entries in the buffer, but maxLights is 2.
    std::vector<LightParticleData> buf = {
        MakeValidEntry(1.0f, 0.0f, 0.0f),
        MakeValidEntry(2.0f, 0.0f, 0.0f),
        MakeValidEntry(3.0f, 0.0f, 0.0f),
        MakeValidEntry(4.0f, 0.0f, 0.0f),
        MakeValidEntry(5.0f, 0.0f, 0.0f),
    };
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 5u);
    EXPECT_EQ(result.size(), 2u)
        << "ProcessReadback must cap at maxLights even when more alive particles exist";
}

TEST(UZ_LightRenderModule_ProcessReadback, AliveCountLessThanMaxLights_ReturnsAliveCount) {
    LightRenderModuleConfig cfg;
    cfg.maxLights = 64;
    LightRenderModule m(cfg);

    std::vector<LightParticleData> buf(3, MakeValidEntry(1.0f, 1.0f, 1.0f));
    const uint32_t bufBytes =
        static_cast<uint32_t>(buf.size() * sizeof(LightParticleData));

    const auto result = m.ProcessReadback(buf.data(), bufBytes, 3u);
    EXPECT_EQ(result.size(), 3u);
}

// =============================================================================
// ParseConfig — valid JSON
// =============================================================================

TEST(UZ_LightRenderModule_ParseConfig, ValidJson_RadiusParsedCorrectly) {
    const auto j = nlohmann::json::parse(R"({ "radius": 8.0 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_NEAR(cfg.radius, 8.0f, 1e-5f);
}

TEST(UZ_LightRenderModule_ParseConfig, ValidJson_IntensityParsedCorrectly) {
    const auto j = nlohmann::json::parse(R"({ "intensity": 2.5 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_NEAR(cfg.intensity, 2.5f, 1e-5f);
}

TEST(UZ_LightRenderModule_ParseConfig, ValidJson_CastShadowTrue) {
    const auto j = nlohmann::json::parse(R"({ "cast_shadow": true })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_TRUE(cfg.castShadow);
}

TEST(UZ_LightRenderModule_ParseConfig, ValidJson_CastShadowFalse) {
    const auto j = nlohmann::json::parse(R"({ "cast_shadow": false })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FALSE(cfg.castShadow);
}

TEST(UZ_LightRenderModule_ParseConfig, ValidJson_MaxLightsParsedCorrectly) {
    const auto j = nlohmann::json::parse(R"({ "max_lights": 128 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.maxLights, 128u);
}

TEST(UZ_LightRenderModule_ParseConfig, EmptyObject_UsesAllDefaults) {
    const auto cfg = LightRenderModule::ParseConfig(nlohmann::json::object());
    EXPECT_FLOAT_EQ(cfg.radius, 5.0f);
    EXPECT_FLOAT_EQ(cfg.intensity, 1.0f);
    EXPECT_FALSE(cfg.castShadow);
    EXPECT_EQ(cfg.maxLights, 64u);
}

// =============================================================================
// ParseConfig — invalid radius (zero, negative, wrong type)
// =============================================================================

TEST(UZ_LightRenderModule_ParseConfig, RadiusZero_UsesDefault) {
    const auto j = nlohmann::json::parse(R"({ "radius": 0.0 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.radius, 5.0f)
        << "radius <= 0 must fall back to default 5.0";
}

TEST(UZ_LightRenderModule_ParseConfig, RadiusNegative_UsesDefault) {
    const auto j = nlohmann::json::parse(R"({ "radius": -3.0 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.radius, 5.0f)
        << "Negative radius must fall back to default 5.0";
}

TEST(UZ_LightRenderModule_ParseConfig, RadiusIsString_UsesDefault) {
    const auto j = nlohmann::json::parse(R"({ "radius": "big" })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.radius, 5.0f)
        << "Non-numeric radius must fall back to default";
}

// =============================================================================
// ParseConfig — invalid intensity (negative, wrong type)
// =============================================================================

TEST(UZ_LightRenderModule_ParseConfig, IntensityNegative_UsesDefault) {
    const auto j = nlohmann::json::parse(R"({ "intensity": -0.5 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.intensity, 1.0f)
        << "Negative intensity must fall back to default 1.0";
}

TEST(UZ_LightRenderModule_ParseConfig, IntensityZero_IsAccepted) {
    const auto j = nlohmann::json::parse(R"({ "intensity": 0.0 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.intensity, 0.0f)
        << "Zero intensity must be accepted";
}

TEST(UZ_LightRenderModule_ParseConfig, IntensityIsString_UsesDefault) {
    const auto j = nlohmann::json::parse(R"({ "intensity": "high" })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.intensity, 1.0f);
}

// =============================================================================
// ParseConfig — maxLights out of range (clamped to [1, 256])
// =============================================================================

TEST(UZ_LightRenderModule_ParseConfig, MaxLightsZero_ClampedToOne) {
    const auto j = nlohmann::json::parse(R"({ "max_lights": 0 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.maxLights, 1u)
        << "max_lights 0 must be clamped to minimum 1";
}

TEST(UZ_LightRenderModule_ParseConfig, MaxLightsNegative_ClampedToOne) {
    const auto j = nlohmann::json::parse(R"({ "max_lights": -10 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.maxLights, 1u)
        << "Negative max_lights must be clamped to minimum 1";
}

TEST(UZ_LightRenderModule_ParseConfig, MaxLightsAbove256_ClampedTo256) {
    const auto j = nlohmann::json::parse(R"({ "max_lights": 512 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.maxLights, 256u)
        << "max_lights > 256 must be clamped to maximum 256";
}

TEST(UZ_LightRenderModule_ParseConfig, MaxLights256_IsAccepted) {
    const auto j = nlohmann::json::parse(R"({ "max_lights": 256 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.maxLights, 256u);
}

TEST(UZ_LightRenderModule_ParseConfig, MaxLightsIsFloat_UsesDefault) {
    // max_lights must be an integer; a float value should use the default.
    const auto j = nlohmann::json::parse(R"({ "max_lights": 32.5 })");
    const auto cfg = LightRenderModule::ParseConfig(j);
    // Non-integer type falls back to default 64.
    EXPECT_EQ(cfg.maxLights, 64u)
        << "Float max_lights must fall back to default 64";
}
