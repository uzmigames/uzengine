/// Unit tests for phase 14.10.0: composition layout adds toolbar
/// + status bar strips.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_composition.h"

using namespace uz;

TEST(VfxEditorComposition1410, ToolbarBarDocksBelowMenu) {
    VfxEditorLayoutConfig cfg{};
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_FLOAT_EQ(rects.toolbarBar.y,      rects.menuBar.height);
    EXPECT_FLOAT_EQ(rects.toolbarBar.height, cfg.toolbarBarHeight);
    EXPECT_FLOAT_EQ(rects.toolbarBar.width,  1280.0f);
}

TEST(VfxEditorComposition1410, StatusBarAboveTimeline) {
    VfxEditorLayoutConfig cfg{};
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_FLOAT_EQ(rects.statusBar.y + rects.statusBar.height,
                     rects.timelineBar.y);
    EXPECT_FLOAT_EQ(rects.statusBar.height, cfg.statusBarHeight);
}

TEST(VfxEditorComposition1410, MiddlePanelShrinksForNewStrips) {
    VfxEditorLayoutConfig cfg{};
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    // middleH = screenH - menu - toolbar - status - timeline
    const float expected = 720.0f - cfg.menuBarHeight -
                            cfg.toolbarBarHeight - cfg.statusBarHeight -
                            cfg.timelineBarHeight;
    EXPECT_NEAR(rects.stackPanel.height, expected, 0.01f);
}

TEST(VfxEditorComposition1410, StackPanelStartsBelowToolbar) {
    VfxEditorLayoutConfig cfg{};
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_FLOAT_EQ(rects.stackPanel.y,
                     rects.menuBar.height + cfg.toolbarBarHeight);
}

TEST(VfxEditorComposition1410, DefaultHeightsMatchDesignIntent) {
    VfxEditorLayoutConfig cfg{};
    EXPECT_FLOAT_EQ(cfg.toolbarBarHeight, 28.0f);
    EXPECT_FLOAT_EQ(cfg.statusBarHeight,  22.0f);
}
