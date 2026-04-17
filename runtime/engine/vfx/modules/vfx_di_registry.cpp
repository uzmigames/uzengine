#include "vfx_di_registry.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

void VfxDIRegistry::Register(std::unique_ptr<IVfxDataInterface> di) {
    if (!di) {
        UZ_LOG(VFX, Warning, "VfxDIRegistry::Register: null DI ignored");
        return;
    }
    const std::string name = di->TypeName();
    if (name.empty()) {
        UZ_LOG(VFX, Warning, "VfxDIRegistry::Register: DI with empty TypeName ignored");
        return;
    }
    UZ_LOG(VFX, Info, "VfxDIRegistry: registered DI '{}'", name);
    dis_[name] = std::move(di);
}

IVfxDataInterface* VfxDIRegistry::Find(const std::string& typeName) const {
    auto it = dis_.find(typeName);
    return (it != dis_.end()) ? it->second.get() : nullptr;
}

uint32_t VfxDIRegistry::Count() const noexcept {
    return static_cast<uint32_t>(dis_.size());
}

void VfxDIRegistry::Clear() {
    dis_.clear();
}

std::vector<std::string> VfxDIRegistry::RegisteredNames() const {
    std::vector<std::string> names;
    names.reserve(dis_.size());
    for (const auto& [name, _] : dis_) {
        names.push_back(name);
    }
    return names;
}

VfxDIRegistry::ComposedDIFragments VfxDIRegistry::ComposeForEmitter(
    const std::vector<std::string>& diNames) const {
    ComposedDIFragments result;
    for (const auto& name : diNames) {
        auto* di = Find(name);
        if (!di) {
            UZ_LOG(VFX, Warning,
                   "VfxDIRegistry::ComposeForEmitter: DI '{}' not found, skipping",
                   name);
            continue;
        }
        result.cbFields        += "// DI: " + name + "\n" + di->EmitCBFields() + "\n";
        result.helperFunctions += "// DI: " + name + "\n" + di->EmitHelperFunctions() + "\n";
        result.callSite        += "// DI: " + name + "\n" + di->EmitCallSite() + "\n";
    }
    return result;
}

} // namespace uz
