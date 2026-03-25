/// VFX Module Registry implementation -- factory storage and lookup.

#include "module_registry.h"

#include "engine/logging/log.h"

namespace uz {

// ---------------------------------------------------------------------------
// ModuleRegistry::RegisterFactory
// ---------------------------------------------------------------------------

void ModuleRegistry::RegisterFactory(const char* name, FactoryFn factory) {
    auto it = factories_.find(name);
    if (it != factories_.end()) {
        UZ_LOG(VFX, Warning,
               "VFX module already registered, overwriting: name='{}'", name);
        it->second = std::move(factory);
        return;
    }

    factories_.emplace(name, std::move(factory));
    UZ_LOG(VFX, Info, "Registered VFX module: name='{}'", name);
}

// ---------------------------------------------------------------------------
// ModuleRegistry::Create
// ---------------------------------------------------------------------------

std::unique_ptr<IVFXModule> ModuleRegistry::Create(const char* name) const {
    const auto it = factories_.find(name);
    if (it == factories_.end()) {
        UZ_LOG(VFX, Error,
               "VFX module not found in registry: name='{}'", name);
        return nullptr;
    }

    return it->second();
}

// ---------------------------------------------------------------------------
// ModuleRegistry::GetRegisteredNames
// ---------------------------------------------------------------------------

std::vector<std::string> ModuleRegistry::GetRegisteredNames() const {
    std::vector<std::string> names;
    names.reserve(factories_.size());

    for (const auto& [name, factory] : factories_) {
        names.push_back(name);
    }

    return names;
}

// ---------------------------------------------------------------------------
// ModuleRegistry::IsRegistered
// ---------------------------------------------------------------------------

bool ModuleRegistry::IsRegistered(const char* name) const {
    return factories_.find(name) != factories_.end();
}

} // namespace uz
