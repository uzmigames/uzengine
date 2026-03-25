/// Unit tests for CollisionModule.
///
/// Covers: default config values, ParseConfig from valid JSON, ParseConfig with
/// missing/invalid fields, GetShaderCode returns non-empty HLSL, GetName returns
/// "collision", CollisionResponse enum string conversions (all 3 values + unknown
/// + case-insensitive FromString), config validation (bounceFactor clamped to
/// [0,1], minBounceSpeed and collisionOffset clamped to >= 0), default constructor
/// usable for ModuleRegistry, GetParameters returns 5 entries, Execute is a no-op.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/collision_module.h"

#include <json.hpp>
#include <string>
#include <string_view>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a fully-specified valid JSON config object.
// ─────────────────────────────────────────────────────────────────────────────

/// Returns a fully-specified nlohmann::json object for CollisionModule config.
nlohmann::json MakeFullJson() {
    return nlohmann::json::parse(R"({
        "response": "bounce",
        "bounce_factor": 0.7,
        "depth_buffer_collision": false,
        "collision_offset": 0.05,
        "min_bounce_speed": 0.2
    })");
}

} // anonymous namespace

// =============================================================================
// Default config values
// =============================================================================

TEST(UZ_CollisionModule, DefaultConfig_Response_IsBounce) {
    CollisionModule m;
    EXPECT_EQ(m.GetConfig().response, CollisionResponse::Bounce);
}

TEST(UZ_CollisionModule, DefaultConfig_BounceFactor_IsPointSix) {
    CollisionModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().bounceFactor, 0.6f);
}

TEST(UZ_CollisionModule, DefaultConfig_DepthBufferCollision_IsTrue) {
    CollisionModule m;
    EXPECT_TRUE(m.GetConfig().depthBufferCollision);
}

TEST(UZ_CollisionModule, DefaultConfig_CollisionOffset_IsPointZeroOne) {
    CollisionModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().collisionOffset, 0.01f);
}

TEST(UZ_CollisionModule, DefaultConfig_MinBounceSpeed_IsPointOne) {
    CollisionModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().minBounceSpeed, 0.1f);
}

// =============================================================================
// GetName
// =============================================================================

TEST(UZ_CollisionModule, GetName_ReturnsCollision) {
    CollisionModule m;
    EXPECT_STREQ(m.GetName(), "collision");
}

// =============================================================================
// GetShaderCode — non-empty HLSL
// =============================================================================

TEST(UZ_CollisionModule, GetShaderCode_IsNonEmpty) {
    CollisionModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_FALSE(code.empty())
        << "CollisionModule must provide a non-empty HLSL snippet";
}

TEST(UZ_CollisionModule, GetShaderCode_ContainsCollisionKeyword) {
    CollisionModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbCollisionEnabled"), std::string_view::npos)
        << "HLSL snippet must reference cbCollisionEnabled";
}

TEST(UZ_CollisionModule, GetShaderCode_ContainsBounceResponse) {
    CollisionModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("cbCollisionResponse"), std::string_view::npos)
        << "HLSL snippet must reference cbCollisionResponse";
}

TEST(UZ_CollisionModule, GetShaderCode_ContainsDepthBufferSample) {
    CollisionModule m;
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("DepthBuffer"), std::string_view::npos)
        << "HLSL snippet must reference DepthBuffer texture";
}

// =============================================================================
// GetParameters — 5 entries covering all config fields
// =============================================================================

TEST(UZ_CollisionModule, GetParameters_ReturnsFiveEntries) {
    CollisionModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 5u)
        << "CollisionModule must expose exactly 5 parameters";
}

TEST(UZ_CollisionModule, GetParameters_FirstEntry_IsResponse) {
    CollisionModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "response");
    EXPECT_EQ(params[0].type, ParameterType::Int);
}

TEST(UZ_CollisionModule, GetParameters_SecondEntry_IsBounceFactor) {
    CollisionModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "bounce_factor");
    EXPECT_EQ(params[1].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 0.6f);
}

TEST(UZ_CollisionModule, GetParameters_ThirdEntry_IsDepthBufferCollision) {
    CollisionModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "depth_buffer_collision");
    EXPECT_EQ(params[2].type, ParameterType::Bool);
    EXPECT_TRUE(params[2].defaultBool);
}

TEST(UZ_CollisionModule, GetParameters_FourthEntry_IsCollisionOffset) {
    CollisionModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "collision_offset");
    EXPECT_EQ(params[3].type, ParameterType::Float);
}

TEST(UZ_CollisionModule, GetParameters_FifthEntry_IsMinBounceSpeed) {
    CollisionModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_STREQ(params[4].name, "min_bounce_speed");
    EXPECT_EQ(params[4].type, ParameterType::Float);
}

// =============================================================================
// Execute — no-op (must not crash, return nothing)
// =============================================================================

TEST(UZ_CollisionModule, Execute_IsNoOp_DoesNotCrash) {
    CollisionModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 100;
    ctx.dt = 0.016f;
    // Must complete without throwing or crashing.
    m.Execute(ctx);
    // The module is GPU-only; no observable CPU state change to assert.
    EXPECT_EQ(m.GetConfig().response, CollisionResponse::Bounce);
}

// =============================================================================
// CollisionResponseToString — all 3 values + unknown
// =============================================================================

TEST(UZ_CollisionResponseToString, Kill_ReturnsKill) {
    EXPECT_STREQ(CollisionResponseToString(CollisionResponse::Kill), "kill");
}

TEST(UZ_CollisionResponseToString, Bounce_ReturnsBounce) {
    EXPECT_STREQ(CollisionResponseToString(CollisionResponse::Bounce), "bounce");
}

TEST(UZ_CollisionResponseToString, Slide_ReturnsSlide) {
    EXPECT_STREQ(CollisionResponseToString(CollisionResponse::Slide), "slide");
}

TEST(UZ_CollisionResponseToString, UnknownValue_ReturnsUnknown) {
    const auto unknown = static_cast<CollisionResponse>(255u);
    EXPECT_STREQ(CollisionResponseToString(unknown), "unknown");
}

// =============================================================================
// CollisionResponseFromString — all 3 values, unknown fallback, case-insensitive
// =============================================================================

TEST(UZ_CollisionResponseFromString, Kill_ParsedCorrectly) {
    EXPECT_EQ(CollisionResponseFromString("kill"), CollisionResponse::Kill);
}

TEST(UZ_CollisionResponseFromString, Bounce_ParsedCorrectly) {
    EXPECT_EQ(CollisionResponseFromString("bounce"), CollisionResponse::Bounce);
}

TEST(UZ_CollisionResponseFromString, Slide_ParsedCorrectly) {
    EXPECT_EQ(CollisionResponseFromString("slide"), CollisionResponse::Slide);
}

TEST(UZ_CollisionResponseFromString, Kill_Uppercase_ParsedCorrectly) {
    EXPECT_EQ(CollisionResponseFromString("KILL"), CollisionResponse::Kill);
}

TEST(UZ_CollisionResponseFromString, Bounce_MixedCase_ParsedCorrectly) {
    EXPECT_EQ(CollisionResponseFromString("Bounce"), CollisionResponse::Bounce);
}

TEST(UZ_CollisionResponseFromString, Slide_AllCaps_ParsedCorrectly) {
    EXPECT_EQ(CollisionResponseFromString("SLIDE"), CollisionResponse::Slide);
}

TEST(UZ_CollisionResponseFromString, UnknownString_DefaultsToBounce) {
    EXPECT_EQ(CollisionResponseFromString("explode"), CollisionResponse::Bounce)
        << "Unknown response string must fall back to Bounce";
}

TEST(UZ_CollisionResponseFromString, EmptyString_DefaultsToBounce) {
    EXPECT_EQ(CollisionResponseFromString(""), CollisionResponse::Bounce)
        << "Empty string must fall back to Bounce";
}

// =============================================================================
// ParseConfig — valid JSON, all fields populated
// =============================================================================

TEST(UZ_CollisionModule_ParseConfig, ValidJson_ResponseIsBounce) {
    const auto cfg = CollisionModule::ParseConfig(MakeFullJson());
    EXPECT_EQ(cfg.response, CollisionResponse::Bounce);
}

TEST(UZ_CollisionModule_ParseConfig, ValidJson_BounceFactorIsPointSeven) {
    const auto cfg = CollisionModule::ParseConfig(MakeFullJson());
    EXPECT_NEAR(cfg.bounceFactor, 0.7f, 1e-5f);
}

TEST(UZ_CollisionModule_ParseConfig, ValidJson_DepthBufferCollisionIsFalse) {
    const auto cfg = CollisionModule::ParseConfig(MakeFullJson());
    EXPECT_FALSE(cfg.depthBufferCollision);
}

TEST(UZ_CollisionModule_ParseConfig, ValidJson_CollisionOffsetIsPointZeroFive) {
    const auto cfg = CollisionModule::ParseConfig(MakeFullJson());
    EXPECT_NEAR(cfg.collisionOffset, 0.05f, 1e-5f);
}

TEST(UZ_CollisionModule_ParseConfig, ValidJson_MinBounceSpeedIsPointTwo) {
    const auto cfg = CollisionModule::ParseConfig(MakeFullJson());
    EXPECT_NEAR(cfg.minBounceSpeed, 0.2f, 1e-5f);
}

TEST(UZ_CollisionModule_ParseConfig, ResponseKill_ParsedCorrectly) {
    const auto j = nlohmann::json::parse(R"({ "response": "kill" })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_EQ(cfg.response, CollisionResponse::Kill);
}

TEST(UZ_CollisionModule_ParseConfig, ResponseSlide_ParsedCorrectly) {
    const auto j = nlohmann::json::parse(R"({ "response": "slide" })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_EQ(cfg.response, CollisionResponse::Slide);
}

// =============================================================================
// ParseConfig — missing fields use defaults
// =============================================================================

TEST(UZ_CollisionModule_ParseConfig, EmptyObject_UsesAllDefaults) {
    const auto cfg = CollisionModule::ParseConfig(nlohmann::json::object());
    EXPECT_EQ(cfg.response, CollisionResponse::Bounce);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 0.6f);
    EXPECT_TRUE(cfg.depthBufferCollision);
    EXPECT_FLOAT_EQ(cfg.collisionOffset, 0.01f);
    EXPECT_FLOAT_EQ(cfg.minBounceSpeed, 0.1f);
}

TEST(UZ_CollisionModule_ParseConfig, MissingBounceFactor_UsesDefault) {
    const auto j = nlohmann::json::parse(R"({ "response": "slide" })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 0.6f)
        << "Missing bounce_factor must use default 0.6";
}

// =============================================================================
// ParseConfig — invalid/non-object input returns defaults
// =============================================================================

TEST(UZ_CollisionModule_ParseConfig, NullInput_ReturnsDefaults) {
    const auto cfg = CollisionModule::ParseConfig(nlohmann::json(nullptr));
    EXPECT_EQ(cfg.response, CollisionResponse::Bounce);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 0.6f);
}

TEST(UZ_CollisionModule_ParseConfig, ArrayInput_ReturnsDefaults) {
    const auto cfg = CollisionModule::ParseConfig(nlohmann::json::array());
    EXPECT_EQ(cfg.response, CollisionResponse::Bounce);
}

TEST(UZ_CollisionModule_ParseConfig, NumberInput_ReturnsDefaults) {
    const auto cfg = CollisionModule::ParseConfig(nlohmann::json(42));
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 0.6f);
}

// =============================================================================
// ParseConfig — bounceFactor clamped to [0, 1]
// =============================================================================

TEST(UZ_CollisionModule_ParseConfig, BounceFactorAboveOne_ClampedToOne) {
    const auto j = nlohmann::json::parse(R"({ "bounce_factor": 2.5 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 1.0f)
        << "bounce_factor > 1.0 must be clamped to 1.0";
}

TEST(UZ_CollisionModule_ParseConfig, BounceFactorBelowZero_ClampedToZero) {
    const auto j = nlohmann::json::parse(R"({ "bounce_factor": -0.5 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 0.0f)
        << "bounce_factor < 0.0 must be clamped to 0.0";
}

TEST(UZ_CollisionModule_ParseConfig, BounceFactorZero_IsAccepted) {
    const auto j = nlohmann::json::parse(R"({ "bounce_factor": 0.0 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 0.0f);
}

TEST(UZ_CollisionModule_ParseConfig, BounceFactorOne_IsAccepted) {
    const auto j = nlohmann::json::parse(R"({ "bounce_factor": 1.0 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.bounceFactor, 1.0f);
}

// =============================================================================
// ParseConfig — minBounceSpeed clamped to >= 0
// =============================================================================

TEST(UZ_CollisionModule_ParseConfig, MinBounceSpeedNegative_ClampedToZero) {
    const auto j = nlohmann::json::parse(R"({ "min_bounce_speed": -1.0 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.minBounceSpeed, 0.0f)
        << "min_bounce_speed < 0 must be clamped to 0";
}

TEST(UZ_CollisionModule_ParseConfig, MinBounceSpeedZero_IsAccepted) {
    const auto j = nlohmann::json::parse(R"({ "min_bounce_speed": 0.0 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.minBounceSpeed, 0.0f);
}

TEST(UZ_CollisionModule_ParseConfig, MinBounceSpeedPositive_StoredCorrectly) {
    const auto j = nlohmann::json::parse(R"({ "min_bounce_speed": 0.5 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_NEAR(cfg.minBounceSpeed, 0.5f, 1e-5f);
}

// =============================================================================
// ParseConfig — collisionOffset clamped to >= 0
// =============================================================================

TEST(UZ_CollisionModule_ParseConfig, CollisionOffsetNegative_ClampedToZero) {
    const auto j = nlohmann::json::parse(R"({ "collision_offset": -0.5 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.collisionOffset, 0.0f)
        << "collision_offset < 0 must be clamped to 0";
}

TEST(UZ_CollisionModule_ParseConfig, CollisionOffsetZero_IsAccepted) {
    const auto j = nlohmann::json::parse(R"({ "collision_offset": 0.0 })");
    const auto cfg = CollisionModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.collisionOffset, 0.0f);
}

// =============================================================================
// Default constructor — usable as default construction for ModuleRegistry
// =============================================================================

TEST(UZ_CollisionModule, DefaultConstructor_ProducesValidModule) {
    // ModuleRegistry calls default constructor; verify it yields a valid module.
    CollisionModule m;
    EXPECT_STREQ(m.GetName(), "collision");
    EXPECT_FALSE(m.GetShaderCode().empty());
    EXPECT_EQ(m.GetParameters().size(), 5u);
}

TEST(UZ_CollisionModule, ExplicitConstructor_StoresConfig) {
    CollisionModuleConfig cfg;
    cfg.response      = CollisionResponse::Kill;
    cfg.bounceFactor  = 0.3f;
    cfg.collisionOffset = 0.05f;

    CollisionModule m(cfg);
    EXPECT_EQ(m.GetConfig().response, CollisionResponse::Kill);
    EXPECT_FLOAT_EQ(m.GetConfig().bounceFactor, 0.3f);
    EXPECT_FLOAT_EQ(m.GetConfig().collisionOffset, 0.05f);
}
