/// Unit tests for phase 11.5.0: `VfxCurvesEditorWidget`.
///
/// Covers the pure-data portion of the canvas editor:
///   - Binding modes (Float / Ramp / None), rejection of Vec4 curves,
///     mirror rebuild on ramp bind.
///   - Scalar mutations: `AddKey` inserts sorted + returns new index,
///     `MoveKey` re-sorts when crossing a neighbour, `RemoveKey` shifts.
///   - Snapping: time/value rounded to `snapTime` / `snapValue` by
///     default; disabled when `snapEnabled=false`.
///   - Ramp mutations: `AddStop` respects `kMaxStops`, `MoveStop`
///     re-sorts, `SetStopColor` preserves position, `RemoveStop` shifts.
///   - Dirty flag set by every mutation; cleared via `ClearDirty`.
///   - `ShouldRebakeNow`: false when clean, true when clean AND not
///     dragging, false when dragging AND idle < debounce, true when
///     dragging AND idle ≥ debounce.
///   - Viewport: `WorldToScreen` + `ScreenToWorld` round-trip; `Zoom`
///     anchors the world point under the cursor.
///   - Layout: Float mode emits Canvas + KeyDot per key; Ramp mode
///     emits Canvas + RampRibbon + RampStop per stop.
///   - Hit-test: precedence KeyDot > Canvas, RampStop > RampRibbon >
///     Canvas.
///   - Callbacks fire on mutation (Curve vs Ramp).

#include <gtest/gtest.h>

#include "engine/vfx/curves/vfx_color_ramp.h"
#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/editor/vfx_curves_editor_widget.h"

#include <cmath>
#include <vector>

using namespace uz;

namespace {

constexpr VfxStackRect kCanvas{ 0.0f, 0.0f, 500.0f, 300.0f };

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Binding
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, BindNullLeavesNoneMode) {
    VfxCurvesEditorWidget w;
    w.BindFloat(nullptr);
    EXPECT_EQ(w.Mode(), VfxCurveBindingMode::None);
    w.BindRamp(nullptr);
    EXPECT_EQ(w.Mode(), VfxCurveBindingMode::None);
}

TEST(VfxCurvesEditorWidget, BindFloatAcceptsFloatCurve) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    EXPECT_EQ(w.Mode(), VfxCurveBindingMode::Float);
    EXPECT_EQ(w.Curve(), &c);
}

TEST(VfxCurvesEditorWidget, BindFloatRejectsVec4Curve) {
    VFXCurve c;
    c.SetKeysVec4({ {0.0f, Vec4{1, 0, 0, 1}},
                    {1.0f, Vec4{0, 1, 0, 1}} });
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    EXPECT_EQ(w.Mode(), VfxCurveBindingMode::None);
}

TEST(VfxCurvesEditorWidget, BindRamp_CopiesStopsToMirrorSorted) {
    VFXColorRamp r;
    r.AddStop(1.0f, 1, 1, 1, 1);
    r.AddStop(0.0f, 0, 0, 0, 1);
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    ASSERT_EQ(w.Stops().size(), 2u);
    EXPECT_LT(w.Stops()[0].t, w.Stops()[1].t);
    EXPECT_EQ(w.Mode(), VfxCurveBindingMode::Ramp);
}

// ─────────────────────────────────────────────────────────────────────────────
// Snap
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, Snap_RoundsToStep) {
    EXPECT_FLOAT_EQ(VfxCurvesEditorWidget::Snap(0.123f, 0.01f), 0.12f);
    EXPECT_FLOAT_EQ(VfxCurvesEditorWidget::Snap(0.125f, 0.01f), 0.13f);
    EXPECT_FLOAT_EQ(VfxCurvesEditorWidget::Snap(0.5f, 0.0f), 0.5f);   // no-op
}

// ─────────────────────────────────────────────────────────────────────────────
// Scalar mutations
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, AddKey_InsertsSortedAndMarksDirty) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);

    const size_t i0 = w.AddKey(0.5f, 2.0f, /*snap*/ false);
    const size_t i1 = w.AddKey(0.0f, 1.0f, false);
    const size_t i2 = w.AddKey(1.0f, 0.0f, false);
    EXPECT_EQ(w.Keys().size(), 3u);
    EXPECT_FLOAT_EQ(w.Keys()[0].time, 0.0f);
    EXPECT_FLOAT_EQ(w.Keys()[1].time, 0.5f);
    EXPECT_FLOAT_EQ(w.Keys()[2].time, 1.0f);
    EXPECT_EQ(i1, 0u);  // new earliest
    EXPECT_EQ(i0, 1u);  // now middle after i1 inserted
    EXPECT_EQ(i2, 2u);
    EXPECT_TRUE(w.IsDirty());
}

TEST(VfxCurvesEditorWidget, AddKey_SnapRoundsTimeAndValue) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.snapTime  = 0.1f;
    w.snapValue = 0.25f;
    const size_t i = w.AddKey(0.373f, 0.87f, true);
    ASSERT_NE(i, static_cast<size_t>(-1));
    EXPECT_FLOAT_EQ(w.Keys()[i].time,  0.4f);
    EXPECT_FLOAT_EQ(w.Keys()[i].value, 0.75f);
}

TEST(VfxCurvesEditorWidget, AddKey_WrongModeReturnsSentinel) {
    VfxCurvesEditorWidget w;
    EXPECT_EQ(w.AddKey(0.0f, 0.0f), static_cast<size_t>(-1));
}

TEST(VfxCurvesEditorWidget, AddKey_NaNRejected) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    EXPECT_EQ(w.AddKey(std::nanf(""), 0.0f), static_cast<size_t>(-1));
    EXPECT_EQ(w.AddKey(0.0f, std::nanf("")), static_cast<size_t>(-1));
}

TEST(VfxCurvesEditorWidget, MoveKey_CrossesNeighbourAndResortsIndex) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.AddKey(0.1f, 1.0f, false);
    w.AddKey(0.3f, 2.0f, false);
    w.AddKey(0.5f, 3.0f, false);

    // Move key at index 0 (time=0.1) to past time=0.4 — should land between
    // indices 1 and 2.
    const size_t newIdx = w.MoveKey(0, 0.4f, 1.0f, false);
    EXPECT_EQ(newIdx, 1u);
    EXPECT_FLOAT_EQ(w.Keys()[0].time, 0.3f);
    EXPECT_FLOAT_EQ(w.Keys()[1].time, 0.4f);
    EXPECT_FLOAT_EQ(w.Keys()[2].time, 0.5f);
}

TEST(VfxCurvesEditorWidget, MoveKey_OutOfRangeReturnsSentinel) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    EXPECT_EQ(w.MoveKey(0, 0.1f, 0.0f), static_cast<size_t>(-1));
}

TEST(VfxCurvesEditorWidget, RemoveKey_Succeeds) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.AddKey(0.1f, 1.0f, false);
    w.AddKey(0.2f, 2.0f, false);
    EXPECT_TRUE(w.RemoveKey(0));
    EXPECT_EQ(w.Keys().size(), 1u);
    EXPECT_FLOAT_EQ(w.Keys()[0].time, 0.2f);
}

TEST(VfxCurvesEditorWidget, SetInterpolation_ProxiesToCurve) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    EXPECT_TRUE(w.SetInterpolation(InterpolationMode::Cubic));
    EXPECT_EQ(c.GetInterpolation(), InterpolationMode::Cubic);
    EXPECT_TRUE(w.IsDirty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback firing
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, OnCurveMutated_FiresOnAddMoveRemove) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    int calls = 0;
    w.onCurveMutated = [&]() { ++calls; };
    w.AddKey(0.1f, 1.0f, false);
    w.MoveKey(0, 0.2f, 1.0f, false);
    w.RemoveKey(0);
    w.SetInterpolation(InterpolationMode::Cubic);
    EXPECT_EQ(calls, 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Ramp mutations
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, AddStop_SortsAndRespectsMax) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);

    // Fill to kMaxStops-2 in reverse order.
    for (int i = 0; i < VFXColorRamp::kMaxStops; ++i) {
        const float t = static_cast<float>(VFXColorRamp::kMaxStops - 1 - i) /
                         static_cast<float>(VFXColorRamp::kMaxStops);
        EXPECT_NE(w.AddStop(t, 1, 1, 1, 1, false), static_cast<size_t>(-1));
    }
    EXPECT_EQ(w.Stops().size(),
              static_cast<size_t>(VFXColorRamp::kMaxStops));
    // Sorted ascending.
    for (size_t i = 1; i < w.Stops().size(); ++i) {
        EXPECT_LE(w.Stops()[i - 1].t, w.Stops()[i].t);
    }
    // Overflow stop rejected.
    EXPECT_EQ(w.AddStop(0.999f, 0, 0, 0, 1, false), static_cast<size_t>(-1));
}

TEST(VfxCurvesEditorWidget, MoveStop_ReordersRespectingCrossings) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    w.AddStop(0.0f, 1, 0, 0, 1, false);
    w.AddStop(0.5f, 0, 1, 0, 1, false);
    w.AddStop(1.0f, 0, 0, 1, 1, false);

    // Move stop at index 0 from t=0 to t=0.7 → past index 1.
    const size_t newIdx = w.MoveStop(0, 0.7f, false);
    EXPECT_EQ(newIdx, 1u);
    EXPECT_FLOAT_EQ(w.Stops()[0].t, 0.5f);
    EXPECT_FLOAT_EQ(w.Stops()[1].t, 0.7f);
    EXPECT_FLOAT_EQ(w.Stops()[2].t, 1.0f);
    // Colour preserved on the moved stop (originally red).
    EXPECT_FLOAT_EQ(w.Stops()[1].r, 1.0f);
}

TEST(VfxCurvesEditorWidget, SetStopColor_KeepsPosition) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    w.AddStop(0.5f, 0.1f, 0.2f, 0.3f, 1.0f, false);
    EXPECT_TRUE(w.SetStopColor(0, 0.9f, 0.8f, 0.7f, 0.6f));
    EXPECT_FLOAT_EQ(w.Stops()[0].t, 0.5f);
    EXPECT_FLOAT_EQ(w.Stops()[0].r, 0.9f);
    EXPECT_FLOAT_EQ(w.Stops()[0].a, 0.6f);
}

TEST(VfxCurvesEditorWidget, RemoveStop_Shifts) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    w.AddStop(0.0f, 1, 0, 0, 1, false);
    w.AddStop(1.0f, 0, 1, 0, 1, false);
    EXPECT_TRUE(w.RemoveStop(0));
    ASSERT_EQ(w.Stops().size(), 1u);
    EXPECT_FLOAT_EQ(w.Stops()[0].t, 1.0f);
}

TEST(VfxCurvesEditorWidget, CommitStops_UpdatesRampDirectly) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    w.AddStop(0.25f, 0.5f, 0.5f, 0.5f, 1.0f, false);
    // Ramp was empty → now 1 stop committed.
    ASSERT_EQ(r.stopCount, 1);
    EXPECT_FLOAT_EQ(r.stops[0].t, 0.25f);
    EXPECT_FLOAT_EQ(r.stops[0].r, 0.5f);
}

TEST(VfxCurvesEditorWidget, OnRampMutated_FiresOnlyOnRampMutations) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    int calls = 0;
    w.onRampMutated = [&]() { ++calls; };
    w.AddStop(0.1f, 1, 1, 1, 1, false);
    w.MoveStop(0, 0.2f, false);
    w.SetStopColor(0, 1, 0, 0, 1);
    w.RemoveStop(0);
    EXPECT_EQ(calls, 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Dirty / re-bake debounce
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, ShouldRebakeNow_CleanReturnsFalse) {
    VfxCurvesEditorWidget w;
    EXPECT_FALSE(w.ShouldRebakeNow(10.0f));
}

TEST(VfxCurvesEditorWidget, ShouldRebakeNow_DirtyAndNotDragging_True) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.AddKey(0.1f, 1.0f, false);
    EXPECT_TRUE(w.ShouldRebakeNow(0.0f));
}

TEST(VfxCurvesEditorWidget, ShouldRebakeNow_DraggingRespectsDebounce) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.debounceIdleSeconds = 0.1f;
    w.AddKey(0.1f, 1.0f, false);

    w.BeginDrag(0);
    EXPECT_FALSE(w.ShouldRebakeNow(0.0f));
    EXPECT_FALSE(w.ShouldRebakeNow(0.09f));
    EXPECT_TRUE(w.ShouldRebakeNow(0.11f));
    w.EndDrag();
    EXPECT_TRUE(w.ShouldRebakeNow(0.0f));
}

TEST(VfxCurvesEditorWidget, ClearDirty_Resets) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.AddKey(0.1f, 1.0f, false);
    w.ClearDirty();
    EXPECT_FALSE(w.IsDirty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Viewport math
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, Viewport_WorldToScreenRoundTrip) {
    VfxCurveViewport vp;
    // Default: visible t in [0,1], value window [0,1] (panY=0.5, zoomY=1).
    const float sx = vp.WorldToScreenX(0.5f, kCanvas);
    const float sy = vp.WorldToScreenY(0.5f, kCanvas);
    EXPECT_NEAR(sx, kCanvas.x + kCanvas.width * 0.5f, 0.001f);
    EXPECT_NEAR(sy, kCanvas.y + kCanvas.height * 0.5f, 0.001f);

    const float bx = vp.ScreenToWorldX(sx, kCanvas);
    const float by = vp.ScreenToWorldY(sy, kCanvas);
    EXPECT_NEAR(bx, 0.5f, 0.001f);
    EXPECT_NEAR(by, 0.5f, 0.001f);
}

TEST(VfxCurvesEditorWidget, Zoom_AnchorsPointUnderCursor) {
    VfxCurvesEditorWidget w;
    const float anchorSx = kCanvas.x + kCanvas.width * 0.3f;
    const float anchorSy = kCanvas.y + kCanvas.height * 0.4f;

    const float worldBeforeX = w.viewport.ScreenToWorldX(anchorSx, kCanvas);
    const float worldBeforeY = w.viewport.ScreenToWorldY(anchorSy, kCanvas);

    w.Zoom(2.0f, anchorSx, anchorSy, kCanvas);

    // After zoom: the same screen coord still maps to the same world coord.
    const float worldAfterX = w.viewport.ScreenToWorldX(anchorSx, kCanvas);
    const float worldAfterY = w.viewport.ScreenToWorldY(anchorSy, kCanvas);
    EXPECT_NEAR(worldAfterX, worldBeforeX, 0.001f);
    EXPECT_NEAR(worldAfterY, worldBeforeY, 0.001f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout + hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxCurvesEditorWidget, Layout_FloatMode_EmitsKeyDots) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.AddKey(0.2f, 0.5f, false);
    w.AddKey(0.7f, 0.8f, false);

    const auto layout = w.ComputeLayout(kCanvas);
    int canvas = 0, dots = 0;
    for (const auto& it : layout) {
        if (it.type == VfxCurveItemType::Canvas) ++canvas;
        if (it.type == VfxCurveItemType::KeyDot) ++dots;
    }
    EXPECT_EQ(canvas, 1);
    EXPECT_EQ(dots, 2);
}

TEST(VfxCurvesEditorWidget, Layout_RampMode_EmitsRibbonAndStops) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    w.AddStop(0.1f, 1, 0, 0, 1, false);
    w.AddStop(0.9f, 0, 0, 1, 1, false);

    const auto layout = w.ComputeLayout(kCanvas);
    int canvas = 0, ribbons = 0, stops = 0;
    for (const auto& it : layout) {
        if (it.type == VfxCurveItemType::Canvas)     ++canvas;
        if (it.type == VfxCurveItemType::RampRibbon) ++ribbons;
        if (it.type == VfxCurveItemType::RampStop)   ++stops;
    }
    EXPECT_EQ(canvas, 1);
    EXPECT_EQ(ribbons, 1);
    EXPECT_EQ(stops, 2);
}

TEST(VfxCurvesEditorWidget, HitTest_KeyDotBeatsCanvas) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    w.AddKey(0.5f, 0.5f, false);

    const auto layout = w.ComputeLayout(kCanvas);
    const VfxCurveLayoutItem* dot = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxCurveItemType::KeyDot) { dot = &it; break; }
    }
    ASSERT_NE(dot, nullptr);
    const float cx = dot->rect.x + dot->rect.width * 0.5f;
    const float cy = dot->rect.y + dot->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxCurveItemType::KeyDot);
}

TEST(VfxCurvesEditorWidget, HitTest_EmptyCanvasReturnsCanvas) {
    VFXCurve c;
    VfxCurvesEditorWidget w;
    w.BindFloat(&c);
    // No keys.
    const auto layout = w.ComputeLayout(kCanvas);
    const auto hit = w.HitTest(layout, kCanvas.x + 10.0f, kCanvas.y + 10.0f);
    EXPECT_EQ(hit.type, VfxCurveItemType::Canvas);
}

TEST(VfxCurvesEditorWidget, HitTest_StopBeatsRibbon) {
    VFXColorRamp r;
    VfxCurvesEditorWidget w;
    w.BindRamp(&r);
    w.AddStop(0.5f, 1, 1, 1, 1, false);

    const auto layout = w.ComputeLayout(kCanvas);
    const VfxCurveLayoutItem* stop = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxCurveItemType::RampStop) { stop = &it; break; }
    }
    ASSERT_NE(stop, nullptr);
    const float cx = stop->rect.x + stop->rect.width * 0.5f;
    const float cy = stop->rect.y + stop->rect.height * 0.5f;
    const auto hit = w.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxCurveItemType::RampStop);
    EXPECT_EQ(hit.index, 0u);
}
