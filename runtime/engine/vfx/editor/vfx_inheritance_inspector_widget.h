#pragma once

/// VFX Inheritance Inspector Widget — parent picker + override indicators.
///
/// Phase 9.2.0. Surfaces the phase 8.3.0 inheritance resolver in the editor
/// UI. Authors see which modules come from the parent vs their own
/// overrides, and can revert overrides one click at a time.
///
/// # Model
///
/// The widget is data-agnostic: the host supplies an input model via
/// `SetInputModel(...)` containing the child JSON (mutable) and a
/// parent-JSON loader lambda. The widget re-resolves on parent change
/// and rebuilds the display list.
///
/// Each row in the display list is tagged with one of four kinds:
/// - `Inherited`      : from parent, not overridden in child.
/// - `OverrideAdd`    : child-only (`action: "add"`, a `replace` with no
///                       matching parent module, or any child entry
///                       when no parent is bound — same as the resolver).
/// - `OverrideReplace`: child replaces parent's matching module.
/// - `OverrideRemove` : child removes parent's module (row shown
///                       crossed out for clarity).
///
/// # Layout
///
/// ```
/// ┌───────────────────────────────────────────────────────────┐
/// │ Parent: [ effects/base_smoke.uzvfx           ] [Browse...] │
/// │                                                             │
/// │ Module stack:                                               │
/// │   ∙ Spawn               (inherited)                          │
/// │   ∙ Gravity             (replace)  [Revert]                  │
/// │   ∙ Drag                (removed, strike-through)            │
/// │   ∙ Burst               (add)      [Revert]                  │
/// └───────────────────────────────────────────────────────────┘
/// ```
///
/// @ref engine/vfx/asset/vfx_emitter_inheritance.h (resolver)
/// @ref engine/vfx/editor/vfx_asset_browser_widget.h (parent picker popup)

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Row classification
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxInheritanceRowKind : uint8_t {
    Inherited       = 0, ///< From parent, not overridden.
    OverrideAdd     = 1, ///< Child-only, action="add".
    OverrideReplace = 2, ///< Child replaces parent's matching module.
    OverrideRemove  = 3, ///< Child removes parent's module.
};

/// Stable string for logs / display. Never null.
[[nodiscard]] const char* VfxInheritanceRowKindName(VfxInheritanceRowKind kind) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Display row
// ─────────────────────────────────────────────────────────────────────────────

/// One row in the resolved module stack display.
struct VfxInheritanceRow {
    /// Module type name (e.g. "Gravity", "Spawn", "Burst").
    std::string moduleType;

    /// Classification.
    VfxInheritanceRowKind kind = VfxInheritanceRowKind::Inherited;

    /// Index into the child's `modules[]` array when this row is an
    /// override, else UINT32_MAX. Used by `RevertOverride(rowIndex)` to
    /// locate the entry to remove from the child JSON.
    uint32_t childModulesIndex = UINT32_MAX;
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxInheritanceInspectorWidget {
public:
    // ── Configuration ───────────────────────────────────────────────────

    /// Parent-JSON loader (same signature as `VfxInheritanceLoader`).
    using ParentLoader = std::function<std::string(const std::string&)>;

    /// Input model bound to the widget.
    struct InputModel {
        /// Child asset JSON text (mutable — the widget edits `parent`
        /// + `modules[]` in place).
        std::string* childJson = nullptr;

        /// Loader for parent JSON by path.
        ParentLoader parentLoader;
    };

    void SetInputModel(const InputModel& model);

    // ── State queries ───────────────────────────────────────────────────

    /// Returns the current parent path (empty when no parent bound).
    [[nodiscard]] const std::string& ParentPath() const noexcept { return parentPath_; }

    /// Mutable buffer for parent-path editing via `StringInput`.
    [[nodiscard]] std::string& ParentPathBuffer() noexcept { return parentPath_; }

    /// Returns the last resolution error (empty when resolution succeeded).
    [[nodiscard]] const std::string& LastError() const noexcept { return lastError_; }

    /// Returns the resolved display rows.
    [[nodiscard]] const std::vector<VfxInheritanceRow>& Rows() const noexcept { return rows_; }

    /// Returns the number of display rows.
    [[nodiscard]] uint32_t RowCount() const noexcept {
        return static_cast<uint32_t>(rows_.size());
    }

    // ── Actions ─────────────────────────────────────────────────────────

    /// Re-runs the resolver against the current child JSON + parent path.
    /// Populates `Rows()` and `LastError()`. Call after editing the parent
    /// path or any module list entry.
    void Resolve();

    /// Clears the parent link (writes `parent: ""` into the child JSON
    /// and re-resolves).
    void ClearParent();

    /// Removes the override entry at `rows_[rowIndex].childModulesIndex`
    /// from the child's `modules[]` array, reverting to the parent value.
    /// No-op when the row is not an override or the index is invalid.
    void RevertOverride(uint32_t rowIndex);

    // ── Callbacks ───────────────────────────────────────────────────────

    /// Fires when the resolved module list changes (after Resolve).
    std::function<void()> onResolved;

private:
    /// Parses the child JSON, reads `parent` + `modules[]`, loads parent
    /// JSON, walks both to classify rows. On error, populates `lastError_`
    /// and leaves `rows_` empty.
    void ResolveInternal();

    InputModel model_{};
    std::string parentPath_;
    std::string lastError_;
    std::vector<VfxInheritanceRow> rows_;
};

} // namespace uz
