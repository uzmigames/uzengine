/// UzEngine Slate UI — ScrollBox container.
///
/// Phase 18 / Step 7. A single-child container that:
///   - measures its child against an unbounded vertical extent so the
///     child can request its full natural height,
///   - clips the child's paint to the box's geometry via
///     `DrawKind::ScissorPush` / `ScissorPop`,
///   - tracks a vertical `ScrollY` offset (Slate units) that the child
///     paint is translated by,
///   - draws an 8 px-wide scrollbar on the right with a thumb whose
///     length is proportional to viewport / content,
///   - converts mouse-wheel deltas into `ScrollY` adjustments (clamped).
///
/// Horizontal scrolling, scrollbar drag, and overscroll bounce are
/// out-of-scope for Step 7. They land alongside the VFX editor's
/// Details panel needs in Step 10.

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_widget.h"

#include <vector>

namespace uz::ui {

class ScrollBox : public Widget {
public:
    /// Replaces the single content widget. Older content is dropped.
    void SetContent(SP<Widget> child);

    /// Current vertical scroll offset in Slate units (clamped 0..maxScroll
    /// at Arrange time).
    Attribute<float> ScrollY{0.0f};

    /// Show the right-edge scrollbar. Default true.
    Attribute<bool> ShowScrollbar{true};

    /// Pixels (Slate units) of vertical scroll consumed per wheel notch.
    Attribute<float> WheelStep{30.0f};

    Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry geometry) override;
    void Paint(SlateRenderer& renderer) override;

    EventReply OnPointerWheel(const PointerEvent& e) override;

    [[nodiscard]] const std::vector<SP<Widget>>* HitTestChildren() const noexcept override {
        return &m_hitTestCache;
    }

    /// Read-only test access.
    [[nodiscard]] float MaxScroll() const noexcept { return m_maxScroll; }
    [[nodiscard]] SP<Widget> Content() const noexcept { return m_content; }

private:
    SP<Widget>              m_content;
    std::vector<SP<Widget>> m_hitTestCache;
    float                   m_maxScroll{0.0f};
};

} // namespace uz::ui
