/// UzEngine Slate UI — Widget base class.
///
/// Phase 18 / Step 2. Defines the retained-mode widget abstraction:
/// every UI element derives from `Widget` and implements (a subset of)
/// the Construct → Tick → Measure → Arrange → Paint lifecycle.
///
/// Lifecycle (per frame, after Construct):
///   1. Tick(dt)               — pull data via Attribute<T>; may flag dirty.
///   2. Measure(maxSize)       — return desired size (no allocation yet).
///   3. Arrange(geometry)      — accept allocated rect; propagate to children.
///   4. Paint(slateRenderer)   — emit DrawElements based on current geometry.
///
/// Widgets are heap-only and managed via `SP<Widget>` (Decision 2).
/// Constructors are protected — use `std::make_shared<W>()` or the
/// declarative `New<W>()` factory (Step 8) so every widget gets
/// `Construct()` called exactly once after construction.

#pragma once

#include "engine/ui/slate_core.h"
#include "engine/ui/slate_input.h"

#include <cstdint>
#include <vector>

namespace uz::ui {

class SlateRenderer;

/// Final geometry assigned to a widget by its parent's Arrange pass.
/// Coordinates are absolute (parent-resolved) Slate units.
struct Geometry {
    Vec2  absolutePos{};      ///< Top-left in screen-space Slate units.
    Vec2  size{};             ///< Allocated width / height in Slate units.
    float dpiScale{1.0f};     ///< Forwarded to descendants for hit-test math.

    /// Convenience: rect in absolute coords.
    [[nodiscard]] Rect ToRect() const noexcept {
        return Rect{absolutePos, size};
    }
};

/// Base widget. All concrete widgets inherit from this.
///
/// Subclasses MUST override `Measure` and `Paint`. They MAY override
/// `Construct`, `Tick`, and `Arrange`. Containers MUST override
/// `Arrange` to propagate geometry into their children's `Arrange`.
class Widget {
public:
    virtual ~Widget() = default;

    // Lifecycle ---------------------------------------------------------

    /// Called once after construction (typically by the `New<>()` factory
    /// or by `std::make_shared` followed by an explicit call from the
    /// owning code). Use it for one-time setup that requires `shared_from_this`
    /// or other post-construction state.
    virtual void Construct() {}

    /// Called once per frame before layout. Pull external data via
    /// `Attribute<T>::Get()` here — NOT inside Paint.
    virtual void Tick(float /*dt*/) {}

    /// Returns the widget's desired size for the given maximum allocation.
    /// MUST not allocate, MUST not invoke Paint, MUST not mutate observable
    /// children layout state. Pure measurement.
    [[nodiscard]] virtual Vec2 Measure(Vec2 maxSize) = 0;

    /// Receives the final geometry from the parent. Default impl stores
    /// the geometry; container widgets override to also arrange children.
    virtual void Arrange(Geometry geometry);

    /// Emits draw elements for the widget into the renderer.
    /// MUST consult `m_geometry` (set by Arrange) for positions/sizes.
    /// MUST NOT call `TextRenderer` directly — use the SlateRenderer.
    virtual void Paint(SlateRenderer& renderer) = 0;

    // Input hooks (Step 4) ----------------------------------------------

    virtual EventReply OnPointerDown(const PointerEvent& /*e*/)  { return EventReply::Unhandled; }
    virtual EventReply OnPointerUp(const PointerEvent& /*e*/)    { return EventReply::Unhandled; }
    virtual EventReply OnPointerMove(const PointerEvent& /*e*/)  { return EventReply::Unhandled; }
    virtual EventReply OnPointerWheel(const PointerEvent& /*e*/) { return EventReply::Unhandled; }
    virtual EventReply OnKeyDown(const KeyEvent& /*e*/)          { return EventReply::Unhandled; }
    virtual EventReply OnKeyUp(const KeyEvent& /*e*/)            { return EventReply::Unhandled; }

    /// Pointer entered the widget (different from the previously-hovered one).
    virtual void OnHoverEnter() {}
    /// Pointer left the widget for a different target (or no target).
    virtual void OnHoverLeave() {}

    // Tree linkage ------------------------------------------------------

    /// Container widgets MUST override to expose their children for
    /// hit-test traversal. Default: no children. Returned vector is
    /// stable across calls within the same frame; callers iterate in
    /// the returned order (later → "on top").
    [[nodiscard]] virtual const std::vector<SP<Widget>>* HitTestChildren() const noexcept {
        return nullptr;
    }

    // Geometry ----------------------------------------------------------

    [[nodiscard]] const Geometry& GetGeometry() const noexcept { return m_geometry; }

    /// Parent linkage maintained by container widgets when they call
    /// `AddSlot` / `SetParent`. Used by InputRouter for bubble routing.
    [[nodiscard]] WP<Widget> GetParent() const noexcept { return m_parent; }
    void SetParent(WP<Widget> parent) noexcept { m_parent = std::move(parent); }

protected:
    Widget() = default;

    /// Set by `Arrange`. Subclasses read it from `Paint`.
    Geometry m_geometry{};

    /// Weak parent ref for input bubble. Containers wire this up in
    /// `AddSlot`. Default empty for orphans / root widgets.
    WP<Widget> m_parent;
};

} // namespace uz::ui
