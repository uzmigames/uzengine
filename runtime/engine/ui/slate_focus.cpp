#include "engine/ui/slate_focus.h"

#include <vector>

namespace uz::ui {

namespace {

void CollectFocusable(const SP<Widget>& root, std::vector<SP<Widget>>& out) {
    if (!root) return;
    if (root->IsFocusable()) out.push_back(root);
    if (auto* children = root->HitTestChildren()) {
        for (const auto& c : *children) {
            CollectFocusable(c, out);
        }
    }
}

} // anonymous namespace

void FocusManager::SetFocus(SP<Widget> widget) {
    SP<Widget> old = m_focused.lock();
    if (old == widget) return;
    if (old)    old->OnFocusLost();
    m_focused = widget;
    if (widget) widget->OnFocusReceived();
}

SP<Widget> FocusManager::CycleNext() {
    if (!m_root) return nullptr;
    std::vector<SP<Widget>> list;
    CollectFocusable(m_root, list);
    if (list.empty()) return nullptr;

    SP<Widget> current = m_focused.lock();
    std::size_t next = 0;
    if (current) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i] == current) { next = (i + 1) % list.size(); break; }
        }
    }
    SetFocus(list[next]);
    return list[next];
}

SP<Widget> FocusManager::CyclePrev() {
    if (!m_root) return nullptr;
    std::vector<SP<Widget>> list;
    CollectFocusable(m_root, list);
    if (list.empty()) return nullptr;

    SP<Widget> current = m_focused.lock();
    std::size_t prev = list.size() - 1;
    if (current) {
        for (std::size_t i = 0; i < list.size(); ++i) {
            if (list[i] == current) {
                prev = (i == 0) ? list.size() - 1 : i - 1;
                break;
            }
        }
    }
    SetFocus(list[prev]);
    return list[prev];
}

FocusManager& Focus() {
    static FocusManager g;
    return g;
}

} // namespace uz::ui
