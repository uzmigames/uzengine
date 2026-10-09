/// VfxSplitterWidget — implementation (phase 15.2.0).

#include "vfx_splitter_widget.h"

#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

namespace uz {

void VfxSplitterWidget::Render(TextRenderer& text,
                               const VfxStackRect& bounds) const {
    const auto& palette = VfxThemeColors();

    // Gutter background — transparent-alpha fill so the panels
    // behind stay visible; only the centred bar reads.
    // (Skipped: we render just the bar, not a full-width fill.)

    Vec4 barColor = palette.borderSubtle;
    if (m_dragging)       barColor = palette.accentPrimary;
    else if (m_hovered)   barColor = palette.borderStrong;

    const float barX = bounds.x + (bounds.width - barWidth) * 0.5f;
    const float barY = bounds.y + 4.0f;
    const float barH = std::max(0.0f, bounds.height - 8.0f);
    text.AddRect(barX, barY, barWidth, barH, barColor);
}

} // namespace uz
