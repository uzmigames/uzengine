/// VFX Curve Loader -- JSON deserialization for .uzcurve files.

#include "vfx_curve_loader.h"

#include "engine/logging/log.h"
#include "file_system.h"

#include <json.hpp>

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz::VFXCurveLoader {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// String-to-Enum Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a curve type string ("float", "vec3", "vec4") into a CurveType enum.
///
/// # Returns
/// CurveType on success, std::nullopt on unrecognized string.
[[nodiscard]] std::optional<CurveType> ParseCurveType(const std::string& s) {
    if (s == "float") return CurveType::Float;
    if (s == "vec3")  return CurveType::Vec3;
    if (s == "vec4")  return CurveType::Vec4;
    return std::nullopt;
}

/// Parses an interpolation mode string into an InterpolationMode enum.
///
/// # Returns
/// InterpolationMode on success, std::nullopt on unrecognized string.
[[nodiscard]] std::optional<InterpolationMode> ParseInterpolation(const std::string& s) {
    if (s == "linear")     return InterpolationMode::Linear;
    if (s == "cubic")      return InterpolationMode::Cubic;
    if (s == "smoothstep") return InterpolationMode::Smoothstep;
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────

/// Validates that float keyframes are sorted and in [0, 1] time range.
[[nodiscard]] bool ValidateFloatKeys(const std::vector<Keyframe>& keys) {
    if (keys.size() < 2) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: need at least 2 keys, got {}",
               keys.size());
        return false;
    }

    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time < 0.0f || keys[i].time > 1.0f) {
            UZ_LOG(VFX, Error,
                   "VFXCurveLoader: key[{}] time={} out of [0,1] range",
                   i, keys[i].time);
            return false;
        }
        if (i > 0 && keys[i].time <= keys[i - 1].time) {
            UZ_LOG(VFX, Error,
                   "VFXCurveLoader: keys not strictly sorted (key[{}].time={} <= key[{}].time={})",
                   i, keys[i].time, i - 1, keys[i - 1].time);
            return false;
        }
    }
    return true;
}

/// Validates that Vec4 keyframes are sorted and in [0, 1] time range.
[[nodiscard]] bool ValidateVec4Keys(const std::vector<KeyframeVec4>& keys) {
    if (keys.size() < 2) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: need at least 2 keys, got {}",
               keys.size());
        return false;
    }

    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].time < 0.0f || keys[i].time > 1.0f) {
            UZ_LOG(VFX, Error,
                   "VFXCurveLoader: key[{}] time={} out of [0,1] range",
                   i, keys[i].time);
            return false;
        }
        if (i > 0 && keys[i].time <= keys[i - 1].time) {
            UZ_LOG(VFX, Error,
                   "VFXCurveLoader: keys not strictly sorted (key[{}].time={} <= key[{}].time={})",
                   i, keys[i].time, i - 1, keys[i - 1].time);
            return false;
        }
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Float Key Parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Parses the "keys" array from JSON into float keyframes.
[[nodiscard]] std::optional<std::vector<Keyframe>>
ParseFloatKeys(const nlohmann::json& keysArray) {
    std::vector<Keyframe> keys;
    keys.reserve(keysArray.size());

    for (size_t i = 0; i < keysArray.size(); ++i) {
        const auto& entry = keysArray[i];
        if (!entry.contains("time") || !entry.contains("value")) {
            UZ_LOG(VFX, Error, "VFXCurveLoader: key[{}] missing 'time' or 'value'", i);
            return std::nullopt;
        }
        if (!entry["time"].is_number() || !entry["value"].is_number()) {
            UZ_LOG(VFX, Error, "VFXCurveLoader: key[{}] 'time'/'value' must be numbers", i);
            return std::nullopt;
        }
        keys.push_back({entry["time"].get<float>(), entry["value"].get<float>()});
    }
    return keys;
}

// ─────────────────────────────────────────────────────────────────────────────
// Vec4 Key Parsing
// ─────────────────────────────────────────────────────────────────────────────

/// Parses the "keys" array from JSON into Vec4 keyframes.
///
/// For vec3 curves, value arrays must have 3 elements (w is set to 0).
/// For vec4 curves, value arrays must have 4 elements.
[[nodiscard]] std::optional<std::vector<KeyframeVec4>>
ParseVec4Keys(const nlohmann::json& keysArray, CurveType type) {
    const size_t expectedComponents = (type == CurveType::Vec3) ? 3 : 4;
    std::vector<KeyframeVec4> keys;
    keys.reserve(keysArray.size());

    for (size_t i = 0; i < keysArray.size(); ++i) {
        const auto& entry = keysArray[i];
        if (!entry.contains("time") || !entry.contains("value")) {
            UZ_LOG(VFX, Error, "VFXCurveLoader: key[{}] missing 'time' or 'value'", i);
            return std::nullopt;
        }
        if (!entry["time"].is_number()) {
            UZ_LOG(VFX, Error, "VFXCurveLoader: key[{}] 'time' must be a number", i);
            return std::nullopt;
        }
        if (!entry["value"].is_array() || entry["value"].size() != expectedComponents) {
            UZ_LOG(VFX, Error,
                   "VFXCurveLoader: key[{}] 'value' must be array of {} numbers",
                   i, expectedComponents);
            return std::nullopt;
        }

        const auto& arr = entry["value"];
        const float t = entry["time"].get<float>();
        const float x = arr[0].get<float>();
        const float y = arr[1].get<float>();
        const float z = arr[2].get<float>();
        const float w = (type == CurveType::Vec3) ? 0.0f : arr[3].get<float>();

        keys.push_back({t, Vec4{x, y, z, w}});
    }
    return keys;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// LoadFromJson
// ─────────────────────────────────────────────────────────────────────────────

std::optional<VFXCurve> LoadFromJson(const std::string& jsonString) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonString);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: JSON parse error: {}", e.what());
        return std::nullopt;
    }

    // Parse "type" field
    if (!root.contains("type") || !root["type"].is_string()) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: missing or invalid 'type' field");
        return std::nullopt;
    }
    const auto curveType = ParseCurveType(root["type"].get<std::string>());
    if (!curveType) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: unknown type '{}'",
               root["type"].get<std::string>());
        return std::nullopt;
    }

    // Parse "interpolation" field (defaults to linear if absent)
    InterpolationMode interp = InterpolationMode::Linear;
    if (root.contains("interpolation") && root["interpolation"].is_string()) {
        const auto parsed = ParseInterpolation(root["interpolation"].get<std::string>());
        if (!parsed) {
            UZ_LOG(VFX, Error, "VFXCurveLoader: unknown interpolation '{}'",
                   root["interpolation"].get<std::string>());
            return std::nullopt;
        }
        interp = *parsed;
    }

    // Parse "keys" array
    if (!root.contains("keys") || !root["keys"].is_array()) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: missing or invalid 'keys' array");
        return std::nullopt;
    }

    if (*curveType == CurveType::Float) {
        auto keys = ParseFloatKeys(root["keys"]);
        if (!keys) return std::nullopt;
        if (!ValidateFloatKeys(*keys)) return std::nullopt;
        return VFXCurve(std::move(*keys), interp);
    }

    // Vec3 or Vec4
    auto keys = ParseVec4Keys(root["keys"], *curveType);
    if (!keys) return std::nullopt;
    if (!ValidateVec4Keys(*keys)) return std::nullopt;
    return VFXCurve(std::move(*keys), *curveType, interp);
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadFromFile
// ─────────────────────────────────────────────────────────────────────────────

std::optional<VFXCurve> LoadFromFile(const std::string& path) {
    const std::string content = FileReadAllText(path.c_str());
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VFXCurveLoader: failed to read file '{}'", path);
        return std::nullopt;
    }
    return LoadFromJson(content);
}

} // namespace uz::VFXCurveLoader
