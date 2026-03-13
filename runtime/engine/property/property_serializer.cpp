/// PropertySerializer implementation — JSON serialization for reflected properties.

#include "property_serializer.h"

#include "vector2.h"
#include "vector3.h"
#include "vector4.h"
#include "quaternion.h"
#include "entity.h"
#include "log.h"

#include <cstring>
#include <string>

UZ_DECLARE_LOG_CATEGORY(Property);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Byte-offset helpers — read/write fields via raw pointer arithmetic
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Returns a const pointer to a field at `offset` bytes from `base`.
template <typename T>
const T& ReadField(const void* base, size_t offset) {
    return *reinterpret_cast<const T*>(static_cast<const char*>(base) + offset);
}

/// Returns a mutable pointer to a field at `offset` bytes from `base`.
template <typename T>
T& WriteField(void* base, size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PropertyToJSON — single property serialization
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json PropertySerializer::PropertyToJSON(const void* instance, const PropertyInfo& prop) {
    switch (prop.type) {
    case PropertyType::Bool:
        return ReadField<bool>(instance, prop.offset);

    case PropertyType::Int:
        return ReadField<int>(instance, prop.offset);

    case PropertyType::Float:
        return ReadField<float>(instance, prop.offset);

    case PropertyType::Double:
        return ReadField<double>(instance, prop.offset);

    case PropertyType::String:
        return ReadField<std::string>(instance, prop.offset);

    case PropertyType::Vec2: {
        const auto& v = ReadField<Vec2>(instance, prop.offset);
        return nlohmann::json::array({v.x, v.y});
    }

    case PropertyType::Vec3: {
        const auto& v = ReadField<Vec3>(instance, prop.offset);
        return nlohmann::json::array({v.x, v.y, v.z});
    }

    case PropertyType::Vec4: {
        const auto& v = ReadField<Vec4>(instance, prop.offset);
        return nlohmann::json::array({v.x, v.y, v.z, v.w});
    }

    case PropertyType::Quat: {
        const auto& q = ReadField<Quat>(instance, prop.offset);
        return nlohmann::json::array({q.x, q.y, q.z, q.w});
    }

    case PropertyType::Entity: {
        const auto& e = ReadField<Entity>(instance, prop.offset);
        return nlohmann::json{{"id", e.id}, {"generation", e.generation}};
    }

    case PropertyType::None:
        break;
    }

    UZ_LOG(Property, Warning, "Cannot serialize property '{}': unsupported type {}", prop.name,
           PropertyTypeName(prop.type));
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// PropertyFromJSON — single property deserialization
// ─────────────────────────────────────────────────────────────────────────────

bool PropertySerializer::PropertyFromJSON(void* instance, const PropertyInfo& prop,
                                          const nlohmann::json& value) {
    switch (prop.type) {
    case PropertyType::Bool:
        if (!value.is_boolean()) {
            UZ_LOG(Property, Warning, "Type mismatch for '{}': expected bool, got {}", prop.name,
                   value.type_name());
            return false;
        }
        WriteField<bool>(instance, prop.offset) = value.get<bool>();
        return true;

    case PropertyType::Int:
        if (!value.is_number_integer()) {
            UZ_LOG(Property, Warning, "Type mismatch for '{}': expected int, got {}", prop.name,
                   value.type_name());
            return false;
        }
        WriteField<int>(instance, prop.offset) = value.get<int>();
        return true;

    case PropertyType::Float:
        if (!value.is_number()) {
            UZ_LOG(Property, Warning, "Type mismatch for '{}': expected number, got {}", prop.name,
                   value.type_name());
            return false;
        }
        WriteField<float>(instance, prop.offset) = value.get<float>();
        return true;

    case PropertyType::Double:
        if (!value.is_number()) {
            UZ_LOG(Property, Warning, "Type mismatch for '{}': expected number, got {}", prop.name,
                   value.type_name());
            return false;
        }
        WriteField<double>(instance, prop.offset) = value.get<double>();
        return true;

    case PropertyType::String:
        if (!value.is_string()) {
            UZ_LOG(Property, Warning, "Type mismatch for '{}': expected string, got {}", prop.name,
                   value.type_name());
            return false;
        }
        WriteField<std::string>(instance, prop.offset) = value.get<std::string>();
        return true;

    case PropertyType::Vec2: {
        if (!value.is_array() || value.size() != 2) {
            UZ_LOG(Property, Warning,
                   "Type mismatch for '{}': expected array of 2 numbers, got {}", prop.name,
                   value.type_name());
            return false;
        }
        auto& v = WriteField<Vec2>(instance, prop.offset);
        v.x = value[0].get<float>();
        v.y = value[1].get<float>();
        return true;
    }

    case PropertyType::Vec3: {
        if (!value.is_array() || value.size() != 3) {
            UZ_LOG(Property, Warning,
                   "Type mismatch for '{}': expected array of 3 numbers, got {}", prop.name,
                   value.type_name());
            return false;
        }
        auto& v = WriteField<Vec3>(instance, prop.offset);
        v.x = value[0].get<float>();
        v.y = value[1].get<float>();
        v.z = value[2].get<float>();
        return true;
    }

    case PropertyType::Vec4: {
        if (!value.is_array() || value.size() != 4) {
            UZ_LOG(Property, Warning,
                   "Type mismatch for '{}': expected array of 4 numbers, got {}", prop.name,
                   value.type_name());
            return false;
        }
        auto& v = WriteField<Vec4>(instance, prop.offset);
        v.x = value[0].get<float>();
        v.y = value[1].get<float>();
        v.z = value[2].get<float>();
        v.w = value[3].get<float>();
        return true;
    }

    case PropertyType::Quat: {
        if (!value.is_array() || value.size() != 4) {
            UZ_LOG(Property, Warning,
                   "Type mismatch for '{}': expected array of 4 numbers, got {}", prop.name,
                   value.type_name());
            return false;
        }
        auto& q = WriteField<Quat>(instance, prop.offset);
        q.x = value[0].get<float>();
        q.y = value[1].get<float>();
        q.z = value[2].get<float>();
        q.w = value[3].get<float>();
        return true;
    }

    case PropertyType::Entity: {
        if (!value.is_object() || !value.contains("id") || !value.contains("generation")) {
            UZ_LOG(Property, Warning,
                   "Type mismatch for '{}': expected object with 'id' and 'generation'", prop.name);
            return false;
        }
        auto& e = WriteField<Entity>(instance, prop.offset);
        e.id = value["id"].get<uint32_t>();
        e.generation = value["generation"].get<uint32_t>();
        return true;
    }

    case PropertyType::None:
        break;
    }

    UZ_LOG(Property, Warning, "Cannot deserialize property '{}': unsupported type {}", prop.name,
           PropertyTypeName(prop.type));
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// SerializeToJSON / DeserializeFromJSON — full instance operations
// ─────────────────────────────────────────────────────────────────────────────

nlohmann::json PropertySerializer::SerializeToJSON(const void* instance, const ClassInfo& info) {
    nlohmann::json result = nlohmann::json::object();

    for (const auto& prop : info.properties) {
        if (HasFlag(prop.flags, PropertyFlags::Transient)) {
            continue;
        }
        auto value = PropertyToJSON(instance, prop);
        if (!value.is_null()) {
            result[prop.name] = std::move(value);
        }
    }

    return result;
}

bool PropertySerializer::DeserializeFromJSON(void* instance, const ClassInfo& info,
                                             const nlohmann::json& data) {
    if (!data.is_object()) {
        UZ_LOG(Property, Error, "DeserializeFromJSON: expected JSON object for class '{}'",
               info.name);
        return false;
    }

    bool all_ok = true;

    for (const auto& prop : info.properties) {
        if (HasFlag(prop.flags, PropertyFlags::Transient)) {
            continue;
        }
        if (!data.contains(prop.name)) {
            continue;
        }
        if (!PropertyFromJSON(instance, prop, data[prop.name])) {
            all_ok = false;
        }
    }

    return all_ok;
}

} // namespace uz
