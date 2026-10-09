/// VFX Material implementation -- texture loading with flipbook auto-detection.

#include "engine/vfx/material/vfx_material.h"

#include "texture_loader.h"
#include "engine/logging/log.h"
#include "engine/paths/paths.h"
#include "file_system.h"

#include <json.hpp>

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <unordered_map>

UZ_DECLARE_LOG_CATEGORY(VFX);

// ─────────────────────────────────────────────────────────────────────────────
// Texture Cache — avoids duplicate loads when multiple emitters share a sprite
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Cached result from TextureLoader::Load.
struct CachedTexture {
    uint32_t srvIndex = UINT32_MAX;
    uint32_t width    = 0;
    uint32_t height   = 0;
};

/// Path-keyed texture cache. Static lifetime — entries persist until process exit.
/// Fine for a demo/editor; a shipping engine would use a ref-counted ResourceManager.
static std::unordered_map<std::string, CachedTexture> s_textureCache;

} // anonymous namespace

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Flipbook Grid Auto-Detection
// ─────────────────────────────────────────────────────────────────────────────

/// Scans a filename for a `_NxM_` or `_NxM.` pattern and extracts grid dims.
///
/// Examples:
///   "fireball_6x6_01.tga"  -> rows=6, cols=6
///   "smoke_4x4.png"        -> rows=4, cols=4
///   "spark_atlas.tga"      -> no match (returns false)
///
/// Uses manual scanning instead of std::regex to avoid the heavy runtime cost
/// of regex compilation (~100us per call on MSVC). This function is called once
/// per material load, but keeping it lean is good practice.
static bool DetectFlipbookGrid(const std::string& path,
                               uint32_t& outRows,
                               uint32_t& outCols) {
    // Search for pattern: underscore, digits, 'x', digits, underscore-or-dot
    // Walk backwards from the end to find the last occurrence.
    const char* str = path.c_str();
    const size_t len = path.size();

    for (size_t i = 0; i + 4 < len; ++i) {
        if (str[i] != '_') continue;

        // Parse first number after underscore
        size_t j = i + 1;
        if (j >= len || str[j] < '1' || str[j] > '9') continue;

        uint32_t rows = 0;
        while (j < len && str[j] >= '0' && str[j] <= '9') {
            rows = rows * 10 + static_cast<uint32_t>(str[j] - '0');
            ++j;
        }

        // Expect 'x' separator
        if (j >= len || str[j] != 'x') continue;
        ++j;

        // Parse second number
        if (j >= len || str[j] < '1' || str[j] > '9') continue;

        uint32_t cols = 0;
        while (j < len && str[j] >= '0' && str[j] <= '9') {
            cols = cols * 10 + static_cast<uint32_t>(str[j] - '0');
            ++j;
        }

        // Must be followed by underscore, dot, or end of string
        if (j < len && str[j] != '_' && str[j] != '.') continue;

        // Sanity: grid dimensions must be reasonable (1-64 per axis)
        if (rows == 0 || rows > 64 || cols == 0 || cols > 64) continue;

        outRows = rows;
        outCols = cols;
        return true;
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXMaterial::LoadTexture — convenience wrapper for TextureLoader
// ─────────────────────────────────────────────────────────────────────────────
//
// NOTE: This is a thin wrapper around the engine's TextureLoader. The only
// VFX-specific logic is flipbook auto-detection from the filename. In future,
// this should be replaced by MaterialManager integration where the texture
// path is set via MaterialInterface::SetTexturePath("Albedo", path) and
// loading happens async via the standard asset pipeline.

bool VFXMaterial::LoadTexture(IRHIDevice* device, IRHICommandList* cmdList,
                              bool isSRGB) {
    if (spriteTexturePath.empty()) {
        UZ_LOG(VFX, Warning, "VFXMaterial::LoadTexture called with empty path");
        return false;
    }

    // Check the texture cache first to avoid duplicate GPU uploads when
    // multiple emitters share the same sprite texture.
    const auto cacheIt = s_textureCache.find(spriteTexturePath);
    if (cacheIt != s_textureCache.end()) {
        spriteTextureSrvIndex = cacheIt->second.srvIndex;
        gpuLoaded = true;
        UZ_LOG(VFX, Debug,
               "VFXMaterial: Reusing cached sprite '{}' (SRV={})",
               spriteTexturePath, spriteTextureSrvIndex);
    } else {
        // Resolve the sprite path: try as-is first (honors absolute/CLI paths),
        // then fall back to exe-dir, then assets/ subdir under exe-dir. This
        // lets presets reference engine-relative paths like "vfx/foo.png"
        // while still allowing absolute paths from CLI.
        std::string resolvedPath = spriteTexturePath;
        if (!std::filesystem::exists(resolvedPath)) {
            auto p1 = uz::ResolveEngineResource(spriteTexturePath);
            if (std::filesystem::exists(p1)) {
                resolvedPath = p1.string();
            } else {
                auto p2 = uz::GetExeDir() / "assets" / spriteTexturePath;
                if (std::filesystem::exists(p2)) {
                    resolvedPath = p2.string();
                }
            }
        }
        auto result = TextureLoader::Load(device, cmdList, resolvedPath, isSRGB);
        if (!result.handle.IsValid()) {
            UZ_LOG(VFX, Error, "VFXMaterial: Failed to load sprite texture '{}'",
                   spriteTexturePath);
            return false;
        }

        spriteTextureSrvIndex = result.srvIndex;
        gpuLoaded = true;

        // Cache the loaded texture for future lookups.
        s_textureCache[spriteTexturePath] = CachedTexture{
            result.srvIndex, result.width, result.height
        };

        const bool hasAlpha = (result.originalChannels == 4);
        UZ_LOG(VFX, Info,
               "VFXMaterial: Loaded sprite '{}' (SRV={}, {}x{}, hasAlpha={}, sRGB={})",
               spriteTexturePath, spriteTextureSrvIndex,
               result.width, result.height, hasAlpha ? "yes" : "no",
               isSRGB ? "yes" : "no");
    }

    // Auto-detect flipbook grid from filename pattern (e.g., _6x6_ -> 6 rows, 6 cols).
    uint32_t detectedRows = 0;
    uint32_t detectedCols = 0;
    if (DetectFlipbookGrid(spriteTexturePath, detectedRows, detectedCols)) {
        flipbookRows = detectedRows;
        flipbookCols = detectedCols;
        flipbookTotalFrames = flipbookRows * flipbookCols;

        // Set sensible defaults if not already configured.
        if (flipbookFPS <= 0.0f) {
            flipbookFPS = 24.0f;
        }
        if (flipbookBlendMode == 0) {
            flipbookBlendMode = 1; // Linear interpolation between frames.
        }

        UZ_LOG(VFX, Info,
               "VFXMaterial: Auto-detected flipbook {}x{} ({} frames) from '{}'",
               flipbookRows, flipbookCols, flipbookTotalFrames, spriteTexturePath);
    }

    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// MaterialInterface overrides
// ─────────────────────────────────────────────────────────────────────────────

const std::string VFXMaterial::kEmptyString;

float VFXMaterial::GetScalarParam(const std::string& name, float defaultVal) const {
    if (name == "EmissiveIntensity") return emissiveIntensity;
    if (name == "Opacity") return 1.0f - alphaErosion;
    if (name == "SoftFadeDistance") return softFadeDistance;
    if (name == "AlphaErosion") return alphaErosion;
    if (name == "FlipbookFPS") return flipbookFPS;
    return defaultVal;
}

void VFXMaterial::GetVectorParam(const std::string& name,
                                  float& outR, float& outG, float& outB,
                                  float defaultR, float defaultG, float defaultB) const {
    if (name == "BaseColorTint") {
        outR = baseColorTintR; outG = baseColorTintG; outB = baseColorTintB;
        return;
    }
    if (name == "EmissiveColor") {
        outR = emissiveIntensity; outG = emissiveIntensity; outB = emissiveIntensity;
        return;
    }
    outR = defaultR; outG = defaultG; outB = defaultB;
}

const std::string& VFXMaterial::GetTexturePath(const std::string& name) const {
    if (name == "Albedo" || name == "Sprite") return spriteTexturePath;
    return kEmptyString;
}

uint32_t VFXMaterial::GetTextureSrvIndex(const std::string& name) const {
    if (name == "Albedo" || name == "Sprite") return spriteTextureSrvIndex;
    return 0;
}

EBlendMode VFXMaterial::GetBlendMode() const {
    switch (blendMode) {
        case 0: return EBlendMode::Translucent;
        case 1: return EBlendMode::Additive;
        case 2: return EBlendMode::Translucent; // PremulAlpha → Translucent
        default: return EBlendMode::Additive;
    }
}

EShadingModel VFXMaterial::GetShadingModel() const {
    return isUnlit ? EShadingModel::Unlit : EShadingModel::DefaultLit;
}

// ─────────────────────────────────────────────────────────────────────────────
// .uzvfxmat JSON — ParseAndValidate
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Parses and validates a .uzvfxmat JSON root object into a VFXMaterial.
///
/// # Returns
/// Populated unique_ptr on success, nullptr on validation failure.
/// Logs all validation errors with context.
[[nodiscard]] std::unique_ptr<VFXMaterial> ParseAndValidateVFXMat(const nlohmann::json& root) {
    // -- format field (required) --
    if (!root.contains("format") || !root["format"].is_string()) {
        UZ_LOG(VFX, Error, "VFXMaterial: missing or invalid 'format' field");
        return nullptr;
    }
    if (root["format"].get<std::string>() != "uzvfxmat") {
        UZ_LOG(VFX, Error, "VFXMaterial: expected format 'uzvfxmat', got '{}'",
               root["format"].get<std::string>());
        return nullptr;
    }

    // -- version field (required, informational) --
    if (!root.contains("version") || !root["version"].is_number_integer()) {
        UZ_LOG(VFX, Error, "VFXMaterial: missing or invalid 'version' field");
        return nullptr;
    }

    auto mat = std::make_unique<VFXMaterial>();

    // -- name (optional, informational — not stored on VFXMaterial) --
    // Silently ignored if present; useful for human readability in the JSON.

    // -- sprite_texture (optional, default empty) --
    if (root.contains("sprite_texture")) {
        if (!root["sprite_texture"].is_string()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'sprite_texture' must be a string");
            return nullptr;
        }
        mat->spriteTexturePath = root["sprite_texture"].get<std::string>();
    }

    // -- blend_mode (optional, default 1 = Additive) --
    if (root.contains("blend_mode")) {
        if (!root["blend_mode"].is_number_integer()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'blend_mode' must be an integer");
            return nullptr;
        }
        const int rawBlendMode = root["blend_mode"].get<int>();
        if (rawBlendMode < 0 || rawBlendMode > 2) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'blend_mode' must be 0 (AlphaBlend), 1 (Additive), or 2 (PremulAlpha), got {}",
                   rawBlendMode);
            return nullptr;
        }
        mat->blendMode = static_cast<uint32_t>(rawBlendMode);
    }

    // -- emissive_intensity (optional, default 1.0) --
    if (root.contains("emissive_intensity")) {
        if (!root["emissive_intensity"].is_number()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'emissive_intensity' must be a number");
            return nullptr;
        }
        mat->emissiveIntensity = root["emissive_intensity"].get<float>();
        if (mat->emissiveIntensity < 0.0f) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'emissive_intensity' must be >= 0, got {}",
                   mat->emissiveIntensity);
            return nullptr;
        }
    }

    // -- soft_fade_distance (optional, default 100.0) --
    if (root.contains("soft_fade_distance")) {
        if (!root["soft_fade_distance"].is_number()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'soft_fade_distance' must be a number");
            return nullptr;
        }
        mat->softFadeDistance = root["soft_fade_distance"].get<float>();
        if (mat->softFadeDistance < 0.0f) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'soft_fade_distance' must be >= 0, got {}",
                   mat->softFadeDistance);
            return nullptr;
        }
    }

    // -- alpha_erosion (optional, default 0.0) --
    if (root.contains("alpha_erosion")) {
        if (!root["alpha_erosion"].is_number()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'alpha_erosion' must be a number");
            return nullptr;
        }
        mat->alphaErosion = root["alpha_erosion"].get<float>();
        if (mat->alphaErosion < 0.0f || mat->alphaErosion > 1.0f) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'alpha_erosion' must be in [0, 1], got {}",
                   mat->alphaErosion);
            return nullptr;
        }
    }

    // -- flipbook_rows (optional, default 1) --
    if (root.contains("flipbook_rows")) {
        if (!root["flipbook_rows"].is_number_integer()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'flipbook_rows' must be an integer");
            return nullptr;
        }
        const int rawRows = root["flipbook_rows"].get<int>();
        if (rawRows < 1 || rawRows > 64) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'flipbook_rows' must be in [1, 64], got {}", rawRows);
            return nullptr;
        }
        mat->flipbookRows = static_cast<uint32_t>(rawRows);
    }

    // -- flipbook_cols (optional, default 1) --
    if (root.contains("flipbook_cols")) {
        if (!root["flipbook_cols"].is_number_integer()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'flipbook_cols' must be an integer");
            return nullptr;
        }
        const int rawCols = root["flipbook_cols"].get<int>();
        if (rawCols < 1 || rawCols > 64) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'flipbook_cols' must be in [1, 64], got {}", rawCols);
            return nullptr;
        }
        mat->flipbookCols = static_cast<uint32_t>(rawCols);
    }

    // -- flipbook_total_frames (optional, default 1) --
    if (root.contains("flipbook_total_frames")) {
        if (!root["flipbook_total_frames"].is_number_integer()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'flipbook_total_frames' must be an integer");
            return nullptr;
        }
        const int rawFrames = root["flipbook_total_frames"].get<int>();
        if (rawFrames < 1) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'flipbook_total_frames' must be >= 1, got {}", rawFrames);
            return nullptr;
        }
        mat->flipbookTotalFrames = static_cast<uint32_t>(rawFrames);
    }

    // -- flipbook_fps (optional, default 0.0) --
    if (root.contains("flipbook_fps")) {
        if (!root["flipbook_fps"].is_number()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'flipbook_fps' must be a number");
            return nullptr;
        }
        mat->flipbookFPS = root["flipbook_fps"].get<float>();
        if (mat->flipbookFPS < 0.0f) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'flipbook_fps' must be >= 0, got {}", mat->flipbookFPS);
            return nullptr;
        }
    }

    // -- flipbook_blend_mode (optional, default 0 = None) --
    if (root.contains("flipbook_blend_mode")) {
        if (!root["flipbook_blend_mode"].is_number_integer()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'flipbook_blend_mode' must be an integer");
            return nullptr;
        }
        const int rawFbBlend = root["flipbook_blend_mode"].get<int>();
        if (rawFbBlend < 0 || rawFbBlend > 2) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'flipbook_blend_mode' must be 0 (None), 1 (Linear), or 2 (Random), got {}",
                   rawFbBlend);
            return nullptr;
        }
        mat->flipbookBlendMode = static_cast<uint32_t>(rawFbBlend);
    }

    // -- base_color_tint (optional, default [1,1,1]) --
    if (root.contains("base_color_tint")) {
        const auto& arr = root["base_color_tint"];
        if (!arr.is_array() || arr.size() != 3) {
            UZ_LOG(VFX, Error,
                   "VFXMaterial: 'base_color_tint' must be an array of 3 numbers");
            return nullptr;
        }
        for (size_t i = 0; i < 3; ++i) {
            if (!arr[i].is_number()) {
                UZ_LOG(VFX, Error,
                       "VFXMaterial: 'base_color_tint[{}]' must be a number", i);
                return nullptr;
            }
        }
        mat->baseColorTintR = arr[0].get<float>();
        mat->baseColorTintG = arr[1].get<float>();
        mat->baseColorTintB = arr[2].get<float>();
    }

    // -- is_unlit (optional, default true) --
    if (root.contains("is_unlit")) {
        if (!root["is_unlit"].is_boolean()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'is_unlit' must be a boolean");
            return nullptr;
        }
        mat->isUnlit = root["is_unlit"].get<bool>();
    }

    // -- is_two_sided (optional, default true) --
    if (root.contains("is_two_sided")) {
        if (!root["is_two_sided"].is_boolean()) {
            UZ_LOG(VFX, Error, "VFXMaterial: 'is_two_sided' must be a boolean");
            return nullptr;
        }
        mat->isTwoSided = root["is_two_sided"].get<bool>();
    }

    return mat;
}

} // anonymous namespace (ParseAndValidateVFXMat)

// ─────────────────────────────────────────────────────────────────────────────
// VFXMaterial::LoadFromString
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VFXMaterial> VFXMaterial::LoadFromString(const std::string& jsonStr) {
    nlohmann::json root;
    try {
        root = nlohmann::json::parse(jsonStr);
    } catch (const nlohmann::json::parse_error& e) {
        UZ_LOG(VFX, Error, "VFXMaterial: JSON parse error: {}", e.what());
        return nullptr;
    }
    return ParseAndValidateVFXMat(root);
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXMaterial::LoadFromFile
// ─────────────────────────────────────────────────────────────────────────────

std::unique_ptr<VFXMaterial> VFXMaterial::LoadFromFile(const std::string& path) {
    const std::string content = FileReadAllText(path.c_str());
    if (content.empty()) {
        UZ_LOG(VFX, Error, "VFXMaterial: failed to read file '{}'", path);
        return nullptr;
    }

    auto mat = LoadFromString(content);
    if (mat) {
        UZ_LOG(VFX, Info, "VFXMaterial: loaded from '{}'", path);
    }
    return mat;
}

// ─────────────────────────────────────────────────────────────────────────────
// VFXMaterial::SaveToFile
// ─────────────────────────────────────────────────────────────────────────────

bool VFXMaterial::SaveToFile(const std::string& path) const {
    nlohmann::json root;
    root["format"]  = "uzvfxmat";
    root["version"] = 1;

    if (!spriteTexturePath.empty()) {
        root["sprite_texture"] = spriteTexturePath;
    }
    root["blend_mode"]          = blendMode;
    root["emissive_intensity"]  = emissiveIntensity;
    root["soft_fade_distance"]  = softFadeDistance;
    root["alpha_erosion"]       = alphaErosion;
    root["flipbook_rows"]       = flipbookRows;
    root["flipbook_cols"]       = flipbookCols;
    root["flipbook_total_frames"] = flipbookTotalFrames;
    root["flipbook_fps"]        = flipbookFPS;
    root["flipbook_blend_mode"] = flipbookBlendMode;
    root["base_color_tint"]     = { baseColorTintR, baseColorTintG, baseColorTintB };
    root["is_unlit"]            = isUnlit;
    root["is_two_sided"]        = isTwoSided;

    std::ofstream ofs(path, std::ios::out | std::ios::trunc);
    if (!ofs.is_open()) {
        UZ_LOG(VFX, Error, "VFXMaterial: failed to open '{}' for writing", path);
        return false;
    }

    ofs << root.dump(4) << '\n';
    if (ofs.fail()) {
        UZ_LOG(VFX, Error, "VFXMaterial: write error on '{}'", path);
        return false;
    }

    UZ_LOG(VFX, Info, "VFXMaterial: saved to '{}'", path);
    return true;
}

} // namespace uz
