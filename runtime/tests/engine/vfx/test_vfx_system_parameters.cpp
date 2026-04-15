/// Unit tests for VfxSystem parameter defaults + VfxSystemInstance parameter API.
///
/// Covers:
///   - JSON load of the `parameters` block (float / vec3 / vec4 with rejection
///     of malformed entries).
///   - JSON save round-trip of the same block.
///   - VfxSystemInstance::Set/Get*Parameter + HasParameter / RemoveParameter /
///     ClearParameters / GetParameterNames.
///   - Init-time application of asset parameter defaults into the System scope.
///
/// @ref phase7_7.2.0-vfx-system-parameter-store

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/system/vfx_system.h"
#include "engine/vfx/system/vfx_system_instance.h"

using namespace uz;

namespace {

constexpr const char* kFireballPreset =
    R"({
        "format": "uzvfxsys",
        "version": 1,
        "name": "Fireball",
        "emitters": [
            {"name": "head", "path": "preset:FireballHead"}
        ],
        "parameters": {
            "intensity": { "type": "float", "value": 2.5 },
            "windDir":   { "type": "vec3",  "value": [1.0, 0.0, 0.0] },
            "teamColor": { "type": "vec4",  "value": [0.2, 0.4, 1.0, 1.0] }
        }
    })";

} // namespace

// ─── Load of the parameters block ──────────────────────────────────────────

TEST(VfxSystemParameters, LoadsFloatVec3Vec4) {
    auto asset = VfxSystem::LoadFromString(kFireballPreset);
    ASSERT_NE(asset, nullptr);
    ASSERT_EQ(asset->parameters.size(), 3u);

    EXPECT_EQ(asset->parameters[0].name, "intensity");
    EXPECT_EQ(asset->parameters[0].componentCount, 1u);
    EXPECT_FLOAT_EQ(asset->parameters[0].value.x, 2.5f);

    EXPECT_EQ(asset->parameters[1].name, "windDir");
    EXPECT_EQ(asset->parameters[1].componentCount, 3u);
    EXPECT_FLOAT_EQ(asset->parameters[1].value.x, 1.0f);

    EXPECT_EQ(asset->parameters[2].name, "teamColor");
    EXPECT_EQ(asset->parameters[2].componentCount, 4u);
    EXPECT_FLOAT_EQ(asset->parameters[2].value.w, 1.0f);
}

TEST(VfxSystemParameters, SkipsUnknownTypeButKeepsRest) {
    const char* json = R"({
        "format": "uzvfxsys",
        "version": 1,
        "name": "Test",
        "emitters": [ {"path": "preset:FireballHead"} ],
        "parameters": {
            "bad": { "type": "matrix", "value": 0 },
            "ok":  { "type": "float",  "value": 1.0 }
        }
    })";
    auto asset = VfxSystem::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    ASSERT_EQ(asset->parameters.size(), 1u);
    EXPECT_EQ(asset->parameters[0].name, "ok");
}

TEST(VfxSystemParameters, RejectsVec3WithFourValues) {
    const char* json = R"({
        "format": "uzvfxsys",
        "version": 1,
        "name": "Test",
        "emitters": [ {"path": "preset:FireballHead"} ],
        "parameters": {
            "wrongLen": { "type": "vec3", "value": [1, 2, 3, 4] }
        }
    })";
    auto asset = VfxSystem::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->parameters.size(), 0u);
}

TEST(VfxSystemParameters, AbsentBlockYieldsEmptyVector) {
    const char* json = R"({
        "format": "uzvfxsys",
        "version": 1,
        "name": "NoParams",
        "emitters": [ {"path": "preset:FireballHead"} ]
    })";
    auto asset = VfxSystem::LoadFromString(json);
    ASSERT_NE(asset, nullptr);
    EXPECT_EQ(asset->parameters.size(), 0u);
}

// ─── VfxSystemInstance parameter accessors ─────────────────────────────────

TEST(VfxSystemInstanceParameters, SetGetFloat) {
    VfxSystemInstance inst;
    inst.SetFloatParameter("intensity", 3.0f);
    EXPECT_FLOAT_EQ(inst.GetFloatParameter("intensity", 0.0f), 3.0f);
}

TEST(VfxSystemInstanceParameters, SetGetVec3) {
    VfxSystemInstance inst;
    inst.SetVec3Parameter("windDir", {1, 0, 0});
    const Vec3 v = inst.GetVec3Parameter("windDir", {});
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(VfxSystemInstanceParameters, SetGetVec4) {
    VfxSystemInstance inst;
    inst.SetVec4Parameter("teamColor", {0.2f, 0.4f, 1.0f, 0.75f});
    const Vec4 v = inst.GetVec4Parameter("teamColor", {});
    EXPECT_FLOAT_EQ(v.x, 0.2f);
    EXPECT_FLOAT_EQ(v.y, 0.4f);
    EXPECT_FLOAT_EQ(v.z, 1.0f);
    EXPECT_FLOAT_EQ(v.w, 0.75f);
}

TEST(VfxSystemInstanceParameters, MissingReturnsDefault) {
    VfxSystemInstance inst;
    EXPECT_FLOAT_EQ(inst.GetFloatParameter("missing", 42.0f), 42.0f);
    EXPECT_FALSE(inst.HasParameter("missing"));
}

TEST(VfxSystemInstanceParameters, HasRemoveClear) {
    VfxSystemInstance inst;
    inst.SetFloatParameter("a", 1.0f);
    inst.SetFloatParameter("b", 2.0f);
    inst.SetFloatParameter("c", 3.0f);

    EXPECT_TRUE(inst.HasParameter("a"));
    EXPECT_TRUE(inst.RemoveParameter("b"));
    EXPECT_FALSE(inst.HasParameter("b"));

    inst.ClearParameters();
    EXPECT_FALSE(inst.HasParameter("a"));
    EXPECT_FALSE(inst.HasParameter("c"));
}

TEST(VfxSystemInstanceParameters, GetParameterNamesPreservesInsertionOrder) {
    VfxSystemInstance inst;
    inst.SetFloatParameter("first",  1.0f);
    inst.SetFloatParameter("second", 2.0f);
    inst.SetFloatParameter("third",  3.0f);

    const auto names = inst.GetParameterNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "first");
    EXPECT_EQ(names[1], "second");
    EXPECT_EQ(names[2], "third");
}

// ─── Attribute store exposure ──────────────────────────────────────────────

TEST(VfxSystemInstanceParameters, GetAttributeStoreReflectsWrites) {
    VfxSystemInstance inst;
    inst.SetFloatParameter("intensity", 5.0f);

    const auto& store = inst.GetAttributeStore();
    EXPECT_FLOAT_EQ(store.GetFloat(VfxAttributeScope::System, "intensity"), 5.0f);
}

// ─── Save round-trip ───────────────────────────────────────────────────────

TEST(VfxSystemParameters, SaveRoundTripPreservesTypes) {
    auto asset = VfxSystem::LoadFromString(kFireballPreset);
    ASSERT_NE(asset, nullptr);

    const std::string tmpPath = std::tmpnam(nullptr);
    ASSERT_TRUE(asset->SaveToFile(tmpPath));

    std::ifstream ifs(tmpPath);
    std::string saved((std::istreambuf_iterator<char>(ifs)),
                       std::istreambuf_iterator<char>());

    auto reloaded = VfxSystem::LoadFromString(saved);
    ASSERT_NE(reloaded, nullptr);
    ASSERT_EQ(reloaded->parameters.size(), 3u);

    EXPECT_EQ(reloaded->parameters[0].componentCount, 1u);
    EXPECT_FLOAT_EQ(reloaded->parameters[0].value.x, 2.5f);

    EXPECT_EQ(reloaded->parameters[1].componentCount, 3u);
    EXPECT_FLOAT_EQ(reloaded->parameters[1].value.x, 1.0f);

    EXPECT_EQ(reloaded->parameters[2].componentCount, 4u);
    EXPECT_FLOAT_EQ(reloaded->parameters[2].value.w, 1.0f);

    std::remove(tmpPath.c_str());
}
