#pragma once

/// VFX Splitter Widget — phase 15.2.0.
///
/// Stateless helper for a single vertical gutter between two
/// panels. Carries drag-anchor state internally so the host just
/// needs to feed `BeginDrag / UpdateDrag / EndDrag` on mouse events.

#include "engine/vfx/editor/vfx_stack_panel_widget.h"  // VfxStackRect

namespace uz {

class TextRenderer;

class VfxSplitterWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float gutterWidth = 6.0f;  ///< Hit-region width.
    float barWidth    = 2.0f;  ///< Drawn vertical bar centred in the gutter.

    // ── Hover state (set by the host before Render) ─────────────────────
    void SetHovered(bool h) { m_hovered = h; }
    [[nodiscard]] bool IsHovered() const { return m_hovered; }

    // ── Drag lifecycle ──────────────────────────────────────────────────

    void BeginDrag(float mouseX) {
        m_dragAnchorX = mouseX;
        m_dragging    = true;
    }

    /// Returns delta from the stored anchor; updates the anchor so
    /// back-to-back calls produce frame-local deltas.
    float UpdateDrag(float mouseX) {
        if (!m_dragging) return 0.0f;
        const float delta = mouseX - m_dragAnchorX;
        m_dragAnchorX = mouseX;
        return delta;
    }

    void EndDrag() {
        m_dragging    = false;
        m_dragAnchorX = 0.0f;
    }

    [[nodiscard]] bool IsDragging() const { return m_dragging; }

    // ── Hit-test + render ───────────────────────────────────────────────

    [[nodiscard]] bool HitTest(float x, float y,
                                const VfxStackRect& bounds) const {
        return x >= bounds.x && x < bounds.x + bounds.width &&
               y >= bounds.y && y < bounds.y + bounds.height;
    }

    /// Renders a 2-px vertical bar centred inside `bounds`. Colour
    /// picks by state: idle `borderSubtle`, hover `borderStrong`,
    /// dragging `accentPrimary`.
    void Render(TextRenderer& text, const VfxStackRect& bounds) const;

private:
    float m_dragAnchorX = 0.0f;
    bool  m_dragging    = false;
    bool  m_hovered     = false;
};

} // namespace uz
