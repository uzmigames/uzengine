/// UzEngine Slate UI — FocusManager.
///
/// Phase 18 / Step 9. A standalone manager that owns the "currently
/// focused widget" reference and walks the tree for tab cycling.
///
/// Widgets opt into being focusable by overriding `IsFocusable()` to
/// return `true` (Step 9 default override on `Widget` base — see
/// `slate_widget.h`). EditableText is a focusable widget; layout
/// containers are not.
///
/// SetFocus(new) fires `OnFocusLost` on the previous target then
/// `OnFocusReceived` on the new one, in that order. Setting nullptr
/// clears focus and fires Lost on the old target only.
///
/// CycleNext walks the tree DFS (via `HitTestChildren`), collects
/// every focusable widget in source order, and advances the focus to
/// the next one (wrapping around). CyclePrev is the reverse.
///
/// Threading: same as the rest of the framework — call from the UI
/// thread only.

#pragma once

#include "engine/ui/slate_widget.h"

namespace uz::ui {

class FocusManager {
public:
    /// Sets the focused widget. Pass `nullptr` to clear focus.
    void SetFocus(SP<Widget> widget);

    /// Returns the currently focused widget, or nullptr.
    [[nodiscard]] SP<Widget> GetFocus() const noexcept { return m_focused.lock(); }

    /// Sets the root that CycleNext / CyclePrev will walk. Typically
    /// the same root the InputRouter knows about.
    void SetRoot(SP<Widget> root) { m_root = std::move(root); }

    /// Advances focus to the next focusable widget under `m_root`,
    /// wrapping around. Returns the new focus target (may be nullptr
    /// if no focusable widget is in the tree).
    SP<Widget> CycleNext();

    /// Reverse of CycleNext.
    SP<Widget> CyclePrev();

private:
    SP<Widget> m_root;
    WP<Widget> m_focused;
};

/// Process-wide singleton (UI thread only).
FocusManager& Focus();

} // namespace uz::ui
