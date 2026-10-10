/// Unit tests for phase 15.3.0: `VfxEditorPaneWidget`.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_pane_widget.h"

using namespace uz;

TEST(VfxEditorPane, DefaultTabIsParameters) {
    // Phase 15.9 dropped the Stack tab; Parameters is now the default.
    VfxEditorPaneWidget pane;
    EXPECT_EQ(pane.ActiveTab(), VfxEditorPaneTab::Parameters);
}

TEST(VfxEditorPane, SetActiveTabChangesTab) {
    VfxEditorPaneWidget pane;
    pane.SetActiveTab(VfxEditorPaneTab::Parameters);
    EXPECT_EQ(pane.ActiveTab(), VfxEditorPaneTab::Parameters);
}

TEST(VfxEditorPane, SetActiveTabCallbackFiresOnChange) {
    VfxEditorPaneWidget pane;
    int fireCount = 0;
    VfxEditorPaneTab observed = VfxEditorPaneTab::Stack;
    pane.onTabChanged = [&](VfxEditorPaneTab t) {
        ++fireCount;
        observed = t;
    };
    pane.SetActiveTab(VfxEditorPaneTab::Emitters);
    EXPECT_EQ(fireCount, 1);
    EXPECT_EQ(observed, VfxEditorPaneTab::Emitters);
}

TEST(VfxEditorPane, SameTabSetDoesNotFireCallback) {
    VfxEditorPaneWidget pane;
    int fireCount = 0;
    pane.onTabChanged = [&](VfxEditorPaneTab) { ++fireCount; };
    pane.SetActiveTab(VfxEditorPaneTab::Parameters);  // already active.
    EXPECT_EQ(fireCount, 0);
}

TEST(VfxEditorPane, HandleClickOnTabReturnsConsumed) {
    VfxEditorPaneWidget pane;
    const VfxStackRect bounds{640, 50, 640, 500};
    // Click on the first tab — after the Stack tab was dropped this
    // maps to Parameters (index 0).
    const auto r = pane.HandleClick(660.0f, 64.0f, bounds);
    EXPECT_TRUE(r.consumed);
    EXPECT_EQ(r.tab, VfxEditorPaneTab::Parameters);
}

TEST(VfxEditorPane, HandleClickBelowStripReturnsNotConsumed) {
    VfxEditorPaneWidget pane;
    const VfxStackRect bounds{640, 50, 640, 500};
    // Click well below the 28-px tab strip.
    const auto r = pane.HandleClick(660.0f, 400.0f, bounds);
    EXPECT_FALSE(r.consumed);
}

TEST(VfxEditorPane, TabsPopulatedToTwo) {
    // Phase 15.9 — Stack tab was dropped. Pane now exposes exactly two
    // tabs (Parameters + Emitters).
    VfxEditorPaneWidget pane;
    const VfxStackRect bounds{0, 0, 400, 300};
    pane.HandleClick(10.0f, 400.0f, bounds);
    EXPECT_TRUE(pane.HandleClick(10.0f,  14.0f, bounds).consumed ||
                 pane.HandleClick(100.0f, 14.0f, bounds).consumed);
}
