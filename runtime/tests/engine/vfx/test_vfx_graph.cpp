/// Unit tests for VFXGraph asset loader.
///
/// Covers: valid .uzvfx JSON loading via LoadFromString, module instantiation
/// order, all builtin module types, parameter type parsing (float/vec3/vec4),
/// event parsing, emitter config parsing, error cases (malformed JSON, wrong
/// format field, missing modules array).
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "asset/vfx_graph.h"
#include "modules/module_registry.h"
#include "modules/register_builtin_modules.h"

#include <memory>
#include <string>

using namespace uz;

namespace {

/// Builds a ModuleRegistry with all builtin modules registered.
/// Reused across every test that loads a graph.
ModuleRegistry MakeRegistry() {
    ModuleRegistry reg;
    RegisterBuiltinModules(reg);
    return reg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Minimal valid JSON fixture used as a building block for tests
// ─────────────────────────────────────────────────────────────────────────────

constexpr const char* kMinimalValid = R"({
  "format": "uzvfx",
  "version": 1,
  "emitter": { "max_particles": 5000, "lifespan_default": 3.0 },
  "modules": [
    { "type": "spawn" },
    { "type": "update" },
    { "type": "decay" }
  ]
})";

} // anonymous namespace

// =============================================================================
// Load valid JSON — basic smoke test
// =============================================================================

/// A minimal valid .uzvfx JSON loads without error and returns a non-null graph.
TEST(VFXGraph, LoadFromString_MinimalValid_ReturnsNonNull) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMinimalValid, reg);
    ASSERT_NE(graph, nullptr)
        << "LoadFromString must return a valid graph for well-formed JSON";
}

/// Version field is parsed correctly from JSON.
TEST(VFXGraph, LoadFromString_ValidJson_VersionParsed) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMinimalValid, reg);
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->GetVersion(), 1u);
}

/// Source path is empty when loaded via LoadFromString (not from disk).
TEST(VFXGraph, LoadFromString_ValidJson_SourcePathEmpty) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMinimalValid, reg);
    ASSERT_NE(graph, nullptr);
    EXPECT_TRUE(graph->GetSourcePath().empty())
        << "LoadFromString must leave source path empty";
}

// =============================================================================
// Module instantiation — order and count
// =============================================================================

/// Three modules listed in JSON produce exactly three module instances.
TEST(VFXGraph, LoadFromString_ThreeModules_CorrectCount) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMinimalValid, reg);
    ASSERT_NE(graph, nullptr);
    EXPECT_EQ(graph->GetModules().size(), 3u);
}

/// Modules are instantiated in the order they appear in the JSON array.
TEST(VFXGraph, LoadFromString_ThreeModules_InstantiatedInOrder) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMinimalValid, reg);
    ASSERT_NE(graph, nullptr);

    const auto& mods = graph->GetModules();
    ASSERT_EQ(mods.size(), 3u);
    EXPECT_STREQ(mods[0]->GetName(), "spawn")  << "First module must be spawn";
    EXPECT_STREQ(mods[1]->GetName(), "update") << "Second module must be update";
    EXPECT_STREQ(mods[2]->GetName(), "decay")  << "Third module must be decay";
}

// =============================================================================
// All six builtin module types
// =============================================================================

/// All six builtin module type strings are recognized and produce non-null instances.
TEST(VFXGraph, LoadFromString_AllSixBuiltinModules_AllInstantiated) {
    constexpr const char* kAllModules = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [
        { "type": "spawn" },
        { "type": "update" },
        { "type": "decay" },
        { "type": "turbulence" },
        { "type": "constraint" },
        { "type": "sprite_render" }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kAllModules, reg);
    ASSERT_NE(graph, nullptr);

    const auto& mods = graph->GetModules();
    ASSERT_EQ(mods.size(), 6u);
    EXPECT_STREQ(mods[0]->GetName(), "spawn");
    EXPECT_STREQ(mods[1]->GetName(), "update");
    EXPECT_STREQ(mods[2]->GetName(), "decay");
    EXPECT_STREQ(mods[3]->GetName(), "turbulence");
    EXPECT_STREQ(mods[4]->GetName(), "constraint");
    EXPECT_STREQ(mods[5]->GetName(), "sprite_render");
}

/// An unknown module type string is skipped; the remaining known modules load.
TEST(VFXGraph, LoadFromString_UnknownModuleType_IsSkipped) {
    constexpr const char* kWithUnknown = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [
        { "type": "spawn" },
        { "type": "DOES_NOT_EXIST_xyz" },
        { "type": "decay" }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kWithUnknown, reg);
    ASSERT_NE(graph, nullptr)
        << "Graph with one unknown module type must still load";

    const auto& mods = graph->GetModules();
    ASSERT_EQ(mods.size(), 2u) << "Unknown module must be skipped, not crash";
    EXPECT_STREQ(mods[0]->GetName(), "spawn");
    EXPECT_STREQ(mods[1]->GetName(), "decay");
}

// =============================================================================
// Parameters section — float type with min/max
// =============================================================================

/// A float parameter is parsed with the correct name, type, default, min, max.
TEST(VFXGraph, LoadFromString_FloatParameter_ParsedCorrectly) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "intensity", "type": "float", "default": 2.5, "min": 0.0, "max": 10.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto& params = graph->GetParameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].name, "intensity");
    EXPECT_EQ(params[0].type, ParameterType::Float);
    EXPECT_FLOAT_EQ(params[0].defaultFloat, 2.5f);
    EXPECT_FLOAT_EQ(params[0].minValue, 0.0f);
    EXPECT_FLOAT_EQ(params[0].maxValue, 10.0f);
}

// =============================================================================
// Parameters section — vec3 with default_vec3
// =============================================================================

/// A vec3 parameter is parsed with the correct type and default_vec3 components.
TEST(VFXGraph, LoadFromString_Vec3Parameter_ParsedCorrectly) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        {
          "name": "wind_dir",
          "type": "vec3",
          "default_vec3": [1.0, 2.0, 3.0]
        }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto& params = graph->GetParameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].name, "wind_dir");
    EXPECT_EQ(params[0].type, ParameterType::Vec3);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.x, 1.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.y, 2.0f);
    EXPECT_FLOAT_EQ(params[0].defaultVec3.z, 3.0f);
}

// =============================================================================
// Parameters section — vec4 with default_vec4
// =============================================================================

/// A vec4 parameter is parsed with the correct type and default_vec4 components.
TEST(VFXGraph, LoadFromString_Vec4Parameter_ParsedCorrectly) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        {
          "name": "tint_color",
          "type": "vec4",
          "default_vec4": [0.1, 0.2, 0.3, 1.0]
        }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto& params = graph->GetParameters();
    ASSERT_EQ(params.size(), 1u);
    EXPECT_EQ(params[0].name, "tint_color");
    EXPECT_EQ(params[0].type, ParameterType::Vec4);
    EXPECT_FLOAT_EQ(params[0].defaultVec4.x, 0.1f);
    EXPECT_FLOAT_EQ(params[0].defaultVec4.y, 0.2f);
    EXPECT_FLOAT_EQ(params[0].defaultVec4.z, 0.3f);
    EXPECT_FLOAT_EQ(params[0].defaultVec4.w, 1.0f);
}

/// Multiple parameters with mixed types are all parsed in order.
TEST(VFXGraph, LoadFromString_MultipleParameters_AllParsedInOrder) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ],
      "parameters": [
        { "name": "alpha",   "type": "float", "default": 1.0, "min": 0.0, "max": 1.0 },
        { "name": "gravity", "type": "vec3",  "default_vec3": [0.0, 0.0, -981.0] },
        { "name": "color",   "type": "vec4",  "default_vec4": [1.0, 0.5, 0.0, 1.0] }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto& params = graph->GetParameters();
    ASSERT_EQ(params.size(), 3u);
    EXPECT_EQ(params[0].name, "alpha");
    EXPECT_EQ(params[0].type, ParameterType::Float);
    EXPECT_EQ(params[1].name, "gravity");
    EXPECT_EQ(params[1].type, ParameterType::Vec3);
    EXPECT_EQ(params[2].name, "color");
    EXPECT_EQ(params[2].type, ParameterType::Vec4);
}

// =============================================================================
// Events section — name, emission_count, velocity_scale
// =============================================================================

/// A single event is parsed with correct name, emission_count, and velocity_scale.
TEST(VFXGraph, LoadFromString_SingleEvent_ParsedCorrectly) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ],
      "events": [
        { "name": "burst", "emission_count": 200, "velocity_scale": 3.0 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto& events = graph->GetEvents();
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].name, "burst");
    EXPECT_EQ(events[0].emissionCount, 200u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 3.0f);
}

/// Multiple events are all parsed and stored in the order they appear.
TEST(VFXGraph, LoadFromString_MultipleEvents_AllParsedInOrder) {
    constexpr const char* kJson = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ],
      "events": [
        { "name": "explode",  "emission_count": 500, "velocity_scale": 5.0 },
        { "name": "spark",    "emission_count": 50,  "velocity_scale": 1.5 },
        { "name": "dissolve", "emission_count": 10,  "velocity_scale": 0.5 }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kJson, reg);
    ASSERT_NE(graph, nullptr);

    const auto& events = graph->GetEvents();
    ASSERT_EQ(events.size(), 3u);
    EXPECT_EQ(events[0].name, "explode");
    EXPECT_EQ(events[0].emissionCount, 500u);
    EXPECT_FLOAT_EQ(events[0].velocityScale, 5.0f);

    EXPECT_EQ(events[1].name, "spark");
    EXPECT_EQ(events[1].emissionCount, 50u);
    EXPECT_FLOAT_EQ(events[1].velocityScale, 1.5f);

    EXPECT_EQ(events[2].name, "dissolve");
    EXPECT_EQ(events[2].emissionCount, 10u);
    EXPECT_FLOAT_EQ(events[2].velocityScale, 0.5f);
}

// =============================================================================
// Emitter config parsing
// =============================================================================

/// max_particles and lifespan_default are parsed from the "emitter" section.
TEST(VFXGraph, LoadFromString_EmitterConfig_MaxParticlesAndLifespanParsed) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMinimalValid, reg);
    ASSERT_NE(graph, nullptr);

    const auto& cfg = graph->GetEmitterConfig();
    EXPECT_EQ(cfg.maxParticles, 5000u);
    EXPECT_FLOAT_EQ(cfg.lifespanDefault, 3.0f);
}

/// When "emitter" section is absent, VFXEmitterConfig defaults are used.
TEST(VFXGraph, LoadFromString_MissingEmitterSection_UsesDefaults) {
    constexpr const char* kNoEmitter = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kNoEmitter, reg);
    ASSERT_NE(graph, nullptr);

    const auto& cfg = graph->GetEmitterConfig();
    EXPECT_EQ(cfg.maxParticles, 10000u)     << "Default max_particles is 10000";
    EXPECT_FLOAT_EQ(cfg.lifespanDefault, 5.0f) << "Default lifespan_default is 5.0";
}

// =============================================================================
// Error paths — malformed / invalid JSON
// =============================================================================

/// Malformed JSON (not valid JSON syntax) returns nullptr.
TEST(VFXGraph, LoadFromString_MalformedJson_ReturnsNull) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString("{ not valid json !!!", reg);
    EXPECT_EQ(graph, nullptr)
        << "Malformed JSON must return nullptr";
}

/// An empty string is not valid JSON and must return nullptr.
TEST(VFXGraph, LoadFromString_EmptyString_ReturnsNull) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString("", reg);
    EXPECT_EQ(graph, nullptr);
}

/// JSON that is valid but is an array (not an object) must return nullptr.
TEST(VFXGraph, LoadFromString_JsonArray_ReturnsNull) {
    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString("[1, 2, 3]", reg);
    EXPECT_EQ(graph, nullptr);
}

// =============================================================================
// Error paths — wrong or missing format field
// =============================================================================

/// format field set to "uzvfx2" (wrong value) must return nullptr.
TEST(VFXGraph, LoadFromString_WrongFormatField_ReturnsNull) {
    constexpr const char* kWrongFormat = R"({
      "format": "uzvfx2",
      "version": 1,
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kWrongFormat, reg);
    EXPECT_EQ(graph, nullptr)
        << "format 'uzvfx2' is not 'uzvfx', must return nullptr";
}

/// format field completely absent must return nullptr.
TEST(VFXGraph, LoadFromString_MissingFormatField_ReturnsNull) {
    constexpr const char* kNoFormat = R"({
      "version": 1,
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kNoFormat, reg);
    EXPECT_EQ(graph, nullptr)
        << "Missing 'format' field must return nullptr";
}

/// format field is an integer, not a string — must return nullptr.
TEST(VFXGraph, LoadFromString_FormatFieldIsInteger_ReturnsNull) {
    constexpr const char* kBadFormat = R"({
      "format": 42,
      "version": 1,
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kBadFormat, reg);
    EXPECT_EQ(graph, nullptr);
}

// =============================================================================
// Missing or empty modules array
// =============================================================================

/// Missing "modules" key produces an empty module vector (graph is still returned).
TEST(VFXGraph, LoadFromString_MissingModulesKey_ProducesEmptyModuleVector) {
    constexpr const char* kNoModulesKey = R"({
      "format": "uzvfx",
      "version": 1
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kNoModulesKey, reg);
    ASSERT_NE(graph, nullptr)
        << "Missing modules array is not a parse error — graph should load";
    EXPECT_TRUE(graph->GetModules().empty())
        << "Absent modules key produces zero modules";
}

/// "modules" set to an empty JSON array produces an empty module vector.
TEST(VFXGraph, LoadFromString_EmptyModulesArray_ProducesEmptyModuleVector) {
    constexpr const char* kEmptyModules = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": []
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kEmptyModules, reg);
    ASSERT_NE(graph, nullptr);
    EXPECT_TRUE(graph->GetModules().empty());
}

/// A module entry with no "type" field is skipped without crashing.
TEST(VFXGraph, LoadFromString_ModuleEntryMissingTypeField_IsSkipped) {
    constexpr const char* kMissingType = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [
        { "config": { "rate": 100 } },
        { "type": "decay" }
      ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kMissingType, reg);
    ASSERT_NE(graph, nullptr);

    const auto& mods = graph->GetModules();
    ASSERT_EQ(mods.size(), 1u) << "Entry without 'type' field must be skipped";
    EXPECT_STREQ(mods[0]->GetName(), "decay");
}

// =============================================================================
// Missing parameters and events sections
// =============================================================================

/// Absent "parameters" key yields an empty parameters vector.
TEST(VFXGraph, LoadFromString_MissingParametersKey_EmptyParameters) {
    constexpr const char* kNoParams = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kNoParams, reg);
    ASSERT_NE(graph, nullptr);
    EXPECT_TRUE(graph->GetParameters().empty());
}

/// Absent "events" key yields an empty events vector.
TEST(VFXGraph, LoadFromString_MissingEventsKey_EmptyEvents) {
    constexpr const char* kNoEvents = R"({
      "format": "uzvfx",
      "version": 1,
      "modules": [ { "type": "spawn" } ]
    })";

    const auto reg = MakeRegistry();
    auto graph = VFXGraph::LoadFromString(kNoEvents, reg);
    ASSERT_NE(graph, nullptr);
    EXPECT_TRUE(graph->GetEvents().empty());
}
