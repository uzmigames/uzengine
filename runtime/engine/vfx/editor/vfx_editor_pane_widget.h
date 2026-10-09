#pragma once

/// VFX Editor Pane Widget — phase 15.3.0 / phase 16.3b.
///
/// Composite widget that hosts the editing sub-panels (stack /
/// parameters / emitters) behind a tab strip. Renders inside the
/// `editorPane` rect produced by the phase-15.1 `SplitHorizontal`
/// layout.
///
/// Phase 16.3b — added `EntityRef` InlineRowKind for the "Spline
/// Target" entity-picker row. The row renders the current entity name
/// (or "None") left-aligned and a "[x] Clear" button right-aligned.
/// Clicking the row body cycles forward through the available entity
/// list; clicking Clear writes `Entity::Invalid()`. The caller populates
/// `entityCandidates` each frame from the ECS world query and writes
/// back via `onEntitySelected`.

#include "engine/vfx/editor/vfx_stack_panel_widget.h" // VfxStackRect
#include "engine/vfx/editor/vfx_tab_strip_widget.h"
#include "engine/ecs/core/entity.h"                   // uz::Entity

#include <algorithm>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;
class MsdfFont;
class VfxStackPanelWidget;
class VfxParameterInspectorWidget;
class VfxTreeWidget;
class VfxSystemOverviewWidget;

enum class VfxEditorPaneTab : uint8_t {
    Stack      = 0,
    Parameters = 1,
    Emitters   = 2,
};

class VfxEditorPaneWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float tabStripHeight = 28.0f;

    // ── Binding ─────────────────────────────────────────────────────────

    void Bind(VfxStackPanelWidget*         stack,
              VfxParameterInspectorWidget* params,
              VfxTreeWidget*               tree,
              VfxSystemOverviewWidget*     overview) {
        m_stack    = stack;
        m_params   = params;
        m_tree     = tree;
        m_overview = overview;
    }

    // ── Active tab ──────────────────────────────────────────────────────

    [[nodiscard]] VfxEditorPaneTab ActiveTab() const { return m_activeTab; }

    void SetActiveTab(VfxEditorPaneTab tab) {
        if (tab == m_activeTab) return;
        m_activeTab = tab;
        if (onTabChanged) onTabChanged(tab);
    }

    // ── Hit-test ────────────────────────────────────────────────────────

    struct HitResult {
        bool consumed = false;
        VfxEditorPaneTab tab = VfxEditorPaneTab::Stack;
    };
    /// Dispatches a click at `(x, y)` against the tab strip. Returns
    /// `{true, newTab}` when the click lands on a tab (caller flips
    /// active state). Other clicks fall through.
    [[nodiscard]] HitResult HandleClick(float x, float y,
                                         const VfxStackRect& bounds);

    // ── Inline emitter attributes (phase 15.8) ──────────────────────────
    //
    // Direct pointer-bound float fields rendered at the top of the
    // `Parameters` tab. Surfaces `ParticleEmitter` inline attrs that
    // live on the struct rather than in `VfxAttributeStore`.
    //
    // Rows come in three kinds so optional modifiers (wind, drag,
    // acceleration, rotation) can stay hidden until the author toggles
    // them on:
    //   - `CoreField`       — scalar always visible (emission rate, etc.)
    //   - `ModifierHeader`  — checkbox + label. Enabled when the target
    //                         float is non-zero; clicking flips the
    //                         target between 0 and `toggleOnValue`.
    //                         Sub-fields below are only rendered when
    //                         enabled.
    //   - `SubField`        — scalar shown only while its owning modifier
    //                         is enabled. Indented one level for clarity.
    enum class InlineRowKind : uint8_t {
        CoreField      = 0,
        ModifierHeader = 1,
        SubField       = 2,
        SectionHeader  = 3,  ///< Visual-only divider (e.g. "Color Over Life").
        ColorGradient  = 4,  ///< Unreal-style colorbox preview with two handles.
        ColorSwatch    = 5,  ///< Clickable RGBA swatch that opens the picker popup.
        UintCoreField  = 6,  ///< Integer scalar, always visible. Drag rounds.
        UintSubField   = 7,  ///< Integer scalar inside a modifier group.
        EnumField      = 8,  ///< uint32 enum — click row cycles to next value.
        EnumSubField   = 9,  ///< Enum inside a modifier group.
        EntityRef      = 10, ///< ECS entity reference. Phase 16.3b — Spline Target picker.
                             ///<   Row body click: advance to next `entityCandidates` entry.
                             ///<   "[x] Clear" button at right edge: write Entity::Invalid().
                             ///<   Caller re-populates `entityCandidates` each frame from ECS.
    };
    struct InlineRow {
        InlineRowKind kind      = InlineRowKind::CoreField;
        std::string   label;

        // CoreField + SubField payload.
        float*        value     = nullptr;
        float         minValue  = 0.0f;
        float         maxValue  = 1.0f;

        // ModifierHeader payload. The toggle target is either a float
        // (non-zero = on, toggleOnValue written when turning on) or a
        // uint32 flag (0/1). Exactly one of the two should be set per
        // header row; the uint path covers `orbitEnabled`,
        // `groundCollision`, `particleLightEnabled`, `sizeBySpeedEnabled`.
        float*        toggleTarget    = nullptr;  ///< Float enable.
        uint32_t*     toggleTargetU32 = nullptr;  ///< Uint enable.
        float         toggleOnValue   = 1.0f;     ///< Written when flipping on (float path).
        bool          toggleOn        = false;    ///< Pre-computed by caller.

        // SubField payload: whether to render (caller filters off-rows).
        bool          subVisible = true;

        // ColorGradient payload. Left = start color, right = end color.
        // Four float pointers per side so R/G/B/A can be dragged via
        // the SubField rows below. `selectedStop` drives which endpoint
        // is currently highlighted (0 = left, 1 = right).
        float*        gradLeft[4]  = {nullptr, nullptr, nullptr, nullptr};
        float*        gradRight[4] = {nullptr, nullptr, nullptr, nullptr};
        uint32_t      selectedStop = 0u;
        /// Transition mode: 0 = smooth lerp (default), 1 = hard step.
        /// Currently preview-only — the particle update CS always
        /// interpolates linearly. Wire to the shader in a follow-up.
        uint32_t      transitionHard = 0u;

        // ColorSwatch payload. Four float* into the target RGBA so the
        // picker popup can write through on preset / HEX / slider edits.
        float*        swatchRgba[4] = {nullptr, nullptr, nullptr, nullptr};

        // UintCoreField / UintSubField / EnumField payload. `uValue`
        // targets the uint32_t field; drag writes round-to-int; enum
        // rows cycle through `enumNames[]` on click.
        uint32_t*         uValue    = nullptr;
        uint32_t          uMin      = 0u;
        uint32_t          uMax      = 0xFFFFFFFFu;
        const char* const* enumNames = nullptr;
        uint32_t          enumCount = 0u;

        // EntityRef payload (phase 16.3b — Spline Target picker).
        //
        // `entityCurrent` holds the entity displayed this frame. The
        // caller sets it from `VfxEmitterComponent::splineTargetEntity`
        // (or whichever ECS field the row targets) before calling
        // `SetInlineRows`. The render pass reads the name from
        // `entityCandidates` for the matching id.
        //
        // `entityCandidates` is a flat list of {entity, display-name}
        // pairs scanned from the ECS world (filtered to entities that
        // own `SplineComponent`). Re-populate every frame so newly
        // added/removed spline entities appear without stale data.
        //
        // `onEntitySelected` fires once per user selection change
        // (row-body cycle OR clear button). The callback writes the
        // chosen entity back to the ECS component field.
        struct EntityCandidate {
            Entity      entity;
            std::string name;
        };
        Entity                              entityCurrent    = Entity::Invalid();
        std::vector<EntityCandidate>        entityCandidates;
        std::function<void(Entity)>         onEntitySelected;
    };
    static constexpr float kInlineRowHeight         = 22.0f;
    static constexpr float kInlineSectionGap        = 6.0f;
    static constexpr float kInlineGradientRowHeight = 46.0f;
    static constexpr float kInlineSwatchRowHeight   = 34.0f;

    void SetInlineRows(std::vector<InlineRow> rows) {
        m_inlineRows = std::move(rows);
    }
    [[nodiscard]] const std::vector<InlineRow>& InlineRows() const noexcept {
        return m_inlineRows;
    }

    // ── Inline-strip scroll ─────────────────────────────────────────────
    [[nodiscard]] float InlineScrollOffset() const noexcept {
        return m_inlineScrollOffset;
    }
    void SetInlineScrollOffset(float offset) noexcept {
        m_inlineScrollOffset = std::max(0.0f, offset);
    }
    /// Total height of every visible row (sum of rendered rows × row
    /// height). Use to size the viewport's scrollable content.
    [[nodiscard]] float InlineContentHeight() const noexcept;

    /// Maps `y` relative to the Parameters-tab body to an inline-row
    /// index. Returns `SIZE_MAX` when the cursor is outside the inline
    /// strip — callers then fall back to the parameter inspector.
    [[nodiscard]] size_t HitTestInline(const VfxStackRect& bounds,
                                         float x, float y) const;

    /// Returns the rendered rect of the inline row at `rowIdx`
    /// accounting for scroll. Callers use this to derive sub-widget
    /// positions (e.g. the ColorSwatch square inside its row) for
    /// hit-testing without duplicating the widget's layout math.
    [[nodiscard]] VfxStackRect InlineRowRect(const VfxStackRect& bounds,
                                               size_t rowIdx) const;

    /// Returns the rect occupied by the inline strip inside `bounds`.
    /// When inline rows are bound the strip takes the full body;
    /// otherwise height is zero and the parameter inspector gets the
    /// full body.
    [[nodiscard]] VfxStackRect InlineStripRect(const VfxStackRect& bounds) const;

    /// Returns the rect the parameter inspector renders into. Zero
    /// height when the inline strip consumes the full body.
    [[nodiscard]] VfxStackRect ParamInspectorRect(const VfxStackRect& bounds) const;

    // ── Render ──────────────────────────────────────────────────────────

    void Render(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, const VfxStackRect& bounds);

    // ── Callbacks ───────────────────────────────────────────────────────

    std::function<void(VfxEditorPaneTab)> onTabChanged;

private:
    VfxEditorPaneTab             m_activeTab = VfxEditorPaneTab::Parameters;
    VfxTabStripWidget            m_tabStrip;

    VfxStackPanelWidget*         m_stack    = nullptr;
    VfxParameterInspectorWidget* m_params   = nullptr;
    VfxTreeWidget*               m_tree     = nullptr;
    VfxSystemOverviewWidget*     m_overview = nullptr;

    std::vector<InlineRow>       m_inlineRows;
    float                        m_inlineScrollOffset = 0.0f;

    void EnsureTabsPopulated();
};

} // namespace uz
