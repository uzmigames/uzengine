/// Unit tests for `MeshSortMode` and the sort fields on `MeshRenderModule`.
///
/// Covers the data-model surface delivered by the
/// `phase16_vfx-mesh-particle-depth-sort` task: enum string conversions,
/// JSON round-trip via `ParseConfig`, default values, the `CustomAttribute
/// + empty name -> None` downgrade, and the parameter list.
///
/// The actual sort dispatch / runtime activation belongs to the follow-up
/// task `phase16_vfx-particle-sort-pipeline-refactor` and is intentionally
/// not exercised here.
///
/// No GPU hardware required — CPU-only.

#include <gtest/gtest.h>

#include "modules/mesh_render_module.h"

#include <json.hpp>
#include <cstring>
#include <string>

using namespace uz;

// =============================================================================
// MeshSortMode <-> string round-trip
// =============================================================================

TEST(UZ_MeshSortMode, ToString_None) {
    EXPECT_STREQ(MeshSortModeToString(MeshSortMode::None), "None");
}

TEST(UZ_MeshSortMode, ToString_ViewDepth) {
    EXPECT_STREQ(MeshSortModeToString(MeshSortMode::ViewDepth), "ViewDepth");
}

TEST(UZ_MeshSortMode, ToString_CustomAttribute) {
    EXPECT_STREQ(MeshSortModeToString(MeshSortMode::CustomAttribute),
                 "CustomAttribute");
}

TEST(UZ_MeshSortMode, ToString_Unknown_ReturnsLiteralUnknown) {
    // Cast a value outside the enum range to exercise the default branch.
    const auto invalid = static_cast<MeshSortMode>(0xFFu);
    EXPECT_STREQ(MeshSortModeToString(invalid), "Unknown");
}

TEST(UZ_MeshSortMode, FromString_None) {
    EXPECT_EQ(MeshSortModeFromString("None"), MeshSortMode::None);
}

TEST(UZ_MeshSortMode, FromString_ViewDepth) {
    EXPECT_EQ(MeshSortModeFromString("ViewDepth"), MeshSortMode::ViewDepth);
}

TEST(UZ_MeshSortMode, FromString_CustomAttribute) {
    EXPECT_EQ(MeshSortModeFromString("CustomAttribute"),
              MeshSortMode::CustomAttribute);
}

TEST(UZ_MeshSortMode, FromString_Unknown_DefaultsToNone) {
    EXPECT_EQ(MeshSortModeFromString("not-a-mode"), MeshSortMode::None);
}

TEST(UZ_MeshSortMode, FromString_CaseSensitive) {
    // The serializer writes canonical PascalCase; lowercase must NOT match
    // so authoring typos are caught at load time, not silently accepted.
    EXPECT_EQ(MeshSortModeFromString("viewdepth"), MeshSortMode::None);
}

// =============================================================================
// Default config values
// =============================================================================

TEST(UZ_MeshRenderModule_Sort, DefaultConfig_SortMode_IsNone) {
    MeshRenderModule m;
    EXPECT_EQ(m.GetConfig().sortMode, MeshSortMode::None)
        << "Default sortMode must be None to preserve current behaviour for "
           "emitters that do not opt into depth sorting";
}

TEST(UZ_MeshRenderModule_Sort, DefaultConfig_CustomSortAttribute_IsEmpty) {
    MeshRenderModule m;
    EXPECT_TRUE(m.GetConfig().customSortAttribute.empty());
}

// =============================================================================
// ParseConfig — JSON parsing
// =============================================================================

TEST(UZ_MeshRenderModule_Sort, ParseConfig_NoSortFields_DefaultsToNone) {
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "meshes/x.uzmesh"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.sortMode, MeshSortMode::None);
    EXPECT_TRUE(cfg.customSortAttribute.empty());
}

TEST(UZ_MeshRenderModule_Sort, ParseConfig_SortMode_ViewDepth) {
    const auto j = nlohmann::json::parse(R"({
        "sort_mode": "ViewDepth"
    })");
    EXPECT_EQ(MeshRenderModule::ParseConfig(j).sortMode,
              MeshSortMode::ViewDepth);
}

TEST(UZ_MeshRenderModule_Sort, ParseConfig_SortMode_CustomAttributeWithName) {
    const auto j = nlohmann::json::parse(R"({
        "sort_mode": "CustomAttribute",
        "custom_sort_attribute": "team"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.sortMode, MeshSortMode::CustomAttribute);
    EXPECT_EQ(cfg.customSortAttribute, "team");
}

TEST(UZ_MeshRenderModule_Sort,
     ParseConfig_CustomAttributeWithoutName_DowngradesToNone) {
    // Authoring a CustomAttribute sort without naming the attribute is a bug
    // — silently sorting by an unset key would mask it. The parser must
    // downgrade to None and the warning is logged at parse time.
    const auto j = nlohmann::json::parse(R"({
        "sort_mode": "CustomAttribute"
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.sortMode, MeshSortMode::None);
    EXPECT_TRUE(cfg.customSortAttribute.empty());
}

TEST(UZ_MeshRenderModule_Sort, ParseConfig_UnknownSortMode_DefaultsToNone) {
    const auto j = nlohmann::json::parse(R"({
        "sort_mode": "BogusMode"
    })");
    EXPECT_EQ(MeshRenderModule::ParseConfig(j).sortMode, MeshSortMode::None);
}

TEST(UZ_MeshRenderModule_Sort, ParseConfig_SortModeWrongType_DefaultsToNone) {
    const auto j = nlohmann::json::parse(R"({
        "sort_mode": 1
    })");
    // ParseConfig only consumes string-typed sort_mode entries; numeric
    // input is silently ignored, leaving the default in place.
    EXPECT_EQ(MeshRenderModule::ParseConfig(j).sortMode, MeshSortMode::None);
}

TEST(UZ_MeshRenderModule_Sort, ParseConfig_PreservesOtherFields) {
    // Sort fields must coexist cleanly with the existing rotation/scale
    // serialization — adding sort_mode must not stomp other config.
    const auto j = nlohmann::json::parse(R"({
        "mesh_path": "meshes/shard.uzmesh",
        "material_path": "materials/glass.uzmat",
        "rotation_mode": "Random",
        "sort_mode": "ViewDepth",
        "mesh_scale": [2.0, 3.0, 4.0],
        "cast_shadow": true
    })");
    const auto cfg = MeshRenderModule::ParseConfig(j);
    EXPECT_EQ(cfg.meshPath, "meshes/shard.uzmesh");
    EXPECT_EQ(cfg.materialPath, "materials/glass.uzmat");
    EXPECT_EQ(cfg.rotationMode, MeshRotationMode::Random);
    EXPECT_EQ(cfg.sortMode, MeshSortMode::ViewDepth);
    EXPECT_FLOAT_EQ(cfg.meshScale.x, 2.0f);
    EXPECT_FLOAT_EQ(cfg.meshScale.y, 3.0f);
    EXPECT_FLOAT_EQ(cfg.meshScale.z, 4.0f);
    EXPECT_TRUE(cfg.castShadow);
}

// =============================================================================
// GetParameters — editor / CB layout
// =============================================================================

TEST(UZ_MeshRenderModule_Sort, GetParameters_IncludesSortMode) {
    MeshRenderModuleConfig cfg;
    cfg.sortMode = MeshSortMode::ViewDepth;
    MeshRenderModule m(cfg);

    const auto params = m.GetParameters();
    bool found = false;
    int32_t actualOrdinal = -1;
    for (const auto& p : params) {
        // ParameterDef::name is const char*; compare contents, not pointer.
        if (p.name != nullptr && std::strcmp(p.name, "sort_mode") == 0) {
            found = true;
            actualOrdinal = p.defaultInt;
            EXPECT_EQ(p.type, ParameterType::Int)
                << "sort_mode must be exposed as Int (enum ordinal) so it "
                   "round-trips through the constant buffer alongside the "
                   "scale floats and rotation_mode";
            break;
        }
    }
    EXPECT_TRUE(found) << "Editor / CB code expects 'sort_mode' parameter";
    EXPECT_EQ(actualOrdinal, static_cast<int32_t>(MeshSortMode::ViewDepth));
}

TEST(UZ_MeshRenderModule_Sort, GetParameters_HasSixEntries) {
    // scale_x, scale_y, scale_z, cast_shadow, rotation_mode, sort_mode.
    // Pinning the count guards against silent drift when a future task
    // adds another parameter without updating the editor binding code.
    MeshRenderModule m;
    EXPECT_EQ(m.GetParameters().size(), 6u);
}
