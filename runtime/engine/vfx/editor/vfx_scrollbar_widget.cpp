/// VfxScrollBar — implementation (phase 14.9.0).

#include "vfx_scrollbar_widget.h"

#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

namespace uz {

void VfxScrollBar::Render(TextRenderer& text) const {
    if (!NeedsScrollbar()) return;
    const auto& palette = VfxThemeColors();

    // Track (subtle fill so idle scrollbars don't dominate the panel).
    const VfxStackRect track = TrackRect();
    Vec4 trackFill = palette.panelBgAlt;
    trackFill.w = 0.30f;
    text.AddRect(track.x, track.y, track.width, track.height, trackFill);

    // Thumb — alpha varies by state.
    const VfxStackRect thumb = ThumbRect();
    Vec4 thumbFill;
    if (m_pressed) {
        thumbFill = palette.accentPrimary;
    } else {
        thumbFill = palette.borderStrong;
        thumbFill.w = m_hovered ? 0.85f : 0.45f;
    }
    const float radius = track.width * 0.5f;
    text.AddRoundedRect(thumb.x, thumb.y, thumb.width, thumb.height,
                         radius, thumbFill);
}

} // namespace uz
