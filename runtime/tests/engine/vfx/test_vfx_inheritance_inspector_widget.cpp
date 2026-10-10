/// Unit tests for phase 9.2.0 — VFX inheritance inspector widget.
///
/// @ref engine/vfx/editor/vfx_inheritance_inspector_widget.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_inheritance_inspector_widget.h"

using namespace uz;

namespace {

constexpr const char* kParentJson = R"({
    "format": "uzvfx",
    "version": 1,
    "modules": [
        { "type": "Spawn",   "config": {} },
        { "type": "Gravity", "config": { "strength": 5.0 } },
        { "type": "Drag",    "config": { "coef": 0.1 } }
    ]
})";

constexpr const char* kChildJsonWithOverrides = R"({
    "format": "uzvfx",
    "version": 1,
    "parent": "parent.uzvfx",
    "modules": [
        { "type": "Gravity", "action": "replace", "config": { "strength": 9.8 } },
        { "type": "Drag",    "action": "remove" },
        { "type": "Burst",   "action": "add",     "config": { "count": 100 } }
    ]
})";

constexpr const char* kChildJsonNoParent = R"({
    "format": "uzvfx",
    "version": 1,
    "modules": [
        { "type": "Spawn", "config": {} }
    ]
})";

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Row kind names
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceInspector, RowKindNames) {
    EXPECT_STREQ(VfxInheritanceRowKindName(VfxInheritanceRowKind::Inherited),       "inherited");
    EXPECT_STREQ(VfxInheritanceRowKindName(VfxInheritanceRowKind::OverrideAdd),     "add");
    EXPECT_STREQ(VfxInheritanceRowKindName(VfxInheritanceRowKind::OverrideReplace), "replace");
    EXPECT_STREQ(VfxInheritanceRowKindName(VfxInheritanceRowKind::OverrideRemove),  "remove");
}

// ─────────────────────────────────────────────────────────────────────────────
// Unbound / error paths
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceInspector, UnboundReportsError) {
    VfxInheritanceInspectorWidget w;
    w.Resolve();
    EXPECT_EQ(w.RowCount(), 0u);
    EXPECT_FALSE(w.LastError().empty());
}

TEST(VfxInheritanceInspector, InvalidJsonReportsError) {
    VfxInheritanceInspectorWidget w;
    std::string bad = "{not json";
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &bad;
    w.SetInputModel(m);
    EXPECT_FALSE(w.LastError().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Full resolve with parent
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceInspector, ResolveWithOverrides) {
    std::string child = kChildJsonWithOverrides;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;
    m.parentLoader = [](const std::string& /*path*/) -> std::string {
        return kParentJson;
    };

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);

    // Parent path populated.
    EXPECT_EQ(w.ParentPath(), "parent.uzvfx");
    EXPECT_TRUE(w.LastError().empty());

    // 4 rows: 3 parent (Spawn/Gravity/Drag) + 1 child-only (Burst).
    ASSERT_EQ(w.RowCount(), 4u);

    const auto& rows = w.Rows();
    EXPECT_EQ(rows[0].moduleType, "Spawn");
    EXPECT_EQ(rows[0].kind, VfxInheritanceRowKind::Inherited);

    EXPECT_EQ(rows[1].moduleType, "Gravity");
    EXPECT_EQ(rows[1].kind, VfxInheritanceRowKind::OverrideReplace);
    EXPECT_EQ(rows[1].childModulesIndex, 0u);

    EXPECT_EQ(rows[2].moduleType, "Drag");
    EXPECT_EQ(rows[2].kind, VfxInheritanceRowKind::OverrideRemove);
    EXPECT_EQ(rows[2].childModulesIndex, 1u);

    EXPECT_EQ(rows[3].moduleType, "Burst");
    EXPECT_EQ(rows[3].kind, VfxInheritanceRowKind::OverrideAdd);
    EXPECT_EQ(rows[3].childModulesIndex, 2u);
}

TEST(VfxInheritanceInspector, ResolveNoParent) {
    std::string child = kChildJsonNoParent;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);

    EXPECT_TRUE(w.ParentPath().empty());
    // Single child-only module → one Add row.
    ASSERT_EQ(w.RowCount(), 1u);
    EXPECT_EQ(w.Rows()[0].moduleType, "Spawn");
    EXPECT_EQ(w.Rows()[0].kind, VfxInheritanceRowKind::OverrideAdd);
}

TEST(VfxInheritanceInspector, ParentLoaderFailureCaptured) {
    std::string child = kChildJsonWithOverrides;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;
    m.parentLoader = [](const std::string&) -> std::string { return ""; };

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);
    EXPECT_FALSE(w.LastError().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Actions — ClearParent + RevertOverride
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceInspector, ClearParent) {
    std::string child = kChildJsonWithOverrides;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;
    m.parentLoader = [](const std::string&) { return kParentJson; };

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);
    EXPECT_EQ(w.ParentPath(), "parent.uzvfx");

    w.ClearParent();
    EXPECT_TRUE(w.ParentPath().empty());
    // After clearing parent, only the child's own modules show as Add.
    EXPECT_EQ(w.RowCount(), 3u);
    for (const auto& row : w.Rows()) {
        EXPECT_NE(row.kind, VfxInheritanceRowKind::Inherited);
    }
}

TEST(VfxInheritanceInspector, RevertOverrideRemovesFromChild) {
    std::string child = kChildJsonWithOverrides;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;
    m.parentLoader = [](const std::string&) { return kParentJson; };

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);
    ASSERT_EQ(w.RowCount(), 4u);

    // Revert row 1 (Gravity replace) → should become Inherited.
    // Row count is unchanged: every parent module still has a row, only
    // Gravity's classification flips. The remaining child entries shift
    // down one slot in `modules[]`.
    w.RevertOverride(1);
    ASSERT_EQ(w.RowCount(), 4u);
    EXPECT_EQ(w.Rows()[1].moduleType, "Gravity");
    EXPECT_EQ(w.Rows()[1].kind, VfxInheritanceRowKind::Inherited);
    EXPECT_EQ(w.Rows()[1].childModulesIndex, UINT32_MAX);
    EXPECT_EQ(w.Rows()[2].kind, VfxInheritanceRowKind::OverrideRemove);
    EXPECT_EQ(w.Rows()[2].childModulesIndex, 0u);
    EXPECT_EQ(w.Rows()[3].kind, VfxInheritanceRowKind::OverrideAdd);
    EXPECT_EQ(w.Rows()[3].childModulesIndex, 1u);
    EXPECT_EQ(child.find("\"replace\""), std::string::npos);
}

TEST(VfxInheritanceInspector, RevertInvalidRowNoOp) {
    std::string child = kChildJsonNoParent;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);
    const uint32_t before = w.RowCount();
    w.RevertOverride(999);
    EXPECT_EQ(w.RowCount(), before);
}

TEST(VfxInheritanceInspector, RevertInheritedRowNoOp) {
    std::string child = kChildJsonWithOverrides;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;
    m.parentLoader = [](const std::string&) { return kParentJson; };

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);
    // Row 0 is "Spawn" → Inherited → RevertOverride should be no-op.
    const uint32_t before = w.RowCount();
    w.RevertOverride(0);
    EXPECT_EQ(w.RowCount(), before);
}

TEST(VfxInheritanceInspector, UnmatchedOverridesFollowResolver) {
    // VfxResolveInheritance appends a `replace` whose target is missing
    // from the parent and drops a `remove` whose target is missing.
    std::string child = R"({
        "format": "uzvfx",
        "version": 1,
        "parent": "parent.uzvfx",
        "modules": [
            { "type": "Vortex", "action": "replace", "config": {} },
            { "type": "Orbit",  "action": "remove" }
        ]
    })";
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;
    m.parentLoader = [](const std::string&) { return kParentJson; };

    VfxInheritanceInspectorWidget w;
    w.SetInputModel(m);
    ASSERT_EQ(w.RowCount(), 4u);
    for (uint32_t i = 0; i < 3u; ++i) {
        EXPECT_EQ(w.Rows()[i].kind, VfxInheritanceRowKind::Inherited);
    }
    EXPECT_EQ(w.Rows()[3].moduleType, "Vortex");
    EXPECT_EQ(w.Rows()[3].kind, VfxInheritanceRowKind::OverrideAdd);
    EXPECT_EQ(w.Rows()[3].childModulesIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInheritanceInspector, OnResolvedFires) {
    std::string child = kChildJsonNoParent;
    VfxInheritanceInspectorWidget::InputModel m;
    m.childJson = &child;

    int count = 0;
    VfxInheritanceInspectorWidget w;
    w.onResolved = [&]() { ++count; };
    w.SetInputModel(m);
    EXPECT_GE(count, 1); // initial Resolve
    w.Resolve();
    EXPECT_GE(count, 2);
}
