#include "material_compiler_extension.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// InjectionPoint names
// ─────────────────────────────────────────────────────────────────────────────

const char* MaterialInjectionPointName(MaterialInjectionPoint point) noexcept {
    switch (point) {
        case MaterialInjectionPoint::VS_PreViewProj:  return "VS_PreViewProj";
        case MaterialInjectionPoint::VS_PrevFrame:    return "VS_PrevFrame";
        case MaterialInjectionPoint::PS_PostLighting:  return "PS_PostLighting";
        case MaterialInjectionPoint::FunctionScope:   return "FunctionScope";
        default: return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// MaterialCompilerExtensionRegistry
// ─────────────────────────────────────────────────────────────────────────────

void MaterialCompilerExtensionRegistry::Register(
    std::unique_ptr<IMaterialCompilerExtension> ext) {
    if (!ext) {
        UZ_LOG(Renderer, Warning,
               "MaterialCompilerExtensionRegistry: null extension ignored");
        return;
    }
    UZ_LOG(Renderer, Info,
           "MaterialCompilerExtensionRegistry: registered '{}'", ext->Name());
    extensions_.push_back(std::move(ext));
}

uint32_t MaterialCompilerExtensionRegistry::Count() const noexcept {
    return static_cast<uint32_t>(extensions_.size());
}

void MaterialCompilerExtensionRegistry::Clear() {
    extensions_.clear();
}

std::string MaterialCompilerExtensionRegistry::CollectHLSL(
    MaterialInjectionPoint point) const {
    std::string result;
    for (const auto& ext : extensions_) {
        std::string snippet;
        if (ext->EmitHLSL(point, snippet)) {
            if (!result.empty()) result += "\n";
            result += "// Extension: ";
            result += ext->Name();
            result += "\n";
            result += snippet;
        }
    }
    return result;
}

std::vector<std::string> MaterialCompilerExtensionRegistry::CollectPermutationTags() const {
    std::vector<std::string> tags;
    for (const auto& ext : extensions_) {
        auto tag = ext->PSOPermutationTag();
        if (!tag.empty()) {
            tags.push_back(std::move(tag));
        }
    }
    return tags;
}

} // namespace uz
