/// UzEngine Slate UI — TooltipManager.
///
/// Phase 18 / Step 9. A small registry of tooltip factories keyed by
/// owner widget. The application's frame loop drives:
///
///   - call `Update(hoveredWidget, cursorPos, dt)` once per frame.
///   - on a sustained hover for `delaySec` seconds, the manager calls
///     the registered factory and stores the resulting widget as the
///     "active tooltip" for the host's TopMost layer to draw.
///
/// Hover changes (cursor leaves the owner) drop the active tooltip and
/// reset the timer. Owners that go out of scope are auto-cleaned via
/// `WP<Widget>` membership checks each Update.

#pragma once

#include "engine/ui/slate_widget.h"

#include <functional>
#include <unordered_map>

namespace uz::ui {

class TooltipManager {
public:
    using Factory = std::function<SP<Widget>()>;

    /// Registers a tooltip factory for `owner`. The factory is called
    /// exactly once each time the cursor sustains hover on `owner`
    /// past `delaySec` seconds. `owner` is held weakly so its dtor
    /// auto-unregisters.
    void Register(const SP<Widget>& owner, Factory factory, float delaySec = 0.5f);

    /// Drops any registration for `owner`. Safe to call when not registered.
    void Unregister(Widget* owner);

    /// Per-frame update. `hoveredWidget` may be `nullptr`; `dt` is the
    /// frame delta. Returns the active tooltip widget (or nullptr).
    SP<Widget> Update(const SP<Widget>& hoveredWidget, Vec2 cursorPos, float dt);

    /// Currently displayed tooltip widget (nullptr when not visible).
    [[nodiscard]] SP<Widget> Active() const noexcept { return m_active; }

    /// Last cursor position the tooltip should anchor to (Slate units).
    [[nodiscard]] Vec2 AnchorPos() const noexcept { return m_anchorPos; }

private:
    struct Entry {
        WP<Widget> owner;
        Factory    factory;
        float      delay;
    };

    std::unordered_map<Widget*, Entry> m_entries;
    WP<Widget>                         m_currentHover;
    float                              m_hoverTimer{0.0f};
    SP<Widget>                         m_active;
    Vec2                               m_anchorPos{};
};

/// Process-wide singleton. UI-thread only.
TooltipManager& Tooltip();

} // namespace uz::ui
