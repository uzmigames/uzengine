/// VfxSystemOverviewWidget — implementation (phase 12.9.0).

#include "vfx_system_overview_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <cstdio>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Drag
// ─────────────────────────────────────────────────────────────────────────────

void VfxSystemOverviewWidget::BeginDrag(size_t index) noexcept {
    if (m_emitters == nullptr) return;
    if (index >= m_emitters->size()) return;
    m_dragActive   = true;
    m_dragSrcIndex = index;
}

void VfxSystemOverviewWidget::EndDrag(size_t targetIndex) {
    if (!m_dragActive) return;
    const size_t src = m_dragSrcIndex;
    m_dragActive   = false;
    m_dragSrcIndex = static_cast<size_t>(-1);
    if (src == targetIndex) return;
    if (m_emitters == nullptr) return;
    if (targetIndex >= m_emitters->size()) return;
    if (onEmitterMoved) onEmitterMoved(src, targetIndex);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxOverviewLayoutItem>
VfxSystemOverviewWidget::ComputeLayout(const VfxStackRect& panelBounds) const {
    std::vector<VfxOverviewLayoutItem> out;
    if (m_emitters == nullptr) return out;

    // Upper bound: 1 row + 4 buttons + 3 sub-rects per emitter + 1 add row.
    out.reserve(m_emitters->size() * 8u + 1u);

    const float x0 = panelBounds.x;
    const float w  = panelBounds.width;

    // Action-column layout from the right edge: [MoveDown][MoveUp][Dup][Del]
    // Four buttons * actionBtnWidth + gap padding.
    const float actionsW = actionBtnWidth * 4.0f + padding * 3.0f;
    const float actionsX = x0 + w - padding - actionsW;

    float y = panelBounds.y;

    for (size_t i = 0; i < m_emitters->size(); ++i) {
        const float ry = y;

        // Whole-row item.
        VfxOverviewLayoutItem row;
        row.type = VfxOverviewItemType::Row;
        row.emitterIndex = i;
        row.rect = { x0, ry, w, rowHeight };
        out.push_back(row);

        // Drag handle (left edge).
        VfxOverviewLayoutItem grip;
        grip.type = VfxOverviewItemType::DragHandle;
        grip.emitterIndex = i;
        grip.rect = { x0 + padding, ry,
                       dragHandleWidth, rowHeight };
        out.push_back(grip);

        // Checkbox.
        VfxOverviewLayoutItem cb;
        cb.type = VfxOverviewItemType::Checkbox;
        cb.emitterIndex = i;
        const float cbX = x0 + padding + dragHandleWidth;
        const float cbY = ry + (rowHeight - checkboxWidth) * 0.5f;
        cb.rect = { cbX, cbY, checkboxWidth, checkboxWidth };
        out.push_back(cb);

        // Status dot.
        VfxOverviewLayoutItem dot;
        dot.type = VfxOverviewItemType::StatusDot;
        dot.emitterIndex = i;
        const float dotX = cbX + checkboxWidth + padding;
        const float dotY = ry + (rowHeight - statusDotSize) * 0.5f;
        dot.rect = { dotX, dotY, statusDotSize, statusDotSize };
        out.push_back(dot);

        // Name label spans from after status dot to start of particle-count area.
        const float nameX = dotX + statusDotSize + padding;
        const float countW = 56.0f;
        const float nameW = std::max(0.0f,
            actionsX - nameX - countW - padding);
        VfxOverviewLayoutItem name;
        name.type = VfxOverviewItemType::NameLabel;
        name.emitterIndex = i;
        name.rect = { nameX, ry, nameW, rowHeight };
        out.push_back(name);

        // Particle count (just before the action buttons).
        VfxOverviewLayoutItem cnt;
        cnt.type = VfxOverviewItemType::ParticleCount;
        cnt.emitterIndex = i;
        cnt.rect = { actionsX - countW - padding, ry, countW, rowHeight };
        out.push_back(cnt);

        // Action buttons — 4 equal-width buttons right-aligned.
        auto emitBtn = [&](VfxOverviewItemType t, int slot) {
            VfxOverviewLayoutItem b;
            b.type = t;
            b.emitterIndex = i;
            const float bx = actionsX +
                static_cast<float>(slot) * (actionBtnWidth + padding);
            const float by = ry + (rowHeight - checkboxWidth) * 0.5f;
            b.rect = { bx, by, actionBtnWidth, checkboxWidth };
            out.push_back(b);
        };
        emitBtn(VfxOverviewItemType::BtnMoveUp,    0);
        emitBtn(VfxOverviewItemType::BtnMoveDown,  1);
        emitBtn(VfxOverviewItemType::BtnDuplicate, 2);
        emitBtn(VfxOverviewItemType::BtnDelete,    3);

        y += rowHeight;
    }

    // Trailing "+ Add Emitter" row.
    VfxOverviewLayoutItem add;
    add.type = VfxOverviewItemType::AddEmitterRow;
    add.rect = { x0, y, w, addRowHeight };
    out.push_back(add);

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxOverviewHitResult VfxSystemOverviewWidget::HitTest(
    const std::vector<VfxOverviewLayoutItem>& layout,
    float x, float y) const {
    VfxOverviewHitResult hit;
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        const int sp = static_cast<int>(item.type);
        const int cu = static_cast<int>(hit.type);
        if (sp > cu || hit.type == VfxOverviewItemType::None) {
            hit.type = item.type;
            hit.emitterIndex = item.emitterIndex;
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxSystemOverviewWidget::Render(TextRenderer& text,
                                       const BitmapFont& font,
                                       const VfxStackRect& panelBounds) const {
    if (m_emitters == nullptr) return;
    const auto& palette = VfxThemeColors();
    const auto layout = ComputeLayout(panelBounds);

    for (const auto& item : layout) {
        if (!(item.rect.width > 0.0f && item.rect.height > 0.0f)) continue;
        switch (item.type) {
        case VfxOverviewItemType::Row: {
            const bool selected =
                (item.emitterIndex == m_selectedIndex);
            const Vec4 bg = selected ? palette.cardBgSelected
                                        : palette.cardBg;
            // V3 minimalist — rounded row card (2 px). No border —
            // rounded corners + bg contrast provide all the visual
            // separation a dense row list needs.
            text.AddRoundedRect(item.rect.x, item.rect.y,
                                 item.rect.width, item.rect.height,
                                 2.0f /*radius.xs*/, bg);
            // Selected accent stripe (3 px, left edge).
            if (selected) {
                text.AddRect(item.rect.x, item.rect.y, 3.0f,
                             item.rect.height, palette.accentPrimary);
            }
            break;
        }

        case VfxOverviewItemType::DragHandle: {
            // Phase 13.5 — sprite GripDots (9x9 centred) replaces
            // the ASCII `:` grip so the handle reads as a grab
            // affordance at a glance.
            constexpr float kGripSize = 9.0f;
            text.AddSprite(VfxUiIcon::GripDots,
                           item.rect.x + (item.rect.width - kGripSize) * 0.5f,
                           item.rect.y + (item.rect.height - kGripSize) * 0.5f,
                           kGripSize, kGripSize, palette.textDim);
            break;
        }

        case VfxOverviewItemType::Checkbox: {
            if (item.emitterIndex >= m_emitters->size()) break;
            const auto& e = (*m_emitters)[item.emitterIndex];
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.borderStrong);
            const float inset = 2.0f;
            text.AddRect(item.rect.x + inset, item.rect.y + inset,
                         item.rect.width - inset * 2.0f,
                         item.rect.height - inset * 2.0f,
                         e.isEnabled ? palette.stateSuccess
                                      : palette.cardBg);
            break;
        }

        case VfxOverviewItemType::StatusDot: {
            if (item.emitterIndex >= m_emitters->size()) break;
            const auto& e = (*m_emitters)[item.emitterIndex];
            Vec4 col = palette.textDim;  // disabled
            if (e.isEmitting && e.aliveCount > 0u) {
                col = palette.stateSuccess;
            } else if (!e.isEmitting && e.aliveCount > 0u) {
                col = palette.stateWarning;
            }
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, col);
            break;
        }

        case VfxOverviewItemType::NameLabel: {
            if (item.emitterIndex >= m_emitters->size()) break;
            const auto& e = (*m_emitters)[item.emitterIndex];
            text.AddText(font, e.name.c_str(),
                         item.rect.x + padding,
                         item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                         palette.textPrimary);
            break;
        }

        case VfxOverviewItemType::ParticleCount: {
            if (item.emitterIndex >= m_emitters->size()) break;
            const auto& e = (*m_emitters)[item.emitterIndex];
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%u", e.aliveCount);
            // Right-align within the cell.
            const float textW =
                static_cast<float>(std::strlen(buf)) * 7.0f;
            text.AddText(font, buf,
                         item.rect.x + item.rect.width - textW - padding,
                         item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                         palette.textDim);
            break;
        }

        case VfxOverviewItemType::BtnMoveUp:
        case VfxOverviewItemType::BtnMoveDown:
        case VfxOverviewItemType::BtnDuplicate:
        case VfxOverviewItemType::BtnDelete: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.cardBgHover);
            const char* glyph =
                (item.type == VfxOverviewItemType::BtnMoveUp)    ? VFX_ICON_CHEVRON_UP   :
                (item.type == VfxOverviewItemType::BtnMoveDown)  ? VFX_ICON_CHEVRON_DOWN :
                (item.type == VfxOverviewItemType::BtnDuplicate) ? VFX_ICON_DUPLICATE    :
                                                                     VFX_ICON_DELETE;
            text.AddText(font, glyph,
                         item.rect.x + 4.0f,
                         item.rect.y + 2.0f,
                         palette.textPrimary);
            break;
        }

        case VfxOverviewItemType::AddEmitterRow: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.panelBgAlt);
            text.AddText(font, "+ Add Emitter",
                         item.rect.x + padding * 2.0f,
                         item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                         palette.accentPrimary);
            break;
        }

        case VfxOverviewItemType::None:
            break;
        }
    }
}

} // namespace uz
