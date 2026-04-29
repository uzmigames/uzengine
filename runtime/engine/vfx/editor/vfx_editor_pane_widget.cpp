/// VfxEditorPaneWidget — implementation (phase 15.3.0 / phase 16.3b).

#include "vfx_editor_pane_widget.h"

#include "editor/font/bitmap_font.h"
#include "editor/font/msdf_font.h"
#include "editor/font/text_renderer.h"
#include "engine/ecs/core/entity.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_parameter_inspector_widget.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"
#include "engine/vfx/editor/vfx_system_overview_widget.h"
#include "engine/vfx/editor/vfx_tree_widget.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace uz {

void VfxEditorPaneWidget::EnsureTabsPopulated() {
    if (m_tabStrip.Count() == 2u) return;
    m_tabStrip.Clear();
    // Stack tab dropped in phase 15.9 — all author-facing editing lives
    // under Parameters (core fields + opt-in modifiers) + Emitters
    // (tree + system overview). Keep the enum entry for ABI stability;
    // HandleClick never maps a visible tab click back to Stack now.
    VfxTabEntry a; a.label = "Parameters"; a.closable = false; m_tabStrip.AddTab(a);
    VfxTabEntry b; b.label = "Emitters";   b.closable = false; m_tabStrip.AddTab(b);
}

VfxEditorPaneWidget::HitResult VfxEditorPaneWidget::HandleClick(
    float x, float y, const VfxStackRect& bounds) {
    HitResult result;
    const VfxStackRect stripBounds{
        bounds.x, bounds.y, bounds.width, tabStripHeight
    };
    EnsureTabsPopulated();
    const auto layout = m_tabStrip.ComputeLayout(stripBounds);
    const auto hit = m_tabStrip.HitTest(layout, x, y);
    if (hit.type == VfxTabItemType::Tab) {
        result.consumed = true;
        // Index layout after the Stack tab was removed (phase 15.9):
        //   0 → Parameters,  1 → Emitters.
        switch (hit.tabIndex) {
        case 0: result.tab = VfxEditorPaneTab::Parameters; break;
        case 1: result.tab = VfxEditorPaneTab::Emitters;   break;
        default: result.consumed = false; break;
        }
    }
    return result;
}

namespace {
// Returns the row's rendered height in pixels — gradient rows are
// taller than stock rows so the preview is actually readable.
float RowHeight(const VfxEditorPaneWidget::InlineRow& r) {
    if (r.kind == VfxEditorPaneWidget::InlineRowKind::ColorGradient) {
        return VfxEditorPaneWidget::kInlineGradientRowHeight;
    }
    if (r.kind == VfxEditorPaneWidget::InlineRowKind::ColorSwatch) {
        return VfxEditorPaneWidget::kInlineSwatchRowHeight;
    }
    return VfxEditorPaneWidget::kInlineRowHeight;
}

// Shared renderer for scalar-value rows (core or sub). The caller
// provides the label, indent, and display buffer. Keeps the Render
// switch tidy across the many row flavours that share the same
// "label left, field right" layout.
void DrawScalarRow(TextRenderer& text, const BitmapFont& font,
                    const Vec4& labelColor, const VfxColorPalette& pal,
                    const VfxStackRect& rowRect, const char* label,
                    float labelIndent, const char* valueText) {
    text.AddText(font, label, rowRect.x + labelIndent, rowRect.y + 4.0f,
                  labelColor);
    constexpr float kFieldW = 120.0f;
    const float fieldX = rowRect.x + rowRect.width - kFieldW - 8.0f;
    text.AddRect(fieldX, rowRect.y + 3.0f, kFieldW, rowRect.height - 6.0f,
                  pal.inputBg);
    text.AddText(font, valueText, fieldX + 6.0f, rowRect.y + 4.0f,
                  pal.textPrimary);
}

// Counts rows that actually render — sub-fields whose owning modifier
// is disabled are culled so the strip height matches visible content.
size_t CountVisibleRows(
    const std::vector<VfxEditorPaneWidget::InlineRow>& rows) {
    size_t n = 0;
    for (const auto& r : rows) {
        if (r.kind == VfxEditorPaneWidget::InlineRowKind::SubField &&
            !r.subVisible) continue;
        ++n;
    }
    return n;
}

// Sum of per-row heights for every visible row.
float ComputeVisibleHeight(
    const std::vector<VfxEditorPaneWidget::InlineRow>& rows) {
    float h = 0.0f;
    for (const auto& r : rows) {
        if (r.kind == VfxEditorPaneWidget::InlineRowKind::SubField &&
            !r.subVisible) continue;
        h += RowHeight(r);
    }
    return h;
}
}

float VfxEditorPaneWidget::InlineContentHeight() const noexcept {
    return ComputeVisibleHeight(m_inlineRows);
}

VfxStackRect VfxEditorPaneWidget::InlineStripRect(const VfxStackRect& bounds) const {
    const VfxStackRect body{
        bounds.x, bounds.y + tabStripHeight,
        bounds.width,
        std::max(0.0f, bounds.height - tabStripHeight)
    };
    if (CountVisibleRows(m_inlineRows) == 0u) {
        return VfxStackRect{ body.x, body.y, body.width, 0.0f };
    }
    // Strip takes the full body so a long list can scroll within its
    // own viewport instead of being clipped / pushed into the empty
    // inspector pane below.
    return body;
}

VfxStackRect VfxEditorPaneWidget::ParamInspectorRect(const VfxStackRect& bounds) const {
    const VfxStackRect body{
        bounds.x, bounds.y + tabStripHeight,
        bounds.width,
        std::max(0.0f, bounds.height - tabStripHeight)
    };
    const auto strip = InlineStripRect(bounds);
    return VfxStackRect{
        body.x, body.y + strip.height,
        body.width,
        std::max(0.0f, body.height - strip.height)
    };
}

VfxStackRect VfxEditorPaneWidget::InlineRowRect(const VfxStackRect& bounds,
                                                  size_t rowIdx) const {
    if (rowIdx >= m_inlineRows.size()) return VfxStackRect{};
    const auto strip = InlineStripRect(bounds);
    float cursor = 0.0f;
    for (size_t i = 0; i <= rowIdx; ++i) {
        const auto& r = m_inlineRows[i];
        if (r.kind == InlineRowKind::SubField && !r.subVisible) continue;
        const float h = RowHeight(r);
        if (i == rowIdx) {
            return VfxStackRect{
                strip.x,
                strip.y + cursor - m_inlineScrollOffset,
                strip.width,
                h
            };
        }
        cursor += h;
    }
    return VfxStackRect{};
}

size_t VfxEditorPaneWidget::HitTestInline(const VfxStackRect& bounds,
                                           float x, float y) const {
    if (m_inlineRows.empty()) return static_cast<size_t>(-1);
    const auto strip = InlineStripRect(bounds);
    if (!strip.Contains(x, y)) return static_cast<size_t>(-1);
    // Reverse the scroll offset so cursor y maps to the logical row
    // y regardless of scroll position. Per-row heights differ now
    // (ColorGradient is taller) so we walk the list cumulatively
    // instead of dividing by a fixed row-height.
    const float localY = (y - strip.y) + m_inlineScrollOffset;
    if (localY < 0.0f) return static_cast<size_t>(-1);
    float cursor = 0.0f;
    for (size_t i = 0; i < m_inlineRows.size(); ++i) {
        const auto& r = m_inlineRows[i];
        if (r.kind == InlineRowKind::SubField && !r.subVisible) continue;
        const float h = RowHeight(r);
        if (localY < cursor + h) return i;
        cursor += h;
    }
    return static_cast<size_t>(-1);
}

void VfxEditorPaneWidget::Render(TextRenderer& text,
                                  const BitmapFont& font,
                                  const MsdfFont* msdf,
                                  const VfxStackRect& bounds) {
    const auto& palette = VfxThemeColors();

    // Panel background.
    text.AddRect(bounds.x, bounds.y, bounds.width, bounds.height,
                 palette.panelBg);

    // Tab strip at the top.
    const VfxStackRect stripBounds{
        bounds.x, bounds.y, bounds.width, tabStripHeight
    };
    EnsureTabsPopulated();
    // Enum values (Stack=0/Parameters=1/Emitters=2) no longer align 1:1
    // with tab strip indices after Stack was dropped. Map explicitly.
    const size_t stripIdx = (m_activeTab == VfxEditorPaneTab::Emitters)
        ? 1u : 0u;
    m_tabStrip.SetActiveIndex(stripIdx);
    m_tabStrip.Render(text, font, msdf, stripBounds);

    // Body slice below the tab strip.
    const VfxStackRect body{
        bounds.x, bounds.y + tabStripHeight,
        bounds.width,
        std::max(0.0f, bounds.height - tabStripHeight)
    };

    auto renderPlaceholder = [&](const char* label) {
        text.AddText(font, label, body.x + 12.0f, body.y + 16.0f,
                      palette.textDim);
    };

    switch (m_activeTab) {
    case VfxEditorPaneTab::Stack:
        if (m_stack != nullptr) {
            m_stack->Render(text, font, body);
        } else {
            renderPlaceholder("Stack: no widget bound");
        }
        break;
    case VfxEditorPaneTab::Parameters: {
        // Inline-attr strip (direct pointer-bound emitter fields) above
        // the store-driven parameter inspector. Each row is a core
        // scalar, a modifier toggle header, or an optional sub-field
        // that only renders when its owning modifier is enabled.
        const VfxStackRect strip = InlineStripRect(bounds);
        if (strip.height > 0.0f && !m_inlineRows.empty()) {
            const float contentH = InlineContentHeight();
            const float maxScroll = std::max(0.0f, contentH - strip.height);
            if (m_inlineScrollOffset > maxScroll) {
                m_inlineScrollOffset = maxScroll;
            }
            char buf[48];
            float cursorY = 0.0f;
            size_t visibleRow = 0;
            for (const auto& r : m_inlineRows) {
                if (r.kind == InlineRowKind::SubField && !r.subVisible) {
                    continue;
                }
                const float rowH = RowHeight(r);
                const float rowY = strip.y + cursorY - m_inlineScrollOffset;
                cursorY += rowH;
                ++visibleRow;
                if (rowY + rowH <= strip.y) continue;
                if (rowY >= strip.y + strip.height) continue;
                const VfxStackRect rowRect{
                    strip.x, rowY, strip.width, rowH
                };
                const Vec4 rowBg = (visibleRow & 1u)
                    ? palette.panelBg : palette.panelBgAlt;
                text.AddRect(rowRect.x, rowRect.y,
                              rowRect.width, rowRect.height, rowBg);

                switch (r.kind) {
                case InlineRowKind::CoreField: {
                    text.AddText(font, r.label,
                                  rowRect.x + 8.0f,
                                  rowRect.y + 4.0f, palette.textSecondary);
                    const float fieldW = 120.0f;
                    const float fieldX = rowRect.x + rowRect.width - fieldW - 8.0f;
                    text.AddRect(fieldX, rowRect.y + 3.0f,
                                  fieldW, rowRect.height - 6.0f,
                                  palette.inputBg);
                    const float v = (r.value != nullptr) ? *r.value : 0.0f;
                    std::snprintf(buf, sizeof(buf), "%.3f", v);
                    text.AddText(font, buf,
                                  fieldX + 6.0f, rowRect.y + 4.0f,
                                  palette.textPrimary);
                    break;
                }
                case InlineRowKind::ModifierHeader: {
                    // 12 px checkbox on the left + bold label. Filled
                    // square when enabled, hollow outline when off.
                    const float cbSize = 12.0f;
                    const float cbX = rowRect.x + 8.0f;
                    const float cbY = rowRect.y + (rowRect.height - cbSize) * 0.5f;
                    const Vec4 cbFill = r.toggleOn
                        ? palette.accentPrimary : palette.inputBg;
                    text.AddRect(cbX, cbY, cbSize, cbSize, cbFill);
                    // 1-px outline (draw 4 sides).
                    text.AddRect(cbX, cbY, cbSize, 1.0f, palette.borderStrong);
                    text.AddRect(cbX, cbY + cbSize - 1.0f, cbSize, 1.0f, palette.borderStrong);
                    text.AddRect(cbX, cbY, 1.0f, cbSize, palette.borderStrong);
                    text.AddRect(cbX + cbSize - 1.0f, cbY, 1.0f, cbSize, palette.borderStrong);
                    // Optional check tick — two diagonals via 2 thin rects.
                    if (r.toggleOn) {
                        const float cx = cbX + cbSize * 0.5f;
                        const float cy = cbY + cbSize * 0.5f;
                        text.AddRect(cx - 4.0f, cy + 0.0f, 4.0f, 1.5f,
                                      palette.textPrimary);
                        text.AddRect(cx + 0.0f, cy - 3.0f, 1.5f, 4.0f,
                                      palette.textPrimary);
                    }
                    text.AddText(font, r.label,
                                  cbX + cbSize + 8.0f,
                                  rowRect.y + 4.0f,
                                  r.toggleOn ? palette.textPrimary
                                              : palette.textSecondary);
                    break;
                }
                case InlineRowKind::SubField: {
                    // Indented label so the grouping reads.
                    text.AddText(font, r.label,
                                  rowRect.x + 28.0f,
                                  rowRect.y + 4.0f, palette.textSecondary);
                    const float fieldW = 120.0f;
                    const float fieldX = rowRect.x + rowRect.width - fieldW - 8.0f;
                    text.AddRect(fieldX, rowRect.y + 3.0f,
                                  fieldW, rowRect.height - 6.0f,
                                  palette.inputBg);
                    const float v = (r.value != nullptr) ? *r.value : 0.0f;
                    std::snprintf(buf, sizeof(buf), "%.3f", v);
                    text.AddText(font, buf,
                                  fieldX + 6.0f, rowRect.y + 4.0f,
                                  palette.textPrimary);
                    break;
                }
                case InlineRowKind::UintCoreField:
                case InlineRowKind::UintSubField: {
                    const float indent = (r.kind == InlineRowKind::UintSubField)
                        ? 28.0f : 8.0f;
                    const uint32_t v = (r.uValue != nullptr) ? *r.uValue : 0u;
                    std::snprintf(buf, sizeof(buf), "%u", v);
                    DrawScalarRow(text, font, palette.textSecondary,
                                   palette, rowRect, r.label.c_str(),
                                   indent, buf);
                    break;
                }
                case InlineRowKind::EnumField:
                case InlineRowKind::EnumSubField: {
                    const float indent = (r.kind == InlineRowKind::EnumSubField)
                        ? 28.0f : 8.0f;
                    const uint32_t v = (r.uValue != nullptr) ? *r.uValue : 0u;
                    const char* label = (r.enumNames != nullptr &&
                                           v < r.enumCount)
                        ? r.enumNames[v] : "?";
                    DrawScalarRow(text, font, palette.textSecondary,
                                   palette, rowRect, r.label.c_str(),
                                   indent, label);
                    break;
                }
                case InlineRowKind::SectionHeader: {
                    // Bold label + subtle underline. No checkbox, no
                    // value — this is a pure visual group divider for
                    // always-active collections (Color/Size Over Life).
                    text.AddText(font, r.label,
                                  rowRect.x + 8.0f,
                                  rowRect.y + 4.0f,
                                  palette.textPrimary);
                    text.AddRect(rowRect.x + 8.0f,
                                  rowRect.y + rowRect.height - 1.0f,
                                  rowRect.width - 16.0f, 1.0f,
                                  palette.borderSubtle);
                    break;
                }
                case InlineRowKind::ColorGradient: {
                    // Unreal-style colorbox: horizontal gradient swatch
                    // with two handles below marking the two stops the
                    // GPU actually samples (start + end). The gradient
                    // is approximated with N vertical slices so every
                    // pixel column samples a lerp between the endpoint
                    // colors; Hard mode snaps at the midpoint.
                    const Vec4 cL{
                        (r.gradLeft[0] != nullptr) ? *r.gradLeft[0] : 0.0f,
                        (r.gradLeft[1] != nullptr) ? *r.gradLeft[1] : 0.0f,
                        (r.gradLeft[2] != nullptr) ? *r.gradLeft[2] : 0.0f,
                        (r.gradLeft[3] != nullptr) ? *r.gradLeft[3] : 1.0f
                    };
                    const Vec4 cR{
                        (r.gradRight[0] != nullptr) ? *r.gradRight[0] : 0.0f,
                        (r.gradRight[1] != nullptr) ? *r.gradRight[1] : 0.0f,
                        (r.gradRight[2] != nullptr) ? *r.gradRight[2] : 0.0f,
                        (r.gradRight[3] != nullptr) ? *r.gradRight[3] : 1.0f
                    };

                    const float swX = rowRect.x + 10.0f;
                    const float swY = rowRect.y + 4.0f;
                    const float swW = rowRect.width - 20.0f;
                    const float swH = rowRect.height - 22.0f;

                    // Checker backdrop for alpha preview (2 rows × N cols).
                    const Vec4 chkA{0.20f, 0.20f, 0.20f, 1.0f};
                    const Vec4 chkB{0.30f, 0.30f, 0.30f, 1.0f};
                    const float chkSize = swH * 0.5f;
                    const int chkCols = std::max(1,
                        static_cast<int>(swW / chkSize));
                    for (int c = 0; c < chkCols; ++c) {
                        for (int row = 0; row < 2; ++row) {
                            const Vec4 col = (((c + row) & 1) != 0) ? chkA : chkB;
                            const float cx = swX +
                                static_cast<float>(c) * chkSize;
                            const float cy = swY +
                                static_cast<float>(row) * chkSize;
                            const float cw = std::min(chkSize, swX + swW - cx);
                            text.AddRect(cx, cy, cw, chkSize, col);
                        }
                    }

                    // Gradient slices. 64 samples blends smoothly at
                    // the cost of 64 AddRect calls per frame — fine
                    // while this is a per-emitter widget.
                    constexpr int kSlices = 64;
                    const float sliceW = swW / static_cast<float>(kSlices);
                    for (int s = 0; s < kSlices; ++s) {
                        const float t = (static_cast<float>(s) + 0.5f)
                                        / static_cast<float>(kSlices);
                        float k = t;
                        if (r.transitionHard != 0u) {
                            k = (t < 0.5f) ? 0.0f : 1.0f;
                        }
                        const Vec4 col{
                            cL.x + (cR.x - cL.x) * k,
                            cL.y + (cR.y - cL.y) * k,
                            cL.z + (cR.z - cL.z) * k,
                            cL.w + (cR.w - cL.w) * k
                        };
                        text.AddRect(swX + static_cast<float>(s) * sliceW,
                                      swY, sliceW, swH, col);
                    }

                    // 1-px border around the swatch.
                    text.AddRect(swX, swY, swW, 1.0f, palette.borderStrong);
                    text.AddRect(swX, swY + swH - 1.0f, swW, 1.0f, palette.borderStrong);
                    text.AddRect(swX, swY, 1.0f, swH, palette.borderStrong);
                    text.AddRect(swX + swW - 1.0f, swY, 1.0f, swH, palette.borderStrong);

                    // Handle triangles below the swatch — left (start)
                    // and right (end). 8-px wide square; selected stop
                    // gets the accent outline.
                    const float handleY = swY + swH + 2.0f;
                    const float handleSize = 10.0f;
                    auto drawHandle = [&](float cx, bool selected) {
                        const float hx = cx - handleSize * 0.5f;
                        text.AddRect(hx, handleY, handleSize, handleSize,
                                      selected ? palette.accentPrimary
                                                : palette.inputBg);
                        text.AddRect(hx, handleY, handleSize, 1.0f,
                                      palette.borderStrong);
                        text.AddRect(hx, handleY + handleSize - 1.0f,
                                      handleSize, 1.0f, palette.borderStrong);
                        text.AddRect(hx, handleY, 1.0f, handleSize,
                                      palette.borderStrong);
                        text.AddRect(hx + handleSize - 1.0f, handleY,
                                      1.0f, handleSize, palette.borderStrong);
                    };
                    drawHandle(swX,            r.selectedStop == 0u);
                    drawHandle(swX + swW,      r.selectedStop == 1u);
                    break;
                }
                case InlineRowKind::ColorSwatch: {
                    // Label left + circular swatch right (click opens
                    // the full RGBA / HEX / presets picker overlay).
                    // Circle approximated by a 5×5 SDF-style rect
                    // stack since TextRenderer lacks a disc primitive.
                    text.AddText(font, r.label,
                                  rowRect.x + 8.0f,
                                  rowRect.y + 9.0f, palette.textSecondary);

                    const Vec4 col{
                        (r.swatchRgba[0] != nullptr) ? *r.swatchRgba[0] : 0.0f,
                        (r.swatchRgba[1] != nullptr) ? *r.swatchRgba[1] : 0.0f,
                        (r.swatchRgba[2] != nullptr) ? *r.swatchRgba[2] : 0.0f,
                        (r.swatchRgba[3] != nullptr) ? *r.swatchRgba[3] : 1.0f
                    };

                    constexpr float kSwatchSize = 24.0f;
                    const float swX = rowRect.x + rowRect.width - kSwatchSize - 10.0f;
                    const float swY = rowRect.y + (rowRect.height - kSwatchSize) * 0.5f;

                    // 4×4 chequer under the swatch for alpha preview.
                    const Vec4 chkA{0.22f, 0.22f, 0.22f, 1.0f};
                    const Vec4 chkB{0.32f, 0.32f, 0.32f, 1.0f};
                    const float chk = kSwatchSize * 0.25f;
                    for (int cy = 0; cy < 4; ++cy) {
                        for (int cx = 0; cx < 4; ++cx) {
                            const Vec4 ccol = (((cx + cy) & 1) != 0) ? chkA : chkB;
                            text.AddRect(swX + static_cast<float>(cx) * chk,
                                          swY + static_cast<float>(cy) * chk,
                                          chk, chk, ccol);
                        }
                    }
                    // Color fill on top (alpha blend shows chequer).
                    text.AddRect(swX, swY, kSwatchSize, kSwatchSize, col);
                    // 1-px frame so the swatch reads as a distinct UI
                    // element, not a flat color span.
                    text.AddRect(swX, swY, kSwatchSize, 1.0f, palette.borderStrong);
                    text.AddRect(swX, swY + kSwatchSize - 1.0f, kSwatchSize, 1.0f, palette.borderStrong);
                    text.AddRect(swX, swY, 1.0f, kSwatchSize, palette.borderStrong);
                    text.AddRect(swX + kSwatchSize - 1.0f, swY, 1.0f, kSwatchSize, palette.borderStrong);

                    // HEX readout next to the swatch.
                    const int rI = std::clamp(static_cast<int>(col.x * 255.0f + 0.5f), 0, 255);
                    const int gI = std::clamp(static_cast<int>(col.y * 255.0f + 0.5f), 0, 255);
                    const int bI = std::clamp(static_cast<int>(col.z * 255.0f + 0.5f), 0, 255);
                    const int aI = std::clamp(static_cast<int>(col.w * 255.0f + 0.5f), 0, 255);
                    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X",
                                   rI, gI, bI, aI);
                    text.AddText(font, buf,
                                  swX - 82.0f, rowRect.y + 9.0f,
                                  palette.textPrimary);
                    break;
                }
                case InlineRowKind::EntityRef: {
                    // Phase 16.3b — "Spline Target" entity-picker row.
                    //
                    // Layout:
                    //   [Label]  [EntityName or "None"]  [[x] Clear]
                    //   <-- 40% label col -->  <-- value field -->  <-- 52px clear btn -->
                    //
                    // Row body click (not on Clear): cycles to the next
                    // candidate in `entityCandidates`. Clear sets Invalid.
                    // Both actions fire `onEntitySelected`.
                    //
                    // This is a display-only render pass: click handling is
                    // done in `VfxEditorOverlay` which hits-tests row index
                    // and sub-region. The widget renders the authoritative
                    // visual so the caller's input code has a stable reference.

                    // Label column (40% of row width, left-aligned).
                    constexpr float kLabelFrac  = 0.40f;
                    constexpr float kClearBtnW  = 52.0f;
                    const float labelW = rowRect.width * kLabelFrac;
                    text.AddText(font, r.label.c_str(),
                                  rowRect.x + 8.0f,
                                  rowRect.y + 4.0f,
                                  palette.textSecondary);

                    // Resolve display name for the current entity.
                    const bool hasEntity = r.entityCurrent.IsValid();
                    const char* entityDisplayName = "None";
                    if (hasEntity) {
                        bool foundInCandidates = false;
                        // Search the candidate list for the current entity.
                        for (const auto& cand : r.entityCandidates) {
                            if (cand.entity == r.entityCurrent) {
                                entityDisplayName = cand.name.c_str();
                                foundInCandidates = true;
                                break;
                            }
                        }
                        // If the current entity is not in the candidate list
                        // (stale from a previous scene state), show a fallback
                        // that makes the stale reference visible to the author.
                        if (!foundInCandidates) {
                            entityDisplayName = "? (stale entity)";
                        }
                    }

                    // Value field: spans from label column to right edge
                    // minus the Clear button.
                    const float fieldX = rowRect.x + labelW;
                    const float fieldW = rowRect.width - labelW - kClearBtnW - 4.0f;
                    text.AddRect(fieldX, rowRect.y + 3.0f,
                                  fieldW, rowRect.height - 6.0f,
                                  palette.inputBg);
                    // 1-px accent border when an entity is selected —
                    // mirrors UE5 object-reference fields.
                    if (hasEntity) {
                        text.AddRect(fieldX, rowRect.y + 3.0f,
                                      fieldW, 1.0f, palette.accentMuted);
                        text.AddRect(fieldX, rowRect.y + rowRect.height - 4.0f,
                                      fieldW, 1.0f, palette.accentMuted);
                        text.AddRect(fieldX, rowRect.y + 3.0f,
                                      1.0f, rowRect.height - 6.0f, palette.accentMuted);
                        text.AddRect(fieldX + fieldW - 1.0f, rowRect.y + 3.0f,
                                      1.0f, rowRect.height - 6.0f, palette.accentMuted);
                    }
                    text.AddText(font, entityDisplayName,
                                  fieldX + 6.0f, rowRect.y + 4.0f,
                                  hasEntity ? palette.textPrimary : palette.textDim);

                    // Clear button: always rendered but only interactable
                    // when `entityCurrent` is valid. Greyed when no entity
                    // is set so the author understands the field is empty.
                    const float clearX = rowRect.x + rowRect.width - kClearBtnW;
                    const Vec4  clearBg   = hasEntity
                        ? palette.stateError
                        : palette.inputBg;
                    const Vec4  clearText = hasEntity
                        ? palette.textPrimary
                        : palette.textDisabled;
                    text.AddRect(clearX, rowRect.y + 3.0f,
                                  kClearBtnW - 2.0f, rowRect.height - 6.0f,
                                  clearBg);
                    text.AddText(font, "[x] Clear",
                                  clearX + 4.0f, rowRect.y + 4.0f,
                                  clearText);
                    break;
                }
                }
            }
        }

        // Parameter inspector underneath.
        const VfxStackRect insRect = ParamInspectorRect(bounds);
        if (m_params != nullptr) {
            m_params->Render(text, font, insRect);
        } else {
            renderPlaceholder("Parameters: no widget bound");
        }
        break;
    }
    case VfxEditorPaneTab::Emitters:
        if (m_tree != nullptr) {
            // Top half: tree; bottom half: system overview (when bound).
            const float half = body.height * 0.5f;
            const VfxStackRect treeR{ body.x, body.y, body.width, half };
            const VfxStackRect ovR{ body.x, body.y + half,
                                      body.width, body.height - half };
            m_tree->Render(text, font, msdf, treeR);
            if (m_overview != nullptr) {
                m_overview->Render(text, font, ovR);
            }
        } else {
            renderPlaceholder("Emitters: no widget bound");
        }
        break;
    }
}

} // namespace uz
