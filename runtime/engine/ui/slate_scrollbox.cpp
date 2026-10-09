#include "engine/ui/slate_scrollbox.h"

#include "engine/ui/slate_renderer.h"

#include <algorithm>

namespace uz::ui {

namespace {
constexpr float kScrollbarWidth = 8.0f;
}

void ScrollBox::SetContent(SP<Widget> child) {
    m_content = child;
    m_hitTestCache.clear();
    if (m_content) m_hitTestCache.push_back(m_content);
    Invalidate(InvalidationFlag::Layout);
}

Vec2 ScrollBox::Measure(Vec2 maxSize) {
    if (!m_content) return Vec2{0.0f, 0.0f};
    const float reserveX = ShowScrollbar.Get() ? kScrollbarWidth : 0.0f;
    const Vec2 childMax{
        std::max(0.0f, maxSize.x - reserveX),
        // Unbounded vertically so the child can claim its full natural height.
        // Caller's Arrange decides how much we actually display.
        std::numeric_limits<float>::max()};
    Vec2 childDesired = m_content->Measure(childMax);
    Vec2 desired = Vec2{childDesired.x + reserveX, childDesired.y};
    if (maxSize.x > 0.0f && desired.x > maxSize.x) desired.x = maxSize.x;
    if (maxSize.y > 0.0f && desired.y > maxSize.y) desired.y = maxSize.y;
    return desired;
}

void ScrollBox::Arrange(Geometry geometry) {
    m_geometry = geometry;
    if (!m_content) return;

    const float reserveX = ShowScrollbar.Get() ? kScrollbarWidth : 0.0f;
    const Vec2 viewportSize{
        std::max(0.0f, geometry.size.x - reserveX),
        geometry.size.y};

    // Re-measure the child against the viewport width to get its true height.
    Vec2 childDesired = m_content->Measure(Vec2{
        viewportSize.x,
        std::numeric_limits<float>::max()});

    // Clamp ScrollY against the new content height.
    m_maxScroll = std::max(0.0f, childDesired.y - viewportSize.y);
    float scroll = ScrollY.Get();
    if (scroll < 0.0f)              scroll = 0.0f;
    if (scroll > m_maxScroll)       scroll = m_maxScroll;
    if (scroll != ScrollY.Get())    ScrollY = scroll;

    Geometry childGeo{};
    childGeo.absolutePos = Vec2{
        geometry.absolutePos.x,
        geometry.absolutePos.y - scroll};
    childGeo.size = Vec2{viewportSize.x, childDesired.y};
    childGeo.dpiScale = geometry.dpiScale;
    m_content->Arrange(childGeo);
}

void ScrollBox::Paint(SlateRenderer& renderer) {
    if (!m_content) return;

    // Push scissor to clip child paint.
    DrawElement push{};
    push.kind = DrawKind::ScissorPush;
    push.rect = m_geometry.ToRect();
    renderer.PushElement(push);

    m_content->Paint(renderer);

    DrawElement pop{};
    pop.kind = DrawKind::ScissorPop;
    renderer.PushElement(pop);

    // Scrollbar (right edge).
    if (ShowScrollbar.Get() && m_maxScroll > 0.0f) {
        const float trackX = m_geometry.absolutePos.x + m_geometry.size.x - kScrollbarWidth;
        const float trackY = m_geometry.absolutePos.y;
        const float trackH = m_geometry.size.y;

        // Track (subtle).
        DrawElement track{};
        track.kind = DrawKind::Quad;
        track.rect = Rect{Vec2{trackX, trackY}, Vec2{kScrollbarWidth, trackH}};
        track.color = Color{0.12f, 0.13f, 0.15f, 0.65f};
        track.cornerRadius = 2.0f;
        renderer.PushElement(track);

        const float content = m_maxScroll + trackH; // total content height
        const float thumbH = std::max(16.0f, trackH * (trackH / content));
        const float thumbY = trackY + (ScrollY.Get() / m_maxScroll) * (trackH - thumbH);

        DrawElement thumb{};
        thumb.kind = DrawKind::Quad;
        thumb.rect = Rect{Vec2{trackX, thumbY}, Vec2{kScrollbarWidth, thumbH}};
        thumb.color = Color{0.50f, 0.54f, 0.60f, 0.8f};
        thumb.cornerRadius = 2.0f;
        renderer.PushElement(thumb);
    }
}

EventReply ScrollBox::OnPointerWheel(const PointerEvent& e) {
    if (m_maxScroll <= 0.0f) return EventReply::Unhandled;
    const float step = WheelStep.Get();
    float scroll = ScrollY.Get() - e.wheelDelta * step;
    if (scroll < 0.0f)        scroll = 0.0f;
    if (scroll > m_maxScroll) scroll = m_maxScroll;
    if (scroll != ScrollY.Get()) {
        ScrollY = scroll;
        Invalidate(InvalidationFlag::Layout);
    }
    return EventReply::Handled;
}

} // namespace uz::ui
