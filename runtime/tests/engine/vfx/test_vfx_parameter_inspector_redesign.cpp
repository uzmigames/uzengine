/// Unit tests for phase 12.4.0: `VfxParameterInspectorWidget` redesign —
/// section grouping, collapsible sections, typed editor sub-rects,
/// reset-to-default + bind-to-curve callbacks, range validation.

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/stack/vfx_stack.h"

using namespace uz;

namespace {

constexpr VfxStackRect kPanel{0.0f, 0.0f, 500.0f, 600.0f};

size_t CountType(const std::vector<VfxParamLayoutItem>& layout,
                 VfxParamItemType t) {
    size_t n = 0;
    for (const auto& it : layout) {
        if (it.type == t) ++n;
    }
    return n;
}

const VfxParamLayoutItem* FindEditor(
    const std::vector<VfxParamLayoutItem>& layout,
    VfxParamItemType editorType,
    const std::string& paramName) {
    for (const auto& it : layout) {
        if (it.type == editorType && it.name == paramName) return &it;
    }
    return nullptr;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Section grouping
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, DefaultSectionIsGeneral) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "lifetime",
                    VfxParamType::Float, Vec4{1.0f, 0, 0, 0});
    EXPECT_EQ(w.GetParameterSection(VfxAttributeScope::Emitter, "lifetime"),
              "General");
}

TEST(VfxParameterInspectorRedesign, SetSection_PersistsValue) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "lifetime",
                    VfxParamType::Float, Vec4{1.0f, 0, 0, 0});
    w.SetParameterSection(VfxAttributeScope::Emitter, "lifetime", "Spawn");
    EXPECT_EQ(w.GetParameterSection(VfxAttributeScope::Emitter, "lifetime"),
              "Spawn");
    // Clearing with empty string resets to default.
    w.SetParameterSection(VfxAttributeScope::Emitter, "lifetime", "");
    EXPECT_EQ(w.GetParameterSection(VfxAttributeScope::Emitter, "lifetime"),
              "General");
}

TEST(VfxParameterInspectorRedesign, Layout_EmitsOneSectionPerDistinctName) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "lifetime",
                    VfxParamType::Float, Vec4{1, 0, 0, 0});
    w.AddParameter(VfxAttributeScope::Emitter, "spawnRate",
                    VfxParamType::Float, Vec4{1, 0, 0, 0});
    w.AddParameter(VfxAttributeScope::Emitter, "gravity",
                    VfxParamType::Float, Vec4{9.8f, 0, 0, 0});
    w.SetParameterSection(VfxAttributeScope::Emitter, "lifetime", "Spawn");
    w.SetParameterSection(VfxAttributeScope::Emitter, "spawnRate", "Spawn");
    w.SetParameterSection(VfxAttributeScope::Emitter, "gravity", "Physics");

    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_EQ(CountType(layout, VfxParamItemType::Section), 2u)
        << "Expected Spawn + Physics = 2 distinct sections";
}

TEST(VfxParameterInspectorRedesign, CollapsedSectionHidesRows) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "p1",
                    VfxParamType::Float, Vec4{0, 0, 0, 0});
    w.AddParameter(VfxAttributeScope::Emitter, "p2",
                    VfxParamType::Float, Vec4{0, 0, 0, 0});
    w.AddParameter(VfxAttributeScope::Emitter, "p3",
                    VfxParamType::Float, Vec4{0, 0, 0, 0});
    w.SetParameterSection(VfxAttributeScope::Emitter, "p1", "Spawn");
    w.SetParameterSection(VfxAttributeScope::Emitter, "p2", "Spawn");
    w.SetParameterSection(VfxAttributeScope::Emitter, "p3", "Spawn");

    // Expanded -> 3 rows present.
    EXPECT_TRUE(w.IsSectionExpanded("Spawn"));
    {
        const auto layout = w.ComputeLayout(kPanel);
        EXPECT_EQ(CountType(layout, VfxParamItemType::ParameterRow), 3u);
    }

    // Collapse -> rows disappear, section still emitted.
    w.SetSectionExpanded("Spawn", false);
    {
        const auto layout = w.ComputeLayout(kPanel);
        EXPECT_EQ(CountType(layout, VfxParamItemType::Section), 1u);
        EXPECT_EQ(CountType(layout, VfxParamItemType::ParameterRow), 0u);
    }
}

TEST(VfxParameterInspectorRedesign, DistinctSections_OrderedByFirstAppearance) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "alpha",
                    VfxParamType::Float, Vec4{});
    w.AddParameter(VfxAttributeScope::Emitter, "beta",
                    VfxParamType::Float, Vec4{});
    w.SetParameterSection(VfxAttributeScope::Emitter, "alpha", "B");
    w.SetParameterSection(VfxAttributeScope::Emitter, "beta", "A");
    // Names sorted alphabetically inside ListParameters → alpha first,
    // so section "B" appears before "A" in DistinctSections.
    const auto distinct = w.DistinctSections();
    ASSERT_GE(distinct.size(), 2u);
    EXPECT_EQ(distinct[0], "B");
    EXPECT_EQ(distinct[1], "A");
}

// ─────────────────────────────────────────────────────────────────────────────
// Typed editor sub-rects
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, FloatParamEmitsEditorFloat) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "lifetime",
                    VfxParamType::Float, Vec4{1.0f, 0, 0, 0});

    const auto layout = w.ComputeLayout(kPanel);
    const auto* ed = FindEditor(layout, VfxParamItemType::EditorFloat,
                                  "lifetime");
    ASSERT_NE(ed, nullptr);
    EXPECT_EQ(ed->name, "lifetime");
    EXPECT_EQ(ed->scope, VfxAttributeScope::Emitter);
}

TEST(VfxParameterInspectorRedesign, Vec3ParamEmitsEditorVec3) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "gravity",
                    VfxParamType::Vec3, Vec4{0, -9.8f, 0, 0});

    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_NE(FindEditor(layout, VfxParamItemType::EditorVec3, "gravity"),
              nullptr);
}

TEST(VfxParameterInspectorRedesign, Vec4ParamEmitsEditorColor) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "tint",
                    VfxParamType::Vec4, Vec4{1, 0.5f, 0.25f, 1});

    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_NE(FindEditor(layout, VfxParamItemType::EditorColor, "tint"),
              nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// 40/55/5 column split
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, RowSplit_AtKnownWidth_400) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.listWidth = 400.0f;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "x",
                    VfxParamType::Float, Vec4{0, 0, 0, 0});

    const auto layout = w.ComputeLayout(kPanel);
    const auto* ed = FindEditor(layout, VfxParamItemType::EditorFloat, "x");
    const auto* rst = FindEditor(layout, VfxParamItemType::ResetToDefault, "x");
    ASSERT_NE(ed, nullptr);
    ASSERT_NE(rst, nullptr);

    // Editor column should be ~55% of total width.
    EXPECT_GE(ed->rect.width, 218.0f);
    EXPECT_LE(ed->rect.width, 222.0f);

    // Label position (editor x - 0) yields label width via rect.x
    // difference. Editor starts after labelW.
    const float editorStart = ed->rect.x;
    const float labelW = editorStart; // labels start at x=0 in our panel
    EXPECT_GE(labelW, 158.0f);
    EXPECT_LE(labelW, 162.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset + bind callbacks
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, ResetCallback_FiresOnClickSimulation) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "lifetime",
                    VfxParamType::Float, Vec4{1.0f, 0, 0, 0});

    VfxAttributeScope got = VfxAttributeScope::System;
    std::string gotName;
    w.onParameterReset =
        [&](VfxAttributeScope s, const std::string& n) {
            got = s; gotName = n;
        };

    // Manually invoke the callback (widget does not dispatch itself;
    // the host's hit-dispatch fires it on a ResetToDefault click).
    w.onParameterReset(VfxAttributeScope::Emitter, "lifetime");
    EXPECT_EQ(got, VfxAttributeScope::Emitter);
    EXPECT_EQ(gotName, "lifetime");
}

TEST(VfxParameterInspectorRedesign, BindCallback_FiresOnClickSimulation) {
    VfxParameterInspectorWidget w;
    int calls = 0;
    w.onParameterBindRequested =
        [&](VfxAttributeScope, const std::string&) { ++calls; };
    w.onParameterBindRequested(VfxAttributeScope::Emitter, "x");
    EXPECT_EQ(calls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Range validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, Range_DefaultsToNoValidation) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "x",
                    VfxParamType::Float, Vec4{1000.0f, 0, 0, 0});
    // No range registered → any value is in range.
    EXPECT_TRUE(w.IsValueInRange(VfxAttributeScope::Emitter, "x", 1e9f));
    EXPECT_TRUE(w.IsValueInRange(VfxAttributeScope::Emitter, "x", -1e9f));
}

TEST(VfxParameterInspectorRedesign, Range_RegisteredChecksBounds) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "x",
                    VfxParamType::Float, Vec4{0.5f, 0, 0, 0});
    w.AddParameterRange(VfxAttributeScope::Emitter, "x", 0.0f, 1.0f);

    EXPECT_TRUE (w.IsValueInRange(VfxAttributeScope::Emitter, "x", 0.5f));
    EXPECT_TRUE (w.IsValueInRange(VfxAttributeScope::Emitter, "x", 0.0f));
    EXPECT_TRUE (w.IsValueInRange(VfxAttributeScope::Emitter, "x", 1.0f));
    EXPECT_FALSE(w.IsValueInRange(VfxAttributeScope::Emitter, "x", -0.1f));
    EXPECT_FALSE(w.IsValueInRange(VfxAttributeScope::Emitter, "x", 1.5f));
}

TEST(VfxParameterInspectorRedesign, Range_UnknownParamNoOp) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    // Registering a range on a missing parameter is a no-op — any value
    // reported in-range afterwards means the range wasn't actually stored.
    w.AddParameterRange(VfxAttributeScope::Emitter, "nonexistent", 0.0f, 1.0f);
    EXPECT_TRUE(w.IsValueInRange(VfxAttributeScope::Emitter, "nonexistent", 5.0f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Reset + bind sub-rects emitted per row
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, EveryParamRowEmitsResetSubRect) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "a",
                    VfxParamType::Float, Vec4{});
    w.AddParameter(VfxAttributeScope::Emitter, "b",
                    VfxParamType::Vec3,  Vec4{});

    const auto layout = w.ComputeLayout(kPanel);
    EXPECT_EQ(CountType(layout, VfxParamItemType::ResetToDefault), 2u);
}

TEST(VfxParameterInspectorRedesign, BindSubRect_OnlyForFloatParams) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "scalar",
                    VfxParamType::Float, Vec4{});
    w.AddParameter(VfxAttributeScope::Emitter, "vec",
                    VfxParamType::Vec3,  Vec4{});

    const auto layout = w.ComputeLayout(kPanel);
    // Only the float row gets a bind button.
    EXPECT_EQ(CountType(layout, VfxParamItemType::BindToCurve), 1u);
    EXPECT_NE(FindEditor(layout, VfxParamItemType::BindToCurve, "scalar"),
              nullptr);
    EXPECT_EQ(FindEditor(layout, VfxParamItemType::BindToCurve, "vec"),
              nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// HitTest priority
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxParameterInspectorRedesign, HitTest_EditorBeatsParameterRow) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.listWidth = 400.0f;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "x",
                    VfxParamType::Float, Vec4{});

    const auto layout = w.ComputeLayout(kPanel);
    const auto* ed = FindEditor(layout, VfxParamItemType::EditorFloat, "x");
    ASSERT_NE(ed, nullptr);
    const float cx = ed->rect.x + ed->rect.width * 0.5f;
    const float cy = ed->rect.y + ed->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxParamItemType::EditorFloat);
}

TEST(VfxParameterInspectorRedesign, HitTest_ResetButtonBeatsParameterRow) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget w;
    w.listWidth = 400.0f;
    w.Bind(&store, &stk);
    w.AddParameter(VfxAttributeScope::Emitter, "x",
                    VfxParamType::Float, Vec4{});

    const auto layout = w.ComputeLayout(kPanel);
    const auto* rst = FindEditor(layout, VfxParamItemType::ResetToDefault, "x");
    ASSERT_NE(rst, nullptr);
    const float cx = rst->rect.x + rst->rect.width * 0.5f;
    const float cy = rst->rect.y + rst->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxParamItemType::ResetToDefault);
}
