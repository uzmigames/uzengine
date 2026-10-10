#pragma once

/// VFX Validator Editor Bridge — live validation + save-gate integration.
///
/// Phase 9.5.0 (editor side). Wires the 8.9.0 validation engine into the
/// editor:
///
/// 1. **Save gate**: An `IEditorSaveExtension` (phase 10.11.0) runs
///    `VfxValidateAsset` in `PreSave` and aborts the save with a human
///    reason when any Error-severity entry fires.
/// 2. **Debounced edit tick**: A helper that callers poke with a
///    monotonic timestamp every frame; after 500 ms of inactivity, it
///    re-runs validation and stores the report for the inspector to
///    render as inline markers / problems panel.
///
/// # Registration pattern
///
/// ```cpp
/// VfxValidatorEditorBridge bridge;
/// bridge.SetContextProvider([&]() { return editor.BuildValidationContext(); });
/// EditorSavePipeline::Instance().Register(bridge.SaveExtension());
/// ```
///
/// # Edit-tick pattern
///
/// ```cpp
/// // Per frame:
/// bridge.OnEdit(currentTimeSeconds);
/// if (bridge.TickDebounced(currentTimeSeconds)) {
///     // Report changed; host refreshes inspector markers.
/// }
/// ```
///
/// @ref engine/vfx/asset/vfx_validation_rules.h (rules engine)
/// @ref engine/vfx/editor/vfx_editor_save_pipeline.h (extension base)
/// @ref docs/engine/25-vfx/26-validator-ci-rules.md

#include "engine/vfx/asset/vfx_validation_rules.h"
#include "engine/vfx/editor/vfx_editor_save_pipeline.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Validator save extension
// ─────────────────────────────────────────────────────────────────────────────

/// IEditorSaveExtension implementation that validates before save.
class VfxValidatorSaveExtension final : public IEditorSaveExtension {
public:
    /// Context provider — called each PreSave to build a validation
    /// context for the asset being saved. The caller typically closes
    /// over the active emitter / system pointer.
    using ContextProvider =
        std::function<VfxValidationContext(VfxSaveAssetKind, const void* asset,
                                            const std::string& outputPath)>;

    void SetContextProvider(ContextProvider provider) {
        provider_ = std::move(provider);
    }

    /// Runs validation. Returns `true` when no Error-severity entries
    /// fired (save proceeds); `false` otherwise (save aborts).
    bool PreSave(VfxSaveAssetKind kind,
                 const void* asset,
                 const std::string& outputPath) override;

    const char* Name() const noexcept override { return "VfxValidator"; }

    /// Returns the last validation report (inspector reads this on
    /// refresh). The report is populated by both PreSave + the debounced
    /// edit tick on the bridge.
    [[nodiscard]] const VfxValidationReport& LastReport() const noexcept {
        return lastReport_;
    }

    /// Overwrites the last report (used by the bridge's edit tick).
    void SetLastReport(VfxValidationReport report) {
        lastReport_ = std::move(report);
    }

private:
    ContextProvider       provider_;
    VfxValidationReport   lastReport_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Validator editor bridge — save gate + debounced edit tick
// ─────────────────────────────────────────────────────────────────────────────

/// Owns the save extension and the debounced edit-tick clock.
class VfxValidatorEditorBridge {
public:
    VfxValidatorEditorBridge();

    /// Sets the context provider for both PreSave and edit-tick passes.
    void SetContextProvider(VfxValidatorSaveExtension::ContextProvider provider);

    /// Returns the save extension pointer. Caller registers it with
    /// `EditorSavePipeline::Register`.
    [[nodiscard]] VfxValidatorSaveExtension* SaveExtension() noexcept {
        return saveExt_.get();
    }

    /// Debounce interval in seconds. Default 0.5s.
    float debounceSeconds = 0.5f;

    /// Records that the user typed / clicked at `timestamp`. Resets the
    /// debounce timer.
    void OnEdit(float timestamp) noexcept;

    /// Called each frame with the current monotonic timestamp.
    /// When `debounceSeconds` has elapsed since the last `OnEdit`,
    /// re-runs validation and stores the report on the save extension.
    ///
    /// # Returns
    /// `true` when validation ran this tick (caller refreshes UI).
    bool TickDebounced(float timestamp);

    /// Returns the most recent validation report.
    [[nodiscard]] const VfxValidationReport& LastReport() const noexcept {
        return saveExt_->LastReport();
    }

    /// Returns the number of validation runs since construction.
    [[nodiscard]] uint32_t RunCount() const noexcept { return runCount_; }

private:
    std::unique_ptr<VfxValidatorSaveExtension> saveExt_;
    VfxValidatorSaveExtension::ContextProvider provider_;

    /// Last edit timestamp. A negative sentinel means "no edit pending".
    float lastEditTime_ = -1.0f;

    uint32_t runCount_ = 0;
};

} // namespace uz
