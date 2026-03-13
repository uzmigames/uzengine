/// ClassRegistry implementation — singleton type registry with logging.

#include "class_registry.h"

#include "log.h"

UZ_DEFINE_LOG_CATEGORY(Property, Info);

namespace uz {

ClassRegistry& ClassRegistry::Get() {
    static ClassRegistry s_instance;
    return s_instance;
}

void ClassRegistry::Register(std::string_view type_name, ClassInfo& info) {
    auto key = std::string(type_name);
    auto it = registry_.find(key);
    if (it != registry_.end()) {
        UZ_LOG(Property, Warning, "Overwriting existing class registration: '{}'", key);
    }
    registry_[key] = &info;
    UZ_LOG(Property, Info, "Registered class '{}' ({} properties, {} bytes)", key,
           info.PropertyCount(), info.instance_size);
}

ClassInfo* ClassRegistry::FindClass(std::string_view type_name) {
    auto it = registry_.find(std::string(type_name));
    if (it != registry_.end()) {
        return it->second;
    }
    return nullptr;
}

const ClassInfo* ClassRegistry::FindClass(std::string_view type_name) const {
    auto it = registry_.find(std::string(type_name));
    if (it != registry_.end()) {
        return it->second;
    }
    return nullptr;
}

bool ClassRegistry::IsRegistered(std::string_view type_name) const {
    return registry_.find(std::string(type_name)) != registry_.end();
}

void ClassRegistry::ForEachClass(
    const std::function<void(std::string_view name, const ClassInfo&)>& fn) const {
    for (const auto& [name, info_ptr] : registry_) {
        fn(name, *info_ptr);
    }
}

void ClassRegistry::ForEachBehavior(
    const std::function<void(std::string_view name, const ClassInfo&)>& fn) const {
    for (const auto& [name, info_ptr] : registry_) {
        if (info_ptr->is_behavior) {
            fn(name, *info_ptr);
        }
    }
}

size_t ClassRegistry::ClassCount() const noexcept {
    return registry_.size();
}

} // namespace uz
