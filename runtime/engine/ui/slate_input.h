/// UzEngine Slate UI — input events + InputRouter.
///
/// Phase 18 / Step 4. Defines the event currency between the platform's
/// raw input layer and the Slate widget tree, plus the deep-first
/// dispatcher that walks the tree.
///
/// Routing model (Slate-style):
///   1. **Hit-test** is **deepest-first**: the dispatcher walks the
///      tree depth-first, testing each widget's geometry, and selects
///      the *innermost* widget whose `Geometry::ToRect().Contains(p)`
///      is true. Containers are tested *after* their children — a
///      child Button under an HBox claims the click before the HBox.
///   2. **Bubble**: if the chosen widget returns `EventReply::Unhandled`,
///      the dispatcher walks back up the parent chain, offering each
///      ancestor a chance to handle. The first `Handled` /
///      `HandledCapture` wins; further ancestors do not see the event.
///   3. **Capture**: a `HandledCapture` reply on `OnPointerDown` makes
///      the captor the exclusive recipient of subsequent `OnPointerMove`
///      and `OnPointerUp` events until the next `PointerUp` (or
///      explicit release). This lets buttons keep tracking the cursor
///      after it leaves their bounds without losing the press.
///
/// Hover transitions: the dispatcher tracks the last hovered widget.
/// When the deepest hit changes, it fires `OnHoverLeave` on the old
/// target and `OnHoverEnter` on the new one. Same target → no calls.
///
/// Threading: dispatcher runs on the render thread (Decision 4).

#pragma once

#include "engine/ui/slate_core.h"

#include <cstdint>

namespace uz::ui {

class Widget;

// ─────────────────────────────────────────────────────────────────────────────
// Event types
// ─────────────────────────────────────────────────────────────────────────────

enum class PointerButton : std::uint8_t {
    None,
    Left,
    Right,
    Middle,
};

struct PointerEvent {
    Vec2          position{};      ///< Cursor position in Slate units.
    Vec2          delta{};         ///< Movement since last event (Move only).
    PointerButton button{PointerButton::None};
    bool          isDown{false};   ///< For Down/Up events: button transition state.
    float         wheelDelta{0.0f}; ///< Lines for wheel events; 0 for non-wheel.
};

struct KeyEvent {
    /// Platform-agnostic key identifier. Mirrors a subset of VK_* / scancodes;
    /// the InputSystem normalises before constructing the KeyEvent.
    std::uint32_t keyCode{0};
    bool          isDown{false};
    bool          ctrl{false};
    bool          alt{false};
    bool          shift{false};
    char32_t      character{0}; ///< Resolved char for text-input events; 0 otherwise.
};

enum class EventReply : std::uint8_t {
    Unhandled,        ///< Event bubbles to parent.
    Handled,          ///< Routing stops; no capture.
    HandledCapture,   ///< Routing stops; captor receives subsequent Move/Up.
};

// ─────────────────────────────────────────────────────────────────────────────
// InputRouter
// ─────────────────────────────────────────────────────────────────────────────

class InputRouter {
public:
    InputRouter() = default;

    /// Sets / replaces the root widget. The router stores a weak reference
    /// to the parent map it maintains internally; passing nullptr resets
    /// all state.
    void SetRoot(SP<Widget> root);

    /// Dispatches a pointer event to the widget tree.
    /// Returns `true` if any widget handled the event.
    bool DispatchPointer(const PointerEvent& e);

    /// Dispatches a key event to the focused widget (focus is owned by
    /// the FocusManager in Step 9; before that, key events go to the
    /// hovered widget for simplicity).
    bool DispatchKey(const KeyEvent& e);

    /// Releases any active pointer capture. Called by the dispatcher on
    /// PointerUp; exposed for callers that want to forcibly drop capture
    /// (window lost focus, modal opened).
    void ReleaseCapture();

    [[nodiscard]] SP<Widget> GetCapture() const { return m_capture.lock(); }
    [[nodiscard]] SP<Widget> GetHover()   const { return m_hover.lock();   }

private:
    /// Recursive helper: returns the deepest widget under `pos` whose
    /// geometry contains the point. Walks children in slot order; later
    /// siblings (drawn later → "on top") take priority on equal depth.
    [[nodiscard]] SP<Widget> HitTest(const SP<Widget>& root, Vec2 pos) const;

    SP<Widget>    m_root;
    WP<Widget>    m_capture;  ///< Active pointer capture target (if any).
    WP<Widget>    m_hover;    ///< Last hover target.
};

} // namespace uz::ui
