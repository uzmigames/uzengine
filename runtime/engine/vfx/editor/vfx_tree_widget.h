#pragma once

/// VFX Tree Widget — phase 14.4.0.
///
/// UE5 World Outliner-style hierarchical list. Each node carries a
/// depth, an expand arrow, a type icon, and a label. Selection +
/// hover fills span the full row width regardless of label length.
///
/// ## Layout
///
/// - Row height: 20 px (UE5 density).
/// - Expand arrow sprite: 12x12 at `row.x + depth*16 + 2`.
/// - Type icon sprite: 14x14 at `row.x + depth*16 + 18`.
/// - Label text: starts at `row.x + depth*16 + 36`.
/// - Per-depth indent guide: 1-px `borderSubtle` vertical rect at
///   `row.x + depth*16 + 7` for every row whose parent has more
///   siblings below.
///
/// ## Interaction
///
/// - Click anywhere on row → `onSelect(id)` (full-row hit region).
/// - Click on expand-arrow region (16x16 at `row.x + depth*16`) →
///   `onToggleExpand(id)` instead of `onSelect`.
///
/// @ref UE5 SSceneOutliner / STreeView

#include "engine/math/vector4.h"                         // Vec4
#include "engine/vfx/editor/vfx_icon_atlas.h"           // VfxUiIcon
#include "engine/vfx/editor/vfx_stack_panel_widget.h"   // VfxStackRect

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;
class MsdfFont;

// ─────────────────────────────────────────────────────────────────────────────
// Tree node
// ─────────────────────────────────────────────────────────────────────────────

struct VfxTreeNode {
    uint32_t    id         = 0;
    std::string label;
    VfxUiIcon   icon       = VfxUiIcon::None;
    uint32_t    depth      = 0;
    bool        expanded   = true;
    bool        hasChildren = false;
    Vec4        iconTint   = { 1.0f, 1.0f, 1.0f, 1.0f };
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout item
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxTreeItemType : uint8_t {
    None          = 0,
    Row           = 1,
    ExpandArrow   = 2,
    IndentGuide   = 3,
};

struct VfxTreeLayoutItem {
    VfxTreeItemType type = VfxTreeItemType::None;
    VfxStackRect    rect;
    uint32_t        nodeId = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxTreeWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float rowHeight    = 20.0f;
    float indentStep   = 16.0f;
    float arrowSize    = 12.0f;
    float iconSize     = 14.0f;
    float arrowHitSize = 16.0f;

    // ── Node list ───────────────────────────────────────────────────────

    void Clear() { m_nodes.clear(); }

    size_t AddNode(const VfxTreeNode& node) {
        m_nodes.push_back(node);
        return m_nodes.size() - 1u;
    }

    [[nodiscard]] size_t Count() const { return m_nodes.size(); }
    [[nodiscard]] const std::vector<VfxTreeNode>& Nodes() const { return m_nodes; }

    // ── Selection ───────────────────────────────────────────────────────

    void SetSelectedId(uint32_t id) { m_selectedId = id; }
    [[nodiscard]] uint32_t SelectedId() const { return m_selectedId; }

    void SetHoveredId(uint32_t id) { m_hoveredId = id; }
    [[nodiscard]] uint32_t HoveredId() const { return m_hoveredId; }

    // ── Layout + hit-test + render ──────────────────────────────────────

    [[nodiscard]] std::vector<VfxTreeLayoutItem> ComputeLayout(
        const VfxStackRect& panelBounds) const;

    struct HitResult {
        VfxTreeItemType type = VfxTreeItemType::None;
        uint32_t nodeId     = 0;
    };
    [[nodiscard]] HitResult HitTest(
        const std::vector<VfxTreeLayoutItem>& layout,
        float x, float y) const;

    void Render(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, const VfxStackRect& panelBounds) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    std::function<void(uint32_t)> onSelect;
    std::function<void(uint32_t)> onToggleExpand;

private:
    std::vector<VfxTreeNode> m_nodes;
    uint32_t m_selectedId = 0;
    uint32_t m_hoveredId  = 0;
};

} // namespace uz
