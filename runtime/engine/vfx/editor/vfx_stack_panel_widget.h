#pragma once

/// VFX Stack Panel Widget — Niagara-style collapsible stack editor.
///
/// Phase 11.2.0. Renders the 7-stage `VfxStack` (from phase 11.1) as a
/// vertical scrolling list of collapsible stage headers, each hosting an
/// ordered list of module cards. Authors click stage headers to collapse
/// / expand, drag module cards to reorder within a stage, right-click
/// cards for a context menu (remove / duplicate / move / disable), and
/// click the "+ Add Module" row at the bottom of each stage to open the
/// module picker (phase 11.3).
///
/// # Pipeline
///
/// ```
///   VfxEditorOverlay (host)
///      │  Bind(stack)
///      ▼
///   VfxStackPanelWidget
///      ├─ ComputeLayout()  -- produces a list of typed rects
///      ├─ Render(TextRenderer, bounds) -- draws to the overlay frame
///      └─ HandleMouse(x, y, button, wheel) -- dispatches callbacks
/// ```
///
/// # Testability
///
/// All interaction math is pure-data: `ComputeLayout`, `HitTest`, and
/// `ComputeInsertionIndex` take no GPU context and return value types.
/// The `Render` call is the only host-dependent path; it is a thin pass
/// over the layout list.
///
/// @ref UE5 `SNiagaraStack`
/// @ref engine/vfx/stack/vfx_stack.h (bound data)
/// @ref docs/engine/25-vfx/44-niagara-stack-model.md

#include "engine/vfx/stack/vfx_stack.h"

#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;

// ─────────────────────────────────────────────────────────────────────────────
// Layout primitives
// ─────────────────────────────────────────────────────────────────────────────

/// Axis-aligned rectangle. Matches the editor overlay convention.
struct VfxStackRect {
    float x = 0.0f;
    float y = 0.0f;
    float width  = 0.0f;
    float height = 0.0f;

    /// Returns true when the point (px, py) falls inside the rect.
    /// Edges inclusive on the top/left, exclusive on the bottom/right.
    [[nodiscard]] constexpr bool Contains(float px, float py) const noexcept {
        return (px >= x) && (px < x + width) && (py >= y) && (py < y + height);
    }
};

/// Layout item kind — matches a `HitResult::type`.
///
/// Numeric order defines hit-test priority (higher = more specific, wins
/// when multiple rects contain the same point). Values 0–4 are preserved
/// from phase 11.2.0 for ABI stability; values 5+ are additions from
/// phase 12.2.0 for Niagara-parity layout (emitter header + inline
/// stage-add button + per-card sub-rects).
enum class VfxStackItemType : uint8_t {
    None             = 0,
    StageHeader      = 1,  ///< Stage header row (chevron + icon + name + count + inline add).
    ModuleCard       = 2,  ///< Module card body (drag handle + checkbox + icon + name + summary + overflow).
    CardCheckbox     = 3,  ///< Checkbox sub-region inside a module card.
    AddRow           = 4,  ///< Legacy "+ Add Module" row (phase 12.2.0+ never emits this).
    EmitterHeader    = 5,  ///< 12.2.0 — emitter-level header card above all stages.
    StageAddButton   = 6,  ///< 12.2.0 — inline "+" action at the right edge of a stage header.
    CardDragHandle   = 7,  ///< 12.2.0 — left-edge grip sub-rect inside a module card.
    CardOverflowMenu = 8,  ///< 12.2.0 — right-edge "..." menu sub-rect inside a module card.
};

/// One laid-out widget item. The layout list is flat and rendered top-down.
struct VfxStackLayoutItem {
    VfxStackItemType type = VfxStackItemType::None;

    /// Stage this item belongs to (valid for all kinds).
    VfxStage stage = VfxStage::ParticleUpdate;

    /// Module instanceId for ModuleCard / CardCheckbox items. Empty for
    /// StageHeader and AddRow.
    std::string instanceId;

    /// Slot index inside the stage for ModuleCard items. `SIZE_MAX` for
    /// non-card items.
    size_t moduleIndex = static_cast<size_t>(-1);

    /// Screen-space rectangle (overlay coordinates).
    VfxStackRect rect;
};

/// Result of a point hit-test.
struct VfxStackHitResult {
    VfxStackItemType type = VfxStackItemType::None;
    VfxStage stage = VfxStage::ParticleUpdate;
    std::string instanceId;
    size_t moduleIndex = static_cast<size_t>(-1);

    /// Returns true when the hit is something actionable (not background).
    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxStackItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

/// Niagara-parity stack editor widget.
///
/// # Thread safety
/// Editor-thread only. Never bind a stack being mutated on another thread.
class VfxStackPanelWidget {
public:
    // ── Layout constants (tunable — kept public so tests can inspect) ───

    /// Row height for stage headers, module cards, and "+ Add Module" rows.
    float headerHeight = 26.0f;
    float cardHeight   = 28.0f;
    float addRowHeight = 22.0f;

    /// Horizontal indentation for module cards (vs. stage headers).
    float cardIndent = 12.0f;

    /// Vertical gap between consecutive cards within a stage.
    /// V3 minimalist — 3 px gives clear card separation without
    /// leaving a noticeable gutter.
    float rowGap = 3.0f;

    /// Vertical gap between stages (applied after the "+ Add Module" row).
    float stageGap = 10.0f;

    /// Width of the drag handle column inside a module card.
    float dragHandleWidth = 14.0f;

    /// Width of the enabled-checkbox column inside a module card.
    float checkboxWidth = 22.0f;

    // ── Niagara-parity layout knobs (phase 12.2.0) ──────────────────────
    /// Height of the emitter header card above the first stage.
    float emitterHeaderHeight = 32.0f;
    /// Width of the three-dot overflow menu hit region on the right of a card.
    float overflowMenuWidth = 20.0f;
    /// Width of the inline "+" add-module button on the right of a stage header.
    float stageAddButtonWidth = 22.0f;
    /// Width of the module-category icon column inside a card (after checkbox).
    float moduleIconWidth = 18.0f;
    /// Width of the accent stripe drawn along a selected card's left edge.
    float selectedStripeWidth = 3.0f;

    // ── Binding ─────────────────────────────────────────────────────────

    /// Binds the widget to a stack. Passing `nullptr` unbinds. The stack
    /// must outlive the widget or be rebound before destruction.
    void Bind(VfxStack* stack) noexcept { m_stack = stack; }

    [[nodiscard]] VfxStack* Stack() noexcept { return m_stack; }
    [[nodiscard]] const VfxStack* Stack() const noexcept { return m_stack; }

    // ── Emitter header (phase 12.2.0) ───────────────────────────────────

    /// Sets the label rendered inside the emitter header card. When
    /// empty (the default), `ComputeLayout` emits no `EmitterHeader`
    /// item and the widget falls back to a stages-only layout.
    void SetEmitterName(std::string name) { m_emitterName = std::move(name); }
    [[nodiscard]] const std::string& EmitterName() const noexcept { return m_emitterName; }

    /// Binds an `emitting`-state toggle pointer. When non-null, the
    /// emitter header renders a checkbox at the left edge reflecting
    /// `*emitterEnabledPtr`. Clicking the checkbox flips the value in
    /// place (caller-applied: the widget writes directly, no callback).
    void SetEmitterEnabledPtr(bool* ptr) noexcept { m_emitterEnabledPtr = ptr; }
    [[nodiscard]] bool* EmitterEnabledPtr() noexcept { return m_emitterEnabledPtr; }

    // ── Expanded-state (per stage) ──────────────────────────────────────

    /// Returns whether `stage`'s modules are visible (header expanded).
    [[nodiscard]] bool IsStageExpanded(VfxStage stage) const noexcept {
        return m_stageExpanded[static_cast<uint8_t>(stage)];
    }

    /// Sets expanded state for `stage`. Call on header click.
    void SetStageExpanded(VfxStage stage, bool expanded) noexcept {
        m_stageExpanded[static_cast<uint8_t>(stage)] = expanded;
    }

    /// Toggles expanded state for `stage` and returns the new state.
    bool ToggleStage(VfxStage stage) noexcept {
        bool& v = m_stageExpanded[static_cast<uint8_t>(stage)];
        v = !v;
        return v;
    }

    // ── Selection ───────────────────────────────────────────────────────

    [[nodiscard]] const std::string& SelectedInstanceId() const noexcept {
        return m_selectedInstanceId;
    }

    /// Selects a module by instanceId. Empty string clears selection.
    /// Fires `onModuleSelected` if the selection changed.
    void SelectModule(const std::string& instanceId);

    /// Clears the selection without firing the callback.
    void ClearSelection() noexcept { m_selectedInstanceId.clear(); }

    // ── Hover ───────────────────────────────────────────────────────────

    [[nodiscard]] const std::string& HoveredInstanceId() const noexcept {
        return m_hoveredInstanceId;
    }

    /// Sets the hovered module (host updates this every frame from mouse
    /// position). Empty string = nothing hovered.
    void SetHoveredInstanceId(const std::string& id) noexcept {
        m_hoveredInstanceId = id;
    }

    // ── Drag state (reorder within a stage) ─────────────────────────────

    /// Begins a drag operation on the module identified by `instanceId`.
    /// No-op if the instance isn't bound / found.
    void BeginDrag(const std::string& instanceId);

    /// Ends any active drag, applying the target insertion index within
    /// the stage. Returns true when a reorder actually happened.
    ///
    /// If the drag was to a different stage, it is treated as a cancel
    /// (cross-stage reorder is a separate 11.9 command).
    bool EndDrag(VfxStage targetStage, size_t targetIndex);

    /// Cancels any active drag without applying a reorder.
    void CancelDrag() noexcept {
        m_dragActive = false;
        m_draggedInstanceId.clear();
    }

    [[nodiscard]] bool IsDragging() const noexcept { return m_dragActive; }

    [[nodiscard]] const std::string& DraggedInstanceId() const noexcept {
        return m_draggedInstanceId;
    }

    [[nodiscard]] VfxStage DraggedFromStage() const noexcept {
        return m_draggedFromStage;
    }

    // ── Layout + hit-test (pure math — no GPU context) ─────────────────

    /// Builds the layout rectangles for the bound stack inside the given
    /// panel rect. The layout list is flat (headers + cards + add-rows in
    /// draw order). Scroll offset is subtracted from `y` of each rect.
    [[nodiscard]] std::vector<VfxStackLayoutItem> ComputeLayout(
        const VfxStackRect& panelBounds) const;

    /// Hit-tests a point against the layout. Returns the first item whose
    /// rect contains (x, y); checkbox sub-regions win over their parent
    /// module card. `None` when nothing under the cursor.
    [[nodiscard]] VfxStackHitResult HitTest(
        const std::vector<VfxStackLayoutItem>& layout,
        float x, float y) const;

    /// Computes the insertion index inside `stage` for a drag operation
    /// whose current mouse-Y is `mouseY`. Returns a slot index in
    /// `[0, stage.modules.size()]`. When the stage is empty / collapsed,
    /// returns 0. Used by `EndDrag` and by the host to render the
    /// insertion gap.
    [[nodiscard]] size_t ComputeInsertionIndex(
        const std::vector<VfxStackLayoutItem>& layout,
        VfxStage stage, float mouseY) const;

    // ── Rendering ───────────────────────────────────────────────────────

    /// Renders the widget to `text` inside `panelBounds`. Thin pass over
    /// `ComputeLayout` — no interaction logic lives here.
    void Render(TextRenderer& text, const BitmapFont& font,
                const VfxStackRect& panelBounds) const;

    // ── Scrolling ───────────────────────────────────────────────────────

    [[nodiscard]] float ScrollOffset() const noexcept { return m_scrollOffset; }

    /// Sets the scroll offset. Positive = content scrolled up.
    void SetScrollOffset(float offset) noexcept { m_scrollOffset = offset; }

    /// Computes the total layout height (ignoring panel bounds). Used
    /// by the host to clamp `m_scrollOffset` into the valid range.
    [[nodiscard]] float ComputeContentHeight() const;

    // ── Callbacks (wired by the host / overlay) ────────────────────────

    /// Fires when the user clicks the "+ Add Module" row. The host opens
    /// the module picker (phase 11.3) for the target stage.
    std::function<void(VfxStage stage)> onAddModuleRequested;

    /// Fires when the selection changes. The host routes the selected
    /// instance to the parameter inspector (phase 11.4).
    std::function<void(const std::string& instanceId)> onModuleSelected;

    /// Fires when the user toggles the "enabled" checkbox on a card.
    /// The host applies the change through the undo/redo stack (phase
    /// 11.9) so it can be reverted.
    std::function<void(const std::string& instanceId, bool enabled)>
        onModuleToggled;

    /// Fires when a drag reorder commits. The host applies the move
    /// through the undo/redo stack.
    std::function<void(const std::string& instanceId,
                       VfxStage stage,
                       size_t newIndex)> onModuleMoved;

    /// Fires on the "Remove" context-menu entry.
    std::function<void(const std::string& instanceId)> onModuleRemoveRequested;

    /// Fires on the "Duplicate" context-menu entry.
    std::function<void(const std::string& instanceId)> onModuleDuplicateRequested;

    /// Phase 11.11. Fires on the "Revert to Parent" context-menu entry
    /// (only emitted for modules whose `overrideFlag` is `OverrideAdd`,
    /// `OverrideReplace`, or `OverrideRemove`). Host wraps the revert
    /// in a `RevertOverrideCommand` so the action participates in undo
    /// / redo history.
    std::function<void(const std::string& instanceId)> onRevertOverrideRequested;

    /// Phase 12.2.0. Fires when the author clicks the three-dot overflow
    /// menu on a module card. The host (12.8 context menu) opens a
    /// popup anchored at `(x, y)` with Rename / Duplicate / Delete /
    /// Revert / Isolate actions.
    std::function<void(const std::string& instanceId,
                       float x, float y)> onModuleContextMenuRequested;

private:
    VfxStack* m_stack = nullptr;

    /// Emitter header label (empty = header not emitted).
    std::string m_emitterName;
    /// Pointer to the emitter's `emitting` flag (null = no checkbox).
    bool* m_emitterEnabledPtr = nullptr;

    std::array<bool, kVfxStageCount> m_stageExpanded{};

    std::string m_selectedInstanceId;
    std::string m_hoveredInstanceId;

    float m_scrollOffset = 0.0f;

    // Drag state
    bool        m_dragActive = false;
    std::string m_draggedInstanceId;
    VfxStage    m_draggedFromStage = VfxStage::ParticleUpdate;
    size_t      m_draggedFromIndex = 0;

    /// Locates the bound stack entry for `stage`, or returns nullptr when
    /// the stack is unbound.
    [[nodiscard]] const VfxStackEntry* StageEntry(VfxStage stage) const noexcept;
    [[nodiscard]] VfxStackEntry* StageEntry(VfxStage stage) noexcept;

public:
    /// Default-constructs with every stage expanded.
    VfxStackPanelWidget() noexcept {
        m_stageExpanded.fill(true);
    }
};

} // namespace uz
