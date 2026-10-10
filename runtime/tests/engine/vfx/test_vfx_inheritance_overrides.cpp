/// Unit tests for phase 11.11.0: inheritance override annotations on
/// resolver output + `RevertOverrideCommand` round-trip.
///
/// Covers:
///   - `VfxResolveInheritance` (runtime path) annotates every surviving
///     module with an `"override"` key:
///       * parent modules untouched by child → `"Inherited"`
///       * child `action: "add"` → `"OverrideAdd"`
///       * child `action: "replace"` → `"OverrideReplace"`
///   - Runtime resolver still drops `action: "remove"` modules.
///   - `VfxResolveInheritanceForEditor` keeps removed modules in the
///     list tagged `"OverrideRemove"` so the stack panel can render
///     them dimmed + strikethrough.
///   - Unchanged chain (no parent) round-trips without adding override
///     tags (backward-compat — unannotated emitters unchanged).
///   - `DeserializeEmitter` picks up the `override` JSON key (already
///     covered in phase 11.1 tests; repeated here as the full chain
///     check: resolver → serializer-round-trip → `VfxStack`).
///   - `RevertOverrideCommand` apply / revert swap a module between its
///     child-override payload and the parent's `Inherited` version.
///   - Parameter inspector `SetOverrideFlag` / `GetOverrideFlag` round-
///     trip; `None` entries erase.

#include <gtest/gtest.h>

#include "engine/vfx/asset/vfx_emitter_inheritance.h"
#include "engine/vfx/components/emitter_serializer.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/editor/undo/vfx_undo_commands.h"
#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/stack/vfx_module_instance.h"
#include "engine/vfx/stack/vfx_stack.h"

#include <json.hpp>

#include <string>

using namespace uz;
using json = nlohmann::json;

namespace {

/// A fixed parent JSON with three modules: Gravity + Drag + Sprite.
const char* kParentJson = R"({
    "format": "uzvfx",
    "version": 1,
    "name": "parent",
    "modules": [
        { "type": "Gravity", "config": { "strength": 9.8 } },
        { "type": "Drag",    "config": { "damping":  0.1 } },
        { "type": "Sprite",  "config": { "tint":    "white" } }
    ]
})";

VfxInheritanceLoader MakeFixedLoader() {
    return [](const std::string&) { return std::string(kParentJson); };
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Resolver annotations
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceOverrides, Runtime_TagsAddReplaceAndDropsRemove) {
    const json child = json::parse(R"({
        "parent": "parent.uzvfx",
        "name": "child",
        "modules": [
            { "type": "Gravity", "action": "replace", "config": { "strength": 3.0 } },
            { "type": "Burst",   "action": "add",     "config": { "count": 10 } },
            { "type": "Drag",    "action": "remove" }
        ]
    })");

    auto resolved = VfxResolveInheritance(child, MakeFixedLoader());
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE((*resolved).contains("modules"));
    const json& mods = (*resolved)["modules"];

    auto findByType = [&](const std::string& t) -> const json* {
        for (const auto& m : mods) {
            if (m.contains("type") && m["type"].get<std::string>() == t) return &m;
        }
        return nullptr;
    };

    // Gravity — replaced (OverrideReplace)
    const json* g = findByType("Gravity");
    ASSERT_NE(g, nullptr);
    EXPECT_EQ((*g)["override"].get<std::string>(), "OverrideReplace");
    EXPECT_FLOAT_EQ((*g)["config"]["strength"].get<float>(), 3.0f);

    // Sprite — inherited untouched
    const json* s = findByType("Sprite");
    ASSERT_NE(s, nullptr);
    EXPECT_EQ((*s)["override"].get<std::string>(), "Inherited");

    // Burst — child-only (OverrideAdd)
    const json* b = findByType("Burst");
    ASSERT_NE(b, nullptr);
    EXPECT_EQ((*b)["override"].get<std::string>(), "OverrideAdd");

    // Drag — removed → must be absent from runtime resolver output.
    EXPECT_EQ(findByType("Drag"), nullptr);
}

TEST(VfxInheritanceOverrides, Editor_KeepsRemovedTaggedAsOverrideRemove) {
    const json child = json::parse(R"({
        "parent": "parent.uzvfx",
        "modules": [
            { "type": "Drag", "action": "remove" }
        ]
    })");

    auto resolved = VfxResolveInheritanceForEditor(child, MakeFixedLoader());
    ASSERT_TRUE(resolved.has_value());
    const json& mods = (*resolved)["modules"];

    const json* drag = nullptr;
    for (const auto& m : mods) {
        if (m.contains("type") && m["type"].get<std::string>() == "Drag") {
            drag = &m; break;
        }
    }
    ASSERT_NE(drag, nullptr);
    EXPECT_EQ((*drag)["override"].get<std::string>(), "OverrideRemove");
}

TEST(VfxInheritanceOverrides, NoParent_LeavesModulesUntagged) {
    const json standalone = json::parse(R"({
        "format": "uzvfx",
        "version": 1,
        "modules": [ { "type": "Foo" } ]
    })");
    auto resolved = VfxResolveInheritance(standalone, MakeFixedLoader());
    ASSERT_TRUE(resolved.has_value());
    const json& mods = (*resolved)["modules"];
    ASSERT_EQ(mods.size(), 1u);
    EXPECT_FALSE(mods[0].contains("override"))
        << "Standalone assets must not grow override tags";
}

// ─────────────────────────────────────────────────────────────────────────────
// Serializer → VfxStack round-trip reads the override key
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceOverrides, Deserialize_PicksUpOverrideTagIntoVfxModuleInstance) {
    const std::string json = R"({
        "stages": [
            {
                "stage": "ParticleUpdate",
                "modules": [
                    { "type": "Gravity",
                      "instanceId": "g1",
                      "enabled": true,
                      "override": "OverrideReplace" }
                ]
            }
        ]
    })";
    ParticleEmitter e;
    ASSERT_TRUE(DeserializeEmitter(json, e));
    const auto& mods = e.stack.GetEntry(VfxStage::ParticleUpdate).modules;
    ASSERT_EQ(mods.size(), 1u);
    EXPECT_EQ(mods[0].overrideFlag, VfxOverrideFlag::OverrideReplace);
}

// ─────────────────────────────────────────────────────────────────────────────
// RevertOverrideCommand
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceOverrides, RevertCommand_SwapsChildOverrideForParent) {
    VfxStack stk;

    // Child override occupies the slot.
    VfxModuleInstance child;
    child.moduleTypeName = "Gravity";
    child.instanceId     = "g";
    child.configJson     = R"({"strength":3.0})";
    child.enabled        = true;
    child.overrideFlag   = VfxOverrideFlag::OverrideReplace;
    stk.GetEntry(VfxStage::ParticleUpdate).Add(child);

    // Parent's original module — what Revert re-inserts.
    VfxModuleInstance parent;
    parent.moduleTypeName = "Gravity";
    parent.instanceId     = "g";
    parent.configJson     = R"({"strength":9.8})";
    parent.enabled        = true;
    parent.overrideFlag   = VfxOverrideFlag::Inherited;

    RevertOverrideCommand cmd(&stk, "g", child, parent);
    cmd.Apply();
    const auto& after = stk.GetEntry(VfxStage::ParticleUpdate).modules[0];
    EXPECT_EQ(after.overrideFlag, VfxOverrideFlag::Inherited);
    EXPECT_EQ(after.configJson, R"({"strength":9.8})");

    cmd.Revert();
    const auto& restored = stk.GetEntry(VfxStage::ParticleUpdate).modules[0];
    EXPECT_EQ(restored.overrideFlag, VfxOverrideFlag::OverrideReplace);
    EXPECT_EQ(restored.configJson, R"({"strength":3.0})");
}

TEST(VfxInheritanceOverrides, RevertCommand_NoOpWhenInstanceMissing) {
    VfxStack stk;
    VfxModuleInstance child;
    child.moduleTypeName = "Gravity";
    child.instanceId     = "missing";
    child.overrideFlag   = VfxOverrideFlag::OverrideReplace;

    VfxModuleInstance parent = child;
    parent.overrideFlag = VfxOverrideFlag::Inherited;

    RevertOverrideCommand cmd(&stk, "missing", child, parent);
    cmd.Apply();   // nothing crashes
    cmd.Revert();  // ditto
    // Stack never had the instance — should still be empty.
    for (const auto& entry : stk.Entries()) {
        EXPECT_TRUE(entry.modules.empty());
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter inspector override flag map
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceOverrides, Parameters_SetGetClearOverrideFlag) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    EXPECT_EQ(w.GetOverrideFlag(VfxAttributeScope::Emitter, "intensity"),
              VfxOverrideFlag::None);

    w.SetOverrideFlag(VfxAttributeScope::Emitter, "intensity",
                        VfxOverrideFlag::OverrideReplace);
    EXPECT_EQ(w.GetOverrideFlag(VfxAttributeScope::Emitter, "intensity"),
              VfxOverrideFlag::OverrideReplace);

    // Setting to None erases the entry.
    w.SetOverrideFlag(VfxAttributeScope::Emitter, "intensity",
                        VfxOverrideFlag::None);
    EXPECT_EQ(w.GetOverrideFlag(VfxAttributeScope::Emitter, "intensity"),
              VfxOverrideFlag::None);

    w.SetOverrideFlag(VfxAttributeScope::System, "teamColor",
                        VfxOverrideFlag::OverrideAdd);
    w.ClearOverrideFlags();
    EXPECT_EQ(w.GetOverrideFlag(VfxAttributeScope::System, "teamColor"),
              VfxOverrideFlag::None);
}
