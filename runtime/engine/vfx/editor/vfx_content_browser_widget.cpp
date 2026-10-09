/// VfxContentBrowserWidget — implementation (phase 14.7.0).

#include "vfx_content_browser_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

#include <algorithm>

namespace uz {

std::vector<VfxAssetLayoutItem> VfxContentBrowserWidget::ComputeLayout(
    const VfxStackRect& panelBounds) const {
    std::vector<VfxAssetLayoutItem> out;
    out.reserve(m_tiles.size() * 3u);
    if (m_tiles.empty()) return out;

    const uint32_t cols = ColumnCount(panelBounds.width);
    const float tileH = TileTotalHeight();
    for (size_t i = 0; i < m_tiles.size(); ++i) {
        const uint32_t row = static_cast<uint32_t>(i) / cols;
        const uint32_t col = static_cast<uint32_t>(i) % cols;
        const float x = panelBounds.x + tileGap +
                        static_cast<float>(col) * (thumbSize + tileGap);
        const float y = panelBounds.y + tileGap +
                        static_cast<float>(row) * (tileH + tileGap);

        VfxAssetLayoutItem tile{};
        tile.type   = VfxAssetItemType::Tile;
        tile.rect   = { x, y, thumbSize, tileH };
        tile.tileId = m_tiles[i].id;
        out.push_back(tile);

        VfxAssetLayoutItem thumb{};
        thumb.type = VfxAssetItemType::Thumbnail;
        thumb.rect = { x, y, thumbSize, thumbSize };
        thumb.tileId = m_tiles[i].id;
        out.push_back(thumb);

        VfxAssetLayoutItem label{};
        label.type = VfxAssetItemType::Label;
        label.rect = { x, y + thumbSize, thumbSize, labelStripH };
        label.tileId = m_tiles[i].id;
        out.push_back(label);
    }
    return out;
}

uint32_t VfxContentBrowserWidget::HitTest(
    const std::vector<VfxAssetLayoutItem>& layout, float x, float y) const {
    for (const auto& it : layout) {
        if (it.type != VfxAssetItemType::Tile) continue;
        if (x >= it.rect.x && x < it.rect.x + it.rect.width &&
            y >= it.rect.y && y < it.rect.y + it.rect.height) {
            return it.tileId;
        }
    }
    return 0u;
}

void VfxContentBrowserWidget::Render(TextRenderer& text,
                                     const BitmapFont& font,
                                     const MsdfFont* msdf,
                                     const VfxStackRect& panelBounds) const {
    const auto& palette = VfxThemeColors();
    text.AddRect(panelBounds.x, panelBounds.y,
                 panelBounds.width, panelBounds.height, palette.panelBg);

    const auto layout = ComputeLayout(panelBounds);
    for (const auto& it : layout) {
        if (it.type != VfxAssetItemType::Tile) continue;

        // Locate tile record by id.
        const VfxAssetTile* tile = nullptr;
        for (const auto& t : m_tiles) {
            if (t.id == it.tileId) { tile = &t; break; }
        }
        if (tile == nullptr) continue;

        const bool selected = (tile->id == m_selectedId);
        const bool hovered  = (tile->id == m_hoveredId) && !selected;

        // Tile fill: selected → `cardBgSelected`, hovered → `cardBgHover`,
        // else `cardBg`.
        Vec4 fill = palette.cardBg;
        if (selected)     fill = palette.cardBgSelected;
        else if (hovered) fill = palette.cardBgHover;

        text.AddRoundedRect(it.rect.x, it.rect.y,
                             it.rect.width, it.rect.height,
                             tileRadius, fill);

        // Type-coloured 2-px top bar.
        text.AddRect(it.rect.x, it.rect.y, it.rect.width,
                     kindBarHeight, tile->kindTint);

        // Thumbnail region: 8x8 checkerboard fallback when no SRV.
        if (tile->thumbnailSrvIndex == 0xFFFFFFFFu) {
            const uint32_t steps = 8u;
            const float step = thumbSize / static_cast<float>(steps);
            for (uint32_t ry = 0; ry < steps; ++ry) {
                for (uint32_t rx = 0; rx < steps; ++rx) {
                    const bool dark = ((rx + ry) & 1u) != 0u;
                    const Vec4 c = dark ? palette.panelBgAlt : palette.panelBg;
                    text.AddRect(it.rect.x + rx * step,
                                  it.rect.y + kindBarHeight + ry * step,
                                  step, step, c);
                }
            }
        }
        // Real-SRV thumbnails would use a phase-future AddTexturedQuad
        // primitive; the widget ships with the fallback so tiles always
        // render something.

        // Label strip.
        text.AddRect(it.rect.x, it.rect.y + thumbSize,
                     it.rect.width, labelStripH,
                     selected ? palette.cardBgSelected : palette.cardBg);

        // Label text — centre-aligned, ellipsis on overflow.
        std::string display = tile->label;
        if (display.size() > 12u) display = display.substr(0, 11u) + "...";
        const float labelW = static_cast<float>(display.size()) * 7.0f;
        const float tx = it.rect.x + (it.rect.width - labelW) * 0.5f;
        const float ty = it.rect.y + thumbSize +
                          (labelStripH - 10.0f) * 0.5f + 1.0f;
        if (msdf != nullptr) {
            text.AddMsdfText(*msdf, display, tx, ty, 10.0f, palette.textPrimary);
        } else {
            text.AddText(font, display, tx, it.rect.y + thumbSize + 4.0f,
                          palette.textPrimary);
        }

        // Selection outline: accent border on all edges.
        if (selected) {
            const Vec4 b = palette.accentPrimary;
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, 2.0f, b);
            text.AddRect(it.rect.x, it.rect.y + it.rect.height - 2.0f,
                         it.rect.width, 2.0f, b);
            text.AddRect(it.rect.x, it.rect.y, 2.0f, it.rect.height, b);
            text.AddRect(it.rect.x + it.rect.width - 2.0f, it.rect.y,
                         2.0f, it.rect.height, b);
        }
    }
}

} // namespace uz
