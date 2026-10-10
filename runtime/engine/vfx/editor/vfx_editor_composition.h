#pragma once

/// Final Niagara-parity editor composition — phase 11.13.0.
///
/// This header is the integration layer that ties every phase-11 widget
/// into a single coherent editor. It contains **no rendering code** and
/// **no GPU state** — just layout math, focus routing, undo-stack
/// dispatch, and the composition of the pre-existing widgets into a
/// 4-panel canvas.
///
/// # Layout
///
/// ```
///  ┌─ Menu bar ───────────────────────────────────────────────────┐
///  │ File  Edit  View  Presets  Help                              │
///  ├─────────────────┬─────────────────────────┬──────────────────┤
///  │ Stack Panel     │ Center Panel (mode)     │ Right Panel      │
///  │ (11.2)          │   - ModuleParams        │ (tabbed)         │
///  │   25% width     │   - Curves (11.5)       │ - Parameters     │
///  │                 │   - Parameters (11.4)   │   list (11.4)    │
///  │                 │   - NodeGraph (10.4)    │ - Snippets (9.4) │
///  │                 │     35% width           │   20% width      │
///  ├─────────────────┴─────────────────────────┴──────────────────┤
///  │ Timeline scrubber (11.6) + burst markers (11.8)  · 32 px     │
///  └──────────────────────────────────────────────────────────────┘
/// ```
///
/// # Design rule
///
/// The composition stays out of the existing `VfxEditorOverlay` (which
/// renders legacy slider panels). The host opts into the Niagara-parity
/// layout by instantiating `VfxEditorComposition`, binding its widgets,
/// and calling `ComputeLayout(screenW, screenH)` each frame to place
/// every panel. Existing overlay callers are untouched — the
/// composition is purely additive.
///
/// @ref docs/engine/25-vfx/51-niagara-layout.md
/// @ref Every phase 11.x widget listed above

#include "engine/vfx/editor/vfx_editor_keybindings.h"  // VfxFocusedPanel
#include "engine/vfx/editor/vfx_stack_panel_widget.h"  // VfxStackRect

#include <cstdint>
#include <functional>
#include <string>

namespace uz {

// Forward decls — heavy widgets stay out of this header.
class VfxStackPanelWidget;
class VfxParameterInspectorWidget;
class VfxCurvesEditorWidget;
class VfxTimelineScrubberWidget;
class VfxModulePickerDialog;
class VfxUndoStack;
class VfxThumbnailDrainer;
class VfxSystemOverviewWidget; // Phase 12.9.0

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

/// Width / height fractions + pixel constants that drive the 4-panel
/// layout. Keeping them public lets tests override individual values
/// and lets hosts pick a custom aspect (e.g. portrait dev tools).
/// Phase 15.1 — overall editor layout mode. `Niagara` keeps the
/// phase-14 three-column composition; `SplitHorizontal` swaps to a
/// user-controlled 2-column split (viewport left / editor pane right).
enum class VfxEditorLayoutMode : uint8_t {
    Niagara          = 0,
    SplitHorizontal  = 1,
};

struct VfxEditorLayoutConfig {
    float stackPanelWidthFrac  = 0.22f;  ///< Left pane (Stack Panel).
    float centerPanelWidthFrac = 0.24f;  ///< Center pane (context).
    float rightPanelWidthFrac  = 0.18f;  ///< Right pane (tabbed).
    float menuBarHeight        = 30.0f;  ///< Top menu bar.
    /// Phase 14.10 — toolbar strip below the emitter bar.
    float toolbarBarHeight     = 28.0f;
    /// Phase 14.10 — status bar strip above the timeline scrubber.
    float statusBarHeight      = 22.0f;
    /// Timeline scrubber strip. Currently disabled (height 0) — the
    /// widget stays in the codebase but is not rendered so the bottom
    /// of the viewport stays clean while playback / seek authoring is
    /// out of scope.
    float timelineBarHeight    = 0.0f;

    /// Phase 15.1 — chosen layout mode.
    VfxEditorLayoutMode layoutMode = VfxEditorLayoutMode::Niagara;
    /// Phase 15.1 — boundary between viewport (left) and editor pane
    /// (right) in `SplitHorizontal` mode. Range clamped to
    /// `[0.2, 0.8]` so neither half collapses.
    float splitFraction = 0.5f;
};

/// Final rectangle positions for every panel. The "reserve" pane is the
/// space left over (e.g. viewport / 3D preview between center and right
/// panels when the fractions don't sum to 1.0).
struct VfxEditorLayoutRects {
    VfxStackRect menuBar;
    VfxStackRect toolbarBar;   ///< Phase 14.10 — UE5 toolbar strip.
    VfxStackRect stackPanel;
    VfxStackRect centerPanel;
    VfxStackRect viewportReserve;
    VfxStackRect rightPanel;
    /// Phase 15.1 — populated only in `SplitHorizontal` mode; hosts
    /// render the composite editor pane here (stack + parameters +
    /// emitter tree stacked in tabs).
    VfxStackRect editorPane;
    /// Phase 15.1 — 6-px vertical gutter between the viewport and
    /// the editor pane. Populated only in `SplitHorizontal` mode.
    VfxStackRect splitter;
    VfxStackRect statusBar;    ///< Phase 14.10 — UE5 status strip.
    VfxStackRect timelineBar;
};

/// Computes the layout rectangles for the given screen size + config.
/// Pure function — no widget state / no GPU calls. Tested in
/// `tests/engine/vfx/test_vfx_editor_composition.cpp`.
[[nodiscard]] VfxEditorLayoutRects VfxComputeEditorLayout(
    float screenW, float screenH, const VfxEditorLayoutConfig& cfg) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Center panel mode
// ─────────────────────────────────────────────────────────────────────────────

/// What the center panel renders. The host switches the mode based on
/// which widget has focus / what the author most recently clicked.
enum class VfxCenterMode : uint8_t {
    ModuleParams = 0,  ///< Default — legacy slider form for the selected module.
    Curves       = 1,  ///< Phase 11.5 curves editor bound to the selected curve.
    Parameters   = 2,  ///< Phase 11.4 parameter inspector form (right pane).
    NodeGraph    = 3,  ///< Phase 10.4 node-graph widget.
};

/// Stable label for diagnostics + logs. Never null.
[[nodiscard]] constexpr const char* VfxCenterModeName(VfxCenterMode m) noexcept {
    switch (m) {
        case VfxCenterMode::ModuleParams: return "ModuleParams";
        case VfxCenterMode::Curves:       return "Curves";
        case VfxCenterMode::Parameters:   return "Parameters";
        case VfxCenterMode::NodeGraph:    return "NodeGraph";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// Composition
// ─────────────────────────────────────────────────────────────────────────────

/// Owns no widgets — only pointers. The host instantiates each widget
/// and registers it here. Binding is idempotent; a null pointer cleanly
/// disables any helper that would touch the missing widget.
class VfxEditorComposition {
public:
    // ── Binding ─────────────────────────────────────────────────────────

    void BindStackPanel(VfxStackPanelWidget* w) noexcept { m_stack = w; }
    void BindParameterInspector(VfxParameterInspectorWidget* w) noexcept {
        m_params = w;
    }
    void BindCurvesEditor(VfxCurvesEditorWidget* w) noexcept { m_curves = w; }
    void BindTimelineScrubber(VfxTimelineScrubberWidget* w) noexcept {
        m_timeline = w;
    }
    void BindModulePicker(VfxModulePickerDialog* d) noexcept { m_picker = d; }
    void BindUndoStack(VfxUndoStack* s) noexcept { m_undo = s; }
    void BindThumbnailDrainer(VfxThumbnailDrainer* d) noexcept { m_drainer = d; }
    void BindKeybindings(VfxEditorKeybindingHandler* k) noexcept {
        m_keybindings = k;
        PropagateKeybindings();
    }
    void BindSystemOverview(VfxSystemOverviewWidget* w) noexcept {
        m_systemOverview = w;
    }

    [[nodiscard]] VfxStackPanelWidget*         StackPanel() const noexcept { return m_stack; }
    [[nodiscard]] VfxParameterInspectorWidget* ParameterInspector() const noexcept { return m_params; }
    [[nodiscard]] VfxCurvesEditorWidget*       CurvesEditor() const noexcept { return m_curves; }
    [[nodiscard]] VfxTimelineScrubberWidget*   TimelineScrubber() const noexcept { return m_timeline; }
    [[nodiscard]] VfxModulePickerDialog*       ModulePicker() const noexcept { return m_picker; }
    [[nodiscard]] VfxUndoStack*                UndoStack() const noexcept { return m_undo; }
    [[nodiscard]] VfxThumbnailDrainer*         ThumbnailDrainer() const noexcept { return m_drainer; }
    [[nodiscard]] VfxEditorKeybindingHandler*  Keybindings() const noexcept { return m_keybindings; }
    [[nodiscard]] VfxSystemOverviewWidget*     SystemOverview() const noexcept { return m_systemOverview; }

    // ── Layout ──────────────────────────────────────────────────────────

    [[nodiscard]] const VfxEditorLayoutConfig& LayoutConfig() const noexcept {
        return m_layoutConfig;
    }
    void SetLayoutConfig(const VfxEditorLayoutConfig& cfg) noexcept {
        m_layoutConfig = cfg;
    }

    [[nodiscard]] VfxEditorLayoutRects ComputeLayout(
        float screenW, float screenH) const noexcept {
        return VfxComputeEditorLayout(screenW, screenH, m_layoutConfig);
    }

    // ── Center panel mode ───────────────────────────────────────────────

    [[nodiscard]] VfxCenterMode CenterMode() const noexcept { return m_centerMode; }
    void SetCenterMode(VfxCenterMode mode) noexcept {
        if (m_centerMode == mode) return;
        m_centerMode = mode;
        if (onCenterModeChanged) onCenterModeChanged(mode);
    }

    std::function<void(VfxCenterMode)> onCenterModeChanged;

    // ── Focus routing ───────────────────────────────────────────────────

    /// Returns the current focused panel (mirrors the keybindings
    /// handler's state when one is bound). When unbound, returns the
    /// composition's own shadow state.
    [[nodiscard]] VfxFocusedPanel FocusedPanel() const noexcept {
        if (m_keybindings != nullptr) return m_keybindings->FocusedPanel();
        return m_shadowFocus;
    }

    void SetFocusedPanel(VfxFocusedPanel p) noexcept {
        if (m_keybindings != nullptr) m_keybindings->SetFocusedPanel(p);
        m_shadowFocus = p;

        // Auto-pick the center mode from the focused panel so the
        // center pane always shows the most-relevant inspector for
        // what the author just clicked.
        switch (p) {
            case VfxFocusedPanel::Curves:     SetCenterMode(VfxCenterMode::Curves);       break;
            case VfxFocusedPanel::Parameters: SetCenterMode(VfxCenterMode::Parameters);   break;
            case VfxFocusedPanel::NodeGraph:  SetCenterMode(VfxCenterMode::NodeGraph);    break;
            case VfxFocusedPanel::Stack:      SetCenterMode(VfxCenterMode::ModuleParams); break;
            default: break;
        }
    }

    // ── Per-frame tick (thumbnail drain) ────────────────────────────────

    /// Invoked by the host viewport app each frame. Drains the
    /// thumbnail queue via the bound drainer and ticks the auto-loop
    /// on the timeline scrubber (which fires `onRestartRequested` for
    /// burst re-firing).
    void Tick(float simDt);

private:
    /// Pushes the current binding state into the keybindings handler so
    /// shortcuts route to the right widgets. Safe to call repeatedly.
    void PropagateKeybindings();

    VfxStackPanelWidget*         m_stack          = nullptr;
    VfxParameterInspectorWidget* m_params         = nullptr;
    VfxCurvesEditorWidget*       m_curves         = nullptr;
    VfxTimelineScrubberWidget*   m_timeline       = nullptr;
    VfxModulePickerDialog*       m_picker         = nullptr;
    VfxUndoStack*                m_undo           = nullptr;
    VfxThumbnailDrainer*         m_drainer        = nullptr;
    VfxEditorKeybindingHandler*  m_keybindings    = nullptr;
    VfxSystemOverviewWidget*     m_systemOverview = nullptr;

    VfxEditorLayoutConfig m_layoutConfig{};
    VfxCenterMode         m_centerMode  = VfxCenterMode::ModuleParams;
    VfxFocusedPanel       m_shadowFocus = VfxFocusedPanel::None;
};

} // namespace uz
