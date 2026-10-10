#pragma once

/// VFX Parameter Inspector Widget — two-pane Niagara-parity parameter editor.
///
/// Phase 11.4.0. Binds to a `VfxAttributeStore` (existing runtime container,
/// three-scope: System / Emitter / Particles) plus the authored `VfxStack`
/// so it can count how many modules reference each parameter. Provides
/// add / rename / retype / edit / delete flows and emits a flat layout
/// list (left pane = list, right pane = inspector form) hit-testable by
/// the overlay host.
///
/// # Data model
///
/// The runtime `VfxAttributeStore` has no persistent type tag — every
/// value is a `Vec4`. The inspector tracks the authored type (Float /
/// Vec3 / Vec4) in a companion map keyed by `(scope, name)`. Type-aware
/// conversion preserves component 0 and zero-fills new components.
///
/// # Usage-count tracking
///
/// Parameter references inside authored `VfxModuleInstance::configJson`
/// use the `"@<paramName>"` token (e.g. `"teamColor"` referenced as
/// `"@teamColor"`). The inspector scans every module's config JSON for
/// that substring when the UI requests a count.
///
/// # Testability
///
/// All mutations (`AddParameter`, `RemoveParameter`, `RenameParameter`,
/// `ChangeType`, `SetDefaultValue`, `CountReferences`, `FindFirstUsage`)
/// are pure and covered by the unit-test suite. Layout / hit-test math
/// follows the same pattern as the stack panel and picker widgets.
///
/// @ref UE5 `SNiagaraParameterPanel`
/// @ref engine/vfx/parameters/vfx_attribute_store.h
/// @ref engine/vfx/stack/vfx_stack.h

#include "engine/vfx/editor/vfx_stack_panel_widget.h"       // VfxStackRect
#include "engine/vfx/parameters/vfx_attribute_store.h"
#include "engine/vfx/stack/vfx_module_instance.h"            // VfxOverrideFlag
#include "engine/vfx/stack/vfx_stack.h"
#include "vector3.h"
#include "vector4.h"

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class TextRenderer;
class BitmapFont;

// ─────────────────────────────────────────────────────────────────────────────
// Parameter type (editor-only — runtime stores every value as Vec4)
// ─────────────────────────────────────────────────────────────────────────────

/// Authored type of a user parameter. Runtime always stores a Vec4, but the
/// inspector's editor widgets (number fields) honour this tag.
enum class VfxParamType : uint8_t {
    Float = 0,
    Vec3  = 1,
    Vec4  = 2,
};

/// Stable name for the type (JSON / UI labels).
[[nodiscard]] constexpr const char* VfxParamTypeName(VfxParamType t) noexcept {
    switch (t) {
        case VfxParamType::Float: return "Float";
        case VfxParamType::Vec3:  return "Vec3";
        case VfxParamType::Vec4:  return "Vec4";
    }
    return "Float";
}

/// Number of authored components for this type.
[[nodiscard]] constexpr uint32_t VfxParamTypeComponentCount(VfxParamType t) noexcept {
    switch (t) {
        case VfxParamType::Float: return 1;
        case VfxParamType::Vec3:  return 3;
        case VfxParamType::Vec4:  return 4;
    }
    return 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameter row (a single entry in the left-pane list)
// ─────────────────────────────────────────────────────────────────────────────

/// Denormalised snapshot of one parameter — used for testing and by the
/// left-pane rendering. Copy cheap (name + 4 floats + two small enums).
struct VfxParameterRow {
    VfxAttributeScope scope = VfxAttributeScope::Emitter;
    std::string       name;
    VfxParamType      type  = VfxParamType::Float;
    Vec4              value{};
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout items (pure-data, for hit-test)
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxParamItemType : uint8_t {
    None         = 0,
    ScopeHeader  = 1,  ///< "System" / "Emitter" collapsible group header.
    AddButton    = 2,  ///< "+ Add Parameter" row inside a scope.
    ParameterRow = 3,  ///< One parameter list entry (left pane).
    // Right-pane inspector controls (phase 11.4.0)
    NameField      = 10,
    ScopeDropdown  = 11,
    TypeDropdown   = 12,
    ValueField     = 13,  ///< N per-component fields (sub-index in `index`).
    DeleteButton   = 14,
    JumpToUsages   = 15,
    // Phase 12.4.0 additions — Niagara-parity section grouping + typed editors
    Section        = 20,  ///< Section header (collapsible sub-group inside a scope).
    EditorFloat    = 30,  ///< Single-float slider + number input.
    EditorVec3     = 31,  ///< Three-field X/Y/Z editor.
    EditorColor    = 32,  ///< RGBA color swatch + channel fields.
    EditorBool     = 33,  ///< Single checkbox.
    EditorEnum     = 34,  ///< Dropdown with current label.
    EditorCurve    = 35,  ///< Compact curve preview + "Edit…" button.
    EditorRamp     = 36,  ///< Horizontal gradient preview.
    EditorString   = 37,  ///< Text field.
    ResetToDefault = 40,  ///< Reset arrow button in the actions column.
    BindToCurve    = 41,  ///< "Bind to curve" chain-link button.
};

struct VfxParamLayoutItem {
    VfxParamItemType type = VfxParamItemType::None;

    /// For scope-scoped items (header, add, row): the owning scope.
    VfxAttributeScope scope = VfxAttributeScope::Emitter;

    /// For `ParameterRow`: the parameter name. For `ValueField`: the
    /// parameter name being edited. Otherwise empty.
    std::string name;

    /// For `ValueField`: component index [0..3]. Otherwise `SIZE_MAX`.
    size_t index = static_cast<size_t>(-1);

    VfxStackRect rect;
};

struct VfxParamHitResult {
    VfxParamItemType  type = VfxParamItemType::None;
    VfxAttributeScope scope = VfxAttributeScope::Emitter;
    std::string       name;
    size_t            index = static_cast<size_t>(-1);

    [[nodiscard]] bool IsHit() const noexcept {
        return type != VfxParamItemType::None;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Widget
// ─────────────────────────────────────────────────────────────────────────────

class VfxParameterInspectorWidget {
public:
    // ── Layout constants (tunable; public for tests + host inspection) ──
    float listWidth        = 200.0f;
    float headerHeight     = 22.0f;
    float rowHeight        = 20.0f;
    float addButtonHeight  = 20.0f;
    float scopeGap         = 6.0f;
    float inspectorPadX    = 8.0f;
    float inspectorFieldH  = 22.0f;
    float inspectorFieldGap = 4.0f;

    // ── Binding ─────────────────────────────────────────────────────────

    /// Binds the widget to a store + stack. `stack` is used for
    /// `CountReferences` / `FindFirstUsage` scans. Passing `nullptr`
    /// unbinds — in which case `ListParameters` returns empty.
    void Bind(VfxAttributeStore* store, VfxStack* stack) noexcept {
        m_store = store;
        m_stack = stack;
    }

    [[nodiscard]] VfxAttributeStore* Store() noexcept { return m_store; }
    [[nodiscard]] VfxStack*          Stack() noexcept { return m_stack; }

    // ── Data model mutations (pure, testable) ──────────────────────────

    /// Returns a sorted list of parameters in `scope`. Names come from
    /// the bound store; types come from the local registry (default
    /// `Float` when the registry has no entry); values come from the
    /// store as a `Vec4` (inspector never narrows the stored data).
    [[nodiscard]] std::vector<VfxParameterRow> ListParameters(
        VfxAttributeScope scope) const;

    /// Returns the first free name starting with `baseName` that
    /// doesn't collide with any existing parameter in `scope`.
    /// Appends a numeric suffix ("NewParam", "NewParam1", …) when the
    /// baseline is taken.
    [[nodiscard]] std::string MakeUniqueName(
        VfxAttributeScope scope, const std::string& baseName) const;

    /// Adds a parameter to the store + registry. Returns false when
    /// `name` is empty, already exists in `scope`, or the store is
    /// unbound. `value` is read according to `type` (Float → .x only).
    bool AddParameter(VfxAttributeScope scope, const std::string& name,
                      VfxParamType type, const Vec4& value);

    /// Removes a parameter. Returns false when the parameter has
    /// outstanding usages (`CountReferences > 0`), is missing, or the
    /// store is unbound. The caller is expected to surface a tooltip.
    bool RemoveParameter(VfxAttributeScope scope, const std::string& name);

    /// Renames a parameter in-place. Rewrites every matching
    /// `"@oldName"` token in every bound `VfxModuleInstance::configJson`
    /// so that downstream references stay attached. Returns false when
    /// the inputs are invalid or `newName` already exists.
    bool RenameParameter(VfxAttributeScope scope,
                         const std::string& oldName,
                         const std::string& newName);

    /// Changes a parameter's authored type. Conversion preserves
    /// component 0 and zero-fills new components (Float → Vec3 keeps .x,
    /// sets .y/.z = 0; Vec4 → Float truncates to .x).
    bool ChangeType(VfxAttributeScope scope, const std::string& name,
                    VfxParamType newType);

    /// Moves a parameter from one scope to another. Keeps the current
    /// value + type. Returns false when the destination already has a
    /// parameter with that name or any argument is invalid.
    bool ChangeScope(VfxAttributeScope fromScope,
                     const std::string& name,
                     VfxAttributeScope toScope);

    /// Writes the parameter's default value. `type` must match the
    /// authored type (no implicit widening). Returns false otherwise.
    bool SetDefaultValue(VfxAttributeScope scope, const std::string& name,
                         const Vec4& value);

    /// Returns the authored type of a parameter. Missing → `Float`.
    [[nodiscard]] VfxParamType GetType(VfxAttributeScope scope,
                                         const std::string& name) const noexcept;

    // ── Phase 12.4.0 — Section grouping (companion to authored type) ───

    /// Annotates a parameter with a section name used to group rows in
    /// the layout. Empty string clears the annotation (defaults to
    /// "General"). No-op when the parameter does not exist.
    void SetParameterSection(VfxAttributeScope scope,
                              const std::string& name,
                              const std::string& sectionName);

    /// Returns the section name for a parameter, or "General" when
    /// nothing has been explicitly set.
    [[nodiscard]] std::string GetParameterSection(
        VfxAttributeScope scope, const std::string& name) const;

    /// Returns distinct section names in the order they first appear
    /// when scanning the bound store across all scopes. Always starts
    /// with "General" when at least one parameter uses the default.
    [[nodiscard]] std::vector<std::string> DistinctSections() const;

    [[nodiscard]] bool IsSectionExpanded(const std::string& section) const;
    void SetSectionExpanded(const std::string& section, bool expanded);
    bool ToggleSectionExpanded(const std::string& section);

    // ── Phase 12.4.0 — Per-parameter numeric range registration ────────

    /// Registers a numeric `[min, max]` validation range. Out-of-range
    /// values render with a `stateError` border. No-op when
    /// `scope / name` doesn't resolve to a parameter.
    void AddParameterRange(VfxAttributeScope scope, const std::string& name,
                            float minValue, float maxValue);

    /// Returns `true` when `value` (component 0) is inside the
    /// registered `[min, max]` range. Missing registrations return
    /// `true` (no validation = always valid).
    [[nodiscard]] bool IsValueInRange(VfxAttributeScope scope,
                                       const std::string& name,
                                       float value) const noexcept;

    // ── Inheritance override flags (phase 11.11) ───────────────────────

    /// Sets the inheritance override flag for a parameter. The widget
    /// stores the flag in a local companion map so list rendering can
    /// show the Niagara-parity ↳ / ● / ⇄ / ✕ icons. Missing entries
    /// default to `VfxOverrideFlag::None` on read.
    void SetOverrideFlag(VfxAttributeScope scope, const std::string& name,
                          VfxOverrideFlag flag);

    [[nodiscard]] VfxOverrideFlag GetOverrideFlag(
        VfxAttributeScope scope, const std::string& name) const noexcept;

    /// Clears every override flag entry. Used when the host rebuilds the
    /// widget for a non-inherited emitter.
    void ClearOverrideFlags() noexcept { m_overrideFlags.clear(); }

    // ── Usage counting ──────────────────────────────────────────────────

    /// Returns the number of `VfxModuleInstance::configJson` strings in
    /// the bound stack that reference `@<name>`. Naive string scan; the
    /// configJson is JSON text, so `"@name"` as a literal substring is
    /// a conservative upper bound and matches Niagara's counting.
    [[nodiscard]] size_t CountReferences(const std::string& name) const;

    /// Returns the first `(stage, instanceId)` pair that references the
    /// parameter, or `{ParticleUpdate, ""}` when none exists. Used by
    /// the "Jump to usages" button.
    struct UsageHit {
        VfxStage   stage = VfxStage::ParticleUpdate;
        std::string instanceId;
        [[nodiscard]] bool IsValid() const noexcept { return !instanceId.empty(); }
    };
    [[nodiscard]] UsageHit FindFirstUsage(const std::string& name) const;

    // ── Selection ──────────────────────────────────────────────────────

    [[nodiscard]] VfxAttributeScope SelectedScope() const noexcept {
        return m_selectedScope;
    }
    [[nodiscard]] const std::string& SelectedName() const noexcept {
        return m_selectedName;
    }

    void SelectParameter(VfxAttributeScope scope, const std::string& name);
    void ClearSelection() noexcept {
        m_selectedName.clear();
    }

    [[nodiscard]] bool IsScopeExpanded(VfxAttributeScope scope) const noexcept {
        return m_scopeExpanded[static_cast<size_t>(scope)];
    }
    void SetScopeExpanded(VfxAttributeScope scope, bool expanded) noexcept {
        m_scopeExpanded[static_cast<size_t>(scope)] = expanded;
    }
    bool ToggleScope(VfxAttributeScope scope) noexcept {
        auto& v = m_scopeExpanded[static_cast<size_t>(scope)];
        v = !v;
        return v;
    }

    // ── Layout + hit-test ───────────────────────────────────────────────

    /// Builds the layout rects for a panel of `panelBounds`. Left pane
    /// fills `listWidth`; the remainder is the right-pane inspector
    /// form for the currently selected parameter.
    [[nodiscard]] std::vector<VfxParamLayoutItem> ComputeLayout(
        const VfxStackRect& panelBounds) const;

    /// Returns the most-specific item under (x, y). Precedence matches
    /// enum ordering (larger `VfxParamItemType` value wins).
    [[nodiscard]] VfxParamHitResult HitTest(
        const std::vector<VfxParamLayoutItem>& layout,
        float x, float y) const;

    // ── Rendering ──────────────────────────────────────────────────────

    /// Thin pass over `ComputeLayout`. No-op when the widget isn't bound.
    void Render(TextRenderer& text, const BitmapFont& font,
                const VfxStackRect& panelBounds) const;

    // ── Callbacks ──────────────────────────────────────────────────────

    /// Fires on selection change (host routes to inspector).
    std::function<void(VfxAttributeScope, const std::string&)> onParameterSelected;

    /// Fires when the user clicks "Jump to usages" with a valid target.
    /// Host should select the referenced module in the stack panel.
    std::function<void(VfxStage stage, const std::string& instanceId)>
        onJumpToUsage;

    /// Phase 12.4.0 — fires when the author clicks the reset-to-default
    /// arrow. Caller-applied: the widget does not mutate the store.
    std::function<void(VfxAttributeScope, const std::string&)> onParameterReset;

    /// Phase 12.4.0 — fires when the author clicks the bind-to-curve
    /// chain-link. The host opens a curve / expression picker.
    std::function<void(VfxAttributeScope, const std::string&)>
        onParameterBindRequested;

private:
    VfxAttributeStore* m_store = nullptr;
    VfxStack*          m_stack = nullptr;

    /// Authored type registry keyed by `(scope, name)`. Missing entries
    /// default to `Float` when read.
    struct TypeKey {
        VfxAttributeScope scope;
        std::string       name;
        [[nodiscard]] bool operator==(const TypeKey& o) const noexcept {
            return scope == o.scope && name == o.name;
        }
    };
    struct TypeKeyHash {
        size_t operator()(const TypeKey& k) const noexcept {
            return std::hash<std::string>{}(k.name) ^
                   (static_cast<size_t>(k.scope) * 0x9e3779b97f4a7c15ull);
        }
    };
    std::unordered_map<TypeKey, VfxParamType, TypeKeyHash> m_types;
    std::unordered_map<TypeKey, VfxOverrideFlag, TypeKeyHash> m_overrideFlags;

    // Phase 12.4.0 — section / range annotations (widget-local; the
    // runtime store stays pure).
    std::unordered_map<TypeKey, std::string, TypeKeyHash> m_sectionNames;
    std::unordered_map<std::string, bool>                  m_sectionExpanded;
    struct ParamRange {
        float minValue = 0.0f;
        float maxValue = 1.0f;
    };
    std::unordered_map<TypeKey, ParamRange, TypeKeyHash>   m_ranges;

    VfxAttributeScope m_selectedScope = VfxAttributeScope::Emitter;
    std::string       m_selectedName;

    /// Per-scope expanded flag in left pane. Indexed by scope id.
    /// `Particles` defaults collapsed (not author-facing in this phase).
    bool m_scopeExpanded[kVfxAttributeScopeCount] = { false, true, true };

    /// Phase 14.3 — zebra-stripe counter advanced per rendered
    /// parameter row. Mutable so `Render` (const) can maintain it.
    mutable uint32_t m_rowIndex = 0u;

    /// Internal helpers.
    void SetTypeEntry(VfxAttributeScope scope, const std::string& name,
                      VfxParamType type);
    void EraseTypeEntry(VfxAttributeScope scope, const std::string& name);
    [[nodiscard]] static std::string BuildReferenceToken(const std::string& name);
};

} // namespace uz
