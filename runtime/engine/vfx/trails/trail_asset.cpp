/// TrailAsset -- JSON deserialization and validation for .uztrail files.

#include "trail_asset.h"

#include "engine/logging/log.h"
#include "file_system.h"
#include "vfs.h"

#include <json.hpp>

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Parse Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Parses a JSON array of 4 numbers into a Vec4, clamping each component to [0, 1].
///
/// # Returns
/// The clamped Vec4, or std::nullopt if the array is missing, not an array,
/// not exactly 4 elements, or contains non-numeric elements.
[[nodiscard]] std::optional<Vec4> ParseColorVec4(const nlohmann::json& arr) {
    if (!arr.is_array() || arr.size() != 4) {
        return std::nullopt;
    }
    for (size_t i = 0; i < 4; ++i) {
        if (!arr[i].is_number()) {
            return std::nullopt;
        }
    }
    // Clamp all components to [0,1] — authors can supply out-of-range values
    // in tools without causing undefined rendering behaviour.
    return Vec4{
        std::clamp(arr[0].get<float>(), 0.0f, 1.0f),
        std::clamp(arr[1].get<float>(), 0.0f, 1.0f),
        std::clamp(arr[2].get<float>(), 0.0f, 1.0f),
        std::clamp(arr[3].get<float>(), 0.0f, 1.0f)
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// Core Parse + Validate
// ─────────────────────────────────────────────────────────────────────────────

/// Parses and validates a .uztrail JSON root object into a TrailAsset.
///
/// # Returns
/// Populated unique_ptr on success, nullptr on validation failure.
/// Logs all validation errors with context.
[[nodiscard]] std::unique_ptr<TrailAsset> ParseAndValidate(const nlohmann::json& root) {
    // -- format field --
    if (!root.contains("format") || !root["format"].is_string()) {
        UZ_LOG(VFX, Error, "TrailAsset: missing or invalid 'format' field");
        return nullptr;
    }
    if (root["format"].get<std::string>() != "uztrail") {
        UZ_LOG(VFX, Error, "TrailAsset: expected format 'uztrail', got '{}'",
               root["format"].get<std::string>());
        return nullptr;
    }

    // -- version field (informational, must be present) --
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        UZ_LOG(VFX, Error, "TrailAsset: missing or invalid 'version' field");
        return nullptr;
    }

    // -- material path (required) --
    if (!root.contains("material") || !root["material"].is_string()) {
        UZ_LOG(VFX, Error, "TrailAsset: missing or invalid 'material' field");
        return nullptr;
    }
    const auto materialPath = root["material"].get<std::string>();
    if (materialPath.empty()) {
        UZ_LOG(VFX, Error, "TrailAsset: 'material' path must not be empty");
        return nullptr;
    }

    // -- width_start (optional, default 0.5) --
    float widthStart = 0.5f;
    if (root.contains("width_start")) {
        if (!root["width_start"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'width_start' must be a number");
            return nullptr;
        }
        widthStart = root["width_start"].get<float>();
        if (widthStart <= 0.0f) {
            UZ_LOG(VFX, Error, "TrailAsset: 'width_start' must be > 0, got {}", widthStart);
            return nullptr;
        }
    }

    // -- width_end (optional, default 0.05) --
    float widthEnd = 0.05f;
    if (root.contains("width_end")) {
        if (!root["width_end"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'width_end' must be a number");
            return nullptr;
        }
        widthEnd = root["width_end"].get<float>();
        if (widthEnd < 0.0f) {
            UZ_LOG(VFX, Error, "TrailAsset: 'width_end' must be >= 0, got {}", widthEnd);
            return nullptr;
        }
    }

    // -- color_start (optional, default [1,1,1,1]) --
    Vec4 colorStart{1.0f, 1.0f, 1.0f, 1.0f};
    if (root.contains("color_start")) {
        const auto parsed = ParseColorVec4(root["color_start"]);
        if (!parsed) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'color_start' must be an array of 4 numbers");
            return nullptr;
        }
        colorStart = *parsed;
    }

    // -- color_end (optional, default [1,1,1,0]) --
    Vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
    if (root.contains("color_end")) {
        const auto parsed = ParseColorVec4(root["color_end"]);
        if (!parsed) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'color_end' must be an array of 4 numbers");
            return nullptr;
        }
        colorEnd = *parsed;
    }

    // -- uv_scroll_rate (optional, default 0.0) --
    float uvScrollRate = 0.0f;
    if (root.contains("uv_scroll_rate")) {
        if (!root["uv_scroll_rate"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'uv_scroll_rate' must be a number");
            return nullptr;
        }
        uvScrollRate = root["uv_scroll_rate"].get<float>();
    }

    // -- max_points (optional, default 100) --
    uint32_t maxPoints = 100;
    if (root.contains("max_points")) {
        if (!root["max_points"].is_number_integer()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'max_points' must be an integer");
            return nullptr;
        }
        const int rawMaxPoints = root["max_points"].get<int>();
        if (rawMaxPoints <= 0) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'max_points' must be > 0, got {}", rawMaxPoints);
            return nullptr;
        }
        maxPoints = static_cast<uint32_t>(rawMaxPoints);
    }

    // -- point_lifetime (optional, default 2.0) --
    float pointLifetime = 2.0f;
    if (root.contains("point_lifetime")) {
        if (!root["point_lifetime"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'point_lifetime' must be a number");
            return nullptr;
        }
        pointLifetime = root["point_lifetime"].get<float>();
        if (pointLifetime <= 0.0f) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'point_lifetime' must be > 0, got {}", pointLifetime);
            return nullptr;
        }
    }

    // -- min_point_distance (optional, default 0.1) --
    float minPointDistance = 0.1f;
    if (root.contains("min_point_distance")) {
        if (!root["min_point_distance"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'min_point_distance' must be a number");
            return nullptr;
        }
        minPointDistance = root["min_point_distance"].get<float>();
        if (minPointDistance <= 0.0f) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'min_point_distance' must be > 0, got {}", minPointDistance);
            return nullptr;
        }
    }

    // -- ribbon_strands (optional, default 1) --
    uint32_t ribbonStrands = 1;
    if (root.contains("ribbon_strands")) {
        if (!root["ribbon_strands"].is_number_integer()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'ribbon_strands' must be an integer");
            return nullptr;
        }
        const int rawStrands = root["ribbon_strands"].get<int>();
        if (rawStrands < 1) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'ribbon_strands' must be >= 1, got {}", rawStrands);
            return nullptr;
        }
        ribbonStrands = static_cast<uint32_t>(rawStrands);
    }

    // -- strand_spacing (optional, default 0.1) --
    float strandSpacing = 0.1f;
    if (root.contains("strand_spacing")) {
        if (!root["strand_spacing"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'strand_spacing' must be a number");
            return nullptr;
        }
        strandSpacing = root["strand_spacing"].get<float>();
        if (strandSpacing < 0.0f) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'strand_spacing' must be >= 0, got {}", strandSpacing);
            return nullptr;
        }
    }

    // -- sort_order (optional, default 0) --
    int sortOrder = 0;
    if (root.contains("sort_order")) {
        if (!root["sort_order"].is_number_integer()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'sort_order' must be an integer");
            return nullptr;
        }
        sortOrder = root["sort_order"].get<int>();
    }

    // -- uv_mode (optional, default "stretch") — phase 8.12.0 --
    VfxTrailUVMode uvMode = VfxTrailUVMode::Stretch;
    if (root.contains("uv_mode")) {
        if (!root["uv_mode"].is_string()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'uv_mode' must be a string");
            return nullptr;
        }
        const std::string uvModeStr = root["uv_mode"].get<std::string>();
        if (!VfxTrailUVModeFromString(uvModeStr, uvMode)) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: unknown uv_mode '{}' (expected stretch|tiled|per_ribbon)",
                   uvModeStr);
            return nullptr;
        }
    }

    // -- tiles_per_ribbon (optional, default 1.0) — phase 8.12.0 --
    float tilesPerRibbon = 1.0f;
    if (root.contains("tiles_per_ribbon")) {
        if (!root["tiles_per_ribbon"].is_number()) {
            UZ_LOG(VFX, Error, "TrailAsset: 'tiles_per_ribbon' must be a number");
            return nullptr;
        }
        tilesPerRibbon = root["tiles_per_ribbon"].get<float>();
        if (tilesPerRibbon <= 0.0f) {
            UZ_LOG(VFX, Error,
                   "TrailAsset: 'tiles_per_ribbon' must be > 0, got {}", tilesPerRibbon);
            return nullptr;
        }
    }

    // -- Assemble the asset --
    auto asset = std::make_unique<TrailAsset>();
    asset->materialPath      = materialPath;
    asset->widthStart        = widthStart;
    asset->widthEnd          = widthEnd;
    asset->colorStart        = colorStart;
    asset->colorEnd          = colorEnd;
    asset->uvScrollRate      = uvScrollRate;
    asset->maxPoints         = maxPoints;
    asset->pointLifetime     = pointLifetime;
    asset->minPointDistance  = minPointDistance;
    asset->ribbonStrands     = ribbonStrands;
    asset->strandSpacing     = strandSpacing;
    asset->sortOrder         = sortOrder;
    asset->uvMode            = uvMode;
    asset->tilesPerRibbon    = tilesPerRibbon;
    return asset;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// TrailAsset::LoadFromString
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<TrailAsset> TrailAsset::LoadFromString(const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "TrailAsset: JSON parse error: {}", e.what());
        return nullptr;
    }
    return ParseAndValidate(root);
}

// ─────────────────────────────────────────────────────────────────────────────
// TrailAsset::Load
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<TrailAsset> TrailAsset::Load(const std::string& path, IVFS* vfs) {
    // Read via VFS when available (supports .uzpak), fallback to disk.
    std::string content;
    if (vfs) {
        auto data = vfs->Read(path);
        content.assign(data.begin(), data.end());
    } else {
        content = FileReadAllText(path.c_str());
    }
    if (content.empty()) {
        UZ_LOG(VFX, Error, "TrailAsset: failed to read file '{}'", path);
        return nullptr;
    }

    auto asset = LoadFromString(content);
    if (asset) {
        asset->sourcePath = path;
    }
    return asset;
}

} // namespace uz
