#include "engine/core/component_registry.h"

#include <cstdio>

namespace uz {

// ============================================================================
// Singleton
// ============================================================================

ComponentRegistry& ComponentRegistry::instance() {
    static ComponentRegistry s_instance;
    return s_instance;
}

// ============================================================================
// register_type
// ============================================================================

void ComponentRegistry::register_type(StringId type_id,
                                       const char* type_name,
                                       ComponentFactory factory,
                                       ProxyFactory proxy_factory) {
    assert(type_name != nullptr && "ComponentRegistry: type_name must not be null");
    assert(factory != nullptr && "ComponentRegistry: factory must not be null");

    auto it = types_.find(type_id.hash);
    if (it != types_.end()) {
        std::fprintf(stderr, "[ComponentRegistry] WARNING: overwriting type '%s' (hash=%u)\n",
                     type_name, type_id.hash);
    }

    types_[type_id.hash] = ComponentTypeInfo{
        type_id,
        type_name,
        std::move(factory),
        std::move(proxy_factory)
    };
}

// ============================================================================
// create
// ============================================================================

IComponent* ComponentRegistry::create(StringId type_id) const {
    auto it = types_.find(type_id.hash);
    if (it == types_.end()) {
        return nullptr;
    }
    return it->second.factory();
}

// ============================================================================
// create_proxy
// ============================================================================

ISceneProxy* ComponentRegistry::create_proxy(StringId type_id) const {
    auto it = types_.find(type_id.hash);
    if (it == types_.end() || !it->second.proxy_factory) {
        return nullptr;
    }
    return it->second.proxy_factory();
}

// ============================================================================
// find
// ============================================================================

const ComponentTypeInfo* ComponentRegistry::find(StringId type_id) const {
    auto it = types_.find(type_id.hash);
    if (it == types_.end()) {
        return nullptr;
    }
    return &it->second;
}

// ============================================================================
// enumerate_types
// ============================================================================

std::vector<const char*> ComponentRegistry::enumerate_types() const {
    std::vector<const char*> names;
    names.reserve(types_.size());
    for (const auto& [hash, info] : types_) {
        names.push_back(info.type_name);
    }
    return names;
}

// ============================================================================
// has_type
// ============================================================================

bool ComponentRegistry::has_type(StringId type_id) const {
    return types_.find(type_id.hash) != types_.end();
}

// ============================================================================
// clear
// ============================================================================

void ComponentRegistry::clear() {
    types_.clear();
}

} // namespace uz
