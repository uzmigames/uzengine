#include "engine/ui/slate_button.h"

#include "engine/ui/slate_renderer.h"

#include <algorithm>

namespace uz::ui {

namespace {

float PadX(const Vec4& p) { return p.x + p.z; }
float PadY(const Vec4& p) { return p.y + p.w; }

} // anonymous namespace

Vec2 Button::Measure(Vec2 maxSize) {
    const Vec4 pad = Padding.Get();
    Vec2 desired{0.0f, 0.0f};
    if (Content) {
        const Vec2 inner = Content->Measure(Vec2{
            std::max(0.0f, maxSize.x - PadX(pad)),
            std::max(0.0f, maxSize.y - PadY(pad))});
        desired.x = inner.x + PadX(pad);
        desired.y = inner.y + PadY(pad);
    } else {
        // Bare button: minimum sensible size for a clickable surface.
        desired = Vec2{24.0f + PadX(pad), 16.0f + PadY(pad)};
    }
    if (maxSize.x > 0.0f && desired.x > maxSize.x) desired.x = maxSize.x;
    if (maxSize.y > 0.0f && desired.y > maxSize.y) desired.y = maxSize.y;
    return desired;
}

void Button::Arrange(Geometry geometry) {
    m_geometry = geometry;

    m_hitTestCache.clear();
    if (Content) {
        const Vec4 pad = Padding.Get();
        Geometry child{};
        child.absolutePos = Vec2{geometry.absolutePos.x + pad.x,
                                 geometry.absolutePos.y + pad.y};
        child.size = Vec2{
            std::max(0.0f, geometry.size.x - PadX(pad)),
            std::max(0.0f, geometry.size.y - PadY(pad))};
        child.dpiScale = geometry.dpiScale;
        Content->Arrange(child);
        m_hitTestCache.push_back(Content);
    }
}

void Button::Paint(SlateRenderer& renderer) {
    DrawElement bg{};
    bg.kind            = DrawKind::Quad;
    bg.rect            = Rect{m_geometry.absolutePos, m_geometry.size};
    bg.cornerRadius    = CornerRadius.Get();
    bg.borderThickness = BorderThickness.Get();
    bg.borderColor     = BorderColor.Get();

    if (m_pressed) {
        bg.color = PressedColor.Get();
    } else if (m_hovered) {
        bg.color = HoverColor.Get();
    } else {
        bg.color = NormalColor.Get();
    }
    renderer.PushElement(bg);

    if (Content) Content->Paint(renderer);
}

EventReply Button::OnPointerDown(const PointerEvent& e) {
    if (e.button != PointerButton::Left) return EventReply::Unhandled;
    m_pressed = true;
    return EventReply::HandledCapture;
}

EventReply Button::OnPointerUp(const PointerEvent& e) {
    if (e.button != PointerButton::Left) return EventReply::Unhandled;
    const bool wasPressed = m_pressed;
    const bool insideNow = m_geometry.ToRect().Contains(e.position);
    m_pressed = false;
    if (wasPressed && insideNow) {
        if (OnClick) OnClick();
    }
    return EventReply::Handled;
}

EventReply Button::OnPointerMove(const PointerEvent& /*e*/) {
    // Hover changes are routed via OnHoverEnter/Leave; PointerMove itself
    // is left unhandled so parents can still see scroll-on-drag etc.
    return EventReply::Unhandled;
}

void Button::OnHoverEnter() {
    m_hovered = true;
}

void Button::OnHoverLeave() {
    m_hovered = false;
    // Defensive: if the user pressed and dragged out, drop the press
    // state so a release outside doesn't fire a click on re-entry.
    m_pressed = false;
}

} // namespace uz::ui
