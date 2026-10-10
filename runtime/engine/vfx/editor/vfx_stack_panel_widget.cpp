/// VfxStackPanelWidget — implementation.
///
/// Phase 11.2.0. Pure-data interaction math (`ComputeLayout`, `HitTest`,
/// `ComputeInsertionIndex`) plus a thin `Render` pass over the layout.
/// All rendering goes through the overlay's `TextRenderer` + `BitmapFont`
/// (no ImGui). See `vfx_stack_panel_widget.h` for the contract.

#include "vfx_stack_panel_widget.h"

#include "editor/font/text_renderer.h"
#include "editor/font/bitmap_font.h"
#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"
#include "engine/vfx/modules/vfx_module_metadata.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Module-category icon helper (phase 12.2.0 — exported for tests)
// ─────────────────────────────────────────────────────────────────────────────

/// Returns the icon glyph constant for `moduleTypeName`'s declared
/// category in `VfxModuleMetadata`. Unknown modules map to
/// `VFX_ICON_CAT_GENERIC`.
const char* VfxModuleIconFor(const std::string& moduleTypeName) noexcept {
    const auto* entry = VfxModuleMetadata::Find(moduleTypeName);
    if (entry == nullptr || entry->category == nullptr) {
        return VFX_ICON_CAT_GENERIC;
    }
    const char* cat = entry->category;
    // Lightweight category -> icon table. Stable across runs.
    if (std::strcmp(cat, "Simulation") == 0) return VFX_ICON_CAT_LIFETIME;
    if (std::strcmp(cat, "Forces")     == 0) return VFX_ICON_CAT_FORCE;
    if (std::strcmp(cat, "Collision")  == 0) return VFX_ICON_CAT_LOCATION;
    if (std::strcmp(cat, "Render")     == 0) return VFX_ICON_CAT_RENDER;
    if (std::strcmp(cat, "DI")         == 0) return VFX_ICON_CAT_EVENT;
    if (std::strcmp(cat, "Dynamic")    == 0) return VFX_ICON_CAT_VELOCITY;
    if (std::strcmp(cat, "Events")     == 0) return VFX_ICON_CAT_EVENT;
    if (std::strcmp(cat, "Simulation") == 0) return VFX_ICON_CAT_LIFETIME;
    return VFX_ICON_CAT_GENERIC;
}

namespace {

// ─── Palette bindings (phase 12.1.0) ────────────────────────────────────────
// Every color the widget draws with comes from `VfxTheme()`. The legacy
// `kColor*` identifiers now alias into theme tokens so existing call sites
// compile unchanged.

const Vec4& kColorHeaderBg    = VfxThemeColors().barBg;
const Vec4& kColorHeaderBgHi  = VfxThemeColors().cardBgHover;
const Vec4& kColorCardBg      = VfxThemeColors().cardBg;
const Vec4& kColorCardBgHover = VfxThemeColors().cardBgHover;
const Vec4& kColorCardBgSel   = VfxThemeColors().cardBgSelected;
const Vec4& kColorCardBgDrag  = VfxThemeColors().cardBgDrag;
const Vec4& kColorAddRowBg    = VfxThemeColors().panelBgAlt;
const Vec4& kColorCheckbox    = VfxThemeColors().textSecondary;
const Vec4& kColorCheckboxOn  = VfxThemeColors().stateSuccess;
const Vec4& kColorText        = VfxThemeColors().textPrimary;
const Vec4& kColorTextDim     = VfxThemeColors().textDim;
const Vec4& kColorTextAccent  = VfxThemeColors().textAccent;
const Vec4& kColorDragHandle  = VfxThemeColors().borderStrong;
const Vec4& kColorInsertGap   = VfxThemeColors().insertionGap;

// ─── Layout constants (local — not tuned per-instance) ──────────────────────

constexpr float kInsertGapHeight = 2.0f;
constexpr float kTextBaselineYOffset = 4.0f;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Stage entry lookup
// ─────────────────────────────────────────────────────────────────────────────

const VfxStackEntry* VfxStackPanelWidget::StageEntry(VfxStage stage) const noexcept {
    if (m_stack == nullptr) return nullptr;
    return &m_stack->GetEntry(stage);
}

VfxStackEntry* VfxStackPanelWidget::StageEntry(VfxStage stage) noexcept {
    if (m_stack == nullptr) return nullptr;
    return &m_stack->GetEntry(stage);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

void VfxStackPanelWidget::SelectModule(const std::string& instanceId) {
    if (m_selectedInstanceId == instanceId) return;
    m_selectedInstanceId = instanceId;
    if (onModuleSelected) onModuleSelected(m_selectedInstanceId);
}

// ─────────────────────────────────────────────────────────────────────────────
// Drag
// ─────────────────────────────────────────────────────────────────────────────

void VfxStackPanelWidget::BeginDrag(const std::string& instanceId) {
    if (m_stack == nullptr || instanceId.empty()) return;
    VfxStage foundStage = VfxStage::ParticleUpdate;
    size_t   foundIndex = 0;
    if (!m_stack->FindInstance(instanceId, foundStage, foundIndex)) return;
    m_dragActive        = true;
    m_draggedInstanceId = instanceId;
    m_draggedFromStage  = foundStage;
    m_draggedFromIndex  = foundIndex;
}

bool VfxStackPanelWidget::EndDrag(VfxStage targetStage, size_t targetIndex) {
    if (!m_dragActive || m_stack == nullptr) {
        m_dragActive = false;
        m_draggedInstanceId.clear();
        return false;
    }
    // Cross-stage reorder is out-of-scope for 11.2 — cancel without effect.
    if (targetStage != m_draggedFromStage) {
        CancelDrag();
        return false;
    }

    VfxStackEntry* entry = StageEntry(targetStage);
    if (entry == nullptr) {
        CancelDrag();
        return false;
    }

    const size_t oldIndex = m_draggedFromIndex;
    // Clamp target into [0, size).
    if (entry->modules.empty()) {
        CancelDrag();
        return false;
    }
    const size_t clamped = (targetIndex >= entry->modules.size())
                               ? entry->modules.size() - 1u
                               : targetIndex;

    if (clamped == oldIndex) {
        CancelDrag();
        return false;
    }

    // Move by swapping towards the target, preserving every other module's
    // relative order.
    if (clamped > oldIndex) {
        for (size_t i = oldIndex; i < clamped; ++i) {
            entry->Swap(i, i + 1u);
        }
    } else {
        for (size_t i = oldIndex; i > clamped; --i) {
            entry->Swap(i, i - 1u);
        }
    }

    const std::string movedId = m_draggedInstanceId;
    const VfxStage    stage   = targetStage;

    m_dragActive = false;
    m_draggedInstanceId.clear();

    if (onModuleMoved) onModuleMoved(movedId, stage, clamped);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

float VfxStackPanelWidget::ComputeContentHeight() const {
    if (m_stack == nullptr) return 0.0f;
    float h = 0.0f;

    // Phase 12.2.0 — emitter header (optional, only when a name is set).
    if (!m_emitterName.empty()) {
        h += emitterHeaderHeight + stageGap;
    }

    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        const VfxStage stage = static_cast<VfxStage>(i);
        h += headerHeight;
        if (IsStageExpanded(stage)) {
            const auto& entry = m_stack->GetEntry(stage);
            for (size_t j = 0; j < entry.modules.size(); ++j) {
                h += cardHeight;
                if (j + 1u < entry.modules.size()) h += rowGap;
            }
            // Phase 12.2.0: the "+ Add Module" row was folded into the
            // stage header's inline add button — no extra height here.
        }
        h += stageGap;
    }
    return h;
}

std::vector<VfxStackLayoutItem> VfxStackPanelWidget::ComputeLayout(
    const VfxStackRect& panelBounds) const {
    std::vector<VfxStackLayoutItem> out;
    if (m_stack == nullptr) return out;

    // Upper bound: emitter header + per-stage (header + add-button) +
    // per-module (card + drag handle + checkbox + overflow menu).
    out.reserve(1u + static_cast<size_t>(kVfxStageCount) * 2u +
                m_stack->TotalModuleCount() * 4u);

    const float x0    = panelBounds.x;
    const float width = panelBounds.width;
    float       y     = panelBounds.y - m_scrollOffset;

    // ── Emitter header (phase 12.2.0) ──────────────────────────────────
    // Emitted only when a name has been set. Acts as the first visual
    // anchor above the stages.
    if (!m_emitterName.empty()) {
        VfxStackLayoutItem hdr;
        hdr.type  = VfxStackItemType::EmitterHeader;
        hdr.rect  = { x0, y, width, emitterHeaderHeight };
        out.push_back(hdr);
        y += emitterHeaderHeight + stageGap;
    }

    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        const VfxStage stage = static_cast<VfxStage>(i);
        const auto& entry = m_stack->GetEntry(stage);

        // ── Stage header ──────────────────────────────────────────────
        VfxStackLayoutItem header;
        header.type  = VfxStackItemType::StageHeader;
        header.stage = stage;
        header.rect  = { x0, y, width, headerHeight };
        out.push_back(header);

        // Inline "+" add-module button on the right edge of the header.
        // 12.2.0: replaces the standalone AddRow at the bottom of each
        // stage with a discoverable affordance tied to the header.
        VfxStackLayoutItem addBtn;
        addBtn.type  = VfxStackItemType::StageAddButton;
        addBtn.stage = stage;
        const float addX = x0 + width - stageAddButtonWidth - 4.0f;
        addBtn.rect  = { addX, y + (headerHeight - stageAddButtonWidth) * 0.5f,
                          stageAddButtonWidth, stageAddButtonWidth };
        out.push_back(addBtn);

        y += headerHeight;

        if (!IsStageExpanded(stage)) {
            y += stageGap;
            continue;
        }

        // ── Module cards ──────────────────────────────────────────────
        for (size_t m = 0; m < entry.modules.size(); ++m) {
            const auto& mod = entry.modules[m];
            const float cardX = x0 + cardIndent;
            const float cardW = width - cardIndent;

            VfxStackLayoutItem card;
            card.type        = VfxStackItemType::ModuleCard;
            card.stage       = stage;
            card.instanceId  = mod.instanceId;
            card.moduleIndex = m;
            card.rect        = { cardX, y, cardW, cardHeight };
            out.push_back(card);

            // Drag handle sub-rect (left-edge grip). Starts after the
            // optional selected-stripe region (3 px) but spans the
            // full drag-handle column width. Clicking this sub-rect is
            // the ONLY way to initiate a reorder drag.
            VfxStackLayoutItem grip;
            grip.type        = VfxStackItemType::CardDragHandle;
            grip.stage       = stage;
            grip.instanceId  = mod.instanceId;
            grip.moduleIndex = m;
            grip.rect        = {
                cardX + selectedStripeWidth,
                y,
                dragHandleWidth - selectedStripeWidth,
                cardHeight
            };
            out.push_back(grip);

            // Checkbox sub-region — centered vertically inside the card
            // after the drag handle.
            VfxStackLayoutItem cb;
            cb.type        = VfxStackItemType::CardCheckbox;
            cb.stage       = stage;
            cb.instanceId  = mod.instanceId;
            cb.moduleIndex = m;
            cb.rect        = {
                cardX + dragHandleWidth,
                y + (cardHeight - checkboxWidth) * 0.5f,
                checkboxWidth,
                checkboxWidth
            };
            out.push_back(cb);

            // Overflow menu sub-rect (right edge of the card).
            VfxStackLayoutItem over;
            over.type        = VfxStackItemType::CardOverflowMenu;
            over.stage       = stage;
            over.instanceId  = mod.instanceId;
            over.moduleIndex = m;
            over.rect        = {
                cardX + cardW - overflowMenuWidth,
                y,
                overflowMenuWidth,
                cardHeight
            };
            out.push_back(over);

            y += cardHeight;
            if (m + 1u < entry.modules.size()) y += rowGap;
        }

        // Phase 12.2.0: no standalone AddRow — the inline
        // `StageAddButton` already handles the "add module" action.
        y += stageGap;
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxStackHitResult VfxStackPanelWidget::HitTest(
    const std::vector<VfxStackLayoutItem>& layout,
    float x, float y) const {
    VfxStackHitResult result;
    // Priority: checkbox > module card > add row > stage header.
    // Walk the layout once, promote the most-specific hit we see.
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        const int specificity = static_cast<int>(item.type);
        const int current     = static_cast<int>(result.type);
        if (specificity > current ||
            result.type == VfxStackItemType::None) {
            result.type        = item.type;
            result.stage       = item.stage;
            result.instanceId  = item.instanceId;
            result.moduleIndex = item.moduleIndex;
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Drag insertion index
// ─────────────────────────────────────────────────────────────────────────────

size_t VfxStackPanelWidget::ComputeInsertionIndex(
    const std::vector<VfxStackLayoutItem>& layout,
    VfxStage stage, float mouseY) const {
    if (!IsStageExpanded(stage)) return 0;
    const VfxStackEntry* entry = StageEntry(stage);
    if (entry == nullptr || entry->modules.empty()) return 0;

    // Collect the cards for `stage` in layout order.
    size_t i = 0;
    for (const auto& item : layout) {
        if (item.type != VfxStackItemType::ModuleCard) continue;
        if (item.stage != stage) continue;

        const float rowMid = item.rect.y + item.rect.height * 0.5f;
        if (mouseY < rowMid) {
            return i;
        }
        ++i;
    }
    return entry->modules.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Rendering
// ─────────────────────────────────────────────────────────────────────────────

namespace {

void FormatBadge(char* buf, size_t bufSize, size_t count) {
    std::snprintf(buf, bufSize, "(%zu)", count);
}

// Phase 11.11: inheritance override visual treatment. Kept as file-local
// constants + a helper that returns (glyph, colour) per flag so both
// the stack panel and the parameter inspector can share the palette
// when 11.11 extends the inspector.
struct OverrideIcon {
    const char* glyph;
    Vec4        color;
};

[[nodiscard]] OverrideIcon IconFor(VfxOverrideFlag f) noexcept {
    switch (f) {
        case VfxOverrideFlag::None:            return { "",   Vec4{0,0,0,0} };
        case VfxOverrideFlag::Inherited:       return { "↳",  Vec4{0.55f, 0.55f, 0.58f, 1.0f} };
        case VfxOverrideFlag::OverrideAdd:     return { "●",  Vec4{0.55f, 0.85f, 0.55f, 1.0f} };
        case VfxOverrideFlag::OverrideReplace: return { "⇄",  Vec4{0.95f, 0.65f, 0.25f, 1.0f} };
        case VfxOverrideFlag::OverrideRemove:  return { "✕",  Vec4{0.90f, 0.35f, 0.35f, 1.0f} };
    }
    return { "", Vec4{0,0,0,0} };
}

std::string ModuleSummaryText(const VfxModuleInstance& mod) {
    // If the module has a config blob, show its first 24 chars inline as a
    // brief preview. Otherwise just the type name.
    if (mod.configJson.empty()) return mod.moduleTypeName;
    std::string out = mod.moduleTypeName;
    out += "  ";
    const size_t n = (mod.configJson.size() < 24u) ? mod.configJson.size() : 24u;
    out.append(mod.configJson, 0, n);
    if (n < mod.configJson.size()) out += "…";
    return out;
}

} // namespace

void VfxStackPanelWidget::Render(TextRenderer& text, const BitmapFont& font,
                                  const VfxStackRect& panelBounds) const {
    if (m_stack == nullptr) return;

    const auto& palette = VfxThemeColors();
    const auto layout = ComputeLayout(panelBounds);

    // Track the last stage bottom-y per iteration so we can draw a
    // 1-px `borderSubtle` divider between stages (phase 12.2.0).
    float lastStageBottom = -1.0f;
    VfxStage lastStage     = VfxStage::ParticleUpdate;
    bool     lastStageSeen = false;

    for (const auto& item : layout) {
        // Skip items scrolled off the top or below the panel.
        if (item.rect.y + item.rect.height < panelBounds.y) continue;
        if (item.rect.y > panelBounds.y + panelBounds.height) continue;

        switch (item.type) {
        case VfxStackItemType::EmitterHeader: {
            // Solid card fill with a 1-px bottom border for separation
            // from the first stage header.
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.barBg);
            text.AddRect(item.rect.x, item.rect.y + item.rect.height - 1.0f,
                         item.rect.width, 1.0f, palette.borderSubtle);

            // Enable checkbox on the left (if a pointer is bound).
            float textX = item.rect.x + 8.0f;
            if (m_emitterEnabledPtr != nullptr) {
                const float cbSize = 14.0f;
                const float cbX    = item.rect.x + 8.0f;
                const float cbY    = item.rect.y + (item.rect.height - cbSize) * 0.5f;
                text.AddRect(cbX, cbY, cbSize, cbSize, palette.borderStrong);
                text.AddRect(cbX + 2.0f, cbY + 2.0f, cbSize - 4.0f, cbSize - 4.0f,
                             *m_emitterEnabledPtr ? palette.stateSuccess
                                                    : palette.cardBg);
                textX = cbX + cbSize + 6.0f;
            }

            text.AddText(font, m_emitterName, textX,
                         item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                         palette.textPrimary);
            break;
        }

        case VfxStackItemType::StageHeader: {
            const bool expanded = IsStageExpanded(item.stage);
            const auto& entry   = m_stack->GetEntry(item.stage);

            // V3 minimalist — stage header is flat. No background fill,
            // no accent stripe. The chevron + uppercase-style header
            // text does all the communicating. Keeps the stack panel
            // reading as one continuous list instead of a grid of
            // boxed sections.

            // Chevron + stage name — no stray "." glyph.
            const char* chevron =
                expanded ? VFX_ICON_CHEVRON_DOWN : VFX_ICON_CHEVRON_RIGHT;
            const float baseY = item.rect.y + kTextBaselineYOffset;
            text.AddText(font, chevron, item.rect.x + 8.0f, baseY,
                         palette.textPrimary);

            text.AddText(font, VfxStageName(item.stage),
                         item.rect.x + 22.0f, baseY, palette.textPrimary);

            // Count badge only when non-zero (zero is noise).
            if (entry.modules.size() > 0u) {
                char badge[24];
                FormatBadge(badge, sizeof(badge), entry.modules.size());
                text.AddText(font, badge,
                             item.rect.x + item.rect.width - stageAddButtonWidth
                               - 40.0f,
                             baseY, palette.textDim);
            }

            // V3 — no section divider. `stageGap` creates whitespace
            // between stages instead of a hard hairline.
            lastStage     = item.stage;
            lastStageSeen = true;
            lastStageBottom = item.rect.y + item.rect.height;
            break;
        }

        case VfxStackItemType::StageAddButton: {
            // Ghost button — transparent background, textDim `+` glyph.
            // Hover state is applied by the overlay; plain render is a
            // minimal affordance to avoid screaming for attention.
            text.AddText(font, VFX_ICON_PLUS,
                         item.rect.x + (item.rect.width - 7.0f) * 0.5f,
                         item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                         palette.textDim);
            break;
        }

        case VfxStackItemType::ModuleCard: {
            const auto* entry = StageEntry(item.stage);
            if (entry == nullptr || item.moduleIndex >= entry->modules.size()) {
                break;
            }
            const auto& mod = entry->modules[item.moduleIndex];

            const bool selected = (mod.instanceId == m_selectedInstanceId);
            const bool hovered  = (mod.instanceId == m_hoveredInstanceId);
            const bool dragging = (m_dragActive &&
                                   mod.instanceId == m_draggedInstanceId);

            Vec4 bg = palette.cardBg;
            if (dragging)      bg = palette.cardBgDrag;
            else if (selected) bg = palette.cardBgSelected;
            else if (hovered)  bg = palette.cardBgHover;

            // V3 minimalist — card is a rounded rect with NO border
            // (borders add visual noise on dense module lists).
            // Hover state uses bg brightness bump, selection uses
            // accent-tinted bg. Rounded corners alone provide card
            // separation.
            text.AddRoundedRect(item.rect.x, item.rect.y,
                                 item.rect.width, item.rect.height,
                                 3.0f /*card radius*/,
                                 bg);

            // Selected accent stripe (3 px, left edge). Drawn only on
            // selected cards to draw the eye to the focused item.
            if (selected) {
                text.AddRect(item.rect.x, item.rect.y,
                             selectedStripeWidth, item.rect.height,
                             palette.accentPrimary);
            }

            // Module text — dimmed when disabled OR when the module is
            // flagged `OverrideRemove`.
            const bool isRemoved =
                (mod.overrideFlag == VfxOverrideFlag::OverrideRemove);
            const Vec4 txt = (mod.enabled && !isRemoved)
                             ? palette.textPrimary
                             : palette.textDim;

            // Category icon + name (centered vertically inside the card).
            const float textY = item.rect.y +
                                (item.rect.height - 14.0f) * 0.5f;
            const float iconX =
                item.rect.x + dragHandleWidth + checkboxWidth + 2.0f;
            text.AddText(font, VfxModuleIconFor(mod.moduleTypeName),
                         iconX, textY, palette.textSecondary);

            const float nameX = iconX + moduleIconWidth + 4.0f;
            text.AddText(font, ModuleSummaryText(mod), nameX, textY, txt);

            // Inheritance-state icon just left of the overflow menu.
            if (mod.overrideFlag != VfxOverrideFlag::None) {
                const OverrideIcon ico = IconFor(mod.overrideFlag);
                const float ovX = item.rect.x + item.rect.width
                                   - overflowMenuWidth - 18.0f;
                text.AddText(font, ico.glyph, ovX, textY, ico.color);
            }
            break;
        }

        case VfxStackItemType::CardDragHandle: {
            // Phase 13.5 — sprite GripDots glyph from the VFX icon
            // atlas (12x12 centred) replaces the ASCII `:` grip.
            constexpr float kGripSize = 12.0f;
            text.AddSprite(VfxUiIcon::GripDots,
                           item.rect.x + (item.rect.width - kGripSize) * 0.5f,
                           item.rect.y + (item.rect.height - kGripSize) * 0.5f,
                           kGripSize, kGripSize, palette.textDim);
            break;
        }

        case VfxStackItemType::CardCheckbox: {
            const auto* entry = StageEntry(item.stage);
            if (entry == nullptr || item.moduleIndex >= entry->modules.size()) {
                break;
            }
            const auto& mod = entry->modules[item.moduleIndex];

            // Outline + inner fill (matches the emitter header checkbox
            // style for consistency).
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.borderStrong);
            const float inset = 2.0f;
            const Vec4 fill = mod.enabled ? palette.stateSuccess
                                           : palette.cardBg;
            text.AddRect(item.rect.x + inset, item.rect.y + inset,
                         item.rect.width - inset * 2.0f,
                         item.rect.height - inset * 2.0f, fill);
            break;
        }

        case VfxStackItemType::CardOverflowMenu: {
            // Only show the `...` glyph when the card is hovered OR
            // selected — otherwise every row looks littered with dots.
            const auto* entry = StageEntry(item.stage);
            if (entry != nullptr && item.moduleIndex < entry->modules.size()) {
                const auto& mod = entry->modules[item.moduleIndex];
                const bool show = (mod.instanceId == m_hoveredInstanceId) ||
                                    (mod.instanceId == m_selectedInstanceId);
                if (show) {
                    text.AddText(font, VFX_ICON_OVERFLOW,
                                 item.rect.x + 4.0f,
                                 item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                                 palette.textDim);
                }
            }
            break;
        }

        case VfxStackItemType::AddRow:
            // Phase 12.2.0: never emitted — the inline `StageAddButton`
            // replaces this. The enum value is kept for ABI stability.
            break;

        case VfxStackItemType::None:
            break;
        }
    }
}

} // namespace uz
