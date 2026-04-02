/// DecalAsset -- JSON deserialization and validation for .uzdecal files.

#include "decal_asset.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>
#include <optional>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Parse Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Maps a blend mode string to its enum value.
///
/// # Returns
/// The corresponding DecalBlendMode, or std::nullopt for unrecognized strings.
[[nodiscard]] std::optional<DecalBlendMode> ParseBlendMode(const std::string& str) {
    if (str == "Opaque")   return DecalBlendMode::Opaque;
    if (str == "Additive") return DecalBlendMode::Additive;
    if (str == "Multiply") return DecalBlendMode::Multiply;
    if (str == "Screen")   return DecalBlendMode::Screen;
    return std::nullopt;
}

/// Parses a JSON array of 3 numbers into a Vec3.
///
/// # Returns
/// The parsed Vec3, or std::nullopt if the array is malformed.
[[nodiscard]] std::optional<Vec3> ParseVec3(const nlohmann::json& arr) {
    if (!arr.is_array() || arr.size() != 3) {
        return std::nullopt;
    }
    for (size_t i = 0; i < 3; ++i) {
        if (!arr[i].is_number()) {
            return std::nullopt;
        }
    }
    return Vec3{arr[0].get<float>(), arr[1].get<float>(), arr[2].get<float>()};
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Parse + Validate
// ─────────────────────────────────────────────────────────────────────────────

/// Parses and validates a .uzdecal JSON root object into a DecalAsset.
///
/// # Returns
/// Populated unique_ptr on success, nullptr on validation failure.
/// Logs all validation errors with context.
[[nodiscard]] std::unique_ptr<DecalAsset> ParseAndValidate(const nlohmann::json& root) {
    // -- format field --
    if (!root.contains("format") || !root["format"].is_string()) {
        UZ_LOG(VFX, Error, "DecalAsset: missing or invalid 'format' field");
        return nullptr;
    }
    if (root["format"].get<std::string>() != "uzdecal") {
        UZ_LOG(VFX, Error, "DecalAsset: expected format 'uzdecal', got '{}'",
               root["format"].get<std::string>());
        return nullptr;
    }

    // -- version field (informational, must be present) --
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        UZ_LOG(VFX, Error, "DecalAsset: missing or invalid 'version' field");
        return nullptr;
    }

    // -- material path --
    if (!root.contains("material") || !root["material"].is_string()) {
        UZ_LOG(VFX, Error, "DecalAsset: missing or invalid 'material' field");
        return nullptr;
    }
    const auto materialPath = root["material"].get<std::string>();
    if (materialPath.empty()) {
        UZ_LOG(VFX, Error, "DecalAsset: 'material' path must not be empty");
        return nullptr;
    }

    // -- blend_mode (optional, defaults to Opaque) --
    DecalBlendMode blendMode = DecalBlendMode::Opaque;
    if (root.contains("blend_mode")) {
        if (!root["blend_mode"].is_string()) {
            UZ_LOG(VFX, Error, "DecalAsset: 'blend_mode' must be a string");
            return nullptr;
        }
        const auto parsed = ParseBlendMode(root["blend_mode"].get<std::string>());
        if (!parsed) {
            UZ_LOG(VFX, Error, "DecalAsset: unknown blend_mode '{}'",
                   root["blend_mode"].get<std::string>());
            return nullptr;
        }
        blendMode = *parsed;
    }

    // -- lifetime (optional, default 10.0) --
    float lifetime = 10.0f;
    if (root.contains("lifetime")) {
        if (!root["lifetime"].is_number()) {
            UZ_LOG(VFX, Error, "DecalAsset: 'lifetime' must be a number");
            return nullptr;
        }
        lifetime = root["lifetime"].get<float>();
        if (lifetime <= 0.0f) {
            UZ_LOG(VFX, Error, "DecalAsset: 'lifetime' must be > 0, got {}", lifetime);
            return nullptr;
        }
    }

    // -- fade_duration (optional, default 2.0) --
    float fadeDuration = 2.0f;
    if (root.contains("fade_duration")) {
        if (!root["fade_duration"].is_number()) {
            UZ_LOG(VFX, Error, "DecalAsset: 'fade_duration' must be a number");
            return nullptr;
        }
        fadeDuration = root["fade_duration"].get<float>();
    }
    // Clamp to [0, lifetime] -- negative fade makes no sense, and fade > lifetime
    // would mean the decal is always fading.
    fadeDuration = std::clamp(fadeDuration, 0.0f, lifetime);

    // -- normal_intensity (optional, default 0.8) --
    float normalIntensity = 0.8f;
    if (root.contains("normal_intensity")) {
        if (!root["normal_intensity"].is_number()) {
            UZ_LOG(VFX, Error, "DecalAsset: 'normal_intensity' must be a number");
            return nullptr;
        }
        normalIntensity = root["normal_intensity"].get<float>();
        // Clamp silently -- out-of-range is not an error, just a bad authored value
        normalIntensity = std::clamp(normalIntensity, 0.0f, 1.0f);
    }

    // -- roughness_override (optional, default -1.0) --
    float roughnessOverride = -1.0f;
    if (root.contains("roughness_override")) {
        if (!root["roughness_override"].is_number()) {
            UZ_LOG(VFX, Error, "DecalAsset: 'roughness_override' must be a number");
            return nullptr;
        }
        roughnessOverride = root["roughness_override"].get<float>();
    }

    // -- sort_order (optional, default 0) --
    int sortOrder = 0;
    if (root.contains("sort_order")) {
        if (!root["sort_order"].is_number_integer()) {
            UZ_LOG(VFX, Error, "DecalAsset: 'sort_order' must be an integer");
            return nullptr;
        }
        sortOrder = root["sort_order"].get<int>();
    }

    // -- half_extents (optional, default [0.5, 0.5, 0.2]) --
    Vec3 halfExtents{0.5f, 0.5f, 0.2f};
    if (root.contains("half_extents")) {
        const auto parsed = ParseVec3(root["half_extents"]);
        if (!parsed) {
            UZ_LOG(VFX, Error,
                   "DecalAsset: 'half_extents' must be an array of 3 numbers");
            return nullptr;
        }
        halfExtents = *parsed;
        if (halfExtents.x <= 0.0f || halfExtents.y <= 0.0f || halfExtents.z <= 0.0f) {
            UZ_LOG(VFX, Error,
                   "DecalAsset: 'half_extents' components must all be > 0, got [{}, {}, {}]",
                   halfExtents.x, halfExtents.y, halfExtents.z);
            return nullptr;
        }
    }

    // -- Assemble the asset --
    auto asset = std::make_unique<DecalAsset>();
    asset->materialPath     = materialPath;
    asset->blendMode        = blendMode;
    asset->lifetime         = lifetime;
    asset->fadeDuration     = fadeDuration;
    asset->normalIntensity  = normalIntensity;
    asset->roughnessOverride = roughnessOverride;
    asset->sortOrder        = sortOrder;
    asset->halfExtents      = halfExtents;
    return asset;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// DecalAsset::LoadFromString
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<DecalAsset> DecalAsset::LoadFromString(const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "DecalAsset: JSON parse error: {}", e.what());
        return nullptr;
    }
    return ParseAndValidate(root);
}

// ─────────────────────────────────────────────────────────────────────────────
// DecalAsset::Load
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<DecalAsset> DecalAsset::Load(const std::string& path, IVFS* vfs) {
    // Read via VFS when available (supports .uzpak), fallback to disk.
    std::string content;
    if (vfs) {
        auto data = vfs->Read(path);
        content.assign(data.begin(), data.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "DecalAsset: failed to read file '{}'", path);
        return nullptr;
    }

    auto asset = LoadFromString(content);
    if (asset) {
        asset->sourcePath = path;
    }
    return asset;
}

} // namespace uz
