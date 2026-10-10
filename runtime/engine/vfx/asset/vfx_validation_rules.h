#pragma once

/// VFX pluggable validation rules — domain constraints for CI + editor.
///
/// Extends the structural validator (vfx_graph_validator.h) with authored
/// domain rules: budget violations, missing material bindings, unresolved
/// preset references, LOD absence, excessive inheritance depth, etc.
///
/// Each rule implements `IVfxValidationRule::Check` and appends entries to
/// a `VfxValidationReport`. The report carries severity (Error / Warning /
/// Info) and a structured source location so the editor can jump to the
/// offending parameter and CI can surface the issue in JUnit XML.
///
/// # CI integration
///
/// `VfxValidationReport::ToJUnitXml()` produces a JUnit XML string that
/// CI runners (GitHub Actions, Jenkins, GitLab CI) ingest natively.
///
/// # Rule authoring
///
/// ```cpp
/// struct MyRule : IVfxValidationRule {
///     const char* Name() const noexcept override { return "MyRule"; }
///     void Check(const VfxValidationContext& ctx,
///                VfxValidationReport& report) const override {
///         if (bad_condition)
///             report.Add(VfxValidationSeverity::Error, Name(), "msg");
///     }
/// };
/// ```
///
/// @ref UE5 UNiagaraValidationRule + FNiagaraValidationContext
/// @ref docs/engine/25-vfx/26-validator-ci-rules.md

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace uz {

class VFXGraph;
struct VfxSystem;
struct ParticleEmitter;

// ─────────────────────────────────────────────────────────────────────────────
// Severity
// ─────────────────────────────────────────────────────────────────────────────

enum class VfxValidationSeverity : uint8_t {
    Info    = 0, ///< Informational — does not block CI.
    Warning = 1, ///< Non-critical — editor yellow, CI warning annotation.
    Error   = 2, ///< Critical — asset is broken; CI fails.
};

/// Returns a static string for `sev` (for XML / log output).
[[nodiscard]] const char* VfxValidationSeverityName(VfxValidationSeverity sev) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// Report entry + report
// ─────────────────────────────────────────────────────────────────────────────

/// One issue found by a rule.
struct VfxValidationEntry {
    VfxValidationSeverity severity = VfxValidationSeverity::Error;
    std::string           ruleName;    ///< `IVfxValidationRule::Name()`.
    std::string           message;     ///< Human-readable description.
    std::string           assetPath;   ///< Originating asset (optional).
};

/// Aggregates every issue across every rule for one validation pass.
class VfxValidationReport {
public:
    /// Appends an entry.
    void Add(VfxValidationSeverity sev, const char* ruleName,
             std::string message, std::string assetPath = {});

    /// True when no Error-severity entries were recorded.
    [[nodiscard]] bool Passed() const noexcept { return errorCount_ == 0u; }

    [[nodiscard]] uint32_t ErrorCount()   const noexcept { return errorCount_; }
    [[nodiscard]] uint32_t WarningCount() const noexcept { return warningCount_; }
    [[nodiscard]] uint32_t InfoCount()    const noexcept { return infoCount_; }
    [[nodiscard]] uint32_t TotalCount()   const noexcept {
        return static_cast<uint32_t>(entries_.size());
    }

    [[nodiscard]] const std::vector<VfxValidationEntry>& Entries() const noexcept {
        return entries_;
    }

    /// Produces JUnit XML consumable by CI runners.
    ///
    /// Each entry becomes a `<testcase>` under a `<testsuite>` named after
    /// the rule. Errors produce `<failure>`, warnings produce `<system-out>`
    /// annotations, info entries are skipped.
    [[nodiscard]] std::string ToJUnitXml(const std::string& suiteName = "VfxValidation") const;

    void Clear() noexcept {
        entries_.clear();
        errorCount_ = warningCount_ = infoCount_ = 0u;
    }

private:
    std::vector<VfxValidationEntry> entries_;
    uint32_t errorCount_   = 0u;
    uint32_t warningCount_ = 0u;
    uint32_t infoCount_    = 0u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Validation context — what rules inspect
// ─────────────────────────────────────────────────────────────────────────────

/// Read-only references to asset data for a single validation target.
/// Rules use whichever pointers are non-null. Typically only one is set
/// at a time (the validator iterates each asset type in turn).
struct VfxValidationContext {
    const VFXGraph*        graph   = nullptr;
    const VfxSystem*       system  = nullptr;
    const ParticleEmitter* emitter = nullptr;
    std::string            assetPath;

    /// Known preset names (populated by the caller). Rules that check
    /// preset resolution compare emitter-ref paths against this list.
    std::vector<std::string> knownPresets;

    /// Maximum allowed particle budget per emitter.
    uint32_t maxParticleBudget = 100000u;

    /// Maximum allowed inheritance depth before triggering a warning.
    uint32_t maxInheritanceDepthWarning = 4u;
};

// ─────────────────────────────────────────────────────────────────────────────
// Rule interface
// ─────────────────────────────────────────────────────────────────────────────

/// Abstract base for a single validation rule.
class IVfxValidationRule {
public:
    virtual ~IVfxValidationRule() = default;

    /// Stable identifier used in reports and JUnit XML.
    [[nodiscard]] virtual const char* Name() const noexcept = 0;

    /// Inspects `ctx` and appends zero or more entries to `report`.
    virtual void Check(const VfxValidationContext& ctx,
                       VfxValidationReport& report) const = 0;
};

// ─────────────────────────────────────────────────────────────────────────────
// Baseline rule set (8 rules)
// ─────────────────────────────────────────────────────────────────────────────

/// maxParticles exceeds the configured budget.
struct VfxRule_BudgetViolation final : IVfxValidationRule {
    const char* Name() const noexcept override { return "BudgetViolation"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// Module stack is empty — no simulation behaviour defined.
struct VfxRule_EmptyModuleStack final : IVfxValidationRule {
    const char* Name() const noexcept override { return "EmptyModuleStack"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// Emitter lifespan is zero or negative.
struct VfxRule_NegativeLifespan final : IVfxValidationRule {
    const char* Name() const noexcept override { return "NegativeLifespan"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// Duplicate parameter names in the graph.
struct VfxRule_DuplicateParameters final : IVfxValidationRule {
    const char* Name() const noexcept override { return "DuplicateParameters"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// Duplicate event names in the graph.
struct VfxRule_DuplicateEvents final : IVfxValidationRule {
    const char* Name() const noexcept override { return "DuplicateEvents"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// VfxSystem emitter-ref path does not match any known preset.
struct VfxRule_UnresolvedPresetRef final : IVfxValidationRule {
    const char* Name() const noexcept override { return "UnresolvedPresetRef"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// VfxSystem contains a self-referencing emitter path.
struct VfxRule_SubEmitterRecursion final : IVfxValidationRule {
    const char* Name() const noexcept override { return "SubEmitterRecursion"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

/// Inheritance chain deeper than the warning threshold.
struct VfxRule_ExcessiveInheritanceDepth final : IVfxValidationRule {
    const char* Name() const noexcept override { return "ExcessiveInheritanceDepth"; }
    void Check(const VfxValidationContext&, VfxValidationReport&) const override;
};

// ─────────────────────────────────────────────────────────────────────────────
// Default rule set factory
// ─────────────────────────────────────────────────────────────────────────────

/// Returns all 8 baseline rules. The caller drives `Check` on each in order.
[[nodiscard]] std::vector<std::unique_ptr<IVfxValidationRule>> VfxGetDefaultValidationRules();

/// Convenience: runs all default rules against `ctx` and returns the report.
[[nodiscard]] VfxValidationReport VfxValidateAsset(const VfxValidationContext& ctx);

} // namespace uz
