/// VfxTabStripWidget — implementation (phase 14.2.0).

#include "vfx_tab_strip_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Measure
// ─────────────────────────────────────────────────────────────────────────────

float VfxTabStripWidget::MeasureTab(const VfxTabEntry& entry) const {
    // Rough label width via 7 px per character (matches the bitmap
    // font's advance width for the editor mono font at body size).
    const float labelW = static_cast<float>(entry.label.size()) * 7.0f;
    const float iconW  = (entry.icon != VfxUiIcon::None) ? 16.0f : 0.0f;
    const float iconGap = (iconW > 0.0f) ? 4.0f : 0.0f;
    const float closeGap = entry.closable ? (closeIconSize + 6.0f) : 0.0f;
    const float content = iconW + iconGap + labelW + closeGap;
    return std::max(tabMinWidth, content + tabPadding * 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxTabLayoutItem> VfxTabStripWidget::ComputeLayout(
    const VfxStackRect& stripBounds) const {
    std::vector<VfxTabLayoutItem> out;
    if (m_tabs.empty()) return out;

    float cursorX = stripBounds.x;
    const float h = stripHeight;
    const float y = stripBounds.y + (stripBounds.height - h) * 0.5f;

    for (size_t i = 0; i < m_tabs.size(); ++i) {
        const VfxTabEntry& entry = m_tabs[i];
        const float w = MeasureTab(entry);

        VfxTabLayoutItem tab{};
        tab.type = VfxTabItemType::Tab;
        tab.rect = { cursorX, y, w, h };
        tab.tabIndex = i;
        out.push_back(tab);

        if (entry.closable) {
            VfxTabLayoutItem close{};
            close.type = VfxTabItemType::CloseIcon;
            close.rect = {
                cursorX + w - tabPadding - closeIconSize,
                y + (h - closeIconSize) * 0.5f,
                closeIconSize,
                closeIconSize
            };
            close.tabIndex = i;
            out.push_back(close);
        }

        cursorX += w;

        // Emit 1-px divider between adjacent tabs.
        if (i + 1 < m_tabs.size()) {
            VfxTabLayoutItem div{};
            div.type = VfxTabItemType::Divider;
            div.rect = { cursorX, y + 4.0f, 1.0f, h - 8.0f };
            div.tabIndex = i;
            out.push_back(div);
        }
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxTabStripWidget::HitResult VfxTabStripWidget::HitTest(
    const std::vector<VfxTabLayoutItem>& layout, float x, float y) const {
    HitResult result{};
    // Close icon takes priority (it sits inside a tab rect).
    for (const auto& it : layout) {
        if (it.type != VfxTabItemType::CloseIcon) continue;
        if (x >= it.rect.x && x < it.rect.x + it.rect.width &&
            y >= it.rect.y && y < it.rect.y + it.rect.height) {
            result.type = VfxTabItemType::CloseIcon;
            result.tabIndex = it.tabIndex;
            return result;
        }
    }
    for (const auto& it : layout) {
        if (it.type != VfxTabItemType::Tab) continue;
        if (x >= it.rect.x && x < it.rect.x + it.rect.width &&
            y >= it.rect.y && y < it.rect.y + it.rect.height) {
            result.type = VfxTabItemType::Tab;
            result.tabIndex = it.tabIndex;
            return result;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxTabStripWidget::Render(TextRenderer& text, const BitmapFont& font,
                               const MsdfFont* msdf,
                               const VfxStackRect& stripBounds) const {
    const auto& palette = VfxThemeColors();

    // Background band behind all tabs — `panelBg` so unused strip
    // area blends with the surrounding panel column.
    text.AddRect(stripBounds.x, stripBounds.y,
                 stripBounds.width, stripBounds.height,
                 palette.panelBg);

    const auto layout = ComputeLayout(stripBounds);
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxTabItemType::Tab: {
            const bool active  = (it.tabIndex == m_activeIdx);
            const bool hovered = (it.tabIndex == m_hoveredIdx);
            Vec4 fill = palette.panelBg;
            if (active)        fill = palette.cardBg;    // `#2C2C2C` merges with panel body
            else if (hovered)  fill = palette.barBg;     // `#242424` hover
            // Tab body (top corners rounded, bottom corners square —
            // since AddRoundedRect uniformly rounds all four corners,
            // we approximate by drawing the rrect and overdrawing a
            // flat rect along the bottom `radius` px to square the
            // bottom edge visually).
            text.AddRoundedRect(it.rect.x, it.rect.y,
                                it.rect.width, it.rect.height,
                                tabRadius, fill);
            // Square the bottom by overdrawing the lower band.
            if (tabRadius > 0.0f) {
                text.AddRect(it.rect.x, it.rect.y + it.rect.height - tabRadius,
                             it.rect.width, tabRadius, fill);
            }

            // Active-tab 2-px accent bar along the top edge.
            if (active) {
                text.AddRect(it.rect.x, it.rect.y, it.rect.width,
                             accentBarH, palette.accentPrimary);
            }

            // Icon + label.
            const VfxTabEntry& entry = m_tabs[it.tabIndex];
            const float textY = it.rect.y +
                                 (it.rect.height - 10.0f) * 0.5f + 1.0f;
            float cursor = it.rect.x + tabPadding;
            if (entry.icon != VfxUiIcon::None) {
                text.AddSprite(entry.icon, cursor,
                                it.rect.y + (it.rect.height - 16.0f) * 0.5f,
                                16.0f, 16.0f, palette.textPrimary);
                cursor += 20.0f;
            }
            const Vec4 labelColor = active
                ? palette.textPrimary : palette.textSecondary;
            if (msdf != nullptr) {
                text.AddMsdfText(*msdf, entry.label, cursor, it.rect.y + 7.0f,
                                  10.0f, labelColor);
            } else {
                text.AddText(font, entry.label, cursor, textY, labelColor);
            }
            break;
        }

        case VfxTabItemType::CloseIcon: {
            // Only rendered on hover — matches UE5 SDockTab.
            if (it.tabIndex != m_hoveredIdx) break;
            text.AddSprite(VfxUiIcon::Cross,
                           it.rect.x, it.rect.y,
                           it.rect.width, it.rect.height,
                           palette.textDim);
            break;
        }

        case VfxTabItemType::Divider:
            text.AddRect(it.rect.x, it.rect.y,
                         it.rect.width, it.rect.height,
                         palette.borderSubtle);
            break;

        case VfxTabItemType::None:
            break;
        }
    }
}

} // namespace uz
