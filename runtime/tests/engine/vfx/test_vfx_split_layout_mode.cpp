/// Unit tests for phase 15.1.0: split-horizontal layout mode.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_composition.h"

using namespace uz;

TEST(VfxSplitLayout, FiftyFiftyAt1280x720) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;
    cfg.splitFraction = 0.5f;

    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);

    // (1280 - 6) * 0.5 = 637 → viewport 637, gutter 6, editor 637.
    EXPECT_FLOAT_EQ(rects.viewportReserve.width, 637.0f);
    EXPECT_FLOAT_EQ(rects.splitter.width,          6.0f);
    EXPECT_FLOAT_EQ(rects.editorPane.width,      637.0f);
    EXPECT_FLOAT_EQ(rects.viewportReserve.x + rects.viewportReserve.width,
                     rects.splitter.x);
    EXPECT_FLOAT_EQ(rects.splitter.x + rects.splitter.width,
                     rects.editorPane.x);
}

TEST(VfxSplitLayout, SplitFractionClampsLow) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;
    cfg.splitFraction = 0.05f;

    const auto rects = VfxComputeEditorLayout(1000.0f, 720.0f, cfg);
    // Clamped to 0.2 → viewport 0.2 * (1000 - 6) = 198.8.
    EXPECT_NEAR(rects.viewportReserve.width, 198.8f, 0.1f);
}

TEST(VfxSplitLayout, SplitFractionClampsHigh) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;
    cfg.splitFraction = 0.95f;

    const auto rects = VfxComputeEditorLayout(1000.0f, 720.0f, cfg);
    // Clamped to 0.8 → viewport 0.8 * 994 = 795.2, editor 198.8.
    EXPECT_NEAR(rects.viewportReserve.width, 795.2f, 0.1f);
    EXPECT_NEAR(rects.editorPane.width,      198.8f, 0.1f);
}

TEST(VfxSplitLayout, SplitModeZeroesThreeColumnRects) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;

    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_FLOAT_EQ(rects.stackPanel.width,  0.0f);
    EXPECT_FLOAT_EQ(rects.centerPanel.width, 0.0f);
    EXPECT_FLOAT_EQ(rects.rightPanel.width,  0.0f);
}

TEST(VfxSplitLayout, NiagaraModePreservesPhase14Rects) {
    VfxEditorLayoutConfig cfg;  // default: Niagara mode.
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    // editorPane + splitter must be zero in Niagara mode.
    EXPECT_FLOAT_EQ(rects.editorPane.width, 0.0f);
    EXPECT_FLOAT_EQ(rects.splitter.width,   0.0f);
    // Three-column rects must be present.
    EXPECT_GT(rects.stackPanel.width,  0.0f);
    EXPECT_GT(rects.centerPanel.width, 0.0f);
    EXPECT_GT(rects.rightPanel.width,  0.0f);
}

TEST(VfxSplitLayout, MiddleContentBandBelowToolbarAboveStatus) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);

    // Middle content top = menuBar.height + toolbarBar.height.
    const float expectedY = cfg.menuBarHeight + cfg.toolbarBarHeight;
    EXPECT_FLOAT_EQ(rects.viewportReserve.y, expectedY);
    EXPECT_FLOAT_EQ(rects.editorPane.y,      expectedY);

    // Middle bottom + status + timeline = screenH.
    const float bottom = rects.viewportReserve.y + rects.viewportReserve.height;
    EXPECT_FLOAT_EQ(rects.statusBar.y, bottom);
    EXPECT_FLOAT_EQ(rects.statusBar.y + rects.statusBar.height,
                     rects.timelineBar.y);
}

TEST(VfxSplitLayout, StripHeightsMatchConfig) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;
    const auto rects = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_FLOAT_EQ(rects.menuBar.height,     cfg.menuBarHeight);
    EXPECT_FLOAT_EQ(rects.toolbarBar.height,  cfg.toolbarBarHeight);
    EXPECT_FLOAT_EQ(rects.statusBar.height,   cfg.statusBarHeight);
    EXPECT_FLOAT_EQ(rects.timelineBar.height, cfg.timelineBarHeight);
}
