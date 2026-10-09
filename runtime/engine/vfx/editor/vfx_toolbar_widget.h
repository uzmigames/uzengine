#pragma once

/// VFX Toolbar Widget — phase 14.5.0.
///
/// UE5-style horizontal toolbar: icon-plus-label buttons grouped
/// with 1-px `borderSubtle` vertical separators between runs.
///
/// ## Geometry
///
/// - Row height: 28 px.
/// - Button inner padding: 4 px.
/// - Gap between buttons: 2 px.
/// - Group separator: 1-px `borderSubtle` vertical rect with 6 px
///   horizontal padding on each side (8 px effective gap).
/// - Button corners: `radius.xs = 2 px`.
///
/// ## Button kinds
///
/// - `Action`: fires `onClick(id, kAction)`.
/// - `Toggle`: flips `toggled`; fires `onClick(id, kToggle)`.
///   While `toggled == true` the button fill stays `cardBgSelected`.
/// - `Dropdown`: right-edge `ChevronDown` sprite; fires
///   `onClick(id, kDropdown)` so the host can open a menu.

#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"  // VfxStackRect

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;
class MsdfFont;

enum class VfxToolbarKind : uint8_t {
    Action   = 0,
    Toggle   = 1,
    Dropdown = 2,
    Separator = 3,
};

struct VfxToolbarButton {
    uint32_t       id       = 0;
    std::string    label;
    VfxUiIcon      icon     = VfxUiIcon::None;
    VfxToolbarKind kind     = VfxToolbarKind::Action;
    bool           toggled  = false;
};

struct VfxToolbarLayoutItem {
    VfxStackRect rect;
    uint32_t     id        = 0;
    VfxToolbarKind kind    = VfxToolbarKind::Action;
    bool         hit       = false;
};

class VfxToolbarWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float rowHeight   = 28.0f;
    float buttonPad   = 4.0f;
    float buttonGap   = 2.0f;
    float sepPad      = 6.0f;
    float buttonRadius = 2.0f;

    // ── Model ───────────────────────────────────────────────────────────

    void Clear() { m_buttons.clear(); }

    uint32_t AddButton(const VfxToolbarButton& b) {
        m_buttons.push_back(b);
        return b.id;
    }

    void AddSeparator() {
        VfxToolbarButton sep;
        sep.kind = VfxToolbarKind::Separator;
        m_buttons.push_back(sep);
    }

    /// Sets the toggled state of a button by id. No-op on non-toggle
    /// buttons.
    void SetToggled(uint32_t id, bool toggled) {
        for (auto& b : m_buttons) {
            if (b.id == id && b.kind == VfxToolbarKind::Toggle) {
                b.toggled = toggled;
                return;
            }
        }
    }

    [[nodiscard]] bool IsToggled(uint32_t id) const {
        for (const auto& b : m_buttons) {
            if (b.id == id) return b.toggled;
        }
        return false;
    }

    [[nodiscard]] const std::vector<VfxToolbarButton>& Buttons() const {
        return m_buttons;
    }

    void SetHoveredId(uint32_t id) { m_hoveredId = id; }

    // ── Measure + layout + hit-test + render ────────────────────────────

    [[nodiscard]] float MeasureButton(const VfxToolbarButton& b) const;

    [[nodiscard]] std::vector<VfxToolbarLayoutItem> ComputeLayout(
        const VfxStackRect& barBounds) const;

    struct HitResult {
        bool           hit  = false;
        uint32_t       id   = 0;
        VfxToolbarKind kind = VfxToolbarKind::Action;
    };
    [[nodiscard]] HitResult HitTest(
        const std::vector<VfxToolbarLayoutItem>& layout,
        float x, float y) const;

    void Render(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, const VfxStackRect& barBounds) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    std::function<void(uint32_t, VfxToolbarKind)> onClick;

private:
    std::vector<VfxToolbarButton> m_buttons;
    uint32_t m_hoveredId = 0;
};

} // namespace uz
