/// VfxToolbarWidget — implementation (phase 14.5.0).

#include "vfx_toolbar_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Measure
// ─────────────────────────────────────────────────────────────────────────────

float VfxToolbarWidget::MeasureButton(const VfxToolbarButton& b) const {
    if (b.kind == VfxToolbarKind::Separator) {
        return 1.0f + sepPad * 2.0f;
    }
    const float iconW  = (b.icon != VfxUiIcon::None) ? 16.0f : 0.0f;
    const float iconGap = (iconW > 0.0f) ? buttonPad : 0.0f;
    const float labelW = static_cast<float>(b.label.size()) * 7.0f;
    const float dropdownGlyph = (b.kind == VfxToolbarKind::Dropdown)
        ? 10.0f + buttonPad : 0.0f;
    const float content = iconW + iconGap + labelW + dropdownGlyph;
    return buttonPad * 2.0f + content;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxToolbarLayoutItem> VfxToolbarWidget::ComputeLayout(
    const VfxStackRect& barBounds) const {
    std::vector<VfxToolbarLayoutItem> out;
    out.reserve(m_buttons.size());

    float cursorX = barBounds.x;
    const float y = barBounds.y + (barBounds.height - rowHeight) * 0.5f;

    for (const auto& b : m_buttons) {
        if (b.kind == VfxToolbarKind::Separator) {
            VfxToolbarLayoutItem it{};
            it.rect = { cursorX + sepPad, y + 4.0f, 1.0f, rowHeight - 8.0f };
            it.id = 0;
            it.kind = VfxToolbarKind::Separator;
            out.push_back(it);
            cursorX += sepPad * 2.0f + 1.0f;
            continue;
        }
        const float w = MeasureButton(b);
        VfxToolbarLayoutItem it{};
        it.rect = { cursorX, y, w, rowHeight };
        it.id = b.id;
        it.kind = b.kind;
        out.push_back(it);
        cursorX += w + buttonGap;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxToolbarWidget::HitResult VfxToolbarWidget::HitTest(
    const std::vector<VfxToolbarLayoutItem>& layout, float x, float y) const {
    HitResult result{};
    for (const auto& it : layout) {
        if (it.kind == VfxToolbarKind::Separator) continue;
        if (x >= it.rect.x && x < it.rect.x + it.rect.width &&
            y >= it.rect.y && y < it.rect.y + it.rect.height) {
            result.hit = true;
            result.id = it.id;
            result.kind = it.kind;
            return result;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxToolbarWidget::Render(TextRenderer& text, const BitmapFont& font,
                              const MsdfFont* msdf,
                              const VfxStackRect& barBounds) const {
    const auto& palette = VfxThemeColors();
    // Strip background — `barBg` so the toolbar reads as chrome.
    text.AddRect(barBounds.x, barBounds.y, barBounds.width, barBounds.height,
                 palette.barBg);

    const auto layout = ComputeLayout(barBounds);
    size_t buttonCursor = 0;
    for (const auto& it : layout) {
        if (it.kind == VfxToolbarKind::Separator) {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.borderSubtle);
            continue;
        }

        // Find corresponding button record — walk linearly since the
        // toolbar button count is always small.
        const VfxToolbarButton* b = nullptr;
        size_t seen = 0;
        for (const auto& candidate : m_buttons) {
            if (candidate.kind == VfxToolbarKind::Separator) continue;
            if (seen == buttonCursor) { b = &candidate; break; }
            ++seen;
        }
        ++buttonCursor;
        if (b == nullptr) continue;

        const bool hovered = (b->id == m_hoveredId);
        Vec4 fill = palette.barBg;
        if (b->kind == VfxToolbarKind::Toggle && b->toggled) {
            fill = palette.cardBgSelected;
        } else if (hovered) {
            fill = palette.cardBgHover;
        }

        text.AddRoundedRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                             buttonRadius, fill);

        // Icon + label.
        float cursor = it.rect.x + buttonPad;
        const Vec4 iconTint = (b->toggled || hovered)
            ? palette.textPrimary : palette.textSecondary;
        if (b->icon != VfxUiIcon::None) {
            text.AddSprite(b->icon, cursor,
                            it.rect.y + (it.rect.height - 16.0f) * 0.5f,
                            16.0f, 16.0f, iconTint);
            cursor += 16.0f + buttonPad;
        }
        const Vec4 labelColor = (b->toggled || hovered)
            ? palette.textPrimary : palette.textSecondary;
        if (!b->label.empty()) {
            const float labelY = it.rect.y + (it.rect.height - 10.0f) * 0.5f + 1.0f;
            if (msdf != nullptr) {
                text.AddMsdfText(*msdf, b->label, cursor, labelY, 10.0f,
                                  labelColor);
            } else {
                text.AddText(font, b->label, cursor, it.rect.y + 7.0f,
                              labelColor);
            }
        }

        // Dropdown chevron at the right edge.
        if (b->kind == VfxToolbarKind::Dropdown) {
            const float chevSize = 10.0f;
            const float cx = it.rect.x + it.rect.width - buttonPad - chevSize;
            const float cy = it.rect.y + (it.rect.height - chevSize) * 0.5f;
            text.AddSprite(VfxUiIcon::ChevronDown, cx, cy,
                            chevSize, chevSize, palette.textDim);
        }
    }
}

} // namespace uz
