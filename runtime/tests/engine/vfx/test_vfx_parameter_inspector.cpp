/// Unit tests for phase 11.4.0: `VfxParameterInspectorWidget`.
///
/// Covers the pure-data portion of the widget:
///   - Add / remove / rename / retype / rescope of parameters against a
///     bound `VfxAttributeStore`.
///   - Add refuses empty names + duplicates; remove refuses when the
///     parameter is in use; rename refuses when the target exists.
///   - `ChangeType` preserves component 0 and zero-fills new components.
///   - `SetDefaultValue` writes through the stored type.
///   - `CountReferences` scans `@<name>` tokens across every module's
///     `configJson` on the bound stack.
///   - `RenameParameter` rewrites every `@oldName` occurrence to
///     `@newName` inside every module's configJson.
///   - `FindFirstUsage` returns the first referencing module in stage
///     / execution order.
///   - `ListParameters` returns a sorted snapshot with authored types.
///   - `MakeUniqueName` appends a numeric suffix on collision.
///   - Layout emits scope headers + add buttons + parameter rows; hit-
///     test precedence follows the enum ordering.
///   - Scope expand / collapse hides rows + add-button from the layout.
///   - Selection routes through the callback only on change.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/stack/vfx_stack.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

namespace {

VfxModuleInstance MakeMod(const char* type, const char* id,
                           const char* cfg = "") {
    VfxModuleInstance m;
    m.moduleTypeName = type;
    m.instanceId     = id;
    m.configJson     = cfg;
    return m;
}

VfxStack StackWithRefs() {
    VfxStack s;
    // ParticleUpdate: two modules, one references @teamColor, one doesn't.
    s.GetEntry(VfxStage::ParticleUpdate).Add(
        MakeMod("ColorOverLife", "colorlife",
                R"({"start":"@teamColor","end":"#ffffff"})"));
    s.GetEntry(VfxStage::ParticleUpdate).Add(
        MakeMod("Gravity", "grav",
                R"({"vector":[0,-9.8,0]})"));
    // Render: sprite references @teamColor as well.
    s.GetEntry(VfxStage::Render).Add(
        MakeMod("SpriteRenderer", "sprite",
                R"({"tint":"@teamColor","blend":"additive"})"));
    return s;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Bind + list
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, Unbound_ListIsEmpty) {
    VfxParameterInspectorWidget w;
    EXPECT_TRUE(w.ListParameters(VfxAttributeScope::Emitter).empty());
    EXPECT_TRUE(w.ComputeLayout({0,0,400,300}).empty());
}

TEST(VfxParameterInspectorWidget, ListParameters_ReturnsSortedWithTypes) {
    VfxAttributeStore store;
    VfxStack          stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "spread",
                                VfxParamType::Float,    Vec4{0.5f, 0, 0, 0}));
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "teamColor",
                                VfxParamType::Vec4,
                                Vec4{1.0f, 0.5f, 0.1f, 1.0f}));
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "wind",
                                VfxParamType::Vec3,
                                Vec4{10.0f, 0.0f, 0.0f, 0.0f}));

    const auto rows = w.ListParameters(VfxAttributeScope::Emitter);
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_EQ(rows[0].name, "spread");   // alphabetical
    EXPECT_EQ(rows[1].name, "teamColor");
    EXPECT_EQ(rows[2].name, "wind");
    EXPECT_EQ(rows[0].type, VfxParamType::Float);
    EXPECT_EQ(rows[1].type, VfxParamType::Vec4);
    EXPECT_EQ(rows[2].type, VfxParamType::Vec3);
    EXPECT_FLOAT_EQ(rows[0].value.x, 0.5f);
}

// ─────────────────────────────────────────────────────────────────────────────
// AddParameter
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, Add_EmptyNameFails) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    EXPECT_FALSE(w.AddParameter(VfxAttributeScope::Emitter, "",
                                  VfxParamType::Float, Vec4{}));
}

TEST(VfxParameterInspectorWidget, Add_DuplicateFails) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "intensity",
                                VfxParamType::Float, Vec4{2, 0, 0, 0}));
    EXPECT_FALSE(w.AddParameter(VfxAttributeScope::Emitter, "intensity",
                                  VfxParamType::Float, Vec4{3, 0, 0, 0}));
    // Value unchanged.
    EXPECT_FLOAT_EQ(
        store.GetFloat(VfxAttributeScope::Emitter, "intensity"), 2.0f);
}

TEST(VfxParameterInspectorWidget, Add_WritesCorrectStoreType) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::System, "teamColor",
                                VfxParamType::Vec4,
                                Vec4{0.2f, 0.4f, 0.6f, 1.0f}));
    const Vec4 v = store.GetVec4(VfxAttributeScope::System, "teamColor");
    EXPECT_FLOAT_EQ(v.x, 0.2f);
    EXPECT_FLOAT_EQ(v.w, 1.0f);
    EXPECT_EQ(w.GetType(VfxAttributeScope::System, "teamColor"),
              VfxParamType::Vec4);
}

// ─────────────────────────────────────────────────────────────────────────────
// MakeUniqueName
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, MakeUniqueName_AppendsSuffixOnCollision) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    EXPECT_EQ(w.MakeUniqueName(VfxAttributeScope::Emitter, "NewParam"),
              "NewParam");
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "NewParam",
                                VfxParamType::Float, Vec4{}));
    EXPECT_EQ(w.MakeUniqueName(VfxAttributeScope::Emitter, "NewParam"),
              "NewParam1");
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "NewParam1",
                                VfxParamType::Float, Vec4{}));
    EXPECT_EQ(w.MakeUniqueName(VfxAttributeScope::Emitter, "NewParam"),
              "NewParam2");
}

// ─────────────────────────────────────────────────────────────────────────────
// RemoveParameter
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, Remove_WhenFree_Succeeds) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "spread",
                                VfxParamType::Float, Vec4{0.5f, 0, 0, 0}));
    EXPECT_TRUE(w.RemoveParameter(VfxAttributeScope::Emitter, "spread"));
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "spread"));
}

TEST(VfxParameterInspectorWidget, Remove_WhenInUse_Fails) {
    VfxAttributeStore store;
    VfxStack          stk = StackWithRefs();
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "teamColor",
                                VfxParamType::Vec4,
                                Vec4{1, 0, 0, 1}));
    // teamColor is referenced twice in StackWithRefs.
    EXPECT_FALSE(w.RemoveParameter(VfxAttributeScope::Emitter, "teamColor"));
    EXPECT_TRUE(store.Has(VfxAttributeScope::Emitter, "teamColor"));
}

TEST(VfxParameterInspectorWidget, Remove_UnknownFails) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    EXPECT_FALSE(w.RemoveParameter(VfxAttributeScope::Emitter, "ghost"));
}

// ─────────────────────────────────────────────────────────────────────────────
// RenameParameter (+ reference rewrite)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, Rename_RewritesReferences) {
    VfxAttributeStore store;
    VfxStack          stk = StackWithRefs();
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "teamColor",
                                VfxParamType::Vec4,
                                Vec4{1, 0, 0, 1}));

    EXPECT_TRUE(w.RenameParameter(VfxAttributeScope::Emitter,
                                    "teamColor", "faction"));

    // Store key swapped
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "teamColor"));
    EXPECT_TRUE (store.Has(VfxAttributeScope::Emitter, "faction"));
    EXPECT_EQ(w.GetType(VfxAttributeScope::Emitter, "faction"),
              VfxParamType::Vec4);

    // All module configs updated — search for @faction in every
    // referenced config.
    const auto& colorLife =
        stk.GetEntry(VfxStage::ParticleUpdate).modules[0].configJson;
    const auto& grav =
        stk.GetEntry(VfxStage::ParticleUpdate).modules[1].configJson;
    const auto& sprite =
        stk.GetEntry(VfxStage::Render).modules[0].configJson;

    EXPECT_NE(colorLife.find("@faction"), std::string::npos);
    EXPECT_EQ(colorLife.find("@teamColor"), std::string::npos);
    EXPECT_NE(sprite.find("@faction"), std::string::npos);
    EXPECT_EQ(sprite.find("@teamColor"), std::string::npos);
    // Gravity had no reference — unchanged
    EXPECT_EQ(grav.find("@faction"), std::string::npos);
}

TEST(VfxParameterInspectorWidget, Rename_WhenTargetExistsFails) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "a",
                                VfxParamType::Float, Vec4{1, 0, 0, 0}));
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "b",
                                VfxParamType::Float, Vec4{2, 0, 0, 0}));
    EXPECT_FALSE(w.RenameParameter(VfxAttributeScope::Emitter, "a", "b"));
    EXPECT_TRUE(store.Has(VfxAttributeScope::Emitter, "a"));
    EXPECT_TRUE(store.Has(VfxAttributeScope::Emitter, "b"));
}

TEST(VfxParameterInspectorWidget, Rename_SameNameNoOp) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "a",
                                VfxParamType::Float, Vec4{}));
    EXPECT_TRUE(w.RenameParameter(VfxAttributeScope::Emitter, "a", "a"));
}

// ─────────────────────────────────────────────────────────────────────────────
// ChangeType — component preservation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, ChangeType_FloatToVec3_PreservesX_ZeroFills) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "p",
                                VfxParamType::Float, Vec4{3.14f, 0, 0, 0}));
    EXPECT_TRUE(w.ChangeType(VfxAttributeScope::Emitter, "p",
                               VfxParamType::Vec3));
    EXPECT_EQ(w.GetType(VfxAttributeScope::Emitter, "p"),
              VfxParamType::Vec3);
    const Vec4 v = store.GetVec4(VfxAttributeScope::Emitter, "p");
    EXPECT_FLOAT_EQ(v.x, 3.14f);
    EXPECT_FLOAT_EQ(v.y, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(VfxParameterInspectorWidget, ChangeType_Vec4ToFloat_Truncates) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "c",
                                VfxParamType::Vec4,
                                Vec4{0.1f, 0.2f, 0.3f, 0.4f}));
    EXPECT_TRUE(w.ChangeType(VfxAttributeScope::Emitter, "c",
                               VfxParamType::Float));
    EXPECT_EQ(w.GetType(VfxAttributeScope::Emitter, "c"),
              VfxParamType::Float);
    EXPECT_FLOAT_EQ(
        store.GetFloat(VfxAttributeScope::Emitter, "c"), 0.1f);
}

// ─────────────────────────────────────────────────────────────────────────────
// ChangeScope
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, ChangeScope_MovesValueAndType) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "p",
                                VfxParamType::Vec3,
                                Vec4{1, 2, 3, 0}));
    EXPECT_TRUE(w.ChangeScope(VfxAttributeScope::Emitter, "p",
                                VfxAttributeScope::System));
    EXPECT_FALSE(store.Has(VfxAttributeScope::Emitter, "p"));
    EXPECT_TRUE (store.Has(VfxAttributeScope::System,  "p"));
    EXPECT_EQ(w.GetType(VfxAttributeScope::System, "p"),
              VfxParamType::Vec3);
    const Vec3 v = store.GetVec3(VfxAttributeScope::System, "p");
    EXPECT_FLOAT_EQ(v.x, 1.0f);
    EXPECT_FLOAT_EQ(v.y, 2.0f);
    EXPECT_FLOAT_EQ(v.z, 3.0f);
}

TEST(VfxParameterInspectorWidget, ChangeScope_CollisionFails) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "p",
                                VfxParamType::Float, Vec4{1, 0, 0, 0}));
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::System, "p",
                                VfxParamType::Float, Vec4{2, 0, 0, 0}));
    EXPECT_FALSE(w.ChangeScope(VfxAttributeScope::Emitter, "p",
                                 VfxAttributeScope::System));
}

// ─────────────────────────────────────────────────────────────────────────────
// Usage count + first-usage lookup
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, CountReferences_AccumulatesAcrossStack) {
    VfxAttributeStore store;
    VfxStack stk = StackWithRefs();
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    EXPECT_EQ(w.CountReferences("teamColor"), 2u);
    EXPECT_EQ(w.CountReferences("ghost"), 0u);
    EXPECT_EQ(w.CountReferences(""), 0u);
}

TEST(VfxParameterInspectorWidget, FindFirstUsage_ReturnsStageInExecutionOrder) {
    VfxAttributeStore store;
    VfxStack stk = StackWithRefs();
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    const auto hit = w.FindFirstUsage("teamColor");
    EXPECT_TRUE(hit.IsValid());
    // ParticleUpdate runs before Render — so the ColorOverLife module is
    // the first usage.
    EXPECT_EQ(hit.stage, VfxStage::ParticleUpdate);
    EXPECT_EQ(hit.instanceId, "colorlife");

    const auto miss = w.FindFirstUsage("notAParam");
    EXPECT_FALSE(miss.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout + hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, Layout_HeadersAndAddButtonsForExpandedScopes) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    const auto layout = w.ComputeLayout({0, 0, 500, 400});

    int headers = 0, adds = 0;
    for (const auto& it : layout) {
        if (it.type == VfxParamItemType::ScopeHeader) ++headers;
        if (it.type == VfxParamItemType::AddButton)   ++adds;
    }
    // Two author-facing scopes (System + Emitter).
    EXPECT_EQ(headers, 2);
    EXPECT_EQ(adds, 2);
}

TEST(VfxParameterInspectorWidget, Layout_CollapsedScopeHidesAddAndRows) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "p",
                                VfxParamType::Float, Vec4{}));

    w.SetScopeExpanded(VfxAttributeScope::Emitter, false);

    const auto layout = w.ComputeLayout({0, 0, 500, 400});
    for (const auto& it : layout) {
        if (it.type == VfxParamItemType::AddButton ||
            it.type == VfxParamItemType::ParameterRow) {
            EXPECT_NE(it.scope, VfxAttributeScope::Emitter);
        }
    }
}

TEST(VfxParameterInspectorWidget, Layout_InspectorFormAppearsWhenSelected) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "p",
                                VfxParamType::Vec3,
                                Vec4{1, 2, 3, 0}));
    w.SelectParameter(VfxAttributeScope::Emitter, "p");

    const auto layout = w.ComputeLayout({0, 0, 700, 400});
    int name = 0, scopeDD = 0, typeDD = 0, valFields = 0, del = 0, jump = 0;
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxParamItemType::NameField:      ++name; break;
        case VfxParamItemType::ScopeDropdown:  ++scopeDD; break;
        case VfxParamItemType::TypeDropdown:   ++typeDD; break;
        case VfxParamItemType::ValueField:     ++valFields; break;
        case VfxParamItemType::DeleteButton:   ++del; break;
        case VfxParamItemType::JumpToUsages:   ++jump; break;
        default: break;
        }
    }
    EXPECT_EQ(name, 1);
    EXPECT_EQ(scopeDD, 1);
    EXPECT_EQ(typeDD, 1);
    EXPECT_EQ(valFields, 3);  // Vec3 → 3 components
    EXPECT_EQ(del, 1);
    EXPECT_EQ(jump, 1);
}

TEST(VfxParameterInspectorWidget, HitTest_RowClickReturnsName) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    ASSERT_TRUE(w.AddParameter(VfxAttributeScope::Emitter, "intensity",
                                VfxParamType::Float, Vec4{}));

    const auto layout = w.ComputeLayout({0, 0, 500, 400});
    const VfxParamLayoutItem* row = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxParamItemType::ParameterRow &&
            it.name == "intensity") {
            row = &it; break;
        }
    }
    ASSERT_NE(row, nullptr);
    const float cx = row->rect.x + row->rect.width * 0.5f;
    const float cy = row->rect.y + row->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxParamItemType::ParameterRow);
    EXPECT_EQ(hit.name, "intensity");
    EXPECT_EQ(hit.scope, VfxAttributeScope::Emitter);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection callback
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorWidget, SelectParameter_FiresOnlyOnChange) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);

    int calls = 0;
    VfxAttributeScope lastScope = VfxAttributeScope::Particles;
    std::string       lastName;
    w.onParameterSelected = [&](VfxAttributeScope s, const std::string& n) {
        ++calls; lastScope = s; lastName = n;
    };

    w.SelectParameter(VfxAttributeScope::Emitter, "a");
    EXPECT_EQ(calls, 1);
    w.SelectParameter(VfxAttributeScope::Emitter, "a");  // unchanged
    EXPECT_EQ(calls, 1);
    w.SelectParameter(VfxAttributeScope::System, "a");
    EXPECT_EQ(calls, 2);
    EXPECT_EQ(lastScope, VfxAttributeScope::System);
    EXPECT_EQ(lastName, "a");
}
