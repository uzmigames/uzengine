#pragma once

/// VFX stack module instance — one authored module inside a stack entry.
///
/// Phase 11.1.0. An emitter's behaviour is expressed as an ordered list of
/// `VfxModuleInstance` per stack stage. Each instance pairs a registered
/// module (name keyed into `ModuleRegistry`) with:
///
///   - a JSON config blob holding per-instance parameters
///   - an `enabled` bit (authors can temporarily disable a module without
///     losing its parameter values)
///   - an `OverrideFlag` describing the instance's relationship to a
///     parent emitter (inherited vs. override, for phase 11.11)
///   - a stable `instanceId` string uuid so undo/redo can locate the
///     module after reorders
///
/// The struct is pure data — no runtime state, no shader objects. The
/// module compositor reads the instance list, looks up each module
/// factory in `ModuleRegistry`, constructs a fresh `IVFXModule` with the
/// configJson, and stitches its HLSL into the composed shader.
///
/// # Thread safety
/// Value-typed. Copying is cheap (small strings). Mutations happen on
/// the editor / main thread only; the render thread sees a snapshot
/// built inside the frame.
///
/// @ref UE5 `FNiagaraStackModuleData`
/// @ref engine/vfx/stack/vfx_stage.h

#include <cstdint>
#include <string>

namespace uz {

/// Inheritance override relationship for a stack module instance.
///
/// Authors see visual markers in the stack panel (phase 11.11) driven by
/// this flag: `Inherited` rows are dimmed, overrides are highlighted,
/// and removed parent modules render crossed out.
enum class VfxOverrideFlag : uint8_t {
    /// Module instance is the emitter's own — no parent relationship,
    /// or this emitter has no parent.
    None = 0,

    /// Module came from the parent emitter and is not overridden in the
    /// child. Editing this instance creates an override (`Replaced`).
    Inherited = 1,

    /// Child-only module appended after parent's list (`"action": "add"`).
    OverrideAdd = 2,

    /// Child replaces the parent's module of the same type
    /// (`"action": "replace"`). Original parent config is preserved by
    /// the resolver for one-click "Revert to Parent".
    OverrideReplace = 3,

    /// Child removes a parent module (`"action": "remove"`). The entry
    /// is kept in the list (rendered crossed-out) so authors can revert.
    OverrideRemove = 4,
};

/// Returns a stable string for the flag (JSON / logs / UI labels).
[[nodiscard]] constexpr const char* VfxOverrideFlagName(VfxOverrideFlag f) noexcept {
    switch (f) {
        case VfxOverrideFlag::None:            return "None";
        case VfxOverrideFlag::Inherited:       return "Inherited";
        case VfxOverrideFlag::OverrideAdd:     return "OverrideAdd";
        case VfxOverrideFlag::OverrideReplace: return "OverrideReplace";
        case VfxOverrideFlag::OverrideRemove:  return "OverrideRemove";
    }
    return "Unknown";
}

/// Parses a flag name. Returns `true` on success, `false` on unknown
/// input (output unchanged).
[[nodiscard]] constexpr bool VfxOverrideFlagFromName(const char* name, VfxOverrideFlag& out) noexcept {
    if (name == nullptr) return false;
    struct Pair { const char* n; VfxOverrideFlag v; };
    constexpr Pair table[] = {
        {"None",            VfxOverrideFlag::None},
        {"Inherited",       VfxOverrideFlag::Inherited},
        {"OverrideAdd",     VfxOverrideFlag::OverrideAdd},
        {"OverrideReplace", VfxOverrideFlag::OverrideReplace},
        {"OverrideRemove",  VfxOverrideFlag::OverrideRemove},
    };
    for (const auto& p : table) {
        const char* a = p.n;
        const char* b = name;
        while (*a != '\0' && *a == *b) { ++a; ++b; }
        if (*a == '\0' && *b == '\0') {
            out = p.v;
            return true;
        }
    }
    return false;
}

/// One authored module inside a `VfxStackEntry`.
struct VfxModuleInstance {
    /// Registered name in `ModuleRegistry`. Must not be empty.
    /// Example: `"Gravity"`, `"ColorOverLife"`, `"SpriteRenderer"`.
    std::string moduleTypeName;

    /// Per-instance parameter values as JSON text. Parsed by the
    /// module's `ApplyJsonConfig()` at composition time. Empty string
    /// means "use module defaults". Keeping this as a string (rather
    /// than a typed `json` object) avoids pulling nlohmann::json into
    /// every translation unit that just needs the stack layout.
    std::string configJson;

    /// Stable UUID-like identifier (e.g. `"m_a3f1d2"`), generated on
    /// creation. Undo/redo commands reference this instead of array
    /// indices so reorders don't invalidate outstanding history.
    std::string instanceId;

    /// Inheritance relationship (set by `VfxEmitterInheritance::Resolve`).
    VfxOverrideFlag overrideFlag = VfxOverrideFlag::None;

    /// When `false`, the module is skipped during shader composition.
    /// Config values are preserved so re-enabling restores behaviour.
    bool enabled = true;

    /// Returns true when the instance has the minimum fields required
    /// for composition (non-empty type name).
    [[nodiscard]] bool IsValid() const noexcept {
        return !moduleTypeName.empty();
    }
};

} // namespace uz
