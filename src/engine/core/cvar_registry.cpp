// cvar_registry.cpp — CVarRegistry implementation.

#include "engine/core/cvar_registry.h"

namespace uz {

// Deduce the CVarType tag from the variant index at registration time.
static CVarType type_from_variant(const CVarValue& v) {
    switch (v.index()) {
        case 0: return CVarType::BOOL;
        case 1: return CVarType::INT;
        case 2: return CVarType::FLOAT;
        case 3: return CVarType::STRING;
        default: assert(false && "Unknown CVarValue index"); return CVarType::BOOL;
    }
}

CVarEntry* CVarRegistry::register_cvar(const std::string& name,
                                       const std::string& description,
                                       CVarValue default_value,
                                       uint32_t flags) {
    // Reject duplicate names.
    if (entries_.count(name) != 0) {
        return nullptr;
    }

    CVarEntry entry;
    entry.name          = name;
    entry.description   = description;
    entry.flags         = flags;
    entry.type          = type_from_variant(default_value);
    entry.default_value = default_value;
    entry.current_value = std::move(default_value);

    auto [it, inserted] = entries_.emplace(name, std::move(entry));
    assert(inserted && "CVarRegistry: emplace failed after count check");
    return &it->second;
}

const CVarEntry* CVarRegistry::find_by_name(const std::string& name) const {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

CVarEntry* CVarRegistry::find_by_name_mut(const std::string& name) {
    auto it = entries_.find(name);
    if (it == entries_.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<const CVarEntry*> CVarRegistry::get_all() const {
    std::vector<const CVarEntry*> result;
    result.reserve(entries_.size());
    for (const auto& [key, entry] : entries_) {
        result.push_back(&entry);
    }
    return result;
}

} // namespace uz
