#pragma once

/// VFX Module Picker Dialog — Niagara-style module browser.
///
/// Phase 11.3.0. Modal popup anchored to the clicked "+ Add Module" row.
/// Shows a category tab bar, a filter text-input, and a tile list of the
/// `ModuleRegistry` entries whose `VfxModuleCatalogEntry::validStageMask`
/// contains the target `VfxStage`. Confirming (double-click / Enter)
/// dispatches `onModuleChosen(moduleName, targetStage)`; escape or click-
/// outside closes silently.
///
/// # Lifecycle
///
/// ```
///   Closed   ──Open(stage, anchor)──►  Open  ──Close()─►  Closed
///                                        │
///                   Escape / click-out ──┘   Confirm → onModuleChosen()
/// ```
///
/// # Testability
///
/// All state mutations are pure: `Open`, `Close`, `SetCategory`,
/// `SetFilter`, `NavigateSelection`, `SelectIndex`, `ConfirmSelection`,
/// and `VisibleModules()` take no GPU context. Layout + hit-test follow
/// the same pattern as `VfxStackPanelWidget` (pure `ComputeLayout` /
/// `HitTest` over value-typed rects).
///
/// @ref UE5 `SNiagaraModuleAsset`
/// @ref engine/vfx/modules/vfx_module_metadata.h
/// @ref engine/vfx/editor/vfx_stack_panel_widget.h (shared layout primitives)

#include "engine/vfx/editor/vfx_stack_panel_widget.h" // VfxStackRect
#include "engine/vfx/stack/vfx_stage.h"

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

/// Kind of hit-testable element in the picker dialog.
///
/// Numeric order defines hit-test priority (higher wins when multiple
/// rects contain the cursor). Values 0–4 are preserved from phase
/// 11.3.0 for ABI stability; 5+ added by phase 12.3.0 for the redesign.
enum class VfxPickerItemType : uint8_t {
    None          = 0,
    CategoryTab   = 1,
    FilterBox     = 2,   ///< Legacy — `SearchInput` (5) is the 12.3.0 replacement.
    ModuleTile    = 3,
    CloseButton   = 4,
    SearchInput   = 5,   ///< 12.3.0 — top-of-dialog search field with icon.
    PreviewArea   = 6,   ///< 12.3.0 — bottom preview pane (description + target hint).
    ButtonCreate  = 7,   ///< 12.3.0 — primary footer button (disabled without selection).
    ButtonCancel  = 8,   ///< 12.3.0 — secondary footer button (same effect as CloseButton).
};

/// One laid-out picker UI element.
struct VfxPickerLayoutItem {
    VfxPickerItemType type = VfxPickerItemType::None;
    VfxStackRect      rect;
    size_t            index = static_cast<size_t>(-1); ///< category or tile index
    std::string       label;                            ///< tab name or module name
};

/// Hit-test result for a mouse coordinate against the picker layout.
struct VfxPickerHitResult {
    VfxPickerItemType type = VfxPickerItemType::None;
    size_t            index = static_cast<size_t>(-1);
    std::string       label;

    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxPickerItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Dialog
// ─────────────────────────────────────────────────────────────────────────────

/// Module picker dialog state machine + layout math.
class VfxModulePickerDialog {
public:
    // ── Layout constants (public for tests + host to inspect) ───────────
    float dialogWidth  = 820.0f;   ///< Widened so 8 category tabs (~100 px each) fit "Simulation N" without overflow.
    float dialogHeight = 560.0f;   ///< 12.3.0: tall enough for ~4 tile rows + preview + footer.
    float headerHeight = 28.0f;
    float searchHeight = 28.0f;    ///< 12.3.0 search-bar row.
    float tabRowHeight = 26.0f;
    float filterHeight = 22.0f;    ///< Legacy; unused in 12.3.0 layout.
    float tileHeight   = 96.0f;    ///< 12.3.0 full tile (icon + name + 2-line desc).
    float tileWidth    = 130.0f;   ///< 12.3.0 nominal per-tile width (4 col fits 560-w).
    float tilePaddingX = 8.0f;
    float tilePaddingY = 6.0f;
    float previewHeight = 96.0f;   ///< 12.3.0 bottom preview pane when selection.
    float footerHeight  = 40.0f;   ///< 12.3.0 footer strip (Create + Cancel).
    float footerBtnWidth = 110.0f; ///< 12.3.0 per footer button width.

    /// Threshold in pixels: `dialogWidth < kFourColumnMinWidth` falls
    /// back to 3 columns. Otherwise 4 columns.
    static constexpr float kFourColumnMinWidth = 600.0f;

    // ── Lifecycle ───────────────────────────────────────────────────────

    /// Opens the dialog anchored near `(anchorX, anchorY)`. If the anchor
    /// would push the dialog off-screen (screenW / screenH > 0), it's
    /// nudged back into bounds. Resets filter + selection.
    void Open(VfxStage targetStage, float anchorX, float anchorY,
              float screenW = 0.0f, float screenH = 0.0f);

    /// Closes without firing `onModuleChosen`. Safe to call when already
    /// closed.
    void Close() noexcept;

    [[nodiscard]] bool     IsOpen()      const noexcept { return m_isOpen; }
    [[nodiscard]] VfxStage TargetStage() const noexcept { return m_targetStage; }

    // ── Category tabs ───────────────────────────────────────────────────

    [[nodiscard]] const std::string& ActiveCategory() const noexcept {
        return m_activeCategory;
    }

    /// Sets the active category tab. Rebuilds `VisibleModules` on next
    /// query. No-op when the category string doesn't exist in
    /// `Categories()`.
    void SetCategory(const std::string& category);

    // ── Filter text ─────────────────────────────────────────────────────

    [[nodiscard]] const std::string& Filter() const noexcept {
        return m_filter;
    }

    /// Sets the filter text (case-insensitive substring match on both
    /// module name and description).
    void SetFilter(const std::string& filter);

    /// Mutable buffer reference for the overlay's `StringInput` binding.
    [[nodiscard]] std::string& FilterBuffer() noexcept { return m_filter; }

    // ── Selection ───────────────────────────────────────────────────────

    /// Returns the currently selected tile index into `VisibleModules()`.
    /// `SIZE_MAX` when nothing is visible / selected.
    [[nodiscard]] size_t SelectedIndex() const noexcept { return m_selectedIndex; }

    /// Clamps and sets the selection. Out-of-range targets wrap into
    /// `[0, visibleCount)`.
    void SelectIndex(size_t index);

    /// Moves the selection by `delta` (wraps at list ends). `+1` = next,
    /// `-1` = previous. No-op when the list is empty.
    void NavigateSelection(int delta);

    /// Jumps to the first visible tile.
    void SelectFirst();

    /// Jumps to the last visible tile.
    void SelectLast();

    // ── Data + commit ───────────────────────────────────────────────────

    /// Rebuilds and returns the filtered tile list: modules whose
    /// `validStageMask` contains `TargetStage`, whose category matches
    /// `ActiveCategory` (or `"All"`), and whose name or description
    /// substring-matches `Filter` (case-insensitive).
    [[nodiscard]] std::vector<std::string> VisibleModules() const;

    /// Commits the current selection. If there's a valid tile selected,
    /// fires `onModuleChosen(moduleName, targetStage)` and closes.
    /// Returns true when a module was chosen.
    bool ConfirmSelection();

    // ── Layout + hit-test ───────────────────────────────────────────────

    [[nodiscard]] VfxStackRect Bounds() const noexcept {
        return { m_anchorX, m_anchorY, dialogWidth, dialogHeight };
    }

    /// Builds the layout rectangles (header / category tabs / filter box /
    /// tile rows / close button). Returns an empty vector when closed.
    [[nodiscard]] std::vector<VfxPickerLayoutItem> ComputeLayout() const;

    /// Hit-tests a point against the layout.
    [[nodiscard]] VfxPickerHitResult HitTest(
        const std::vector<VfxPickerLayoutItem>& layout,
        float x, float y) const;

    // ── Callbacks ───────────────────────────────────────────────────────

    /// Fires on ConfirmSelection with the chosen module name and the
    /// dialog's target stage. The host inserts a new `VfxModuleInstance`
    /// via the undo/redo stack (phase 11.9).
    std::function<void(const std::string& moduleName, VfxStage stage)>
        onModuleChosen;

    // ── Rendering ───────────────────────────────────────────────────────

    /// Renders the dialog into `text`. No-op when closed.
    void Render(TextRenderer& text, const BitmapFont& font) const;

private:
    bool      m_isOpen       = false;
    VfxStage  m_targetStage  = VfxStage::ParticleUpdate;
    float     m_anchorX      = 0.0f;
    float     m_anchorY      = 0.0f;

    std::string m_activeCategory = "All";
    std::string m_filter;
    size_t      m_selectedIndex = static_cast<size_t>(-1);

    /// Lowercase in-place helper.
    [[nodiscard]] static std::string LowerCopy(const std::string& s);

    /// Case-insensitive substring test.
    [[nodiscard]] static bool MatchesFilter(const std::string& lowerFilter,
                                             const std::string& name,
                                             const std::string& desc);
};

} // namespace uz
