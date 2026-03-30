/// VFX Material implementation -- texture loading with flipbook auto-detection.

#include "engine/vfx/material/vfx_material.h"

#include "texture_loader.h"
#include "engine/logging/log.h"

#include <cstdlib>
#include <cstring>

UZ_DECLARE_LOG_CATEGORY(VFX);

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
// VFXMaterial::LoadTexture
// ─────────────────────────────────────────────────────────────────────────────

bool VFXMaterial::LoadTexture(IRHIDevice* device, IRHICommandList* cmdList) {
    if (spriteTexturePath.empty()) {
        UZ_LOG(VFX, Warning, "VFXMaterial::LoadTexture called with empty path");
        return false;
    }

    // Load via the synchronous TextureLoader pipeline (stb_image -> staging -> GPU).
    // isSRGB = false: particle sprites are authored in linear or use manual
    // gamma in the shader (emissive/additive blending operates in linear space).
    auto result = TextureLoader::Load(device, cmdList, spriteTexturePath, false);
    if (!result.handle.IsValid()) {
        UZ_LOG(VFX, Error, "VFXMaterial: Failed to load sprite texture '{}'",
               spriteTexturePath);
        return false;
    }

    spriteTextureSrvIndex = result.srvIndex;

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

    UZ_LOG(VFX, Info, "VFXMaterial: Loaded sprite '{}' (SRV={}, {}x{})",
           spriteTexturePath, spriteTextureSrvIndex,
           result.width, result.height);
    return true;
}

} // namespace uz
