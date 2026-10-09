/// Unit tests for phase 12.10.0: `VfxEditorOverlay::InjectKey` +
/// right-click state + every-widget-bound composition graph. Visual
/// parity (screenshot) verification happens interactively via the
/// demo app.

#include <gtest/gtest.h>

#include <memory>

#include "engine/vfx/editor/undo/vfx_undo_command.h"
#include "engine/vfx/editor/undo/vfx_undo_stack.h"
#include "engine/vfx/editor/vfx_editor_overlay.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// InjectKey — forwards to the bound keybindings handler
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMountOverhaul, InjectKey_RoutesToKeybindings) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();

    // Queue an undo command so Ctrl+Z has something to pop.
    struct Nop final : IVfxUndoCommand {
        int* c;
        explicit Nop(int* p) : c(p) {}
        void Apply()  override { ++(*c); }
        void Revert() override { --(*c); }
        const char* Name() const noexcept override { return "Nop"; }
    };
    int counter = 1;
    overlay.UndoStack().Push(std::make_unique<Nop>(&counter));

    overlay.KeybindingHandler().SetFocusedPanel(VfxFocusedPanel::Stack);
    EXPECT_TRUE(overlay.InjectKey(VfxEditorKeybindingHandler::Key::Z,
                                    /*ctrl=*/true, /*shift=*/false));
    EXPECT_EQ(counter, 0);
    EXPECT_EQ(overlay.KeybindingHandler().UndoCount(), 1u);
}

TEST(VfxEditorMountOverhaul, InjectKey_EscapeClosesContextMenu) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();
    overlay.ContextMenu().Open(
        10.0f, 10.0f,
        {VfxContextMenuItem{"A"}, VfxContextMenuItem{"B"}});
    EXPECT_TRUE(overlay.ContextMenu().IsOpen());
    EXPECT_TRUE(overlay.InjectKey(VfxEditorKeybindingHandler::Key::Escape,
                                    /*ctrl=*/false, /*shift=*/false));
    EXPECT_FALSE(overlay.ContextMenu().IsOpen());
}

// ─────────────────────────────────────────────────────────────────────────────
// System overview — bound in composition
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMountOverhaul, Composition_BindsSystemOverview) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();
    EXPECT_EQ(overlay.Composition().SystemOverview(),
              &overlay.SystemOverview());
}

// ─────────────────────────────────────────────────────────────────────────────
// onMenuCommand callback plumbing
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMountOverhaul, OnMenuCommandCallback_IsBindable) {
    VfxEditorOverlay overlay;
    VfxMenuCommand got = VfxMenuCommand::None;
    overlay.onMenuCommand = [&](VfxMenuCommand cmd) { got = cmd; };
    overlay.onMenuCommand(VfxMenuCommand::View_NiagaraMode);
    EXPECT_EQ(got, VfxMenuCommand::View_NiagaraMode);
}

TEST(VfxEditorMountOverhaul, OnAddEmitterRequestedCallback_IsBindable) {
    VfxEditorOverlay overlay;
    int calls = 0;
    overlay.onAddEmitterRequested = [&] { ++calls; };
    overlay.onAddEmitterRequested();
    EXPECT_EQ(calls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menu on top — wired into overlay + composition
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMountOverhaul, ContextMenuAccessor_ReturnsSameInstance) {
    VfxEditorOverlay overlay;
    auto& a = overlay.ContextMenu();
    auto& b = overlay.ContextMenu();
    EXPECT_EQ(&a, &b);
}

// ─────────────────────────────────────────────────────────────────────────────
// Niagara-mode toggle + Composition identity
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorMountOverhaul, NiagaraMode_ToggleRoundTrip) {
    VfxEditorOverlay overlay;
    EXPECT_FALSE(overlay.IsNiagaraMode());
    overlay.ToggleNiagaraMode();
    EXPECT_TRUE(overlay.IsNiagaraMode());
    overlay.ToggleNiagaraMode();
    EXPECT_FALSE(overlay.IsNiagaraMode());
}

TEST(VfxEditorMountOverhaul, EnsureCompositionBound_BindsEveryWidget) {
    VfxEditorOverlay overlay;
    overlay.EnsureCompositionBoundForTest();
    const auto& c = overlay.Composition();
    EXPECT_EQ(c.StackPanel(),          &overlay.StackPanelWidget());
    EXPECT_EQ(c.ParameterInspector(),  &overlay.ParameterInspector());
    EXPECT_EQ(c.CurvesEditor(),        &overlay.CurvesEditor());
    EXPECT_EQ(c.TimelineScrubber(),    &overlay.TimelineScrubber());
    EXPECT_EQ(c.ModulePicker(),        &overlay.ModulePickerDialog());
    EXPECT_EQ(c.UndoStack(),           &overlay.UndoStack());
    EXPECT_EQ(c.ThumbnailDrainer(),    &overlay.ThumbnailDrainer());
    EXPECT_EQ(c.Keybindings(),         &overlay.KeybindingHandler());
    EXPECT_EQ(c.SystemOverview(),      &overlay.SystemOverview());
}
