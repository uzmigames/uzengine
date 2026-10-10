#pragma once

/// Unified keyboard-shortcut dispatcher for the phase-11 VFX editor.
///
/// Phase 11.10.0. Composes the phase 9.3 `VfxGraphKeybindingHandler`
/// for graph-specific shortcuts with the Niagara-parity editor's new
/// cross-panel shortcut layer. The host calls `ProcessKeyEvent` once
/// per discrete keypress with the platform-translated `Key` code and
/// modifier state; the handler dispatches to the appropriate widget
/// based on the `FocusedPanel` state.
///
/// # Registration
///
/// ```cpp
/// VfxEditorKeybindingHandler keys;
/// keys.BindUndoStack(&undoStack);
/// keys.BindStackPanel(&stackPanel);
/// keys.BindParameterInspector(&paramInspector);
/// keys.BindCurvesEditor(&curvesEditor);
/// keys.BindTimelineScrubber(&timelineScrubber);
/// keys.BindModulePicker(&modulePicker);
/// keys.BindGraphKeybindings(&graphKeys);           // optional — for NodeGraph focus
/// keys.SetSaveCallback([&] { savePipeline.Save(); });
///
/// // Each frame after reading input:
/// keys.ProcessKeyEvent(VfxEditorKeybindingHandler::Key::Z, /*ctrl*/ true, /*shift*/ false);
/// ```
///
/// # Focus model
///
/// Every panel sets `focusedPanel` via `SetFocusedPanel` when it gains
/// hover + first click. `Tab` cycles through the major panels. Several
/// shortcuts are panel-specific; others (Ctrl+Z / Y / S / F / Escape)
/// fire regardless of focus.
///
/// @ref UE5 `FNiagaraEditorCommands`
/// @ref engine/vfx/editor/vfx_graph_keybindings.h (node graph subset)

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

// Forward decls — keeps this header light.
class VfxUndoStack;
class VfxStackPanelWidget;
class VfxParameterInspectorWidget;
class VfxCurvesEditorWidget;
class VfxTimelineScrubberWidget;
class VfxModulePickerDialog;
class VfxGraphKeybindingHandler;

/// The panel currently consuming keyboard input.
enum class VfxFocusedPanel : uint8_t {
    None       = 0,
    Stack      = 1,
    Curves     = 2,
    Parameters = 3,
    Timeline   = 4,
    NodeGraph  = 5,
    Picker     = 6,
};

/// Stable label for a panel — used for logs, diagnostics, and UI
/// status lines. Never returns null.
[[nodiscard]] constexpr const char* VfxFocusedPanelName(VfxFocusedPanel p) noexcept {
    switch (p) {
        case VfxFocusedPanel::None:       return "None";
        case VfxFocusedPanel::Stack:      return "Stack";
        case VfxFocusedPanel::Curves:     return "Curves";
        case VfxFocusedPanel::Parameters: return "Parameters";
        case VfxFocusedPanel::Timeline:   return "Timeline";
        case VfxFocusedPanel::NodeGraph:  return "NodeGraph";
        case VfxFocusedPanel::Picker:     return "Picker";
    }
    return "Unknown";
}

class VfxEditorKeybindingHandler {
public:
    /// Platform-independent key codes. Host translates its native keys
    /// to these before calling `ProcessKeyEvent`.
    enum class Key : uint16_t {
        None = 0,
        // Letters
        A, C, D, F, S, V, Y, Z,
        // Function keys
        F2,
        // Numeric row (speed presets)
        N1, N2, N3, N4, N5,
        // Navigation
        Left, Right, Up, Down,
        // Specials
        Space, Tab, Escape, Delete, Enter, Home, End,
        // Punctuation used by scrubber
        Comma,       // ","
        Period,      // "."
    };

    // ── Bindings (every binding is optional) ────────────────────────────

    void BindUndoStack(VfxUndoStack* s) noexcept { m_undo = s; }
    void BindStackPanel(VfxStackPanelWidget* w) noexcept { m_stack = w; }
    void BindParameterInspector(VfxParameterInspectorWidget* w) noexcept {
        m_params = w;
    }
    void BindCurvesEditor(VfxCurvesEditorWidget* w) noexcept { m_curves = w; }
    void BindTimelineScrubber(VfxTimelineScrubberWidget* w) noexcept {
        m_timeline = w;
    }
    void BindModulePicker(VfxModulePickerDialog* d) noexcept { m_picker = d; }
    void BindGraphKeybindings(VfxGraphKeybindingHandler* g) noexcept {
        m_graphKeys = g;
    }

    /// Callback fired on `Ctrl+S`. Host wires to `EditorSavePipeline::Save`.
    std::function<void()> onSaveRequested;

    /// Callback fired on `Ctrl+F` when the focused panel supports
    /// "focus search". Arg = which panel to focus (Stack / Parameters /
    /// Picker — the only ones with a search input today).
    std::function<void(VfxFocusedPanel)> onFocusSearch;

    /// Callback fired on `F2` when the Parameters panel is focused and
    /// a parameter is selected.
    std::function<void()> onRenameRequested;

    // ── Focus ───────────────────────────────────────────────────────────

    [[nodiscard]] VfxFocusedPanel FocusedPanel() const noexcept { return m_focus; }

    void SetFocusedPanel(VfxFocusedPanel p) noexcept { m_focus = p; }

    /// Advances `focusedPanel` through Stack → Parameters → Curves →
    /// Timeline → Stack. `None` / `Picker` / `NodeGraph` seed at Stack.
    /// Returns the new focus.
    VfxFocusedPanel CycleFocusedPanel() noexcept;

    // ── Dispatch ────────────────────────────────────────────────────────

    /// Processes a key-down event. Returns true when the handler
    /// consumed the key. Host should skip further dispatch when true.
    bool ProcessKeyEvent(Key key, bool ctrlHeld, bool shiftHeld);

    // ── Diagnostic counters (for tests) ─────────────────────────────────

    [[nodiscard]] uint32_t UndoCount()   const noexcept { return m_undoCount; }
    [[nodiscard]] uint32_t RedoCount()   const noexcept { return m_redoCount; }
    [[nodiscard]] uint32_t SaveCount()   const noexcept { return m_saveCount; }
    [[nodiscard]] uint32_t FocusSearchCount() const noexcept {
        return m_focusSearchCount;
    }

private:
    // Global dispatchers — return true when consumed.
    bool HandleGlobal(Key key, bool ctrl, bool shift);
    // Panel-specific dispatchers.
    bool HandleTimeline(Key key, bool ctrl, bool shift);
    bool HandleStack(Key key, bool ctrl, bool shift);
    bool HandleCurves(Key key, bool ctrl, bool shift);
    bool HandleParameters(Key key, bool ctrl, bool shift);

    VfxUndoStack*                m_undo     = nullptr;
    VfxStackPanelWidget*         m_stack    = nullptr;
    VfxParameterInspectorWidget* m_params   = nullptr;
    VfxCurvesEditorWidget*       m_curves   = nullptr;
    VfxTimelineScrubberWidget*   m_timeline = nullptr;
    VfxModulePickerDialog*       m_picker   = nullptr;
    VfxGraphKeybindingHandler*   m_graphKeys = nullptr;

    VfxFocusedPanel m_focus = VfxFocusedPanel::None;

    uint32_t m_undoCount = 0u;
    uint32_t m_redoCount = 0u;
    uint32_t m_saveCount = 0u;
    uint32_t m_focusSearchCount = 0u;
};

} // namespace uz
