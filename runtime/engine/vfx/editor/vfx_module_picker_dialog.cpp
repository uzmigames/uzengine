/// VfxModulePickerDialog — implementation.

#include "vfx_module_picker_dialog.h"

#include "editor/font/text_renderer.h"
#include "editor/font/bitmap_font.h"
#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/modules/vfx_module_metadata.h"

#include <algorithm>
#include <cctype>
#include <cstdio>

namespace uz {

namespace {

// Dialog palette (phase 12.1.0 — theme-sourced).
const Vec4& kColorBg        = VfxThemeColors().popupBg;
const Vec4& kColorHeader    = VfxThemeColors().barBg;
const Vec4& kColorTabBg     = VfxThemeColors().cardBg;
const Vec4& kColorTabActive = VfxThemeColors().cardBgSelected;
const Vec4& kColorTile      = VfxThemeColors().cardBg;
const Vec4& kColorTileSel   = VfxThemeColors().cardBgSelected;
const Vec4& kColorBorder    = VfxThemeColors().borderStrong;
const Vec4& kColorText      = VfxThemeColors().textPrimary;
const Vec4& kColorTextDim   = VfxThemeColors().textDim;
const Vec4& kColorFilter    = VfxThemeColors().inputBg;

constexpr float kBorderThickness = 1.0f;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

void VfxModulePickerDialog::Open(VfxStage targetStage, float anchorX, float anchorY,
                                  float screenW, float screenH) {
    m_isOpen       = true;
    m_targetStage  = targetStage;
    m_anchorX      = anchorX;
    m_anchorY      = anchorY;
    m_filter.clear();
    m_activeCategory = "All";

    // Nudge back into bounds when screen size is provided.
    if (screenW > 0.0f) {
        if (m_anchorX + dialogWidth > screenW) {
            m_anchorX = std::max(0.0f, screenW - dialogWidth);
        }
        if (m_anchorX < 0.0f) m_anchorX = 0.0f;
    }
    if (screenH > 0.0f) {
        if (m_anchorY + dialogHeight > screenH) {
            m_anchorY = std::max(0.0f, screenH - dialogHeight);
        }
        if (m_anchorY < 0.0f) m_anchorY = 0.0f;
    }

    // Pre-select the first visible tile when one exists.
    const auto visible = VisibleModules();
    m_selectedIndex = visible.empty() ? static_cast<size_t>(-1) : 0u;
}

void VfxModulePickerDialog::Close() noexcept {
    m_isOpen = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Category + filter
// ─────────────────────────────────────────────────────────────────────────────

void VfxModulePickerDialog::SetCategory(const std::string& category) {
    // Validate against the catalog's category list so the dialog never
    // lands in a state where the tab bar doesn't reflect the active value.
    const auto cats = VfxModuleMetadata::Categories();
    const bool known = std::any_of(cats.begin(), cats.end(),
                                    [&](const std::string& c) { return c == category; });
    if (!known) return;
    m_activeCategory = category;

    // Keep the selection if the module is still visible, otherwise snap
    // to the first visible tile (or clear).
    const auto visible = VisibleModules();
    m_selectedIndex = visible.empty() ? static_cast<size_t>(-1) : 0u;
}

void VfxModulePickerDialog::SetFilter(const std::string& filter) {
    m_filter = filter;
    const auto visible = VisibleModules();
    m_selectedIndex = visible.empty() ? static_cast<size_t>(-1) : 0u;
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

void VfxModulePickerDialog::SelectIndex(size_t index) {
    const auto visible = VisibleModules();
    if (visible.empty()) {
        m_selectedIndex = static_cast<size_t>(-1);
        return;
    }
    if (index >= visible.size()) {
        // Wrap out-of-range targets into [0, count).
        index = index % visible.size();
    }
    m_selectedIndex = index;
}

void VfxModulePickerDialog::NavigateSelection(int delta) {
    const auto visible = VisibleModules();
    if (visible.empty()) {
        m_selectedIndex = static_cast<size_t>(-1);
        return;
    }
    const int  n   = static_cast<int>(visible.size());
    const int  cur = (m_selectedIndex == static_cast<size_t>(-1))
                         ? 0
                         : static_cast<int>(m_selectedIndex);
    int next = (cur + delta) % n;
    if (next < 0) next += n;
    m_selectedIndex = static_cast<size_t>(next);
}

void VfxModulePickerDialog::SelectFirst() {
    const auto visible = VisibleModules();
    m_selectedIndex = visible.empty() ? static_cast<size_t>(-1) : 0u;
}

void VfxModulePickerDialog::SelectLast() {
    const auto visible = VisibleModules();
    m_selectedIndex = visible.empty()
                          ? static_cast<size_t>(-1)
                          : visible.size() - 1u;
}

// ─────────────────────────────────────────────────────────────────────────────
// Filter matching
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxModulePickerDialog::LowerCopy(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        out += static_cast<char>(
            std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

bool VfxModulePickerDialog::MatchesFilter(const std::string& lowerFilter,
                                           const std::string& name,
                                           const std::string& desc) {
    if (lowerFilter.empty()) return true;
    if (LowerCopy(name).find(lowerFilter) != std::string::npos) return true;
    if (LowerCopy(desc).find(lowerFilter) != std::string::npos) return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Visible modules (category + stage + filter)
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> VfxModulePickerDialog::VisibleModules() const {
    auto baseline = VfxModuleMetadata::FilterByCategoryAndStage(
        m_activeCategory, m_targetStage);
    if (m_filter.empty()) return baseline;

    const std::string lowerFilter = LowerCopy(m_filter);
    std::vector<std::string> out;
    out.reserve(baseline.size());
    for (const auto& name : baseline) {
        const std::string desc = VfxModuleMetadata::GetDescription(name);
        if (MatchesFilter(lowerFilter, name, desc)) {
            out.push_back(name);
        }
    }
    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Commit
// ─────────────────────────────────────────────────────────────────────────────

bool VfxModulePickerDialog::ConfirmSelection() {
    if (!m_isOpen) return false;
    const auto visible = VisibleModules();
    if (visible.empty()) return false;
    if (m_selectedIndex == static_cast<size_t>(-1) ||
        m_selectedIndex >= visible.size()) {
        return false;
    }

    const std::string chosen = visible[m_selectedIndex];
    const VfxStage    stage  = m_targetStage;

    m_isOpen = false;
    if (onModuleChosen) onModuleChosen(chosen, stage);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxPickerLayoutItem> VfxModulePickerDialog::ComputeLayout() const {
    std::vector<VfxPickerLayoutItem> out;
    if (!m_isOpen) return out;

    const auto visible = VisibleModules();
    const auto cats    = VfxModuleMetadata::Categories();
    out.reserve(cats.size() + visible.size() + 6u);

    const bool hasSelection =
        (m_selectedIndex != static_cast<size_t>(-1) &&
         m_selectedIndex < visible.size());

    // ── Close button (top-right, inside the header) ───────────────────
    {
        VfxPickerLayoutItem it;
        it.type = VfxPickerItemType::CloseButton;
        it.rect = {
            m_anchorX + dialogWidth - headerHeight,
            m_anchorY,
            headerHeight,
            headerHeight
        };
        it.label = "x";
        out.push_back(it);
    }

    // ── Search input (row 1 — below header) ───────────────────────────
    const float searchTop = m_anchorY + headerHeight;
    {
        VfxPickerLayoutItem it;
        it.type  = VfxPickerItemType::SearchInput;
        it.rect  = {
            m_anchorX + tilePaddingX,
            searchTop + 2.0f,
            dialogWidth - tilePaddingX * 2.0f,
            searchHeight - 4.0f
        };
        it.label = m_filter.empty() ? std::string("Search modules...")
                                      : m_filter;
        out.push_back(it);
    }

    // ── Category tabs with count badges (row 2) ───────────────────────
    const float tabsTop = searchTop + searchHeight;
    const float tabWidth =
        (cats.empty() ? dialogWidth
                       : dialogWidth / static_cast<float>(cats.size()));
    for (size_t i = 0; i < cats.size(); ++i) {
        const size_t count = VfxModuleMetadata::CountByCategoryAndStage(
            cats[i], m_targetStage);
        VfxPickerLayoutItem it;
        it.type  = VfxPickerItemType::CategoryTab;
        it.index = i;
        // Embed the count in the label so tests + render share one
        // string (spec §"Category tabs carry count badges"). ASCII
        // "%s %zu" replaces the prior "\xB7 %zu" — the single byte 0xB7
        // is invalid UTF-8 and the bitmap font had no glyph for U+00B7,
        // so the bullet rendered as "?" at every tab. The plain space
        // keeps the label short enough ("Simulation 12" = 13 chars) to
        // fit inside a ~100 px tab at the default 12 px bitmap font.
        {
            char buf[48];
            std::snprintf(buf, sizeof(buf), "%s %zu",
                           cats[i].c_str(), count);
            it.label = buf;
        }
        it.rect  = {
            m_anchorX + tabWidth * static_cast<float>(i),
            tabsTop,
            tabWidth,
            tabRowHeight
        };
        out.push_back(it);
    }

    // ── Tile grid (responsive 3/4 columns) ────────────────────────────
    const int columns = (dialogWidth >= kFourColumnMinWidth) ? 4 : 3;
    const float gridTop = tabsTop + tabRowHeight + tilePaddingY;
    const float gridBottom = m_anchorY + dialogHeight - footerHeight
                              - (hasSelection ? previewHeight : 0.0f);
    const float availableW = dialogWidth - tilePaddingX * 2.0f;
    const float columnW =
        (availableW - tilePaddingX * (static_cast<float>(columns) - 1.0f))
        / static_cast<float>(columns);

    for (size_t i = 0; i < visible.size(); ++i) {
        const int col = static_cast<int>(i) % columns;
        const int row = static_cast<int>(i) / columns;
        const float x = m_anchorX + tilePaddingX +
                         static_cast<float>(col) * (columnW + tilePaddingX);
        const float y = gridTop +
                         static_cast<float>(row) * (tileHeight + tilePaddingY);
        if (y + tileHeight > gridBottom) break;

        VfxPickerLayoutItem it;
        it.type  = VfxPickerItemType::ModuleTile;
        it.index = i;
        it.label = visible[i];
        it.rect  = { x, y, columnW, tileHeight };
        out.push_back(it);
    }

    // ── Preview area (only when a tile is selected) ───────────────────
    if (hasSelection) {
        VfxPickerLayoutItem it;
        it.type  = VfxPickerItemType::PreviewArea;
        it.rect  = {
            m_anchorX + tilePaddingX,
            m_anchorY + dialogHeight - footerHeight - previewHeight,
            dialogWidth - tilePaddingX * 2.0f,
            previewHeight - tilePaddingY
        };
        it.label = visible[m_selectedIndex];
        out.push_back(it);
    }

    // ── Footer buttons (Create primary + Cancel secondary) ────────────
    {
        const float footerTop = m_anchorY + dialogHeight - footerHeight;
        VfxPickerLayoutItem cancel;
        cancel.type  = VfxPickerItemType::ButtonCancel;
        cancel.label = "Cancel";
        cancel.rect  = {
            m_anchorX + dialogWidth - footerBtnWidth * 2.0f - tilePaddingX * 2.0f,
            footerTop + tilePaddingY,
            footerBtnWidth,
            footerHeight - tilePaddingY * 2.0f
        };
        out.push_back(cancel);

        VfxPickerLayoutItem create;
        create.type  = VfxPickerItemType::ButtonCreate;
        create.label = "Create";
        create.rect  = {
            m_anchorX + dialogWidth - footerBtnWidth - tilePaddingX,
            footerTop + tilePaddingY,
            footerBtnWidth,
            footerHeight - tilePaddingY * 2.0f
        };
        out.push_back(create);
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxPickerHitResult VfxModulePickerDialog::HitTest(
    const std::vector<VfxPickerLayoutItem>& layout,
    float x, float y) const {
    VfxPickerHitResult hit;
    // Precedence: CloseButton > CategoryTab > FilterBox > ModuleTile.
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        const int spec = static_cast<int>(item.type);
        const int cur  = static_cast<int>(hit.type);
        // Note: CloseButton (4) must win over ModuleTile (3) — the specificity
        // ordering in the enum matches desired priority.
        if (spec > cur || hit.type == VfxPickerItemType::None) {
            hit.type  = item.type;
            hit.index = item.index;
            hit.label = item.label;
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

void VfxModulePickerDialog::Render(TextRenderer& text, const BitmapFont& font) const {
    if (!m_isOpen) return;

    const auto& palette = VfxThemeColors();

    // Phase 13.3.0 — single SDF rounded-rect carries the dialog panel
    // background + 1-px border + soft drop shadow in a single shader
    // pass. Replaces the phase-12 stack of offset rects that stood in
    // for a shadow.
    // V4 UE5 — dialog radius `lg` = 6 px (shrunk from V3's 8).
    constexpr float kPanelCornerRadius = 6.0f;
    constexpr float kShadowBlur        = 10.0f;
    const Vec2     shadowOffset{ 0.0f, 4.0f };
    text.AddRoundedRect(m_anchorX, m_anchorY, dialogWidth, dialogHeight,
                        kPanelCornerRadius,
                        palette.popupBg,
                        kBorderThickness, palette.borderStrong,
                        shadowOffset, kShadowBlur, palette.shadow);

    // Header row — also rounded on the TOP corners of the same panel.
    // A second rrect with matching radius + the dialog's panel colour
    // as the fill keeps rounded geometry flush with the panel.
    text.AddRoundedRect(m_anchorX, m_anchorY, dialogWidth, headerHeight,
                        kPanelCornerRadius,
                        palette.barBg);
    // Separator uses ASCII "-" because the bitmap font does not bake
    // U+00B7 and the raw byte 0xB7 is not valid UTF-8, so the prior
    // "\xB7" sequence rendered as the replacement glyph "?".
    std::string title = "Add Module  -  ";
    title += VfxStageName(m_targetStage);
    text.AddText(font, title, m_anchorX + 10.0f,
                 m_anchorY + (headerHeight - 14.0f) * 0.5f,
                 palette.textPrimary);

    const bool hasSelection =
        (m_selectedIndex != static_cast<size_t>(-1));

    const auto layout = ComputeLayout();
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxPickerItemType::SearchInput: {
            // Background
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.inputBg);
            // Search glyph on the left
            text.AddText(font, VFX_ICON_SEARCH,
                         it.rect.x + 6.0f,
                         it.rect.y + (it.rect.height - 14.0f) * 0.5f,
                         palette.textDim);
            // Placeholder or filter text
            text.AddText(font, it.label,
                         it.rect.x + 20.0f,
                         it.rect.y + (it.rect.height - 14.0f) * 0.5f,
                         m_filter.empty() ? palette.textDim
                                          : palette.textPrimary);
            break;
        }

        case VfxPickerItemType::CategoryTab: {
            // Extract the canonical category — last space separates the
            // name from the count badge in the "Name N" format, so slice
            // before `rfind(' ')`. Used to match against m_activeCategory.
            const std::string& full = it.label;
            const size_t sp = full.rfind(' ');
            const std::string baseCat = (sp == std::string::npos)
                ? full : full.substr(0, sp);
            const bool active = (baseCat == m_activeCategory);

            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         active ? palette.barBg : palette.panelBgAlt);

            text.AddText(font, it.label,
                         it.rect.x + 6.0f,
                         it.rect.y + (it.rect.height - 14.0f) * 0.5f,
                         active ? palette.textPrimary
                                 : palette.textSecondary);

            // 2-px accent underline on the active tab.
            if (active) {
                text.AddRect(it.rect.x, it.rect.y + it.rect.height - 2.0f,
                             it.rect.width, 2.0f, palette.accentPrimary);
            }
            break;
        }

        case VfxPickerItemType::FilterBox: {
            // Legacy — never emitted in 12.3.0 layouts, but kept for
            // forward compatibility with tests that call Render after
            // hand-crafting old layouts.
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.inputBg);
            text.AddText(font, it.label, it.rect.x + 6.0f,
                         it.rect.y + 4.0f,
                         m_filter.empty() ? palette.textDim
                                           : palette.textPrimary);
            break;
        }

        case VfxPickerItemType::ModuleTile: {
            const bool selected = (it.index == m_selectedIndex);
            const Vec4 bg = selected ? palette.cardBgSelected
                                      : palette.cardBg;
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         bg);

            // 2-px accent border on the selected tile (four edges).
            if (selected) {
                const Vec4 border = palette.accentPrimary;
                text.AddRect(it.rect.x, it.rect.y,
                             it.rect.width, 2.0f, border);
                text.AddRect(it.rect.x, it.rect.y + it.rect.height - 2.0f,
                             it.rect.width, 2.0f, border);
                text.AddRect(it.rect.x, it.rect.y,
                             2.0f, it.rect.height, border);
                text.AddRect(it.rect.x + it.rect.width - 2.0f, it.rect.y,
                             2.0f, it.rect.height, border);
            }

            // Icon (top-left)
            const char* glyph = VfxModuleMetadata::GetIconGlyph(it.label);
            text.AddText(font, glyph,
                         it.rect.x + 10.0f,
                         it.rect.y + 10.0f,
                         palette.textSecondary);

            // Module name (bold-ish — rendered at standard size but in
            // textPrimary; below the icon)
            text.AddText(font, it.label,
                         it.rect.x + 10.0f,
                         it.rect.y + 30.0f,
                         palette.textPrimary);

            // 2-line description (wrapped at word boundary)
            const std::string desc =
                VfxModuleMetadata::GetDescription(it.label);
            if (!desc.empty()) {
                const float wrapWidth = it.rect.width - 20.0f;
                const float charW = 7.0f; // approx bitmap-font char advance
                const size_t lineChars =
                    std::max<size_t>(8, static_cast<size_t>(wrapWidth / charW));
                std::string line1, line2;
                if (desc.size() <= lineChars) {
                    line1 = desc;
                } else {
                    // Wrap at last space within the first N chars.
                    size_t split = desc.rfind(' ', lineChars);
                    if (split == std::string::npos || split < 8u) split = lineChars;
                    line1 = desc.substr(0, split);
                    line2 = desc.substr(split < desc.size() ? split + 1u
                                                              : split);
                    if (line2.size() > lineChars) {
                        line2 = line2.substr(0, lineChars - 1u) + "\xC9";
                    }
                }
                text.AddText(font, line1,
                             it.rect.x + 10.0f,
                             it.rect.y + 50.0f, palette.textDim);
                if (!line2.empty()) {
                    text.AddText(font, line2,
                                 it.rect.x + 10.0f,
                                 it.rect.y + 66.0f, palette.textDim);
                }
            }
            break;
        }

        case VfxPickerItemType::PreviewArea: {
            // Bordered panel at the bottom of the dialog above the footer.
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.panelBgAlt);
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, 1.0f,
                         palette.borderSubtle);

            // Icon + name header line
            const char* glyph = VfxModuleMetadata::GetIconGlyph(it.label);
            text.AddText(font, glyph, it.rect.x + 10.0f, it.rect.y + 8.0f,
                         palette.accentPrimary);
            text.AddText(font, it.label,
                         it.rect.x + 28.0f, it.rect.y + 8.0f,
                         palette.textPrimary);

            // Full description
            const std::string desc =
                VfxModuleMetadata::GetDescription(it.label);
            if (!desc.empty()) {
                text.AddText(font, desc,
                             it.rect.x + 10.0f, it.rect.y + 28.0f,
                             palette.textSecondary);
            }

            // Target-stage hint
            std::string hint = "Will be added to: ";
            hint += VfxStageName(m_targetStage);
            text.AddText(font, hint,
                         it.rect.x + 10.0f,
                         it.rect.y + it.rect.height - 18.0f,
                         palette.textDim);
            break;
        }

        case VfxPickerItemType::ButtonCreate: {
            const bool enabled = hasSelection;
            const Vec4 bg = enabled ? palette.accentPrimary
                                     : palette.cardBgDisabled;
            const Vec4 fg = enabled ? palette.textOnAccent
                                     : palette.textDisabled;
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         bg);
            const float textX = it.rect.x +
                (it.rect.width - static_cast<float>(it.label.size()) * 7.0f)
                * 0.5f;
            text.AddText(font, it.label, textX,
                         it.rect.y + (it.rect.height - 14.0f) * 0.5f, fg);
            break;
        }

        case VfxPickerItemType::ButtonCancel: {
            text.AddRect(it.rect.x, it.rect.y, it.rect.width, it.rect.height,
                         palette.cardBg);
            const float textX = it.rect.x +
                (it.rect.width - static_cast<float>(it.label.size()) * 7.0f)
                * 0.5f;
            text.AddText(font, it.label, textX,
                         it.rect.y + (it.rect.height - 14.0f) * 0.5f,
                         palette.textPrimary);
            break;
        }

        case VfxPickerItemType::CloseButton: {
            text.AddText(font, "x",
                         it.rect.x + it.rect.width * 0.5f - 3.0f,
                         it.rect.y + (it.rect.height - 14.0f) * 0.5f,
                         palette.textSecondary);
            break;
        }

        case VfxPickerItemType::None:
            break;
        }
    }
}

} // namespace uz
