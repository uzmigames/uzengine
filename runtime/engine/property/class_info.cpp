/// ClassInfo implementation — property lookup and iteration.

#include "class_info.h"

namespace uz {

const PropertyInfo* ClassInfo::FindProperty(std::string_view prop_name) const {
    for (const auto& prop : properties) {
        if (prop_name == prop.name) {
            return &prop;
        }
    }
    return nullptr;
}

void ClassInfo::ForEachProperty(const std::function<void(const PropertyInfo&)>& fn) const {
    for (const auto& prop : properties) {
        fn(prop);
    }
}

size_t ClassInfo::PropertyCount() const noexcept {
    return properties.size();
}

} // namespace uz
