/// VfxEditorComposition — phase 11.13 integration layer implementation.

#include "vfx_editor_composition.h"

#include "vfx_curves_editor_widget.h"
#include "vfx_module_picker_dialog.h"
#include "vfx_parameter_inspector_widget.h"
#include "vfx_stack_panel_widget.h"
#include "vfx_thumbnail_drainer.h"
#include "vfx_timeline_scrubber_widget.h"

#include "engine/vfx/editor/undo/vfx_undo_stack.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

VfxEditorLayoutRects VfxComputeEditorLayout(float screenW, float screenH,
                                              const VfxEditorLayoutConfig& cfg) noexcept {
    VfxEditorLayoutRects out{};

    // Degenerate screens (tests sometimes pass 0×0). Return empty rects
    // but stable field layout so callers can still iterate.
    if (screenW <= 0.0f || screenH <= 0.0f) return out;

    const float menuH     = std::min(cfg.menuBarHeight, screenH);
    const float toolbarH  = std::min(cfg.toolbarBarHeight,
                                      std::max(0.0f, screenH - menuH));
    const float timelineH = std::min(cfg.timelineBarHeight,
                                      std::max(0.0f, screenH - menuH - toolbarH));
    const float statusH   = std::min(cfg.statusBarHeight,
                                      std::max(0.0f,
                                                screenH - menuH - toolbarH - timelineH));

    const float middleY      = menuH + toolbarH;
    const float middleHeight = std::max(0.0f,
                                         screenH - menuH - toolbarH - statusH - timelineH);

    out.menuBar     = { 0.0f, 0.0f, screenW, menuH };
    out.toolbarBar  = { 0.0f, menuH, screenW, toolbarH };
    out.statusBar   = { 0.0f, middleY + middleHeight, screenW, statusH };
    out.timelineBar = { 0.0f, middleY + middleHeight + statusH,
                          screenW, timelineH };

    // Phase 15.1 — SplitHorizontal swaps the 3-column composition
    // for a user-controlled split: viewport left / editor pane
    // right with a 6-px draggable gutter between them.
    if (cfg.layoutMode == VfxEditorLayoutMode::SplitHorizontal) {
        constexpr float kGutterW = 6.0f;
        const float frac = std::clamp(cfg.splitFraction, 0.2f, 0.8f);
        const float leftW  = std::max(0.0f,
                                        (screenW - kGutterW) * frac);
        const float rightW = std::max(0.0f,
                                        (screenW - kGutterW) - leftW);
        out.viewportReserve = { 0.0f, middleY, leftW, middleHeight };
        out.splitter        = { leftW, middleY, kGutterW, middleHeight };
        out.editorPane      = { leftW + kGutterW, middleY,
                                 rightW, middleHeight };
        // Three-column rects are zeroed in split mode so consumer
        // widgets that iterate them with `width > 0` checks skip.
        out.stackPanel  = {};
        out.centerPanel = {};
        out.rightPanel  = {};
        return out;
    }

    // Horizontal column widths. Clamp fractions so a bad config can't
    // explode the layout — sum ≤ 1.0 is enforced by proportional
    // rescaling when necessary.
    float stackFrac  = std::clamp(cfg.stackPanelWidthFrac,  0.0f, 1.0f);
    float centerFrac = std::clamp(cfg.centerPanelWidthFrac, 0.0f, 1.0f);
    float rightFrac  = std::clamp(cfg.rightPanelWidthFrac,  0.0f, 1.0f);
    const float totalFrac = stackFrac + centerFrac + rightFrac;
    if (totalFrac > 1.0f) {
        const float scale = 1.0f / totalFrac;
        stackFrac  *= scale;
        centerFrac *= scale;
        rightFrac  *= scale;
    }

    const float stackW  = screenW * stackFrac;
    const float centerW = screenW * centerFrac;
    const float rightW  = screenW * rightFrac;

    out.stackPanel      = { 0.0f,             middleY, stackW,  middleHeight };
    out.centerPanel     = { stackW,           middleY, centerW, middleHeight };

    const float viewportX = stackW + centerW;
    const float viewportW = std::max(0.0f, screenW - stackW - centerW - rightW);
    out.viewportReserve = { viewportX, middleY, viewportW, middleHeight };

    out.rightPanel      = { viewportX + viewportW, middleY, rightW, middleHeight };

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Keybinding wiring
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorComposition::PropagateKeybindings() {
    if (m_keybindings == nullptr) return;
    m_keybindings->BindUndoStack(m_undo);
    m_keybindings->BindStackPanel(m_stack);
    m_keybindings->BindParameterInspector(m_params);
    m_keybindings->BindCurvesEditor(m_curves);
    m_keybindings->BindTimelineScrubber(m_timeline);
    m_keybindings->BindModulePicker(m_picker);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void VfxEditorComposition::Tick(float simDt) {
    if (m_drainer  != nullptr) m_drainer->Tick(simDt);
    if (m_timeline != nullptr) m_timeline->TickAutoLoop();
}

} // namespace uz
