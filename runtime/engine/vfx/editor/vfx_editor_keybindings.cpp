/// VfxEditorKeybindingHandler — implementation.
///
/// Phase 11.10.0. See `vfx_editor_keybindings.h` for the contract.
/// Dispatch strategy:
///   1. Try global shortcuts (Ctrl+Z/Y/S/F, Tab, Escape) — these fire
///      regardless of focus.
///   2. If not consumed, try the panel-specific handler for the
///      current `focusedPanel`. `Picker` has priority over whatever
///      else is focused when the picker is open (Escape closes it).
///   3. If still not consumed and the focused panel is `NodeGraph`,
///      forward to the composed `VfxGraphKeybindingHandler` with an
///      adapter that translates the local `Key` to the graph handler's
///      enum.

#include "vfx_editor_keybindings.h"

#include "vfx_curves_editor_widget.h"
#include "vfx_graph_keybindings.h"
#include "vfx_module_picker_dialog.h"
#include "vfx_parameter_inspector_widget.h"
#include "vfx_stack_panel_widget.h"
#include "vfx_timeline_scrubber_widget.h"

#include "engine/vfx/editor/undo/vfx_undo_stack.h"

namespace uz {

namespace {

/// Map local `VfxEditorKeybindingHandler::Key` to
/// `VfxGraphKeybindingHandler::Key`. Returns `None` for codes that
/// aren't graph-relevant; caller skips forwarding when that happens.
[[nodiscard]] VfxGraphKeybindingHandler::Key ToGraphKey(
    VfxEditorKeybindingHandler::Key k) noexcept {
    using L = VfxEditorKeybindingHandler::Key;
    using G = VfxGraphKeybindingHandler::Key;
    switch (k) {
        case L::A:       return G::A;
        case L::C:       return G::C;
        case L::V:       return G::V;
        case L::D:       return G::D;
        case L::Delete:  return G::Delete;
        case L::Escape:  return G::Escape;
        case L::Left:    return G::Left;
        case L::Right:   return G::Right;
        case L::Up:      return G::Up;
        case L::Down:    return G::Down;
        default:         return G::None;
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Focus cycling
// ─────────────────────────────────────────────────────────────────────────────

VfxFocusedPanel VfxEditorKeybindingHandler::CycleFocusedPanel() noexcept {
    switch (m_focus) {
        case VfxFocusedPanel::Stack:      m_focus = VfxFocusedPanel::Parameters; break;
        case VfxFocusedPanel::Parameters: m_focus = VfxFocusedPanel::Curves;     break;
        case VfxFocusedPanel::Curves:     m_focus = VfxFocusedPanel::Timeline;   break;
        case VfxFocusedPanel::Timeline:   m_focus = VfxFocusedPanel::Stack;      break;
        default:                           m_focus = VfxFocusedPanel::Stack;      break;
    }
    return m_focus;
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorKeybindingHandler::ProcessKeyEvent(Key key, bool ctrl, bool shift) {
    if (key == Key::None) return false;

    // Picker modality — when the picker is open, Escape closes it
    // before any other dispatch runs.
    if (m_picker != nullptr && m_picker->IsOpen()) {
        if (key == Key::Escape) {
            m_picker->Close();
            return true;
        }
        // Arrow nav + Enter land in the picker while it's focused.
        if (key == Key::Up || key == Key::Down) {
            m_picker->NavigateSelection(key == Key::Up ? -1 : +1);
            return true;
        }
        if (key == Key::Home) {
            m_picker->SelectFirst();
            return true;
        }
        if (key == Key::End) {
            m_picker->SelectLast();
            return true;
        }
        if (key == Key::Enter) {
            m_picker->ConfirmSelection();
            return true;
        }
    }

    if (HandleGlobal(key, ctrl, shift)) return true;

    switch (m_focus) {
        case VfxFocusedPanel::Timeline:   return HandleTimeline(key, ctrl, shift);
        case VfxFocusedPanel::Stack:      return HandleStack(key, ctrl, shift);
        case VfxFocusedPanel::Curves:     return HandleCurves(key, ctrl, shift);
        case VfxFocusedPanel::Parameters: return HandleParameters(key, ctrl, shift);
        case VfxFocusedPanel::NodeGraph: {
            if (m_graphKeys == nullptr) return false;
            const auto gk = ToGraphKey(key);
            if (gk == VfxGraphKeybindingHandler::Key::None) return false;
            return m_graphKeys->ProcessKeyEvent(gk, ctrl, shift);
        }
        case VfxFocusedPanel::None:
        case VfxFocusedPanel::Picker:
            return false;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Global shortcuts
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorKeybindingHandler::HandleGlobal(Key key, bool ctrl, bool shift) {
    if (ctrl) {
        switch (key) {
            case Key::Z:
                if (shift) {
                    if (m_undo != nullptr) { m_undo->Redo(); ++m_redoCount; }
                } else {
                    if (m_undo != nullptr) { m_undo->Undo(); ++m_undoCount; }
                }
                return true;
            case Key::Y:
                if (m_undo != nullptr) { m_undo->Redo(); ++m_redoCount; }
                return true;
            case Key::S:
                if (onSaveRequested) onSaveRequested();
                ++m_saveCount;
                return true;
            case Key::F:
                if (onFocusSearch) {
                    // Only panels with a search input fire the callback.
                    if (m_focus == VfxFocusedPanel::Stack ||
                        m_focus == VfxFocusedPanel::Parameters ||
                        m_focus == VfxFocusedPanel::Picker) {
                        onFocusSearch(m_focus);
                        ++m_focusSearchCount;
                    }
                }
                return true;
            default: break;
        }
    }

    if (key == Key::Tab && !ctrl) {
        CycleFocusedPanel();
        return true;
    }

    if (key == Key::Escape) {
        // Global Escape: deselect on the focused panel, close picker
        // (already handled above), exit edit mode.
        switch (m_focus) {
            case VfxFocusedPanel::Stack:
                if (m_stack != nullptr) m_stack->ClearSelection();
                break;
            case VfxFocusedPanel::Parameters:
                if (m_params != nullptr) m_params->ClearSelection();
                break;
            case VfxFocusedPanel::Curves:
                if (m_curves != nullptr) m_curves->ClearSelection();
                break;
            default: break;
        }
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Timeline
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorKeybindingHandler::HandleTimeline(Key key, bool /*ctrl*/, bool shift) {
    if (m_timeline == nullptr) return false;

    switch (key) {
        case Key::Space:
            m_timeline->TogglePlayPause();
            return true;
        case Key::Left: {
            const float delta = shift ? -1.0f : -0.1f;
            m_timeline->Seek(m_timeline->CurrentTime() + delta);
            return true;
        }
        case Key::Right: {
            const float delta = shift ? 1.0f : 0.1f;
            m_timeline->Seek(m_timeline->CurrentTime() + delta);
            return true;
        }
        case Key::Comma:
            m_timeline->StepBackward();
            return true;
        case Key::Period:
            m_timeline->StepForward();
            return true;
        case Key::N1: m_timeline->SetSpeed(0.25f); return true;
        case Key::N2: m_timeline->SetSpeed(0.5f);  return true;
        case Key::N3: m_timeline->SetSpeed(1.0f);  return true;
        case Key::N4: m_timeline->SetSpeed(2.0f);  return true;
        case Key::N5: m_timeline->SetSpeed(4.0f);  return true;
        default: return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Stack panel
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorKeybindingHandler::HandleStack(Key key, bool ctrl, bool /*shift*/) {
    if (m_stack == nullptr) return false;
    const std::string sel = m_stack->SelectedInstanceId();
    if (sel.empty()) return false;

    if (key == Key::Delete) {
        if (m_stack->onModuleRemoveRequested) {
            m_stack->onModuleRemoveRequested(sel);
        }
        return true;
    }

    if (ctrl && key == Key::D) {
        if (m_stack->onModuleDuplicateRequested) {
            m_stack->onModuleDuplicateRequested(sel);
        }
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Curves
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorKeybindingHandler::HandleCurves(Key key, bool /*ctrl*/, bool /*shift*/) {
    if (m_curves == nullptr) return false;
    const size_t sel = m_curves->SelectedIndex();
    if (sel == static_cast<size_t>(-1)) return false;

    if (key == Key::Delete) {
        if (m_curves->Mode() == VfxCurveBindingMode::Float) {
            m_curves->RemoveKey(sel);
        } else if (m_curves->Mode() == VfxCurveBindingMode::Ramp) {
            m_curves->RemoveStop(sel);
        }
        return true;
    }

    // 1-px nudge — map to a small world-space delta so the key position
    // actually changes visibly regardless of zoom. Uses the widget's
    // snap config as the nudge step (falls back to 0.01 time / 0.001
    // value when snap is disabled).
    const float dtStep = (m_curves->snapTime  > 0.0f) ? m_curves->snapTime  : 0.01f;
    const float dvStep = (m_curves->snapValue > 0.0f) ? m_curves->snapValue : 0.001f;

    if (m_curves->Mode() == VfxCurveBindingMode::Float) {
        const auto& keys = m_curves->Keys();
        if (sel >= keys.size()) return false;
        const Keyframe& k = keys[sel];
        switch (key) {
            case Key::Left:  m_curves->MoveKey(sel, k.time - dtStep, k.value, false); return true;
            case Key::Right: m_curves->MoveKey(sel, k.time + dtStep, k.value, false); return true;
            case Key::Up:    m_curves->MoveKey(sel, k.time, k.value + dvStep, false); return true;
            case Key::Down:  m_curves->MoveKey(sel, k.time, k.value - dvStep, false); return true;
            default: return false;
        }
    }
    if (m_curves->Mode() == VfxCurveBindingMode::Ramp) {
        const auto& stops = m_curves->Stops();
        if (sel >= stops.size()) return false;
        const float t = stops[sel].t;
        switch (key) {
            case Key::Left:  m_curves->MoveStop(sel, t - dtStep, false); return true;
            case Key::Right: m_curves->MoveStop(sel, t + dtStep, false); return true;
            default: return false;
        }
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

bool VfxEditorKeybindingHandler::HandleParameters(Key key, bool /*ctrl*/, bool /*shift*/) {
    if (m_params == nullptr) return false;

    if (key == Key::F2) {
        if (onRenameRequested) onRenameRequested();
        return true;
    }

    return false;
}

} // namespace uz
