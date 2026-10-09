#include "vfx_validator_editor_bridge.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// VfxValidatorSaveExtension
// ─────────────────────────────────────────────────────────────────────────────

bool VfxValidatorSaveExtension::PreSave(VfxSaveAssetKind kind,
                                          const void* asset,
                                          const std::string& outputPath) {
    if (!provider_) {
        // No validation context → skip (preserves save behaviour when the
        // bridge isn't wired up).
        return true;
    }

    const auto ctx = provider_(kind, asset, outputPath);
    lastReport_ = VfxValidateAsset(ctx);

    if (lastReport_.Passed()) return true;

    UZ_LOG(VFX, Warning,
           "VfxValidator: save of '{}' blocked — {} error(s), {} warning(s)",
           outputPath, lastReport_.ErrorCount(), lastReport_.WarningCount());
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxValidatorEditorBridge
// ─────────────────────────────────────────────────────────────────────────────

VfxValidatorEditorBridge::VfxValidatorEditorBridge()
    : saveExt_(std::make_unique<VfxValidatorSaveExtension>()) {}

void VfxValidatorEditorBridge::SetContextProvider(
    VfxValidatorSaveExtension::ContextProvider provider) {
    provider_ = provider;
    saveExt_->SetContextProvider(std::move(provider));
}

void VfxValidatorEditorBridge::OnEdit(float timestamp) noexcept {
    lastEditTime_ = timestamp;
}

bool VfxValidatorEditorBridge::TickDebounced(float timestamp) {
    if (lastEditTime_ < 0.0f) return false;          // no edit pending
    if (timestamp - lastEditTime_ < debounceSeconds) return false; // still typing
    if (!provider_) { lastEditTime_ = -1.0f; return false; }

    // Build context with placeholder kind / no asset pointer — the
    // provider closes over the active editor state.
    VfxValidationContext ctx =
        provider_(VfxSaveAssetKind::Unknown, nullptr, std::string{});
    saveExt_->SetLastReport(VfxValidateAsset(ctx));

    lastEditTime_ = -1.0f;
    ++runCount_;
    return true;
}

} // namespace uz
