/// UzEngine Slate UI — Button widget.
///
/// Phase 18 / Step 4. Click semantics follow Slate / WPF / web:
///
///   1. PointerDown inside  → m_pressed = true, capture, Handled.
///   2. PointerMove leaves  → still pressed (capture keeps it tracking).
///   3. PointerUp inside    → invokes OnClick, m_pressed = false.
///   4. PointerUp outside   → m_pressed = false, NO OnClick.
///   5. HoverLeave during press → m_pressed = false (defensive).
///
/// Visual cascade for the background quad:
///     pressed:   PressedColor
///     hovered:   HoverColor
///     idle:      NormalColor
///
/// `Content` is an arbitrary child widget (typically a `TextBlock`).
/// Its position is the button's geometry inset by `Padding` (default
/// `{8, 4, 8, 4}` Slate units).

#pragma once

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_widget.h"

#include <functional>
#include <vector>

namespace uz::ui {

class Button : public Widget {
public:
    SP<Widget>            Content;       ///< Optional inner widget (e.g. TextBlock).
    Attribute<Color>      NormalColor{Color{0.18f, 0.18f, 0.20f, 1.0f}};
    Attribute<Color>      HoverColor{Color{0.24f, 0.26f, 0.30f, 1.0f}};
    Attribute<Color>      PressedColor{Color{0.10f, 0.12f, 0.14f, 1.0f}};
    Attribute<float>      CornerRadius{4.0f};
    Attribute<Color>      BorderColor{Color{0.0f, 0.0f, 0.0f, 0.0f}};
    Attribute<float>      BorderThickness{0.0f};
    Attribute<Vec4>       Padding{Vec4{8.0f, 4.0f, 8.0f, 4.0f}};

    /// Invoked once per Press → Up sequence that completes inside the button.
    std::function<void()> OnClick;

    [[nodiscard]] Vec2 Measure(Vec2 maxSize) override;
    void Arrange(Geometry geometry) override;
    void Paint(SlateRenderer& renderer) override;

    EventReply OnPointerDown(const PointerEvent& e) override;
    EventReply OnPointerUp(const PointerEvent& e) override;
    EventReply OnPointerMove(const PointerEvent& e) override;
    void       OnHoverEnter() override;
    void       OnHoverLeave() override;

    [[nodiscard]] const std::vector<SP<Widget>>* HitTestChildren() const noexcept override {
        return &m_hitTestCache;
    }

    /// Test accessors.
    [[nodiscard]] bool IsHovered() const noexcept { return m_hovered; }
    [[nodiscard]] bool IsPressed() const noexcept { return m_pressed; }

private:
    bool m_hovered{false};
    bool m_pressed{false};
    /// Re-built every Arrange so tests / router see Content as a child
    /// when set, and an empty list otherwise.
    std::vector<SP<Widget>> m_hitTestCache;
};

} // namespace uz::ui
