#include "vfx_graph_keybindings.h"

#include "engine/vfx/editor/vfx_node_graph.h"
#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ProcessKeyEvent — dispatch table
// ─────────────────────────────────────────────────────────────────────────────

bool VfxGraphKeybindingHandler::ProcessKeyEvent(Key key, bool ctrlHeld, bool /*shiftHeld*/) {
    if (!widget_ || !widget_->GetGraph()) return false;

    // Ctrl-modified shortcuts.
    if (ctrlHeld) {
        switch (key) {
            case Key::C: return DoCopy();
            case Key::V: return DoPaste();
            case Key::D: return DoDuplicate();
            case Key::A: return DoSelectAll();
            default: break;
        }
    }

    // Bare keys.
    switch (key) {
        case Key::Delete: return DoDelete();
        case Key::Escape: return DoClear();
        case Key::Left:   return DoNudge(-1.0f, 0.0f);
        case Key::Right:  return DoNudge( 1.0f, 0.0f);
        case Key::Up:     return DoNudge( 0.0f, -1.0f);
        case Key::Down:   return DoNudge( 0.0f,  1.0f);
        default: return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Action helpers
// ─────────────────────────────────────────────────────────────────────────────

bool VfxGraphKeybindingHandler::DoCopy() {
    if (!widget_ || !widget_->GetGraph()) return false;
    if (widget_->SelectionCount() == 0) return false;

    std::vector<std::string> ids(widget_->SelectedIds().begin(),
                                  widget_->SelectedIds().end());
    const std::string snippet = VfxNodeGraphClipboard::Copy(*widget_->GetGraph(), ids);

    auto* provider = provider_ ? provider_ : &defaultProvider_;
    provider->Write(snippet);

    ++copyCount_;
    UZ_LOG(VFX, Info, "VfxGraphKeybindings: copied {} node(s)", ids.size());
    return true;
}

bool VfxGraphKeybindingHandler::DoPaste() {
    if (!widget_ || !widget_->GetGraph()) return false;

    auto* provider = provider_ ? provider_ : &defaultProvider_;
    const std::string snippet = provider->Read();
    if (snippet.empty()) return false;

    const auto newIds = VfxNodeGraphClipboard::Paste(*widget_->GetGraph(), snippet);
    if (newIds.empty()) return false;

    // Select the newly pasted nodes.
    widget_->ClearSelection();
    for (const auto& id : newIds) {
        widget_->ToggleSelect(id);
    }

    ++pasteCount_;
    UZ_LOG(VFX, Info, "VfxGraphKeybindings: pasted {} node(s)", newIds.size());
    return true;
}

bool VfxGraphKeybindingHandler::DoDuplicate() {
    // Duplicate = Copy + Paste in one action. Uses the in-process
    // clipboard so the OS clipboard is not affected.
    if (!widget_ || !widget_->GetGraph()) return false;
    if (widget_->SelectionCount() == 0) return false;

    std::vector<std::string> ids(widget_->SelectedIds().begin(),
                                  widget_->SelectedIds().end());
    const std::string snippet = VfxNodeGraphClipboard::Copy(*widget_->GetGraph(), ids);
    const auto newIds = VfxNodeGraphClipboard::Paste(*widget_->GetGraph(), snippet);
    if (newIds.empty()) return false;

    widget_->ClearSelection();
    for (const auto& id : newIds) {
        widget_->ToggleSelect(id);
    }

    ++copyCount_;
    ++pasteCount_;
    UZ_LOG(VFX, Info, "VfxGraphKeybindings: duplicated {} node(s)", newIds.size());
    return true;
}

bool VfxGraphKeybindingHandler::DoDelete() {
    if (!widget_) return false;
    const uint32_t removed = widget_->DeleteSelection();
    if (removed == 0) return false;
    ++deleteCount_;
    return true;
}

bool VfxGraphKeybindingHandler::DoSelectAll() {
    if (!widget_) return false;
    widget_->SelectAll();
    return true;
}

bool VfxGraphKeybindingHandler::DoClear() {
    if (!widget_) return false;
    // Cancel any active wire drag or rubber-band first, then clear selection.
    if (widget_->IsWireDragActive()) {
        widget_->CancelWireDrag();
        return true;
    }
    if (widget_->IsRubberBandActive()) {
        widget_->EndRubberBand();
        return true;
    }
    if (widget_->SelectionCount() > 0) {
        widget_->ClearSelection();
        return true;
    }
    return false;
}

bool VfxGraphKeybindingHandler::DoNudge(float dx, float dy) {
    if (!widget_ || widget_->SelectionCount() == 0) return false;
    widget_->NudgeSelection(dx, dy);
    return true;
}

} // namespace uz
