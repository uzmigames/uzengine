#pragma once

/// UzEngine SDF Font Atlas Generator
///
/// Generates a multi-channel SDF atlas texture from a font file using msdfgen.
/// Each glyph is rendered as an MSDF into a packed atlas. The atlas texture is
/// RGBA8 where RGB encodes the signed distance field and A is set to 1.0 (opaque).
///
/// @ref Chlumsky 2015 "Shape Decomposition for Multi-Channel Distance Fields"
/// @ref msdfgen 1.13 — https://github.com/Chlumsky/msdfgen

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SDFGlyphInfo — Per-glyph atlas placement and metrics
// ─────────────────────────────────────────────────────────────────────────────

/// Glyph info stored in the atlas.
///
/// All pixel-space values are relative to the atlas coordinate system (top-left
/// origin). Metric values (bearing, advance) are in pixels at the atlas font
/// size and are used by the text layout system to position quads.
struct SDFGlyphInfo {
    uint32_t codepoint = 0; ///< Unicode codepoint
    float atlasX  = 0.f;   ///< Atlas position X (pixels, top-left of glyph region)
    float atlasY  = 0.f;   ///< Atlas position Y (pixels, top-left of glyph region)
    float atlasW  = 0.f;   ///< Glyph width in atlas (pixels, includes SDF padding)
    float atlasH  = 0.f;   ///< Glyph height in atlas (pixels, includes SDF padding)
    float bearingX = 0.f;  ///< Left side bearing (pixels, at atlas font size)
    float bearingY = 0.f;  ///< Top side bearing (pixels, baseline to top edge)
    float advance  = 0.f;  ///< Horizontal advance (pixels, at atlas font size)
};

// ─────────────────────────────────────────────────────────────────────────────
// SDFAtlasResult — Output of atlas generation
// ─────────────────────────────────────────────────────────────────────────────

/// Result of SDF atlas generation.
///
/// On success, `success` is true, `pixels` contains the RGBA8 atlas data, and
/// `glyphs` holds per-glyph placement info. On failure, `success` is false and
/// `error` describes what went wrong.
struct SDFAtlasResult {
    std::vector<uint8_t> pixels; ///< RGBA8 atlas texture data (width * height * 4 bytes)
    uint32_t width  = 0;        ///< Atlas width in pixels
    uint32_t height = 0;        ///< Atlas height in pixels
    float fontSize  = 0.f;      ///< Font size used for generation (pixels)
    float sdfRange  = 0.f;      ///< SDF pixel range (for shader screenPxRange)
    std::vector<SDFGlyphInfo> glyphs; ///< Per-glyph atlas info
    bool success = false;
    std::string error;
};

// ─────────────────────────────────────────────────────────────────────────────
// SDFAtlasOptions — Generation parameters
// ─────────────────────────────────────────────────────────────────────────────

/// Options for SDF atlas generation.
///
/// Default values produce a reasonable atlas for ASCII text rendering at
/// typical screen resolutions (1080p–4K).
struct SDFAtlasOptions {
    float    fontSize       = 48.0f;  ///< Font size in pixels for rasterization
    float    sdfRange       = 4.0f;   ///< SDF range in pixels (msdfgen pxRange)
    uint32_t atlasWidth     = 1024;   ///< Atlas texture width
    uint32_t atlasHeight    = 1024;   ///< Atlas texture height
    uint32_t padding        = 2;      ///< Padding between glyphs in texels
    uint32_t firstCodepoint = 32;     ///< First codepoint to include (default: space)
    uint32_t lastCodepoint  = 126;    ///< Last codepoint to include (default: tilde)
    double   angleThreshold = 3.0;    ///< Edge coloring angle threshold (radians)
};

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

/// Generate an MSDF font atlas from a font file.
///
/// Uses msdfgen to render each glyph as a multi-channel signed distance field,
/// packs them into a single RGBA8 atlas texture using a simple shelf/row packer.
///
/// Requires FontManager to be initialized before calling.
///
/// @param fontPath  Path to .ttf or .otf font file.
/// @param options   Atlas generation parameters.
/// @return Atlas result with pixel data and glyph info.
[[nodiscard]] SDFAtlasResult GenerateSDFAtlas(const std::string& fontPath,
                                              const SDFAtlasOptions& options = {});

} // namespace uz
