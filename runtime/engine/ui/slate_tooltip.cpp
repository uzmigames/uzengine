#include "engine/ui/slate_tooltip.h"

namespace uz::ui {

void TooltipManager::Register(const SP<Widget>& owner, Factory factory, float delaySec) {
    if (!owner) return;
    Entry e{owner, std::move(factory), delaySec};
    m_entries[owner.get()] = std::move(e);
}

void TooltipManager::Unregister(Widget* owner) {
    m_entries.erase(owner);
    if (m_currentHover.lock().get() == owner) {
        m_currentHover.reset();
        m_hoverTimer = 0.0f;
        m_active.reset();
    }
}

SP<Widget> TooltipManager::Update(const SP<Widget>& hoveredWidget, Vec2 cursorPos, float dt) {
    SP<Widget> prev = m_currentHover.lock();
    if (hoveredWidget != prev) {
        m_currentHover = hoveredWidget;
        m_hoverTimer = 0.0f;
        m_active.reset();
    }

    m_anchorPos = cursorPos;

    if (!hoveredWidget) return nullptr;

    auto it = m_entries.find(hoveredWidget.get());
    if (it == m_entries.end()) return nullptr;
    if (it->second.owner.expired()) {
        m_entries.erase(it);
        return nullptr;
    }

    m_hoverTimer += dt;
    if (m_hoverTimer >= it->second.delay && !m_active) {
        m_active = it->second.factory ? it->second.factory() : nullptr;
    }
    return m_active;
}

TooltipManager& Tooltip() {
    static TooltipManager g;
    return g;
}

} // namespace uz::ui
