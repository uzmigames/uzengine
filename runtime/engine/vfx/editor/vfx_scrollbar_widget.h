#pragma once

/// VFX Scroll Bar Widget — phase 14.9.0.
///
/// UE5-style 6-px idle / 8-px hover slim scrollbar. Stateless
/// data-driven widget — the host feeds `panelRect` + `contentHeight`
/// + `scrollY` each frame; the widget produces the thumb geometry
/// and provides mouse + wheel helpers.

#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"

#include <algorithm>

namespace uz {

class TextRenderer;

class VfxScrollBar {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float idleWidth     = 6.0f;
    float hoverWidth    = 8.0f;
    float minThumbHeight = 20.0f;

    // ── Input state ─────────────────────────────────────────────────────

    /// Sets panel + content dimensions for the next render/hit-test.
    void SetView(const VfxStackRect& panelRect, float contentHeight) {
        m_panelRect    = panelRect;
        m_contentHeight = (contentHeight < 1.0f) ? 1.0f : contentHeight;
    }

    void SetScrollY(float s) {
        m_scrollY = std::clamp(s, 0.0f, MaxScroll());
    }
    [[nodiscard]] float ScrollY() const { return m_scrollY; }

    void SetHovered(bool h) { m_hovered = h; }
    void SetPressed(bool p) { m_pressed = p; }

    // ── Derived geometry ────────────────────────────────────────────────

    [[nodiscard]] float MaxScroll() const {
        const float excess = m_contentHeight - m_panelRect.height;
        return (excess < 0.0f) ? 0.0f : excess;
    }

    [[nodiscard]] bool NeedsScrollbar() const { return MaxScroll() > 0.0f; }

    [[nodiscard]] float Width() const {
        return (m_hovered || m_pressed) ? hoverWidth : idleWidth;
    }

    [[nodiscard]] VfxStackRect TrackRect() const {
        return {
            m_panelRect.x + m_panelRect.width - Width(),
            m_panelRect.y,
            Width(),
            m_panelRect.height
        };
    }

    [[nodiscard]] float ThumbHeight() const {
        const float ratio = std::min(1.0f, m_panelRect.height / m_contentHeight);
        return std::max(minThumbHeight, m_panelRect.height * ratio);
    }

    [[nodiscard]] VfxStackRect ThumbRect() const {
        const float maxScroll = MaxScroll();
        const float travel = m_panelRect.height - ThumbHeight();
        const float y = (maxScroll > 0.0f)
            ? m_panelRect.y + (m_scrollY / maxScroll) * travel
            : m_panelRect.y;
        return {
            m_panelRect.x + m_panelRect.width - Width(),
            y,
            Width(),
            ThumbHeight()
        };
    }

    // ── Input helpers ───────────────────────────────────────────────────

    /// Advances `scrollY` by the mouse-wheel notch delta. Positive
    /// `wheelDelta` scrolls UP (reduces scrollY). The caller feeds raw
    /// wheel events (e.g. `-1.0f` per notch down on Windows).
    void Scroll(float wheelDelta, float pixelsPerNotch = 40.0f) {
        SetScrollY(m_scrollY - wheelDelta * pixelsPerNotch);
    }

    /// Applies a thumb-drag `dy` (screen pixels). Converts to
    /// content-space scrollY delta.
    void Drag(float dy) {
        const float maxScroll = MaxScroll();
        const float travel = m_panelRect.height - ThumbHeight();
        if (travel <= 0.0f || maxScroll <= 0.0f) return;
        SetScrollY(m_scrollY + dy * (maxScroll / travel));
    }

    /// Convenience page-up / page-down (click above or below the thumb).
    void PageUp()   { SetScrollY(m_scrollY - m_panelRect.height); }
    void PageDown() { SetScrollY(m_scrollY + m_panelRect.height); }

    /// Renders the track + thumb. Emits nothing when `!NeedsScrollbar`.
    void Render(TextRenderer& text) const;

private:
    VfxStackRect m_panelRect{};
    float m_contentHeight = 1.0f;
    float m_scrollY       = 0.0f;
    bool  m_hovered       = false;
    bool  m_pressed       = false;
};

} // namespace uz
