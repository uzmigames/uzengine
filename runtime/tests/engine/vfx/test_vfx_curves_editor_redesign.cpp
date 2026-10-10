/// Unit tests for phase 12.5.0: `VfxCurvesEditorWidget` redesign —
/// toolbar, grid layout items, tangent handles on selection,
/// HandleScroll cursor-anchored zoom, snap toggle, ZoomToFit,
/// color-ramp responsive plot area.

#include <gtest/gtest.h>

#include <cmath>

#include "engine/vfx/curves/vfx_color_ramp.h"
#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/editor/vfx_curves_editor_widget.h"

using namespace uz;

namespace {

constexpr VfxStackRect kCanvas{0.0f, 0.0f, 800.0f, 400.0f};

size_t CountType(const std::vector<VfxCurveLayoutItem>& layout,
                 VfxCurveItemType t) {
    size_t n = 0;
    for (const auto& it : layout) {
        if (it.type == t) ++n;
    }
    return n;
}

const VfxCurveLayoutItem* First(
    const std::vector<VfxCurveLayoutItem>& layout,
    VfxCurveItemType t) {
    for (const auto& it : layout) {
        if (it.type == t) return &it;
    }
    return nullptr;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Toolbar
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, Toolbar_EmittedWhenBoundToFloat) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);

    const auto layout = w.ComputeLayout(kCanvas);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::Toolbar), 1u);
}

TEST(VfxCurvesEditorRedesign, Toolbar_EmitsAllFiveButtonKinds) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    const auto layout = w.ComputeLayout(kCanvas);

    EXPECT_EQ(CountType(layout, VfxCurveItemType::ButtonAddKey), 1u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::ButtonDeleteKey), 1u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::ButtonZoomFit), 1u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::ButtonSnapToggle), 1u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::InterpPicker), 1u);
}

TEST(VfxCurvesEditorRedesign, Toolbar_SitsAtTopOfCanvas) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    const auto layout = w.ComputeLayout(kCanvas);

    const auto* bar = First(layout, VfxCurveItemType::Toolbar);
    const auto* canvas = First(layout, VfxCurveItemType::Canvas);
    ASSERT_NE(bar, nullptr);
    ASSERT_NE(canvas, nullptr);
    EXPECT_FLOAT_EQ(bar->rect.y, kCanvas.y);
    EXPECT_GE(canvas->rect.y, bar->rect.y + bar->rect.height - 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Grid
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, Grid_EmitsMajorAndMinorLines) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    // Default viewport spans t in [0, 1] (zoomX=1, panX=0) so at most 2
    // integer verticals appear (t=0 and t=1). To hit the scenario "at
    // integer seconds in a range [0, 3]", zoom out: zoomX = 1/3 means
    // visible width = 3 world units.
    w.viewport.zoomX = 1.0f / 3.0f;
    w.viewport.panX  = 0.0f;

    const auto layout = w.ComputeLayout(kCanvas);
    EXPECT_GE(CountType(layout, VfxCurveItemType::GridMajor), 4u)
        << "Expected at least 4 major lines (t=0,1,2,3 verticals)";
    // Minor lines: 3 per major interval * 3 intervals = 9, minus clipped.
    EXPECT_GE(CountType(layout, VfxCurveItemType::GridMinor), 6u);
}

TEST(VfxCurvesEditorRedesign, Grid_MajorLineCarriesLabel) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    const auto layout = w.ComputeLayout(kCanvas);
    // At least one GridMajor must carry a label.
    bool sawLabeledMajor = false;
    for (const auto& it : layout) {
        if (it.type == VfxCurveItemType::GridMajor && !it.label.empty()) {
            sawLabeledMajor = true; break;
        }
    }
    EXPECT_TRUE(sawLabeledMajor);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tangent handles
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, TangentHandles_AbsentWithoutSelection) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.AddKey(0.0f, 0.0f);
    w.AddKey(0.5f, 1.0f);
    w.AddKey(1.0f, 0.0f);
    w.ClearSelection();

    const auto layout = w.ComputeLayout(kCanvas);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::TangentHandleLeft),  0u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::TangentHandleRight), 0u);
}

TEST(VfxCurvesEditorRedesign, TangentHandles_EmittedForSelectedKey) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.AddKey(0.0f, 0.0f);
    w.AddKey(0.5f, 1.0f);
    w.AddKey(1.0f, 0.0f);
    w.SelectIndex(1u);

    const auto layout = w.ComputeLayout(kCanvas);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::TangentHandleLeft),  1u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::TangentHandleRight), 1u);
}

TEST(VfxCurvesEditorRedesign, TangentHandles_FlankTheSelectedKey) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.AddKey(0.5f, 0.5f);
    w.SelectIndex(0u);

    const auto layout = w.ComputeLayout(kCanvas);
    const auto* lh = First(layout, VfxCurveItemType::TangentHandleLeft);
    const auto* rh = First(layout, VfxCurveItemType::TangentHandleRight);
    const auto* key = First(layout, VfxCurveItemType::KeyDot);
    ASSERT_NE(lh, nullptr);
    ASSERT_NE(rh, nullptr);
    ASSERT_NE(key, nullptr);
    const float keyX = key->rect.x + key->rect.width * 0.5f;
    EXPECT_LT(lh->rect.x + lh->rect.width * 0.5f, keyX);
    EXPECT_GT(rh->rect.x + rh->rect.width * 0.5f, keyX);
}

// ─────────────────────────────────────────────────────────────────────────────
// HandleScroll — cursor-anchored zoom
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, HandleScroll_KeepsWorldPointUnderCursor) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);

    // Pick a cursor at (200, 150) in canvas coords + record the world
    // point under it.
    const float cursorX = 200.0f;
    const float cursorY = 150.0f;
    const float worldXBefore = w.viewport.ScreenToWorldX(cursorX, kCanvas);
    const float worldYBefore = w.viewport.ScreenToWorldY(cursorY, kCanvas);

    // One notch of zoom-in.
    w.HandleScroll(1.0f, cursorX, cursorY, kCanvas);

    const float screenXAfter = w.viewport.WorldToScreenX(worldXBefore, kCanvas);
    const float screenYAfter = w.viewport.WorldToScreenY(worldYBefore, kCanvas);
    EXPECT_NEAR(screenXAfter, cursorX, 1.0f);
    EXPECT_NEAR(screenYAfter, cursorY, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Snap toggle
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, SnapToggle_DefaultIsOn) {
    VfxCurvesEditorWidget w;
    EXPECT_TRUE(w.IsSnapEnabled());
}

TEST(VfxCurvesEditorRedesign, SnapToggle_Flips) {
    VfxCurvesEditorWidget w;
    EXPECT_FALSE(w.ToggleSnapEnabled());
    EXPECT_FALSE(w.IsSnapEnabled());
    EXPECT_TRUE(w.ToggleSnapEnabled());
    EXPECT_TRUE(w.IsSnapEnabled());
}

// ─────────────────────────────────────────────────────────────────────────────
// Pan gesture
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, PanGesture_UpdatesOnlyWhileActive) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    const float panXBefore = w.viewport.panX;

    // UpdatePan without BeginPan should be a no-op.
    w.UpdatePan(50.0f, 0.0f, kCanvas);
    EXPECT_FLOAT_EQ(w.viewport.panX, panXBefore);

    // Active pan moves panX.
    w.BeginPan();
    EXPECT_TRUE(w.IsPanning());
    w.UpdatePan(50.0f, 0.0f, kCanvas);
    EXPECT_NE(w.viewport.panX, panXBefore);
    w.EndPan();
    EXPECT_FALSE(w.IsPanning());
}

// ─────────────────────────────────────────────────────────────────────────────
// ZoomToFit
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, ZoomToFit_WithEmptyCurveResetsViewport) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.viewport.panX = 5.0f;
    w.viewport.zoomX = 10.0f;
    w.ZoomToFit();
    EXPECT_FLOAT_EQ(w.viewport.panX, 0.0f);
    EXPECT_FLOAT_EQ(w.viewport.zoomX, 1.0f);
}

TEST(VfxCurvesEditorRedesign, ZoomToFit_WithKeysCoversTimeRange) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.AddKey(0.0f, 0.2f, /*snapEnabled=*/false);
    w.AddKey(2.0f, 0.8f, /*snapEnabled=*/false);
    w.ZoomToFit();
    // Visible span should be ≥ 2.0 world units of time.
    const float visibleW = 1.0f / w.viewport.zoomX;
    EXPECT_GE(visibleW, 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Color ramp mode
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, RampMode_EmitsRibbonAndStops) {
    VFXColorRamp ramp;
    VfxCurvesEditorWidget w;
    w.BindRamp(&ramp);
    w.AddStop(0.0f, 1.0f, 0.0f, 0.0f, 1.0f);
    w.AddStop(1.0f, 0.0f, 0.0f, 1.0f, 1.0f);

    const auto layout = w.ComputeLayout(kCanvas);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::RampRibbon), 1u);
    EXPECT_EQ(CountType(layout, VfxCurveItemType::RampStop), 2u);
}

TEST(VfxCurvesEditorRedesign, RampMode_RibbonCentredInPlotCanvas) {
    VFXColorRamp ramp;
    VfxCurvesEditorWidget w;
    w.BindRamp(&ramp);
    const auto layout = w.ComputeLayout(kCanvas);
    const auto* rib = First(layout, VfxCurveItemType::RampRibbon);
    const auto* canvas = First(layout, VfxCurveItemType::Canvas);
    ASSERT_NE(rib, nullptr);
    ASSERT_NE(canvas, nullptr);
    // Ribbon sits inside the plot canvas (not over the toolbar).
    EXPECT_GE(rib->rect.y, canvas->rect.y - 0.01f);
    EXPECT_LE(rib->rect.y + rib->rect.height,
              canvas->rect.y + canvas->rect.height + 0.01f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callbacks exist + fire
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorRedesign, TangentCallback_FieldIsBindable) {
    VfxCurvesEditorWidget w;
    bool called = false;
    w.onKeyTangentChanged = [&](size_t, float, float) { called = true; };
    w.onKeyTangentChanged(0u, 1.0f, -1.0f);
    EXPECT_TRUE(called);
}

TEST(VfxCurvesEditorRedesign, ToolbarCallback_FieldIsBindable) {
    VfxCurvesEditorWidget w;
    VfxCurveItemType got = VfxCurveItemType::None;
    w.onToolbarAction = [&](VfxCurveItemType t) { got = t; };
    w.onToolbarAction(VfxCurveItemType::ButtonZoomFit);
    EXPECT_EQ(got, VfxCurveItemType::ButtonZoomFit);
}
