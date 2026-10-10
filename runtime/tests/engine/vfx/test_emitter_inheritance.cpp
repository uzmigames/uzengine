/// Unit tests for VFX emitter inheritance (phase 8.3.0).
///
/// Validates the JSON-level inheritance resolver in
/// `engine/vfx/asset/vfx_emitter_inheritance.h`:
///   - simple parent → child merge,
///   - multi-level chains,
///   - module override actions (add / replace / remove),
///   - `config` deep-merge on `replace`,
///   - cycle rejection,
///   - chain-depth cap,
///   - parameter / event / tag union with child-wins-on-duplicate,
///   - no-parent pass-through strips leftover `action` fields.
///
/// All tests are CPU-only. Parent lookup is simulated via an in-memory map
/// so no disk or VFS is touched.
///
/// @ref docs/engine/25-vfx/20-emitter-inheritance.md

#include <gtest/gtest.h>

#include "engine/vfx/asset/vfx_emitter_inheritance.h"

#include <json.hpp>

#include <string>
#include <unordered_map>

using namespace uz;
using json = nlohmann::json;

namespace {

/// Builds a loader closure over an in-memory { path → JSON text } map.
VfxInheritanceLoader MakeLoader(const std::unordered_map<std::string, std::string>& files) {
    return [files](const std::string& path) -> std::string {
        auto it = files.find(path);
        return (it == files.end()) ? std::string{} : it->second;
    };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Pass-through (no parent)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritance, NoParent_ReturnsInputMinusAction) {
    const json child = {
        {"format", "uzvfx"},
        {"modules", json::array({
            { {"type", "Gravity"}, {"action", "add"}, {"config", { {"g", 9.8f} }} }
        })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader({}));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->contains("modules"));
    ASSERT_EQ((*result)["modules"].size(), 1u);
    // The `action` field must be stripped so ParseModules never sees it.
    EXPECT_FALSE((*result)["modules"][0].contains("action"));
    EXPECT_EQ((*result)["modules"][0]["type"].get<std::string>(), "Gravity");
}

// ─────────────────────────────────────────────────────────────────────────────
// Single-level merge
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritance, SingleLevel_AddAction_AppendsAfterParent) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({
            "format": "uzvfx",
            "modules": [ { "type": "Spawn" }, { "type": "Update" } ]
        })" }
    };

    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"modules", json::array({
            { {"type", "Render"}, {"action", "add"} }
        })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    ASSERT_TRUE(result->contains("modules"));
    const auto& mods = (*result)["modules"];
    ASSERT_EQ(mods.size(), 3u);
    EXPECT_EQ(mods[0]["type"].get<std::string>(), "Spawn");
    EXPECT_EQ(mods[1]["type"].get<std::string>(), "Update");
    EXPECT_EQ(mods[2]["type"].get<std::string>(), "Render");
    EXPECT_FALSE(result->contains("parent"));
}

TEST(VfxInheritance, ReplaceAction_DeepMergesConfig) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({
            "format": "uzvfx",
            "modules": [
                { "type": "Gravity", "config": { "g": 9.8, "axis": [0,0,-1] } }
            ]
        })" }
    };

    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"modules", json::array({
            { {"type", "Gravity"}, {"action", "replace"},
              {"config", { {"g", 19.6f} }} }
        })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    const auto& cfg = (*result)["modules"][0]["config"];
    EXPECT_FLOAT_EQ(cfg["g"].get<float>(), 19.6f);
    // Parent's axis key must survive because the child didn't overwrite it.
    ASSERT_TRUE(cfg.contains("axis"));
    EXPECT_EQ(cfg["axis"][2].get<int>(), -1);
}

TEST(VfxInheritance, ReplaceAction_NotFoundAppends) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({ "format": "uzvfx", "modules": [ { "type": "A" } ] })" }
    };

    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"modules", json::array({
            { {"type", "Missing"}, {"action", "replace"} }
        })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    ASSERT_EQ((*result)["modules"].size(), 2u);
    EXPECT_EQ((*result)["modules"][1]["type"].get<std::string>(), "Missing");
}

TEST(VfxInheritance, RemoveAction_DropsParentModule) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({
            "format": "uzvfx",
            "modules": [ { "type": "A" }, { "type": "B" }, { "type": "C" } ]
        })" }
    };

    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"modules", json::array({ { {"type", "B"}, {"action", "remove"} } })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    const auto& mods = (*result)["modules"];
    ASSERT_EQ(mods.size(), 2u);
    EXPECT_EQ(mods[0]["type"].get<std::string>(), "A");
    EXPECT_EQ(mods[1]["type"].get<std::string>(), "C");
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-level chains
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritance, ThreeLevelChain_AppliesOverridesInOrder) {
    std::unordered_map<std::string, std::string> files = {
        { "grand.uzvfx", R"({ "format": "uzvfx",
            "modules": [ { "type": "Spawn" } ] })" },
        { "parent.uzvfx", R"({ "format": "uzvfx", "parent": "grand.uzvfx",
            "modules": [ { "type": "Update", "action": "add" } ] })" }
    };

    const json child = {
        {"format", "uzvfx"},
        {"parent", "parent.uzvfx"},
        {"modules", json::array({ { {"type", "Render"} } })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    const auto& mods = (*result)["modules"];
    ASSERT_EQ(mods.size(), 3u);
    EXPECT_EQ(mods[0]["type"].get<std::string>(), "Spawn");
    EXPECT_EQ(mods[1]["type"].get<std::string>(), "Update");
    EXPECT_EQ(mods[2]["type"].get<std::string>(), "Render");
}

// ─────────────────────────────────────────────────────────────────────────────
// Cycle detection and depth limit
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritance, SelfReference_Rejected) {
    std::unordered_map<std::string, std::string> files = {
        { "a.uzvfx", R"({ "format": "uzvfx", "parent": "a.uzvfx" })" }
    };
    const json child = { {"format", "uzvfx"}, {"parent", "a.uzvfx"} };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    EXPECT_FALSE(result.has_value());
}

TEST(VfxInheritance, MutualCycle_Rejected) {
    std::unordered_map<std::string, std::string> files = {
        { "a.uzvfx", R"({ "format": "uzvfx", "parent": "b.uzvfx" })" },
        { "b.uzvfx", R"({ "format": "uzvfx", "parent": "a.uzvfx" })" }
    };
    const json child = { {"format", "uzvfx"}, {"parent", "a.uzvfx"} };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    EXPECT_FALSE(result.has_value());
}

TEST(VfxInheritance, NormalizedPath_DetectsCycleAcrossCase) {
    // Same file referenced with mixed casing + slashes — Windows-style.
    std::unordered_map<std::string, std::string> files = {
        { "A/Foo.uzvfx", R"({ "format": "uzvfx", "parent": "a\\foo.uzvfx" })" }
    };
    // Loader resolves either spelling.
    auto loader = [files](const std::string& p) -> std::string {
        for (const auto& kv : files) {
            if (VfxNormalizeInheritancePath(kv.first) ==
                VfxNormalizeInheritancePath(p)) {
                return kv.second;
            }
        }
        return {};
    };

    const json child = { {"format", "uzvfx"}, {"parent", "A/Foo.uzvfx"} };
    auto result = VfxResolveInheritance(child, loader);
    EXPECT_FALSE(result.has_value());
}

TEST(VfxInheritance, DepthCap_Rejected) {
    // Build a 4-deep chain, cap at 2, expect rejection.
    std::unordered_map<std::string, std::string> files = {
        { "l3.uzvfx", R"({ "format": "uzvfx" })" },
        { "l2.uzvfx", R"({ "format": "uzvfx", "parent": "l3.uzvfx" })" },
        { "l1.uzvfx", R"({ "format": "uzvfx", "parent": "l2.uzvfx" })" }
    };
    const json child = { {"format", "uzvfx"}, {"parent", "l1.uzvfx"} };

    auto result = VfxResolveInheritance(child, MakeLoader(files), /*maxDepth=*/2u);
    EXPECT_FALSE(result.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// Emitter / parameter / event merge
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritance, EmitterBlock_ChildKeysOverrideParent) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({
            "format": "uzvfx",
            "emitter": { "max_particles": 1000, "lifespan_default": 5.0 }
        })" }
    };
    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"emitter", { {"max_particles", 9000} }}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ((*result)["emitter"]["max_particles"].get<int>(), 9000);
    EXPECT_FLOAT_EQ((*result)["emitter"]["lifespan_default"].get<float>(), 5.0f);
}

TEST(VfxInheritance, ParametersEvents_UnionByName_ChildWins) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({ "format": "uzvfx",
            "parameters": [ { "name": "intensity", "default": 1.0 } ],
            "events":     [ { "name": "burst",     "emission_count": 50 } ] })" }
    };
    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"parameters", json::array({ { {"name", "intensity"}, {"default", 5.0f} } })},
        {"events",     json::array({ { {"name", "impact"},    {"emission_count", 100} } })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    // intensity — child value wins
    ASSERT_EQ((*result)["parameters"].size(), 1u);
    EXPECT_FLOAT_EQ((*result)["parameters"][0]["default"].get<float>(), 5.0f);
    // events — union (parent burst + child impact)
    ASSERT_EQ((*result)["events"].size(), 2u);
}

TEST(VfxInheritance, Tags_UnionWithDedup) {
    std::unordered_map<std::string, std::string> files = {
        { "base.uzvfx", R"({ "format": "uzvfx", "tags": ["fire", "combat"] })" }
    };
    const json child = {
        {"format", "uzvfx"},
        {"parent", "base.uzvfx"},
        {"tags", json::array({ "combat", "hero" })}
    };

    auto result = VfxResolveInheritance(child, MakeLoader(files));
    ASSERT_TRUE(result.has_value());
    const auto& tags = (*result)["tags"];
    ASSERT_EQ(tags.size(), 3u);
    EXPECT_EQ(tags[0].get<std::string>(), "fire");
    EXPECT_EQ(tags[1].get<std::string>(), "combat");
    EXPECT_EQ(tags[2].get<std::string>(), "hero");
}
