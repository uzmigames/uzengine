/// UzEngine Slate UI — HorizontalBox / VerticalBox / Overlay containers.
///
/// Phase 18 / Step 3. The three foundational containers:
///
///   - `HorizontalBox` (alias `HBox`) — children laid out left-to-right.
///   - `VerticalBox`   (alias `VBox`) — children laid out top-to-bottom.
///   - `Overlay`                       — children stacked z-order on the
///     same rect; per-slot HAlign/VAlign aligns each child within the
///     parent's allocation.
///
/// Layout follows Slate's two-pass model:
///
///   1. **Measure**: each child reports a desired size against the
///      `maxSize` it would receive *if* it were the only child. The
///      container aggregates: HBox sums on the X axis + max on the Y;
///      VBox max on X + sums on Y; Overlay max on both axes.
///   2. **Arrange**: with the final geometry from the parent, the
///      container distributes axis space:
///        - `SizeRule::Auto` slots get exactly `Measure(...).x` (HBox)
///          or `.y` (VBox).
///        - `SizeRule::Fill` slots split the remainder pro-rata by
///          `fillFactor`.
///        - All slots are then HAlign'd / VAlign'd inside their cell.
///
/// Padding is subtracted from each slot's allocation before the child
/// receives its rect. The container's own outer padding lives on its
/// `Outer` member if needed; default is zero.

#pragma once

#include "engine/ui/slate_slot.h"
#include "engine/ui/slate_widget.h"

#include <vector>

namespace uz::ui {

// ─────────────────────────────────────────────────────────────────────────────
// HorizontalBox / VerticalBox
// ─────────────────────────────────────────────────────────────────────────────

class HorizontalBox : public Widget {
public:
    /// Adds `child` as a new slot at the end of the row.
    /// Returns a reference to the slot for chained configuration.
    /// Parent linkage for input bubble is wired separately by
    /// `InputRouter::SetRoot` walking the tree (see `slate_input.cpp`).
    SlotBase& AddSlot(SP<Widget> child);

    /// Number of currently-attached slots.
    [[nodiscard]] std::size_t SlotCount() const noexcept { return m_slots.size(); }

    /// Direct read-only access for tests and Step-8 declarative wiring.
    [[nodiscard]] const std::vector<SlotBase>& Slots() const noexcept { return m_slots; }

    Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry geometry) override;
    void Paint(SlateRenderer& renderer) override;

    [[nodiscard]] const std::vector<SP<Widget>>* HitTestChildren() const noexcept override {
        return &m_hitTestCache;
    }

protected:
    std::vector<SlotBase>      m_slots;
    std::vector<SP<Widget>>    m_hitTestCache; ///< Mirrors slot widgets for HitTestChildren.
};

class VerticalBox : public Widget {
public:
    SlotBase& AddSlot(SP<Widget> child);
    [[nodiscard]] std::size_t SlotCount() const noexcept { return m_slots.size(); }
    [[nodiscard]] const std::vector<SlotBase>& Slots() const noexcept { return m_slots; }

    Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry geometry) override;
    void Paint(SlateRenderer& renderer) override;

    [[nodiscard]] const std::vector<SP<Widget>>* HitTestChildren() const noexcept override {
        return &m_hitTestCache;
    }

protected:
    std::vector<SlotBase>   m_slots;
    std::vector<SP<Widget>> m_hitTestCache;
};

// Conventional short aliases — match Unreal Slate's `SHBox`/`SVBox` ergonomics.
using HBox = HorizontalBox;
using VBox = VerticalBox;

// ─────────────────────────────────────────────────────────────────────────────
// Overlay
// ─────────────────────────────────────────────────────────────────────────────

class Overlay : public Widget {
public:
    SlotBase& AddSlot(SP<Widget> child);
    [[nodiscard]] std::size_t SlotCount() const noexcept { return m_slots.size(); }
    [[nodiscard]] const std::vector<SlotBase>& Slots() const noexcept { return m_slots; }

    Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry geometry) override;
    void Paint(SlateRenderer& renderer) override;

    [[nodiscard]] const std::vector<SP<Widget>>* HitTestChildren() const noexcept override {
        return &m_hitTestCache;
    }

protected:
    std::vector<SlotBase>   m_slots;
    std::vector<SP<Widget>> m_hitTestCache;
};

} // namespace uz::ui
