/// JSON deserialization helpers for component serializers.
///
/// Shared utilities to read typed values from nlohmann::json objects
/// with fallback defaults. Used by all component serializer .cpp files.

#pragma once

#include "../math/vector3.h"

#include <json.hpp>

#include <cstdint>
#include <string>

namespace uz::json {

/// Reads a float from JSON, returns `def` if key is missing or wrong type.
inline float Float(const nlohmann::json& j, const char* key, float def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<float>() : def;
}

/// Reads a bool from JSON, returns `def` if key is missing or wrong type.
inline bool Bool(const nlohmann::json& j, const char* key, bool def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

/// Reads a uint32 from JSON, returns `def` if key is missing or wrong type.
inline uint32_t Uint(const nlohmann::json& j, const char* key, uint32_t def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_unsigned()) ? it->get<uint32_t>() : def;
}

/// Reads an int32 from JSON, returns `def` if key is missing or wrong type.
inline int32_t Int(const nlohmann::json& j, const char* key, int32_t def) {
    auto it = j.find(key);
    return (it != j.end() && it->is_number_integer()) ? it->get<int32_t>() : def;
}

/// Reads a string from JSON, returns `def` if key is missing or wrong type.
inline std::string String(const nlohmann::json& j, const char* key, const std::string& def = {}) {
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
}

/// Reads a Vec3 from a JSON array [x, y, z], returns default if missing.
inline Vec3 Vec3FromArray(const nlohmann::json& j, const char* key, const Vec3& def) {
    auto it = j.find(key);
    if (it != j.end() && it->is_array() && it->size() >= 3) {
        return {(*it)[0].get<float>(), (*it)[1].get<float>(), (*it)[2].get<float>()};
    }
    return def;
}

/// Writes a Vec3 as a JSON array [x, y, z].
inline void WriteVec3(nlohmann::json& j, const char* key, const Vec3& v) {
    j[key] = {v.x, v.y, v.z};
}

} // namespace uz::json
