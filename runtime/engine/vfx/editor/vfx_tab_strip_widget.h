#pragma once

/// VFX Tab Strip Widget — phase 14.2.0.
///
/// UE5-style horizontal tab strip. Tabs render with rounded TOP
/// corners (`radius.sm` = 3 px) and flat bottom corners so the
/// active tab visually merges with the panel body directly below.
/// Active tab carries a 2-px `accentPrimary` bar along its top
/// edge. Close `x` sprite appears on hover.
///
/// ## Geometry
///
/// - Strip height: 28 px (UE5 default tab strip).
/// - Tab width: dynamic (content-sized) with `kTabMinWidth = 80`
///   + `kTabPadding = 12` on each side.
/// - Adjacent tabs separated by a 1-px `borderSubtle` vertical rect.
///
/// ## Interaction
///
/// - Mouse-down anywhere in the tab: `onSelect(tabId)`.
/// - Mouse-down on the close-`x` region (hover-only): `onClose(tabId)`.
/// - Drag beyond 6 px threshold: `onReorder(fromIdx, toIdx)` fires on
///   mouse-up (spec — the hook is emitted; reorder wiring happens in
///   the host).
///
/// ## Thread safety
///
/// NOT thread-safe. Host-thread only.
///
/// @ref UE5 SDockTab / SDockingTabStack

#include "engine/vfx/editor/vfx_stack_panel_widget.h"  // VfxStackRect
#include "engine/vfx/editor/vfx_icon_atlas.h"          // VfxUiIcon

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;
class MsdfFont;

// ─────────────────────────────────────────────────────────────────────────────
// Tab entry
// ─────────────────────────────────────────────────────────────────────────────

struct VfxTabEntry {
    std::string label;
    VfxUiIcon   icon     = VfxUiIcon::None;
    bool        closable = true;
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout item types for testing
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxTabItemType : uint8_t {
    None      = 0,
    Tab       = 1,
    CloseIcon = 2,
    Divider   = 3,
};

struct VfxTabLayoutItem {
    VfxTabItemType type = VfxTabItemType::None;
    VfxStackRect   rect;
    size_t         tabIndex = static_cast<size_t>(-1);
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxTabStripWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float stripHeight   = 28.0f;
    float tabMinWidth   = 80.0f;
    float tabPadding    = 12.0f;
    float tabRadius     = 3.0f;        ///< `radius.sm`
    float accentBarH    = 2.0f;
    float closeIconSize = 10.0f;

    // ── Binding ─────────────────────────────────────────────────────────

    /// Adds a tab and returns its index.
    size_t AddTab(const VfxTabEntry& entry) {
        m_tabs.push_back(entry);
        return m_tabs.size() - 1u;
    }

    /// Clears all tabs.
    void Clear() { m_tabs.clear(); }

    /// Returns the number of tabs.
    [[nodiscard]] size_t Count() const { return m_tabs.size(); }

    /// Sets the selected tab index (clamped to `[0, Count)`).
    void SetActiveIndex(size_t idx) {
        m_activeIdx = (idx < m_tabs.size()) ? idx : m_activeIdx;
    }

    [[nodiscard]] size_t ActiveIndex() const { return m_activeIdx; }

    /// Sets the hovered tab index (used to drive the close-`x` render).
    /// Pass `(size_t)-1` when no tab is hovered.
    void SetHoveredIndex(size_t idx) { m_hoveredIdx = idx; }

    // ── Layout + hit-test + render ──────────────────────────────────────

    /// Computes layout items for the strip within `stripBounds`.
    [[nodiscard]] std::vector<VfxTabLayoutItem> ComputeLayout(
        const VfxStackRect& stripBounds) const;

    /// Measures a single tab's pixel width (font-dependent, approximated).
    [[nodiscard]] float MeasureTab(const VfxTabEntry& entry) const;

    /// Hit-tests `(x, y)` against the layout. Returns `{Tab, idx}` on
    /// tab hit, `{CloseIcon, idx}` on close-button hit, `{None, -1}`
    /// when outside.
    struct HitResult {
        VfxTabItemType type = VfxTabItemType::None;
        size_t tabIndex     = static_cast<size_t>(-1);
    };
    [[nodiscard]] HitResult HitTest(
        const std::vector<VfxTabLayoutItem>& layout,
        float x, float y) const;

    /// Renders the strip + tabs. Pass `msdf=nullptr` to fall back to
    /// the bitmap font for labels.
    void Render(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, const VfxStackRect& stripBounds) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    std::function<void(size_t)>         onSelect;
    std::function<void(size_t)>         onClose;
    std::function<void(size_t, size_t)> onReorder;

private:
    std::vector<VfxTabEntry> m_tabs;
    size_t m_activeIdx  = 0;
    size_t m_hoveredIdx = static_cast<size_t>(-1);
};

} // namespace uz
