/// Unit tests for phase 11.14.0: `VfxEditorOverlay` Niagara-mode
/// mounting of the `VfxEditorComposition`.
///
/// The overlay is default-constructed (no GPU init) and the Niagara-mode
/// API surface is exercised purely through member function calls. We do
/// not call `BuildUI` here — that path requires a live `TextRenderer`
/// and `BitmapFont` which only Init() can stand up. Visual correctness
/// is verified by the demo app (see docs/engine/25-vfx/52-editor-mount.md).

#include <gtest/gtest.h>

#include <memory>

#include "engine/vfx/editor/undo/vfx_undo_command.h"
#include "engine/vfx/editor/undo/vfx_undo_stack.h"
#include "engine/vfx/editor/vfx_editor_overlay.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Niagara-mode API surface
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMount, DefaultModeIsLegacy) {
    VfxEditorOverlay overlay;
    EXPECT_FALSE(overlay.IsNiagaraMode());
}

TEST(VfxEditorMount, SetNiagaraModeTrueFlipsFlag) {
    VfxEditorOverlay overlay;
    overlay.SetNiagaraMode(true);
    EXPECT_TRUE(overlay.IsNiagaraMode());
}

TEST(VfxEditorMount, SetNiagaraModeFalseClearsFlag) {
    VfxEditorOverlay overlay;
    overlay.SetNiagaraMode(true);
    overlay.SetNiagaraMode(false);
    EXPECT_FALSE(overlay.IsNiagaraMode());
}

TEST(VfxEditorMount, ToggleNiagaraModeFlipsAndReturnsNewState) {
    VfxEditorOverlay overlay;
    EXPECT_TRUE(overlay.ToggleNiagaraMode());
    EXPECT_TRUE(overlay.IsNiagaraMode());
    EXPECT_FALSE(overlay.ToggleNiagaraMode());
    EXPECT_FALSE(overlay.IsNiagaraMode());
}

TEST(VfxEditorMount, ToggleIsIndependentOfVisibility) {
    // The app gates F11 on visibility, but the overlay itself doesn't —
    // toggling Niagara mode while hidden still flips the flag. (Rationale:
    // authors may reconfigure before showing the overlay.)
    VfxEditorOverlay overlay;
    EXPECT_FALSE(overlay.IsVisible());
    overlay.ToggleNiagaraMode();
    EXPECT_TRUE(overlay.IsNiagaraMode());
}

// ─────────────────────────────────────────────────────────────────────────────
// Composition accessor
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMount, CompositionAccessorReturnsSameObject) {
    VfxEditorOverlay overlay;
    VfxEditorComposition& a = overlay.Composition();
    VfxEditorComposition& b = overlay.Composition();
    EXPECT_EQ(&a, &b);
}

TEST(VfxEditorMount, CompositionStartsUnbound) {
    VfxEditorOverlay overlay;
    const auto& comp = overlay.Composition();
    EXPECT_EQ(comp.StackPanel(), nullptr);
    EXPECT_EQ(comp.ParameterInspector(), nullptr);
    EXPECT_EQ(comp.CurvesEditor(), nullptr);
    EXPECT_EQ(comp.TimelineScrubber(), nullptr);
    EXPECT_EQ(comp.ModulePicker(), nullptr);
    EXPECT_EQ(comp.UndoStack(), nullptr);
    EXPECT_EQ(comp.ThumbnailDrainer(), nullptr);
    EXPECT_EQ(comp.Keybindings(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// EnsureCompositionBound
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMount, EnsureCompositionBoundBindsAllWidgets) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();

    const auto& comp = overlay.Composition();
    EXPECT_EQ(comp.StackPanel(),         &overlay.StackPanelWidget());
    EXPECT_EQ(comp.ParameterInspector(), &overlay.ParameterInspector());
    EXPECT_EQ(comp.CurvesEditor(),       &overlay.CurvesEditor());
    EXPECT_EQ(comp.TimelineScrubber(),   &overlay.TimelineScrubber());
    EXPECT_EQ(comp.ModulePicker(),       &overlay.ModulePickerDialog());
    EXPECT_EQ(comp.UndoStack(),          &overlay.UndoStack());
    EXPECT_EQ(comp.ThumbnailDrainer(),   &overlay.ThumbnailDrainer());
    EXPECT_EQ(comp.Keybindings(),        &overlay.KeybindingHandler());
}

TEST(VfxEditorMount, EnsureCompositionBoundIsIdempotent) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();
    VfxStackPanelWidget* stackFirst = overlay.Composition().StackPanel();
    overlay.EnsureCompositionBoundForTest();
    overlay.EnsureCompositionBoundForTest();
    EXPECT_EQ(overlay.Composition().StackPanel(), stackFirst);
}

TEST(VfxEditorMount, EnsureCompositionBoundPropagatesKeybindings) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();

    // VfxEditorComposition::BindKeybindings calls PropagateKeybindings
    // which pushes every widget pointer into the keybindings handler.
    // The handler has no public getters for those pointers (binding is
    // a one-way write), so verify behavioral routing: a queued command
    // on the overlay's undo stack is popped by a Ctrl+Z keystroke.
    struct NoopCommand final : IVfxUndoCommand {
        int* counter;
        explicit NoopCommand(int* c) : counter(c) {}
        void Apply()  override { ++(*counter); }
        void Revert() override { --(*counter); }
        const char* Name() const noexcept override { return "Noop"; }
    };

    int counter = 0;
    ++counter; // simulate "the mutation already happened" (caller-applied).
    overlay.UndoStack().Push(std::make_unique<NoopCommand>(&counter));

    auto& kb = overlay.KeybindingHandler();
    kb.SetFocusedPanel(VfxFocusedPanel::Stack);
    EXPECT_TRUE(kb.ProcessKeyEvent(VfxEditorKeybindingHandler::Key::Z,
                                    /*ctrlHeld=*/true, /*shiftHeld=*/false));
    EXPECT_EQ(counter, 0);
    EXPECT_EQ(kb.UndoCount(), 1u);
}

TEST(VfxEditorMount, TimelineScrubberBoundToOverlayPreviewClock) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();

    // Binding the timeline scrubber to the overlay's preview clock is
    // what makes Play / Pause from the scrubber UI drive the simulation
    // delta. Verify identity + observable behavior.
    EXPECT_EQ(overlay.TimelineScrubber().Clock(), &overlay.PreviewClock());

    overlay.PreviewClock().Pause();
    EXPECT_FALSE(overlay.TimelineScrubber().IsPlaying());
    overlay.PreviewClock().Play();
    EXPECT_TRUE(overlay.TimelineScrubber().IsPlaying());
}

// ─────────────────────────────────────────────────────────────────────────────
// Composition binding happens lazily (not in the overlay constructor)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMount, CompositionNotBoundUntilEnsureCalled) {
    VfxEditorOverlay overlay;
    // Fresh overlay has nullptr bindings on the composition.
    EXPECT_EQ(overlay.Composition().StackPanel(), nullptr);
    // Toggling Niagara mode alone doesn't bind either — mounts happen
    // during BuildUI or the explicit test hook.
    overlay.ToggleNiagaraMode();
    EXPECT_EQ(overlay.Composition().StackPanel(), nullptr);
    overlay.EnsureCompositionBoundForTest();
    EXPECT_NE(overlay.Composition().StackPanel(), nullptr);
}
