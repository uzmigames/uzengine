/// VfxContextMenuWidget — implementation (phase 12.8.0).

#include "vfx_context_menu_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void VfxContextMenuWidget::Open(float anchorX, float anchorY,
                                  std::vector<VfxContextMenuItem> items,
                                  float screenW, float screenH) {
    m_isOpen         = true;
    m_items          = std::move(items);
    m_anchorX        = anchorX;
    m_anchorY        = anchorY;
    m_screenW        = screenW;
    m_screenH        = screenH;
    m_hoveredIndex   = static_cast<size_t>(-1);
    m_hoverElapsed   = 0.0f;
    m_submenuActive  = false;

    // Anchor-flip: if menu would spill past the right / bottom edge,
    // move the top-left back inside the viewport.
    const float w = panelWidth;
    const float h = ComputePanelHeight();
    m_renderX = anchorX;
    m_renderY = anchorY;
    if (screenW > 0.0f && anchorX + w > screenW) {
        m_renderX = std::max(0.0f, anchorX - w);
    }
    if (screenH > 0.0f && anchorY + h > screenH) {
        m_renderY = std::max(0.0f, anchorY - h);
    }
}

void VfxContextMenuWidget::Close() noexcept {
    m_isOpen = false;
    m_items.clear();
    m_hoveredIndex  = static_cast<size_t>(-1);
    m_hoverElapsed  = 0.0f;
    m_submenuActive = false;
}

VfxStackRect VfxContextMenuWidget::Bounds() const noexcept {
    if (!m_isOpen) return {};
    return { m_renderX, m_renderY, panelWidth, ComputePanelHeight() };
}

float VfxContextMenuWidget::ComputePanelHeight() const noexcept {
    float h = padding * 2.0f;
    for (const auto& it : m_items) {
        h += it.isSeparator ? separatorHeight : rowHeight;
    }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hover + keyboard
// ─────────────────────────────────────────────────────────────────────────────

void VfxContextMenuWidget::SetHover(float x, float y) {
    if (!m_isOpen) return;
    const auto layout = ComputeLayout();
    const auto hit    = HitTest(layout, x, y);
    size_t newHover = (hit.type == VfxContextMenuItemType::Row ||
                       hit.type == VfxContextMenuItemType::SubmenuCaret)
        ? hit.itemIndex
        : static_cast<size_t>(-1);
    if (newHover != m_hoveredIndex) {
        m_hoveredIndex  = newHover;
        m_hoverElapsed  = 0.0f;
        m_submenuActive = false;
    }
}

void VfxContextMenuWidget::Tick(float dtSeconds) noexcept {
    if (!m_isOpen) return;
    if (m_hoveredIndex == static_cast<size_t>(-1)) return;
    if (m_hoveredIndex >= m_items.size()) return;

    const auto& item = m_items[m_hoveredIndex];
    if (item.submenu.empty()) {
        m_hoverElapsed  = 0.0f;
        m_submenuActive = false;
        return;
    }
    m_hoverElapsed += dtSeconds;
    if (m_hoverElapsed >= submenuHoverDelay) {
        m_submenuActive = true;
    }
}

bool VfxContextMenuWidget::ProcessKey(Key key) noexcept {
    if (!m_isOpen) return false;
    if (key == Key::Escape) {
        Close();
        return true;
    }
    return false;
}

bool VfxContextMenuWidget::ClickAt(float x, float y) {
    if (!m_isOpen) return false;
    const auto bounds = Bounds();
    if (!bounds.Contains(x, y)) {
        Close();
        return true;  // consumed — dismissal
    }
    const auto layout = ComputeLayout();
    const auto hit    = HitTest(layout, x, y);
    if (hit.type != VfxContextMenuItemType::Row) return true;
    if (!hit.enabled) return true;
    if (hit.itemIndex >= m_items.size()) return true;

    auto cb = m_items[hit.itemIndex].onActivate;
    // Close before invoking so a callback that re-opens the menu works.
    Close();
    if (cb) cb();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxContextMenuLayoutItem>
VfxContextMenuWidget::ComputeLayout() const {
    std::vector<VfxContextMenuLayoutItem> out;
    if (!m_isOpen) return out;

    const float x = m_renderX;
    const float w = panelWidth;
    const float h = ComputePanelHeight();

    // Backdrop item — lowest priority, acts as the click-on-blank fallback.
    VfxContextMenuLayoutItem back;
    back.type = VfxContextMenuItemType::Backdrop;
    back.rect = { x, m_renderY, w, h };
    out.push_back(back);

    float cy = m_renderY + padding;
    for (size_t i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];
        if (item.isSeparator) {
            VfxContextMenuLayoutItem sep;
            sep.type      = VfxContextMenuItemType::Separator;
            sep.itemIndex = i;
            sep.rect      = { x, cy, w, separatorHeight };
            out.push_back(sep);
            cy += separatorHeight;
            continue;
        }

        VfxContextMenuLayoutItem row;
        row.type      = VfxContextMenuItemType::Row;
        row.itemIndex = i;
        row.rect      = { x, cy, w, rowHeight };
        out.push_back(row);

        if (!item.submenu.empty()) {
            VfxContextMenuLayoutItem car;
            car.type      = VfxContextMenuItemType::SubmenuCaret;
            car.itemIndex = i;
            const float cw = 12.0f;
            car.rect      = { x + w - cw - 4.0f, cy, cw, rowHeight };
            out.push_back(car);
        }
        cy += rowHeight;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxContextMenuHitResult VfxContextMenuWidget::HitTest(
    const std::vector<VfxContextMenuLayoutItem>& layout,
    float x, float y) const {
    VfxContextMenuHitResult hit;
    // Enum ordering = priority. Higher value wins when rects overlap.
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        const int sp = static_cast<int>(item.type);
        const int cu = static_cast<int>(hit.type);
        if (sp > cu || hit.type == VfxContextMenuItemType::None) {
            hit.type      = item.type;
            hit.itemIndex = item.itemIndex;
            hit.enabled   = (item.itemIndex < m_items.size())
                ? m_items[item.itemIndex].enabled
                : true;
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxContextMenuWidget::Render(TextRenderer& text,
                                    const BitmapFont& font) const {
    if (!m_isOpen) return;
    const auto& palette = VfxThemeColors();
    const auto& radius  = VfxThemeRadius();
    const auto& shadows = VfxThemeShadows();
    const auto layout = ComputeLayout();

    // Phase 13.5 — backdrop is a single rounded-rect SDF primitive
    // carrying the popup fill + 1-px border + drop shadow.
    Vec4 shadowColor = palette.shadow;
    shadowColor.w = shadows.popup.alpha;

    // Main pass — draws backdrop + rows + separators + carets + hover.
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxContextMenuItemType::Backdrop:
            text.AddRoundedRect(it.rect.x, it.rect.y,
                                 it.rect.width, it.rect.height,
                                 radius.sm /*4 px*/,
                                 palette.popupBg,
                                 1.0f, palette.borderStrong,
                                 Vec2{0.0f, shadows.popup.offsetY},
                                 shadows.popup.blur, shadowColor);
            break;

        case VfxContextMenuItemType::Row: {
            if (it.itemIndex >= m_items.size()) break;
            const auto& item = m_items[it.itemIndex];
            const bool hovered = (it.itemIndex == m_hoveredIndex);
            if (hovered && item.enabled) {
                text.AddRect(it.rect.x, it.rect.y, it.rect.width,
                             it.rect.height, palette.cardBgHover);
            }
            const Vec4 fg = item.enabled ? palette.textPrimary
                                            : palette.textDisabled;
            if (!item.iconGlyph.empty()) {
                text.AddText(font, item.iconGlyph,
                             it.rect.x + padding + 2.0f,
                             it.rect.y + 4.0f, fg);
            }
            text.AddText(font, item.label,
                         it.rect.x + padding + iconColumnW,
                         it.rect.y + 4.0f, fg);
            if (!item.hotkey.empty()) {
                text.AddText(font, item.hotkey,
                             it.rect.x + it.rect.width - hotkeyColumnW,
                             it.rect.y + 4.0f, palette.textDim);
            }
            break;
        }

        case VfxContextMenuItemType::Separator:
            text.AddRect(it.rect.x + padding + 2.0f,
                         it.rect.y + it.rect.height * 0.5f,
                         it.rect.width - padding * 2.0f - 4.0f,
                         1.0f, palette.borderSubtle);
            break;

        case VfxContextMenuItemType::SubmenuCaret:
            text.AddSprite(VfxUiIcon::ChevronRight,
                            it.rect.x + 2.0f, it.rect.y + 6.0f,
                            10.0f, 10.0f,
                            palette.textDim);
            break;

        case VfxContextMenuItemType::None:
            break;
        }
    }

    // Submenu (nested pass) — rendered after the primary items so it
    // appears above the root backdrop.
    if (m_submenuActive &&
        m_hoveredIndex != static_cast<size_t>(-1) &&
        m_hoveredIndex < m_items.size() &&
        !m_items[m_hoveredIndex].submenu.empty()) {
        const auto& parent = m_items[m_hoveredIndex];
        const float parentRowY =
            m_renderY + padding +
            rowHeight * static_cast<float>(m_hoveredIndex);
        const float subX = m_renderX + panelWidth - 2.0f;
        // Compute submenu panel height.
        float subH = padding * 2.0f;
        for (const auto& sit : parent.submenu) {
            subH += sit.isSeparator ? separatorHeight : rowHeight;
        }
        text.AddRoundedRect(subX, parentRowY, panelWidth, subH,
                             radius.sm,
                             palette.popupBg,
                             1.0f, palette.borderStrong,
                             Vec2{0.0f, shadows.popup.offsetY},
                             shadows.popup.blur, shadowColor);
        float cy = parentRowY + padding;
        for (const auto& sit : parent.submenu) {
            if (sit.isSeparator) {
                text.AddRect(subX + padding + 2.0f,
                             cy + separatorHeight * 0.5f,
                             panelWidth - padding * 2.0f - 4.0f, 1.0f,
                             palette.borderSubtle);
                cy += separatorHeight;
                continue;
            }
            text.AddText(font, sit.label,
                         subX + padding + iconColumnW,
                         cy + 4.0f,
                         sit.enabled ? palette.textPrimary
                                      : palette.textDisabled);
            if (!sit.hotkey.empty()) {
                text.AddText(font, sit.hotkey,
                             subX + panelWidth - hotkeyColumnW,
                             cy + 4.0f, palette.textDim);
            }
            cy += rowHeight;
        }
    }
}

} // namespace uz
