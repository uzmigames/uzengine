#pragma once

/// VFX Context Menu Widget — shared right-click menu (phase 12.8.0).
///
/// Unified popup used by the stack panel (module card right-click), the
/// parameter inspector (param right-click), and the curves editor (key
/// right-click). Host provides the item list + callbacks; widget owns
/// the render + hit-test + lifecycle.
///
/// # Lifecycle
///
/// ```
///   Closed ── Open(anchor, items) ──► Open
///                                      │
///           Escape / click-outside ── │
///           row click (enabled)   ────┴──► Close + onActivate
/// ```
///
/// # Submenu hover timing
///
/// Hovering a row whose `submenu` is non-empty for >= 200 ms opens the
/// nested menu anchored at the row's right edge. `Tick(dtSeconds)` must
/// be called once per frame by the host to advance the hover timer.
///
/// # Testability
///
/// Every state mutation is pure-data: Open, Close, SetHover,
/// ProcessKey, ClickAt, Tick, ComputeLayout, HitTest. The widget does
/// not own GPU state; tests exercise the full lifecycle without
/// standing up a TextRenderer.
///
/// @ref docs/engine/25-vfx/60-context-menu.md
/// @ref UE5 `FUIAction` / `FMenuBuilder`

#include "engine/vfx/editor/vfx_editor_keybindings.h"  // Key enum
#include "engine/vfx/editor/vfx_stack_panel_widget.h"  // VfxStackRect

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;

// ─────────────────────────────────────────────────────────────────────────────
// Item
// ─────────────────────────────────────────────────────────────────────────────

struct VfxContextMenuItem {
    std::string label;
    std::string hotkey;      ///< e.g. "Del"; may be empty.
    std::string iconGlyph;   ///< ASCII-safe icon from vfx_editor_icons.h.
    std::function<void()> onActivate;  ///< Fired on enabled-row click.
    bool isSeparator = false;
    bool enabled     = true;

    /// Optional nested menu. Rendered on hover >= 200 ms.
    std::vector<VfxContextMenuItem> submenu;
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxContextMenuItemType : uint8_t {
    None          = 0,
    Backdrop      = 1, ///< Panel background (fallback hit).
    Row           = 2, ///< Actionable menu row.
    Separator     = 3, ///< Horizontal divider.
    SubmenuCaret  = 4, ///< Right-chevron on rows with a submenu.
};

struct VfxContextMenuLayoutItem {
    VfxContextMenuItemType type = VfxContextMenuItemType::None;
    VfxStackRect           rect;
    size_t                 itemIndex = static_cast<size_t>(-1);
};

struct VfxContextMenuHitResult {
    VfxContextMenuItemType type = VfxContextMenuItemType::None;
    size_t                 itemIndex = static_cast<size_t>(-1);
    bool                   enabled   = true;

    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxContextMenuItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxContextMenuWidget {
public:
    using Key = VfxEditorKeybindingHandler::Key;

    // ── Tunables ───────────────────────────────────────────────────────
    float rowHeight        = 22.0f;
    float separatorHeight  = 7.0f;
    float panelWidth       = 200.0f;
    float iconColumnW      = 16.0f;
    float hotkeyColumnW    = 72.0f;
    float padding          = 4.0f;
    float submenuHoverDelay = 0.2f;  ///< seconds before submenu opens.

    // ── Lifecycle ──────────────────────────────────────────────────────

    /// Opens the menu anchored at `(anchorX, anchorY)`. If the menu
    /// would spill past `screenW` / `screenH`, the anchor flips so it
    /// stays on-screen. Replaces any prior items.
    void Open(float anchorX, float anchorY,
              std::vector<VfxContextMenuItem> items,
              float screenW = 0.0f, float screenH = 0.0f);

    /// Closes the menu (and any active submenu). Safe to call when
    /// already closed.
    void Close() noexcept;

    [[nodiscard]] bool IsOpen() const noexcept { return m_isOpen; }

    [[nodiscard]] const std::vector<VfxContextMenuItem>& Items() const noexcept {
        return m_items;
    }

    /// Visible rect. Empty when closed.
    [[nodiscard]] VfxStackRect Bounds() const noexcept;

    // ── Hover + keyboard ──────────────────────────────────────────────

    /// Updates hovered-row index by screen-coord cursor. Resets hover
    /// timer on a row change.
    void SetHover(float x, float y);

    /// Advances the submenu hover timer.
    void Tick(float dtSeconds) noexcept;

    /// Returns true when the nested submenu is currently rendered.
    [[nodiscard]] bool IsSubmenuActive() const noexcept { return m_submenuActive; }

    /// Returns the hovered row index or SIZE_MAX.
    [[nodiscard]] size_t HoveredIndex() const noexcept { return m_hoveredIndex; }

    /// Processes a key event. Currently only Escape is handled —
    /// returns true when the event is consumed.
    bool ProcessKey(Key key) noexcept;

    /// Dispatches a click at `(x, y)`. When the click hits an enabled
    /// row, activates it (fires `onActivate` and closes the menu).
    /// When the click falls outside the bounds, the menu closes.
    /// Returns true when the menu consumed the click.
    bool ClickAt(float x, float y);

    // ── Layout / Hit-test / Render ────────────────────────────────────

    [[nodiscard]] std::vector<VfxContextMenuLayoutItem> ComputeLayout() const;

    [[nodiscard]] VfxContextMenuHitResult HitTest(
        const std::vector<VfxContextMenuLayoutItem>& layout,
        float x, float y) const;

    void Render(TextRenderer& text, const BitmapFont& font) const;

private:
    bool m_isOpen = false;

    float m_anchorX = 0.0f;
    float m_anchorY = 0.0f;

    /// Final top-left after anchor-flip clamping.
    float m_renderX = 0.0f;
    float m_renderY = 0.0f;

    /// Screen size at Open time — used to re-clamp on Tick if needed.
    float m_screenW = 0.0f;
    float m_screenH = 0.0f;

    std::vector<VfxContextMenuItem> m_items;

    size_t m_hoveredIndex  = static_cast<size_t>(-1);
    float  m_hoverElapsed  = 0.0f;
    bool   m_submenuActive = false;

    /// Computes the total panel height from the current item list.
    [[nodiscard]] float ComputePanelHeight() const noexcept;
};

} // namespace uz
