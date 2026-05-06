#include "engine/ui/slate_input.h"

#include "engine/ui/slate_widget.h"

#include <functional>

namespace uz::ui {

namespace {

/// Walks the tree depth-first and sets every widget's parent weak ref
/// to its real parent. Required for `OnPointerDown` bubble routing.
void WireParents(const SP<Widget>& node, WP<Widget> parent) {
    if (!node) return;
    node->SetParent(std::move(parent));
    if (auto* children = node->HitTestChildren()) {
        for (const auto& c : *children) {
            WireParents(c, node);
        }
    }
}

} // anonymous namespace

void InputRouter::SetRoot(SP<Widget> root) {
    m_root = std::move(root);
    m_capture.reset();
    m_hover.reset();
    if (m_root) {
        WireParents(m_root, WP<Widget>{});
    }
}

SP<Widget> InputRouter::HitTest(const SP<Widget>& root, Vec2 pos) const {
    if (!root) return nullptr;
    if (!root->GetGeometry().ToRect().Contains(pos)) return nullptr;

    // Walk children in reverse so later siblings ("on top") win on equal depth.
    if (auto* children = root->HitTestChildren()) {
        for (auto it = children->rbegin(); it != children->rend(); ++it) {
            if (auto deeper = HitTest(*it, pos)) {
                return deeper;
            }
        }
    }
    return root;
}

bool InputRouter::DispatchPointer(const PointerEvent& e) {
    if (!m_root) return false;

    // ── Capture path: captor exclusively receives Move/Up. ────────────
    if (auto cap = m_capture.lock()) {
        if (e.button != PointerButton::None && e.isDown) {
            // Down events while captured: still go through normal hit-test.
            // (Slate restricts capture to Move/Up; new presses route normally.)
        } else {
            EventReply r = EventReply::Unhandled;
            if (e.button != PointerButton::None && !e.isDown) {
                r = cap->OnPointerUp(e);
                ReleaseCapture();
            } else if (e.wheelDelta != 0.0f) {
                r = cap->OnPointerWheel(e);
            } else {
                r = cap->OnPointerMove(e);
            }
            return r != EventReply::Unhandled;
        }
    }

    // ── Hover diff. ──────────────────────────────────────────────────
    SP<Widget> hit = HitTest(m_root, e.position);
    SP<Widget> oldHover = m_hover.lock();
    if (hit != oldHover) {
        if (oldHover) oldHover->OnHoverLeave();
        if (hit)      hit->OnHoverEnter();
        m_hover = hit;
    }

    if (!hit) return false;

    // ── Bubble. ─────────────────────────────────────────────────────
    SP<Widget> target = hit;
    while (target) {
        EventReply r = EventReply::Unhandled;
        if (e.wheelDelta != 0.0f) {
            r = target->OnPointerWheel(e);
        } else if (e.button == PointerButton::None) {
            r = target->OnPointerMove(e);
        } else if (e.isDown) {
            r = target->OnPointerDown(e);
            if (r == EventReply::HandledCapture) {
                m_capture = target;
            }
        } else {
            r = target->OnPointerUp(e);
        }

        if (r != EventReply::Unhandled) {
            return true;
        }
        target = target->GetParent().lock();
    }
    return false;
}

bool InputRouter::DispatchKey(const KeyEvent& e) {
    // Without a FocusManager (Step 9), route keys to the hovered widget +
    // bubble. Good enough for Step 4 button hotkeys.
    SP<Widget> target = m_hover.lock();
    while (target) {
        EventReply r = e.isDown ? target->OnKeyDown(e) : target->OnKeyUp(e);
        if (r != EventReply::Unhandled) return true;
        target = target->GetParent().lock();
    }
    return false;
}

void InputRouter::ReleaseCapture() {
    m_capture.reset();
}

} // namespace uz::ui
