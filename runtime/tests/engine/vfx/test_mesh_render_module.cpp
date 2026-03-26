/// Unit tests for MeshRenderModule.
///
/// Covers: default config values, ParseConfig from valid JSON, ParseConfig with
/// missing/invalid fields, GetShaderCode returns non-empty HLSL, GetName returns
/// "mesh_render", MeshRotationMode enum string conversions (None/VelocityAligned/
/// Random + unknown), different HLSL snippets per rotation mode, default
/// constructor usable for ModuleRegistry, GetParameters returns 5 entries, and
/// Execute is a no-op.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "modules/mesh_render_module.h"

#include <json.hpp>
#include <string_view>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a MeshRenderModule with a specific rotation mode.
// ─────────────────────────────────────────────────────────────────────────────

/// Builds a MeshRenderModule configured with the given rotation mode.
MeshRenderModule MakeModuleWithMode(MeshRotationMode mode) {
    MeshRenderModuleConfig cfg;
    cfg.meshPath      = "meshes/test.uzmesh";
    cfg.materialPath  = "materials/test.uzmat";
    cfg.rotationMode  = mode;
    return MeshRenderModule(cfg);
}

} // anonymous namespace

// =============================================================================
// Default config values
// =============================================================================

TEST(UZ_MeshRenderModule, DefaultConfig_MeshPath_IsEmpty) {
    MeshRenderModule m;
    EXPECT_TRUE(m.GetConfig().meshPath.empty())
        << "Default meshPath must be empty (not set until asset load)";
}

TEST(UZ_MeshRenderModule, DefaultConfig_MaterialPath_IsEmpty) {
    MeshRenderModule m;
    EXPECT_TRUE(m.GetConfig().materialPath.empty())
        << "Default materialPath must be empty";
}

TEST(UZ_MeshRenderModule, DefaultConfig_MeshScale_IsOneOneOne) {
    MeshRenderModule m;
    EXPECT_FLOAT_EQ(m.GetConfig().meshScale.x, 1.0f);
    EXPECT_FLOAT_EQ(m.GetConfig().meshScale.y, 1.0f);
    EXPECT_FLOAT_EQ(m.GetConfig().meshScale.z, 1.0f);
}

TEST(UZ_MeshRenderModule, DefaultConfig_CastShadow_IsFalse) {
    MeshRenderModule m;
    EXPECT_FALSE(m.GetConfig().castShadow);
}

TEST(UZ_MeshRenderModule, DefaultConfig_RotationMode_IsVelocityAligned) {
    MeshRenderModule m;
    EXPECT_EQ(m.GetConfig().rotationMode, MeshRotationMode::VelocityAligned);
}

// =============================================================================
// GetName
// =============================================================================

TEST(UZ_MeshRenderModule, GetName_ReturnsMeshRender) {
    MeshRenderModule m;
    EXPECT_STREQ(m.GetName(), "mesh_render");
}

// =============================================================================
// GetShaderCode — non-empty for all rotation modes
// =============================================================================

TEST(UZ_MeshRenderModule, GetShaderCode_VelocityAligned_IsNonEmpty) {
    const auto m = MakeModuleWithMode(MeshRotationMode::VelocityAligned);
    EXPECT_FALSE(m.GetShaderCode().empty())
        << "VelocityAligned mode must produce a non-empty HLSL snippet";
}

TEST(UZ_MeshRenderModule, GetShaderCode_None_IsNonEmpty) {
    const auto m = MakeModuleWithMode(MeshRotationMode::None);
    EXPECT_FALSE(m.GetShaderCode().empty())
        << "None rotation mode must produce a non-empty HLSL snippet";
}

TEST(UZ_MeshRenderModule, GetShaderCode_Random_IsNonEmpty) {
    const auto m = MakeModuleWithMode(MeshRotationMode::Random);
    EXPECT_FALSE(m.GetShaderCode().empty())
        << "Random rotation mode must produce a non-empty HLSL snippet";
}

// =============================================================================
// GetShaderCode — different HLSL per rotation mode
// =============================================================================

TEST(UZ_MeshRenderModule, GetShaderCode_VelocityAligned_DiffersFromNone) {
    const auto velocity = MakeModuleWithMode(MeshRotationMode::VelocityAligned);
    const auto none     = MakeModuleWithMode(MeshRotationMode::None);
    EXPECT_NE(velocity.GetShaderCode(), none.GetShaderCode())
        << "VelocityAligned and None modes must emit different HLSL";
}

TEST(UZ_MeshRenderModule, GetShaderCode_VelocityAligned_DiffersFromRandom) {
    const auto velocity = MakeModuleWithMode(MeshRotationMode::VelocityAligned);
    const auto random   = MakeModuleWithMode(MeshRotationMode::Random);
    EXPECT_NE(velocity.GetShaderCode(), random.GetShaderCode())
        << "VelocityAligned and Random modes must emit different HLSL";
}

TEST(UZ_MeshRenderModule, GetShaderCode_None_DiffersFromRandom) {
    const auto none   = MakeModuleWithMode(MeshRotationMode::None);
    const auto random = MakeModuleWithMode(MeshRotationMode::Random);
    EXPECT_NE(none.GetShaderCode(), random.GetShaderCode())
        << "None and Random modes must emit different HLSL";
}

TEST(UZ_MeshRenderModule, GetShaderCode_VelocityAligned_ContainsVelocityKeyword) {
    const auto m = MakeModuleWithMode(MeshRotationMode::VelocityAligned);
    const std::string_view code = m.GetShaderCode();
    EXPECT_NE(code.find("Velocities"), std::string_view::npos)
        << "VelocityAligned HLSL must reference Velocities attribute";
}

TEST(UZ_MeshRenderModule, GetShaderCode_Random_ContainsHashKeyword) {
    const auto m = MakeModuleWithMode(MeshRotationMode::Random);
    const std::string_view code = m.GetShaderCode();
    // The random mode uses a Wang hash algorithm with a seed variable.
    EXPECT_NE(code.find("seed"), std::string_view::npos)
        << "Random HLSL must reference seed variable for pseudo-random rotation";
}

TEST(UZ_MeshRenderModule, GetShaderCode_AllModes_ContainMeshInstanceTransforms) {
    for (const auto mode : {
        MeshRotationMode::None,
        MeshRotationMode::VelocityAligned,
        MeshRotationMode::Random
    }) {
        const auto m = MakeModuleWithMode(mode);
        const std::string_view code = m.GetShaderCode();
        EXPECT_NE(code.find("MeshInstanceTransforms"), std::string_view::npos)
            << "All rotation modes must write to MeshInstanceTransforms";
    }
}

// =============================================================================
// GetParameters — 5 entries
// =============================================================================

TEST(UZ_MeshRenderModule, GetParameters_ReturnsFiveEntries) {
    MeshRenderModule m;
    const auto params = m.GetParameters();
    EXPECT_EQ(params.size(), 5u)
        << "MeshRenderModule must expose exactly 5 parameters";
}

TEST(UZ_MeshRenderModule, GetParameters_ScaleX_IsFloat) {
    MeshRenderModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 1u);
    EXPECT_STREQ(params[0].name, "scale_x");
    EXPECT_EQ(params[0].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[0].defaultFloat, 1.0f);
}

TEST(UZ_MeshRenderModule, GetParameters_ScaleY_IsFloat) {
    MeshRenderModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 2u);
    EXPECT_STREQ(params[1].name, "scale_y");
    EXPECT_EQ(params[1].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[1].defaultFloat, 1.0f);
}

TEST(UZ_MeshRenderModule, GetParameters_ScaleZ_IsFloat) {
    MeshRenderModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 3u);
    EXPECT_STREQ(params[2].name, "scale_z");
    EXPECT_EQ(params[2].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[2].defaultFloat, 1.0f);
}

TEST(UZ_MeshRenderModule, GetParameters_CastShadow_IsBool) {
    MeshRenderModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 4u);
    EXPECT_STREQ(params[3].name, "cast_shadow");
    EXPECT_EQ(params[3].type, ParameterType::Bool);
    EXPECT_FALSE(params[3].defaultBool);
}

TEST(UZ_MeshRenderModule, GetParameters_RotationMode_IsInt) {
    MeshRenderModule m;
    const auto params = m.GetParameters();
    ASSERT_GE(params.size(), 5u);
    EXPECT_STREQ(params[4].name, "rotation_mode");
    EXPECT_EQ(params[4].type, ParameterType::Int);
}

// =============================================================================
// Execute — no-op, must not crash
// =============================================================================

TEST(UZ_MeshRenderModule, Execute_IsNoOp_DoesNotCrash) {
    MeshRenderModule m;
    ExecuteContext ctx{};
    ctx.numParticles = 200;
    ctx.dt = 0.016f;
    m.Execute(ctx);
    EXPECT_STREQ(m.GetName(), "mesh_render");
}

// =============================================================================
// MeshRotationModeToString — all 3 values + unknown
// =============================================================================

TEST(UZ_MeshRotationModeToString, None_ReturnsNone) {
    EXPECT_STREQ(MeshRotationModeToString(MeshRotationMode::None), "None");
}

TEST(UZ_MeshRotationModeToString, VelocityAligned_ReturnsVelocityAligned) {
    EXPECT_STREQ(
        MeshRotationModeToString(MeshRotationMode::VelocityAligned),
        "VelocityAligned"
    );
}

TEST(UZ_MeshRotationModeToString, Random_ReturnsRandom) {
    EXPECT_STREQ(MeshRotationModeToString(MeshRotationMode::Random), "Random");
}

TEST(UZ_MeshRotationModeToString, UnknownValue_ReturnsUnknown) {
    const auto unknown = static_cast<MeshRotationMode>(255u);
    EXPECT_STREQ(MeshRotationModeToString(unknown), "Unknown");
}

// =============================================================================
// MeshRotationModeFromString — case-sensitive, unknown fallback to None
// =============================================================================

TEST(UZ_MeshRotationModeFromString, None_ParsedCorrectly) {
    EXPECT_EQ(MeshRotationModeFromString("None"), MeshRotationMode::None);
}

TEST(UZ_MeshRotationModeFromString, VelocityAligned_ParsedCorrectly) {
    EXPECT_EQ(
        MeshRotationModeFromString("VelocityAligned"),
        MeshRotationMode::VelocityAligned
    );
}

TEST(UZ_MeshRotationModeFromString, Random_ParsedCorrectly) {
    EXPECT_EQ(MeshRotationModeFromString("Random"), MeshRotationMode::Random);
}

TEST(UZ_MeshRotationModeFromString, LowercaseNone_DefaultsToNone) {
    // Case-sensitive: "none" is not recognised, falls back to None.
    EXPECT_EQ(MeshRotationModeFromString("none"), MeshRotationMode::None)
        << "Unrecognised string must fall back to None";
}

TEST(UZ_MeshRotationModeFromString, UnknownString_DefaultsToNone) {
    EXPECT_EQ(MeshRotationModeFromString("orbit"), MeshRotationMode::None)
        << "Unknown rotation mode string must default to None";
}

TEST(UZ_MeshRotationModeFromString, EmptyString_DefaultsToNone) {
    EXPECT_EQ(MeshRotationModeFromString(""), MeshRotationMode::None)
        << "Empty string must default to None";
}

// =============================================================================
// ParseConfig — valid JSON
// =============================================================================

TEST(UZ_MeshRenderModule_ParseConfig, ValidJson_MeshPathParsed) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "meshes/debris.uzmesh",
        "material_path": "materials/debris.uzmat"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.meshPath, "meshes/debris.uzmesh");
}

TEST(UZ_MeshRenderModule_ParseConfig, ValidJson_MaterialPathParsed) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "meshes/leaf.uzmesh",
        "material_path": "materials/leaf.uzmat"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.materialPath, "materials/leaf.uzmat");
}

TEST(UZ_MeshRenderModule_ParseConfig, ValidJson_MeshScaleParsed) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "mesh_scale": [2.0, 3.0, 0.5]
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_NEAR(cfg.meshScale.x, 2.0f, 1e-5f);
    EXPECT_NEAR(cfg.meshScale.y, 3.0f, 1e-5f);
    EXPECT_NEAR(cfg.meshScale.z, 0.5f, 1e-5f);
}

TEST(UZ_MeshRenderModule_ParseConfig, ValidJson_CastShadowTrue) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "cast_shadow": true
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_TRUE(cfg.castShadow);
}

TEST(UZ_MeshRenderModule_ParseConfig, ValidJson_RotationModeNone) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "rotation_mode": "None"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::None);
}

TEST(UZ_MeshRenderModule_ParseConfig, ValidJson_RotationModeRandom) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "rotation_mode": "Random"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::Random);
}

// =============================================================================
// ParseConfig — missing fields use defaults
// =============================================================================

TEST(UZ_MeshRenderModule_ParseConfig, EmptyObject_UsesAllDefaults) {
    const auto cfg = MeshRenderModule::ParseConfig(nlohmann::json::object());
    EXPECT_TRUE(cfg.meshPath.empty());
    EXPECT_TRUE(cfg.materialPath.empty());
    EXPECT_FLOAT_EQ(cfg.meshScale.x, 1.0f);
    EXPECT_FLOAT_EQ(cfg.meshScale.y, 1.0f);
    EXPECT_FLOAT_EQ(cfg.meshScale.z, 1.0f);
    EXPECT_FALSE(cfg.castShadow);
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::VelocityAligned);
}

TEST(UZ_MeshRenderModule_ParseConfig, MeshScaleWithTwoElements_IgnoresScale) {
    // mesh_scale must have at least 3 elements; a 2-element array is not applied.
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "mesh_scale": [2.0, 3.0]
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    // Default scale is preserved when the array is too short.
    EXPECT_FLOAT_EQ(cfg.meshScale.x, 1.0f);
    EXPECT_FLOAT_EQ(cfg.meshScale.y, 1.0f);
    EXPECT_FLOAT_EQ(cfg.meshScale.z, 1.0f);
}

TEST(UZ_MeshRenderModule_ParseConfig, MeshScaleNotArray_IgnoresScale) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "mesh_scale": "uniform"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_FLOAT_EQ(cfg.meshScale.x, 1.0f);
}

TEST(UZ_MeshRenderModule_ParseConfig, UnknownRotationMode_DefaultsToNone) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "m.uzmesh",
        "material_path": "m.uzmat",
        "rotation_mode": "Tumble"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::None)
        << "Unknown rotation_mode string must default to None";
}

// =============================================================================
// Default constructor — usable for ModuleRegistry
// =============================================================================

TEST(UZ_MeshRenderModule, DefaultConstructor_ProducesValidModule) {
    MeshRenderModule m;
    EXPECT_STREQ(m.GetName(), "mesh_render");
    EXPECT_FALSE(m.GetShaderCode().empty());
    EXPECT_EQ(m.GetParameters().size(), 5u);
}

TEST(UZ_MeshRenderModule, ExplicitConstructor_StoresConfig) {
    MeshRenderModuleConfig cfg;
    cfg.meshPath     = "meshes/spark.uzmesh";
    cfg.materialPath = "materials/spark.uzmat";
    cfg.castShadow   = true;
    cfg.rotationMode = MeshRotationMode::Random;
    cfg.meshScale    = Vec3{2.0f, 2.0f, 2.0f};

    MeshRenderModule m(cfg);
    EXPECT_EQ(m.GetConfig().meshPath, "meshes/spark.uzmesh");
    EXPECT_EQ(m.GetConfig().materialPath, "materials/spark.uzmat");
    EXPECT_TRUE(m.GetConfig().castShadow);
    EXPECT_EQ(m.GetConfig().rotationMode, MeshRotationMode::Random);
    EXPECT_FLOAT_EQ(m.GetConfig().meshScale.x, 2.0f);
}
