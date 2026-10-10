#include "vfx_editor_save_pipeline.h"

#include <algorithm>

namespace uz {

const char* VfxSaveAssetKindName(VfxSaveAssetKind kind) noexcept {
    switch (kind) {
        case VfxSaveAssetKind::Unknown:       return "unknown";
        case VfxSaveAssetKind::EmitterPreset: return "emitter_preset";
        case VfxSaveAssetKind::VfxSystem:     return "vfx_system";
        case VfxSaveAssetKind::NodeGraph:     return "node_graph";
        case VfxSaveAssetKind::TangentCurve:  return "tangent_curve";
        case VfxSaveAssetKind::Snapshot:      return "snapshot";
        case VfxSaveAssetKind::BakeAsset:     return "bake_asset";
        case VfxSaveAssetKind::Material:      return "material";
        case VfxSaveAssetKind::EffectType:    return "effect_type";
    }
    return "unknown";
}

EditorSavePipeline& EditorSavePipeline::Instance() noexcept {
    static EditorSavePipeline instance;
    return instance;
}

EditorSaveExtensionHandle EditorSavePipeline::Register(
    IEditorSaveExtension* extension) {
    if (extension == nullptr) return kInvalidEditorSaveExtensionHandle;
    const EditorSaveExtensionHandle h = nextHandle_++;
    slots_.push_back({h, extension});
    return h;
}

void EditorSavePipeline::Unregister(EditorSaveExtensionHandle handle) noexcept {
    if (handle == kInvalidEditorSaveExtensionHandle) return;
    slots_.erase(
        std::remove_if(slots_.begin(), slots_.end(),
                       [handle](const Slot& s) { return s.handle == handle; }),
        slots_.end());
}

bool EditorSavePipeline::RunPreSave(VfxSaveAssetKind kind,
                                      const void* asset,
                                      const std::string& outputPath,
                                      std::string& outAbortReason) {
    outAbortReason.clear();
    // Snapshot before iterating so extensions that register/unregister
    // during PreSave don't invalidate our iterator.
    const auto snapshot = slots_;
    for (const auto& s : snapshot) {
        if (s.extension == nullptr) continue;
        if (!s.extension->PreSave(kind, asset, outputPath)) {
            outAbortReason = s.extension->Name();
            return false;
        }
    }
    return true;
}

void EditorSavePipeline::RunPostSave(VfxSaveAssetKind kind,
                                      const void* asset,
                                      const std::string& outputPath) {
    const auto snapshot = slots_;
    for (const auto& s : snapshot) {
        if (s.extension == nullptr) continue;
        s.extension->PostSave(kind, asset, outputPath);
    }
}

uint32_t EditorSavePipeline::ExtensionCount() const noexcept {
    return static_cast<uint32_t>(slots_.size());
}

void EditorSavePipeline::Clear() noexcept {
    slots_.clear();
    // Keep `nextHandle_` monotonic — tests that rely on unique handles
    // across Clear() don't get collisions.
}

} // namespace uz
