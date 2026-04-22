/// VfxTreeWidget — implementation (phase 14.4.0).

#include "vfx_tree_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxTreeLayoutItem> VfxTreeWidget::ComputeLayout(
    const VfxStackRect& panelBounds) const {
    std::vector<VfxTreeLayoutItem> out;
    if (m_nodes.empty()) return out;

    // Walk visible nodes honouring collapsed ancestors.
    float y = panelBounds.y;
    std::vector<uint32_t> collapsedAtDepth;
    collapsedAtDepth.reserve(8);

    for (const auto& n : m_nodes) {
        // Pop collapsed markers whose depth is >= this node's depth.
        while (!collapsedAtDepth.empty() &&
               collapsedAtDepth.back() >= n.depth) {
            collapsedAtDepth.pop_back();
        }
        if (!collapsedAtDepth.empty()) {
            // A shallower ancestor is collapsed → skip this node.
            if (n.hasChildren && !n.expanded) {
                collapsedAtDepth.push_back(n.depth);
            }
            continue;
        }

        VfxTreeLayoutItem row{};
        row.type = VfxTreeItemType::Row;
        row.rect = { panelBounds.x, y, panelBounds.width, rowHeight };
        row.nodeId = n.id;
        out.push_back(row);

        if (n.hasChildren) {
            VfxTreeLayoutItem arrow{};
            arrow.type = VfxTreeItemType::ExpandArrow;
            arrow.rect = {
                panelBounds.x + static_cast<float>(n.depth) * indentStep,
                y + (rowHeight - arrowHitSize) * 0.5f,
                arrowHitSize,
                arrowHitSize
            };
            arrow.nodeId = n.id;
            out.push_back(arrow);
        }

        // Vertical indent guide rendered per ancestor depth (1-px rect
        // at `row.x + d*indentStep + 7`) so the tree visually tracks
        // hierarchy without per-level connector glyphs.
        for (uint32_t d = 0; d < n.depth; ++d) {
            VfxTreeLayoutItem guide{};
            guide.type = VfxTreeItemType::IndentGuide;
            guide.rect = {
                panelBounds.x + static_cast<float>(d) * indentStep + 7.0f,
                y,
                1.0f,
                rowHeight
            };
            guide.nodeId = n.id;
            out.push_back(guide);
        }

        if (n.hasChildren && !n.expanded) {
            collapsedAtDepth.push_back(n.depth);
        }

        y += rowHeight;
        if (y > panelBounds.y + panelBounds.height) break;
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxTreeWidget::HitResult VfxTreeWidget::HitTest(
    const std::vector<VfxTreeLayoutItem>& layout, float x, float y) const {
    HitResult result{};
    // Expand arrow takes priority.
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::ExpandArrow) continue;
        if (x >= it.rect.x && x < it.rect.x + it.rect.width &&
            y >= it.rect.y && y < it.rect.y + it.rect.height) {
            result.type = VfxTreeItemType::ExpandArrow;
            result.nodeId = it.nodeId;
            return result;
        }
    }
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::Row) continue;
        if (x >= it.rect.x && x < it.rect.x + it.rect.width &&
            y >= it.rect.y && y < it.rect.y + it.rect.height) {
            result.type = VfxTreeItemType::Row;
            result.nodeId = it.nodeId;
            return result;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxTreeWidget::Render(TextRenderer& text, const BitmapFont& font,
                           const MsdfFont* msdf,
                           const VfxStackRect& panelBounds) const {
    const auto& palette = VfxThemeColors();
    const auto layout = ComputeLayout(panelBounds);

    // Pass 1 — row backgrounds (behind indent guides + text).
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::Row) continue;
        const bool selected = (it.nodeId == m_selectedId);
        const bool hovered  = (it.nodeId == m_hoveredId) && !selected;
        Vec4 fill{ 0.0f, 0.0f, 0.0f, 0.0f };
        if (selected)      fill = palette.cardBgSelected;
        else if (hovered)  fill = palette.cardBgHover;
        if (fill.w > 0.0f) {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width,
                         it.rect.height, fill);
        }
    }

    // Pass 2 — indent guides.
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::IndentGuide) continue;
        text.AddRect(it.rect.x, it.rect.y, it.rect.width,
                     it.rect.height, palette.borderSubtle);
    }

    // Pass 3 — expand arrows.
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::ExpandArrow) continue;
        // Locate the owning node to read `expanded`.
        const VfxTreeNode* node = nullptr;
        for (const auto& n : m_nodes) {
            if (n.id == it.nodeId) { node = &n; break; }
        }
        if (node == nullptr) continue;
        const VfxUiIcon glyph =
            node->expanded ? VfxUiIcon::ChevronDown
                            : VfxUiIcon::ChevronRight;
        const float cx = it.rect.x + (it.rect.width - arrowSize) * 0.5f;
        const float cy = it.rect.y + (it.rect.height - arrowSize) * 0.5f;
        text.AddSprite(glyph, cx, cy, arrowSize, arrowSize,
                        palette.textSecondary);
    }

    // Pass 4 — type icons + labels.
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::Row) continue;
        const VfxTreeNode* node = nullptr;
        for (const auto& n : m_nodes) {
            if (n.id == it.nodeId) { node = &n; break; }
        }
        if (node == nullptr) continue;

        const float indentPx = static_cast<float>(node->depth) * indentStep;
        const float iconX = it.rect.x + indentPx + 18.0f;
        const float iconY = it.rect.y + (it.rect.height - iconSize) * 0.5f;
        if (node->icon != VfxUiIcon::None) {
            text.AddSprite(node->icon, iconX, iconY,
                            iconSize, iconSize, node->iconTint);
        }

        const float labelX = it.rect.x + indentPx + 36.0f;
        const float labelY = it.rect.y +
                              (it.rect.height - 10.0f) * 0.5f + 1.0f;
        const Vec4 labelColor = (it.nodeId == m_selectedId)
            ? palette.textPrimary : palette.textPrimary;
        if (msdf != nullptr) {
            text.AddMsdfText(*msdf, node->label, labelX, labelY, 10.0f,
                              labelColor);
        } else {
            text.AddText(font, node->label, labelX,
                          it.rect.y + 4.0f, labelColor);
        }
    }
}

} // namespace uz
