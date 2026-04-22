/// Unit tests for phase 15.4.0: split-layout overlay integration
/// invariants that do not require a GPU device.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_composition.h"
#include "engine/vfx/editor/vfx_splitter_widget.h"

using namespace uz;

namespace {

/// Mirrors the overlay's drag-to-fraction math so the arithmetic
/// is locked in without needing an overlay instance.
float ApplyDrag(float fraction, float dx, float screenW) {
    const float raw = fraction + dx / screenW;
    return std::clamp(raw, 0.2f, 0.8f);
}

} // namespace

TEST(VfxSplitLayoutIntegration, SplitFractionUpdatesFromDrag) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode    = VfxEditorLayoutMode::SplitHorizontal;
    cfg.splitFraction = 0.5f;

    cfg.splitFraction = ApplyDrag(cfg.splitFraction, 64.0f, 1280.0f);
    EXPECT_NEAR(cfg.splitFraction, 0.55f, 1e-5f);
}

TEST(VfxSplitLayoutIntegration, DragClampsAtLowerBound) {
    const float result = ApplyDrag(0.20f, -200.0f, 1280.0f);
    EXPECT_FLOAT_EQ(result, 0.2f);
}

TEST(VfxSplitLayoutIntegration, DragClampsAtUpperBound) {
    const float result = ApplyDrag(0.80f, 500.0f, 1280.0f);
    EXPECT_FLOAT_EQ(result, 0.8f);
}

TEST(VfxSplitLayoutIntegration, LayoutReflectsFractionChange) {
    VfxEditorLayoutConfig cfg;
    cfg.layoutMode = VfxEditorLayoutMode::SplitHorizontal;

    cfg.splitFraction = 0.3f;
    const auto r = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_NEAR(r.viewportReserve.width, (1280.0f - 6.0f) * 0.3f, 0.5f);
    EXPECT_NEAR(r.editorPane.width,      (1280.0f - 6.0f) * 0.7f, 0.5f);
}

TEST(VfxSplitLayoutIntegration, SplitterStateMachine) {
    VfxSplitterWidget s;
    EXPECT_FALSE(s.IsDragging());
    s.BeginDrag(100.0f);
    EXPECT_TRUE(s.IsDragging());
    const float d = s.UpdateDrag(150.0f);
    EXPECT_FLOAT_EQ(d, 50.0f);
    s.EndDrag();
    EXPECT_FALSE(s.IsDragging());
}

TEST(VfxSplitLayoutIntegration, NiagaraModeStillBuilds3ColumnRects) {
    VfxEditorLayoutConfig cfg;  // default Niagara mode
    const auto r = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    // Phase-14 rects must be present.
    EXPECT_GT(r.stackPanel.width,  0.0f);
    EXPECT_GT(r.centerPanel.width, 0.0f);
    EXPECT_GT(r.rightPanel.width,  0.0f);
    // Split-only rects must be empty.
    EXPECT_FLOAT_EQ(r.editorPane.width, 0.0f);
    EXPECT_FLOAT_EQ(r.splitter.width,   0.0f);
}
