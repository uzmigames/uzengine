#pragma once

/// VFX Content Browser Widget — phase 14.7.0.
///
/// UE5-style asset grid: 96x120 tiles (96x96 thumbnail + 24 px
/// label strip) with type-coloured 2-px top border, rounded-corner
/// selection, drag-threshold gesture, and auto-reflowing grid
/// layout.

#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;
class MsdfFont;

struct VfxAssetTile {
    uint32_t    id        = 0;
    std::string label;
    Vec4        kindTint  = { 0.5f, 0.5f, 0.5f, 1.0f };
    /// SRV index into the bindless atlas. 0xFFFFFFFF = no thumbnail
    /// (renders a checkerboard fallback).
    uint32_t    thumbnailSrvIndex = 0xFFFFFFFFu;
};

enum class VfxAssetItemType : uint8_t {
    None      = 0,
    Tile      = 1,
    Thumbnail = 2,
    Label     = 3,
};

struct VfxAssetLayoutItem {
    VfxAssetItemType type = VfxAssetItemType::None;
    VfxStackRect     rect;
    uint32_t         tileId = 0;
};

class VfxContentBrowserWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float thumbSize     = 96.0f;
    float labelStripH   = 24.0f;
    float tileGap       = 8.0f;
    float tileRadius    = 2.0f;
    float kindBarHeight = 2.0f;
    float dragThreshold = 6.0f;

    // ── Assets ──────────────────────────────────────────────────────────

    void Clear() { m_tiles.clear(); }

    void AddTile(const VfxAssetTile& tile) { m_tiles.push_back(tile); }

    void SetTiles(std::vector<VfxAssetTile> tiles) {
        m_tiles = std::move(tiles);
    }

    [[nodiscard]] size_t Count() const { return m_tiles.size(); }

    [[nodiscard]] const std::vector<VfxAssetTile>& Tiles() const {
        return m_tiles;
    }

    // ── Selection / hover ──────────────────────────────────────────────

    void SetSelectedId(uint32_t id) { m_selectedId = id; }
    [[nodiscard]] uint32_t SelectedId() const { return m_selectedId; }
    void SetHoveredId(uint32_t id) { m_hoveredId = id; }

    // ── Drag gesture ───────────────────────────────────────────────────

    void BeginMouseDown(uint32_t tileId, float x, float y) {
        m_mouseDownTile = tileId;
        m_mouseDownX = x;
        m_mouseDownY = y;
        m_dragActive = false;
    }

    /// Updates with cursor `(x, y)` while the mouse is held. Returns
    /// `true` exactly once when the cursor first crosses the drag
    /// threshold — host uses that to fire `onDragStart`.
    bool MouseMove(float x, float y) {
        if (m_mouseDownTile == 0u || m_dragActive) return false;
        const float dx = x - m_mouseDownX;
        const float dy = y - m_mouseDownY;
        const float d2 = dx * dx + dy * dy;
        if (d2 >= dragThreshold * dragThreshold) {
            m_dragActive = true;
            return true;
        }
        return false;
    }

    /// Releases the mouse-down state; returns the tile id that was
    /// the gesture source (0 if nothing active).
    uint32_t EndMouseUp() {
        const uint32_t id = m_mouseDownTile;
        m_mouseDownTile = 0u;
        m_dragActive = false;
        return id;
    }

    [[nodiscard]] bool IsDragActive() const { return m_dragActive; }

    // ── Layout + hit-test + render ──────────────────────────────────────

    /// Returns the number of columns that fit in `panelWidth`.
    [[nodiscard]] uint32_t ColumnCount(float panelWidth) const {
        const float pitch = thumbSize + tileGap;
        if (pitch <= 0.0f) return 1u;
        const float inner = panelWidth - tileGap;
        const uint32_t cols = static_cast<uint32_t>(inner / pitch);
        return (cols == 0u) ? 1u : cols;
    }

    [[nodiscard]] float TileTotalHeight() const {
        return thumbSize + labelStripH;
    }

    [[nodiscard]] std::vector<VfxAssetLayoutItem> ComputeLayout(
        const VfxStackRect& panelBounds) const;

    [[nodiscard]] uint32_t HitTest(
        const std::vector<VfxAssetLayoutItem>& layout,
        float x, float y) const;

    void Render(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, const VfxStackRect& panelBounds) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    std::function<void(uint32_t)>              onSelect;
    std::function<void(uint32_t)>              onDoubleClick;
    std::function<void(uint32_t)>              onDragStart;
    std::function<void(uint32_t, float, float)> onContextMenu;

private:
    std::vector<VfxAssetTile> m_tiles;
    uint32_t m_selectedId = 0u;
    uint32_t m_hoveredId  = 0u;

    uint32_t m_mouseDownTile = 0u;
    float    m_mouseDownX    = 0.0f;
    float    m_mouseDownY    = 0.0f;
    bool     m_dragActive    = false;
};

} // namespace uz
