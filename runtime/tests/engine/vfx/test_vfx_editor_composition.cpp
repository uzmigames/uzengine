/// Unit tests for phase 11.13.0: `VfxEditorComposition` layout +
/// center-mode + focus routing + undo-stack propagation + per-frame
/// tick.
///
/// Covers:
///   - `VfxComputeEditorLayout` produces sane rects at 1920×1080,
///     1280×720, and degenerate sizes (0×0, portrait).
///   - Layout fractions clamp when the sum exceeds 1.0 (config mistake
///     doesn't blow up the layout).
///   - Menu bar + timeline bar heights respected; leftover space is
///     the viewport reserve column.
///   - `SetCenterMode` fires the change callback only on actual mode
///     change.
///   - `SetFocusedPanel` auto-picks the center mode from the panel
///     identity (Curves focus → `Curves` mode, etc.).
///   - Keybinding handler inherits the composition's bindings when
///     `BindKeybindings` is called (stack panel, parameters, curves,
///     timeline, picker, undo stack all wired through).
///   - `Tick(simDt)` forwards to drainer + timeline auto-loop.

#include <gtest/gtest.h>

#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/editor/undo/vfx_undo_stack.h"
#include "engine/vfx/editor/vfx_curves_editor_widget.h"
#include "engine/vfx/editor/vfx_editor_composition.h"
#include "engine/vfx/editor/vfx_editor_keybindings.h"
#include "engine/vfx/editor/vfx_module_picker_dialog.h"
#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"
#include "engine/vfx/editor/vfx_thumbnail_drainer.h"
#include "engine/vfx/editor/vfx_thumbnail_save_ext.h"
#include "engine/vfx/editor/vfx_timeline_scrubber_widget.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/stack/vfx_stack.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Layout math
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorLayout, DefaultFractions_1920x1080) {
    const VfxEditorLayoutConfig cfg{};
    const auto r = VfxComputeEditorLayout(1920.0f, 1080.0f, cfg);

    // Menu bar spans full width, sits at the top.
    EXPECT_FLOAT_EQ(r.menuBar.x, 0.0f);
    EXPECT_FLOAT_EQ(r.menuBar.y, 0.0f);
    EXPECT_FLOAT_EQ(r.menuBar.width, 1920.0f);
    EXPECT_FLOAT_EQ(r.menuBar.height, cfg.menuBarHeight);

    // Timeline bar spans full width at the bottom.
    EXPECT_FLOAT_EQ(r.timelineBar.x, 0.0f);
    EXPECT_FLOAT_EQ(r.timelineBar.width, 1920.0f);
    EXPECT_FLOAT_EQ(r.timelineBar.height, cfg.timelineBarHeight);
    EXPECT_FLOAT_EQ(r.timelineBar.y + r.timelineBar.height, 1080.0f);

    // Vertical stripe between bars.
    const float midTop    = cfg.menuBarHeight;
    const float midHeight = 1080.0f - cfg.menuBarHeight - cfg.timelineBarHeight;
    EXPECT_FLOAT_EQ(r.stackPanel.y, midTop);
    EXPECT_FLOAT_EQ(r.stackPanel.height, midHeight);

    // Horizontal widths.
    EXPECT_FLOAT_EQ(r.stackPanel.width,  1920.0f * cfg.stackPanelWidthFrac);
    EXPECT_FLOAT_EQ(r.centerPanel.width, 1920.0f * cfg.centerPanelWidthFrac);
    EXPECT_FLOAT_EQ(r.rightPanel.width,  1920.0f * cfg.rightPanelWidthFrac);

    // Reserve fills the rest.
    EXPECT_FLOAT_EQ(
        r.viewportReserve.width,
        1920.0f - r.stackPanel.width - r.centerPanel.width - r.rightPanel.width);

    // Column order sums to screen width.
    EXPECT_NEAR(r.stackPanel.width + r.centerPanel.width +
                    r.viewportReserve.width + r.rightPanel.width,
                 1920.0f, 0.001f);
}

TEST(VfxEditorLayout, 1280x720_Degenerate0x0_StablePortrait) {
    const VfxEditorLayoutConfig cfg{};

    const auto r720 = VfxComputeEditorLayout(1280.0f, 720.0f, cfg);
    EXPECT_FLOAT_EQ(r720.menuBar.width, 1280.0f);
    EXPECT_GT(r720.stackPanel.width, 0.0f);

    // 0x0 returns empty rects — no crash.
    const auto r0 = VfxComputeEditorLayout(0.0f, 0.0f, cfg);
    EXPECT_FLOAT_EQ(r0.menuBar.width, 0.0f);
    EXPECT_FLOAT_EQ(r0.stackPanel.height, 0.0f);

    // Portrait aspect — same math, narrow panels.
    const auto rPortrait = VfxComputeEditorLayout(540.0f, 960.0f, cfg);
    EXPECT_FLOAT_EQ(rPortrait.menuBar.width, 540.0f);
    EXPECT_FLOAT_EQ(rPortrait.stackPanel.width, 540.0f * cfg.stackPanelWidthFrac);
}

TEST(VfxEditorLayout, FractionsExceedingOne_RescaleProportionally) {
    VfxEditorLayoutConfig cfg{};
    cfg.stackPanelWidthFrac  = 0.5f;
    cfg.centerPanelWidthFrac = 0.5f;
    cfg.rightPanelWidthFrac  = 0.5f;   // total 1.5 → clamp

    const auto r = VfxComputeEditorLayout(1500.0f, 900.0f, cfg);
    // After proportional rescale, every column is 1/3 of width.
    EXPECT_NEAR(r.stackPanel.width,  500.0f, 0.001f);
    EXPECT_NEAR(r.centerPanel.width, 500.0f, 0.001f);
    EXPECT_NEAR(r.rightPanel.width,  500.0f, 0.001f);
    EXPECT_NEAR(r.viewportReserve.width, 0.0f, 0.001f);
}

TEST(VfxEditorLayout, MenuBarTallerThanScreen_ClampsHeight) {
    VfxEditorLayoutConfig cfg{};
    cfg.menuBarHeight = 100.0f;  // larger than screen
    const auto r = VfxComputeEditorLayout(400.0f, 50.0f, cfg);
    EXPECT_FLOAT_EQ(r.menuBar.height, 50.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Center mode
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorComposition, SetCenterMode_FiresCallbackOnlyOnChange) {
    VfxEditorComposition c;
    int calls = 0;
    VfxCenterMode last = VfxCenterMode::ModuleParams;
    c.onCenterModeChanged = [&](VfxCenterMode m) { ++calls; last = m; };

    c.SetCenterMode(VfxCenterMode::ModuleParams);  // unchanged
    EXPECT_EQ(calls, 0);

    c.SetCenterMode(VfxCenterMode::Curves);
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(last, VfxCenterMode::Curves);

    c.SetCenterMode(VfxCenterMode::Curves);        // same again
    EXPECT_EQ(calls, 1);

    c.SetCenterMode(VfxCenterMode::NodeGraph);
    EXPECT_EQ(calls, 2);
}

TEST(VfxEditorComposition, CenterModeName_StableLabels) {
    EXPECT_STREQ(VfxCenterModeName(VfxCenterMode::ModuleParams), "ModuleParams");
    EXPECT_STREQ(VfxCenterModeName(VfxCenterMode::Curves),       "Curves");
    EXPECT_STREQ(VfxCenterModeName(VfxCenterMode::Parameters),   "Parameters");
    EXPECT_STREQ(VfxCenterModeName(VfxCenterMode::NodeGraph),    "NodeGraph");
}

// ─────────────────────────────────────────────────────────────────────────────
// Focus → center-mode auto-pick
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorComposition, SetFocusedPanel_AutoPicksCenterMode) {
    VfxEditorComposition c;

    c.SetFocusedPanel(VfxFocusedPanel::Curves);
    EXPECT_EQ(c.CenterMode(), VfxCenterMode::Curves);

    c.SetFocusedPanel(VfxFocusedPanel::Parameters);
    EXPECT_EQ(c.CenterMode(), VfxCenterMode::Parameters);

    c.SetFocusedPanel(VfxFocusedPanel::NodeGraph);
    EXPECT_EQ(c.CenterMode(), VfxCenterMode::NodeGraph);

    c.SetFocusedPanel(VfxFocusedPanel::Stack);
    EXPECT_EQ(c.CenterMode(), VfxCenterMode::ModuleParams);

    // Timeline / None don't change the center mode — leave the last pick.
    c.SetCenterMode(VfxCenterMode::Curves);
    c.SetFocusedPanel(VfxFocusedPanel::Timeline);
    EXPECT_EQ(c.CenterMode(), VfxCenterMode::Curves);
}

// ─────────────────────────────────────────────────────────────────────────────
// Keybinding propagation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorComposition, BindKeybindings_PropagatesAllWidgetPointers) {
    VfxStackPanelWidget stack;
    VfxParameterInspectorWidget params;
    VfxCurvesEditorWidget curves;
    VfxTimelineScrubberWidget timeline;
    VfxModulePickerDialog picker;
    VfxUndoStack undo;
    VfxEditorKeybindingHandler keys;

    VfxEditorComposition c;
    c.BindStackPanel(&stack);
    c.BindParameterInspector(&params);
    c.BindCurvesEditor(&curves);
    c.BindTimelineScrubber(&timeline);
    c.BindModulePicker(&picker);
    c.BindUndoStack(&undo);
    c.BindKeybindings(&keys);

    // Every Bind* on the composition maps to a Bind on the handler —
    // the handler's diagnostic counters stay zero until keys fire, but
    // we can verify routing indirectly by exercising one key path.
    keys.SetFocusedPanel(VfxFocusedPanel::Stack);
    // Put one command on the stack so Ctrl+Z has something to pop.
    struct C final : IVfxUndoCommand {
        int* s; int d;
        C(int* ss, int dd) : s(ss), d(dd) {}
        void Apply()  override { *s += d; }
        void Revert() override { *s -= d; }
        const char* Name() const noexcept override { return "C"; }
    };
    int state = 0;
    state += 5;
    undo.Push(std::unique_ptr<IVfxUndoCommand>(new C(&state, 5)));

    EXPECT_TRUE(keys.ProcessKeyEvent(VfxEditorKeybindingHandler::Key::Z,
                                        true, false));
    EXPECT_EQ(state, 0);
    EXPECT_EQ(keys.UndoCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick: drainer + timeline auto-loop
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorComposition, Tick_ForwardsToDrainerAndTimelineAutoLoop) {
    VfxThumbnailCaptureQueue q;
    VfxThumbnailDrainer drainer;
    drainer.Bind(&q);

    VfxPreviewClockConfig ccfg; ccfg.maxDuration = 1.0f;
    VfxPreviewClock clock(ccfg);
    clock.Scrub(1.0f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget timeline;
    timeline.Bind(&clock);

    int restarts = 0;
    timeline.onRestartRequested = [&] { ++restarts; };

    VfxEditorComposition c;
    c.BindThumbnailDrainer(&drainer);
    c.BindTimelineScrubber(&timeline);

    // With clock at maxDuration + loop on (default true) the composition
    // Tick should fire an auto-loop restart.
    c.Tick(0.016f);
    EXPECT_EQ(restarts, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Focus state shadow (no keybindings bound)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorComposition, UnboundKeys_FocusTrackedLocally) {
    VfxEditorComposition c;
    EXPECT_EQ(c.FocusedPanel(), VfxFocusedPanel::None);
    c.SetFocusedPanel(VfxFocusedPanel::Stack);
    EXPECT_EQ(c.FocusedPanel(), VfxFocusedPanel::Stack);
}
