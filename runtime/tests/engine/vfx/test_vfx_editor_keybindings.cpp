/// Unit tests for phase 11.10.0: `VfxEditorKeybindingHandler`.
///
/// Covers:
///   - Global shortcuts: Ctrl+Z / Ctrl+Y / Ctrl+Shift+Z route to the
///     undo stack; Ctrl+S fires `onSaveRequested`; Ctrl+F fires
///     `onFocusSearch` only for panels with search inputs; Escape
///     clears selection per-panel; Tab cycles focus through the major
///     panels.
///   - Timeline focus: Space toggle play/pause, arrows seek ±0.1s,
///     Shift+arrows seek ±1s, `.` / `,` step, `1-5` quick-set speed.
///   - Stack focus: Delete fires `onModuleRemoveRequested`, Ctrl+D
///     fires `onModuleDuplicateRequested`.
///   - Curves focus: arrows nudge the selected key by snap step;
///     Delete removes the selected key / stop.
///   - Parameters focus: F2 fires `onRenameRequested`.
///   - Picker modality: when open, Escape closes; arrows navigate;
///     Enter confirms; other keys consumed by the picker.

#include <gtest/gtest.h>

#include "engine/vfx/curves/vfx_curve.h"
#include "engine/vfx/editor/undo/vfx_undo_stack.h"
#include "engine/vfx/editor/vfx_curves_editor_widget.h"
#include "engine/vfx/editor/vfx_editor_keybindings.h"
#include "engine/vfx/editor/vfx_module_picker_dialog.h"
#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/editor/vfx_preview_clock.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"
#include "engine/vfx/editor/vfx_timeline_scrubber_widget.h"
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/stack/vfx_stack.h"

#include <memory>
#include <string>

using namespace uz;

namespace {

using Key = VfxEditorKeybindingHandler::Key;

/// Minimal test command — flips a counter on Apply/Revert so we can
/// verify undo/redo routing without touching real editor data.
class CounterCommand final : public IVfxUndoCommand {
public:
    CounterCommand(int* slot, int delta) noexcept : m_slot(slot), m_delta(delta) {}
    void Apply()  override { if (m_slot) *m_slot += m_delta; }
    void Revert() override { if (m_slot) *m_slot -= m_delta; }
    [[nodiscard]] const char* Name() const noexcept override { return "Counter"; }
private:
    int* m_slot;
    int  m_delta;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Focus cycling
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, TabCyclesMajorPanels) {
    VfxEditorKeybindingHandler h;
    // From None → seed at Stack.
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Tab, false, false));
    EXPECT_EQ(h.FocusedPanel(), VfxFocusedPanel::Stack);
    h.ProcessKeyEvent(Key::Tab, false, false);
    EXPECT_EQ(h.FocusedPanel(), VfxFocusedPanel::Parameters);
    h.ProcessKeyEvent(Key::Tab, false, false);
    EXPECT_EQ(h.FocusedPanel(), VfxFocusedPanel::Curves);
    h.ProcessKeyEvent(Key::Tab, false, false);
    EXPECT_EQ(h.FocusedPanel(), VfxFocusedPanel::Timeline);
    h.ProcessKeyEvent(Key::Tab, false, false);
    EXPECT_EQ(h.FocusedPanel(), VfxFocusedPanel::Stack);
}

// ─────────────────────────────────────────────────────────────────────────────
// Undo / redo
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, CtrlZ_Undo_CtrlY_Redo) {
    VfxUndoStack stack;
    int state = 0;
    state += 5;
    stack.Push(std::make_unique<CounterCommand>(&state, 5));

    VfxEditorKeybindingHandler h;
    h.BindUndoStack(&stack);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Z, true, false));  // Ctrl+Z
    EXPECT_EQ(state, 0);
    EXPECT_EQ(h.UndoCount(), 1u);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Y, true, false));  // Ctrl+Y
    EXPECT_EQ(state, 5);
    EXPECT_EQ(h.RedoCount(), 1u);
}

TEST(VfxEditorKeybindings, CtrlShiftZ_AlsoRedoes) {
    VfxUndoStack stack;
    int state = 0;
    state += 3;
    stack.Push(std::make_unique<CounterCommand>(&state, 3));
    stack.Undo();
    EXPECT_EQ(state, 0);

    VfxEditorKeybindingHandler h;
    h.BindUndoStack(&stack);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Z, true, true));   // Ctrl+Shift+Z
    EXPECT_EQ(state, 3);
    EXPECT_EQ(h.RedoCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Save / Search callbacks
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, CtrlS_FiresSaveCallback) {
    VfxEditorKeybindingHandler h;
    int calls = 0;
    h.onSaveRequested = [&] { ++calls; };
    EXPECT_TRUE(h.ProcessKeyEvent(Key::S, true, false));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(h.SaveCount(), 1u);
}

TEST(VfxEditorKeybindings, CtrlF_FiresOnlyForSearchablePanels) {
    VfxEditorKeybindingHandler h;
    int calls = 0;
    VfxFocusedPanel last = VfxFocusedPanel::None;
    h.onFocusSearch = [&](VfxFocusedPanel p) { ++calls; last = p; };

    h.SetFocusedPanel(VfxFocusedPanel::Curves);   // no search input
    EXPECT_TRUE(h.ProcessKeyEvent(Key::F, true, false));
    EXPECT_EQ(calls, 0);

    h.SetFocusedPanel(VfxFocusedPanel::Stack);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::F, true, false));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(last, VfxFocusedPanel::Stack);

    h.SetFocusedPanel(VfxFocusedPanel::Parameters);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::F, true, false));
    EXPECT_EQ(calls, 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Escape clears selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, Escape_ClearsStackSelection) {
    VfxStack stk;
    VfxModuleInstance mod;
    mod.moduleTypeName = "Gravity";
    mod.instanceId = "g1";
    stk.GetEntry(VfxStage::ParticleUpdate).Add(mod);

    VfxStackPanelWidget panel;
    panel.Bind(&stk);
    panel.SelectModule("g1");
    EXPECT_FALSE(panel.SelectedInstanceId().empty());

    VfxEditorKeybindingHandler h;
    h.BindStackPanel(&panel);
    h.SetFocusedPanel(VfxFocusedPanel::Stack);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Escape, false, false));
    EXPECT_TRUE(panel.SelectedInstanceId().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Timeline shortcuts
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, Timeline_Space_TogglesPlayPause) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 10.0f;
    VfxPreviewClock clock(cfg);
    clock.Pause();

    VfxTimelineScrubberWidget ts;
    ts.Bind(&clock);

    VfxEditorKeybindingHandler h;
    h.BindTimelineScrubber(&ts);
    h.SetFocusedPanel(VfxFocusedPanel::Timeline);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Space, false, false));
    EXPECT_TRUE(clock.IsPlaying());
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Space, false, false));
    EXPECT_FALSE(clock.IsPlaying());
}

TEST(VfxEditorKeybindings, Timeline_ArrowsSeek) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 10.0f;
    VfxPreviewClock clock(cfg);
    clock.Scrub(5.0f);
    (void)clock.Tick(0.0f);

    VfxTimelineScrubberWidget ts;
    ts.Bind(&clock);

    VfxEditorKeybindingHandler h;
    h.BindTimelineScrubber(&ts);
    h.SetFocusedPanel(VfxFocusedPanel::Timeline);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Right, false, false));
    (void)clock.Tick(0.0f);
    EXPECT_NEAR(clock.GetCurrentTime(), 5.1f, 1e-5f);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Left, false, true));  // Shift+Left = -1s
    (void)clock.Tick(0.0f);
    EXPECT_NEAR(clock.GetCurrentTime(), 4.1f, 1e-5f);
}

TEST(VfxEditorKeybindings, Timeline_SpeedPresets) {
    VfxPreviewClockConfig cfg; cfg.maxDuration = 10.0f;
    VfxPreviewClock clock(cfg);
    VfxTimelineScrubberWidget ts;
    ts.Bind(&clock);

    VfxEditorKeybindingHandler h;
    h.BindTimelineScrubber(&ts);
    h.SetFocusedPanel(VfxFocusedPanel::Timeline);

    h.ProcessKeyEvent(Key::N1, false, false); EXPECT_FLOAT_EQ(ts.Speed(), 0.25f);
    h.ProcessKeyEvent(Key::N2, false, false); EXPECT_FLOAT_EQ(ts.Speed(), 0.5f);
    h.ProcessKeyEvent(Key::N3, false, false); EXPECT_FLOAT_EQ(ts.Speed(), 1.0f);
    h.ProcessKeyEvent(Key::N4, false, false); EXPECT_FLOAT_EQ(ts.Speed(), 2.0f);
    h.ProcessKeyEvent(Key::N5, false, false); EXPECT_FLOAT_EQ(ts.Speed(), 4.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack shortcuts
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, Stack_DeleteFiresRemoveCallback) {
    VfxStack stk;
    VfxModuleInstance mod; mod.moduleTypeName = "Gravity"; mod.instanceId = "g1";
    stk.GetEntry(VfxStage::ParticleUpdate).Add(mod);

    VfxStackPanelWidget panel;
    panel.Bind(&stk);
    panel.SelectModule("g1");

    int calls = 0;
    std::string target;
    panel.onModuleRemoveRequested = [&](const std::string& id) {
        ++calls; target = id;
    };

    VfxEditorKeybindingHandler h;
    h.BindStackPanel(&panel);
    h.SetFocusedPanel(VfxFocusedPanel::Stack);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Delete, false, false));
    EXPECT_EQ(calls, 1);
    EXPECT_EQ(target, "g1");
}

TEST(VfxEditorKeybindings, Stack_CtrlD_FiresDuplicateCallback) {
    VfxStack stk;
    VfxModuleInstance mod; mod.moduleTypeName = "Gravity"; mod.instanceId = "g1";
    stk.GetEntry(VfxStage::ParticleUpdate).Add(mod);

    VfxStackPanelWidget panel;
    panel.Bind(&stk);
    panel.SelectModule("g1");

    int calls = 0;
    panel.onModuleDuplicateRequested = [&](const std::string&) { ++calls; };

    VfxEditorKeybindingHandler h;
    h.BindStackPanel(&panel);
    h.SetFocusedPanel(VfxFocusedPanel::Stack);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::D, true, false));
    EXPECT_EQ(calls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Curves shortcuts
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, Curves_Arrows_NudgeSelectedKey) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.snapTime  = 0.1f;
    w.snapValue = 0.1f;
    w.AddKey(0.5f, 0.5f, false);
    w.SelectIndex(0);

    VfxEditorKeybindingHandler h;
    h.BindCurvesEditor(&w);
    h.SetFocusedPanel(VfxFocusedPanel::Curves);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Right, false, false));
    EXPECT_NEAR(w.Keys()[0].time, 0.6f, 1e-5f);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Up, false, false));
    EXPECT_NEAR(w.Keys()[0].value, 0.6f, 1e-5f);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Left, false, false));
    EXPECT_NEAR(w.Keys()[0].time, 0.5f, 1e-5f);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Down, false, false));
    EXPECT_NEAR(w.Keys()[0].value, 0.5f, 1e-5f);
}

TEST(VfxEditorKeybindings, Curves_Delete_RemovesKey) {
    VFXCurve curve;
    VfxCurvesEditorWidget w;
    w.BindFloat(&curve);
    w.AddKey(0.2f, 1.0f, false);
    w.AddKey(0.8f, 2.0f, false);
    w.SelectIndex(0);

    VfxEditorKeybindingHandler h;
    h.BindCurvesEditor(&w);
    h.SetFocusedPanel(VfxFocusedPanel::Curves);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Delete, false, false));
    ASSERT_EQ(w.Keys().size(), 1u);
    EXPECT_FLOAT_EQ(w.Keys()[0].time, 0.8f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameters shortcuts
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, Parameters_F2_FiresRename) {
    VfxAttributeStore store;
    VfxStack stk;
    VfxParameterInspectorWidget panel;
    panel.Bind(&store, &stk);

    VfxEditorKeybindingHandler h;
    h.BindParameterInspector(&panel);
    h.SetFocusedPanel(VfxFocusedPanel::Parameters);

    int calls = 0;
    h.onRenameRequested = [&] { ++calls; };

    EXPECT_TRUE(h.ProcessKeyEvent(Key::F2, false, false));
    EXPECT_EQ(calls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Picker modality
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorKeybindings, Picker_Escape_Closes) {
    VfxModulePickerDialog picker;
    picker.Open(VfxStage::ParticleUpdate, 0, 0);

    VfxEditorKeybindingHandler h;
    h.BindModulePicker(&picker);

    EXPECT_TRUE(h.ProcessKeyEvent(Key::Escape, false, false));
    EXPECT_FALSE(picker.IsOpen());
}

TEST(VfxEditorKeybindings, Picker_ArrowsNavigate_EnterConfirms) {
    VfxModulePickerDialog picker;
    picker.Open(VfxStage::ParticleUpdate, 0, 0);

    int calls = 0;
    picker.onModuleChosen = [&](const std::string&, VfxStage) { ++calls; };

    VfxEditorKeybindingHandler h;
    h.BindModulePicker(&picker);

    const size_t firstIdx = picker.SelectedIndex();
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Down, false, false));
    EXPECT_NE(picker.SelectedIndex(), firstIdx);
    EXPECT_TRUE(h.ProcessKeyEvent(Key::Enter, false, false));
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(picker.IsOpen());
}
