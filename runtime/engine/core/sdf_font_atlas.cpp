/// UzEngine SDF Font Atlas Generator — Implementation
///
/// @ref Chlumsky 2015 "Shape Decomposition for Multi-Channel Distance Fields"
/// @ref msdfgen 1.13 — https://github.com/Chlumsky/msdfgen

#include "sdf_font_atlas.h"
#include "font_manager.h"

// FreeType headers must come before msdfgen ext headers so that FT_LOAD_DEFAULT
// is defined, enabling the adoptFreetypeFont() overload in import-font.h.
#include <ft2build.h>
#include FT_FREETYPE_H

// MSDFGEN_PUBLIC must be defined before including msdfgen headers to bypass the
// CMake-generated config header (msdfgen-config.h) which does not exist in our
// Zig build. The thirdparty .cpp files get this via -DMSDFGEN_PUBLIC= compiler
// flag, but engine code including the headers must define it explicitly.
#ifndef MSDFGEN_PUBLIC
#define MSDFGEN_PUBLIC
#endif

#include <msdfgen.h>
#include <msdfgen-ext.h>

#include "../logging/log.h"

#include <algorithm>
#include <cmath>
#include <cstring>

UZ_DEFINE_LOG_CATEGORY(SDFAtlas, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Shelf Packer — simple row-based atlas packer
// ─────────────────────────────────────────────────────────────────────────────

/// Row-based (shelf) atlas packer.
///
/// Glyphs are placed left-to-right along a "shelf" (row). When a glyph does
/// not fit horizontally, a new shelf is started below the current one. The
/// shelf height is the tallest glyph placed on that row plus padding.
///
/// This is intentionally simple — it wastes some vertical space but is fast,
/// deterministic, and sufficient for font atlases where glyphs are similarly
/// sized.
class ShelfPacker {
public:
    ShelfPacker(uint32_t atlasW, uint32_t atlasH, uint32_t pad)
        : m_atlasW(atlasW)
        , m_atlasH(atlasH)
        , m_padding(pad)
        , m_cursorX(pad)
        , m_cursorY(pad)
        , m_shelfHeight(0) {}

    /// Try to place a glyph of the given dimensions.
    /// On success, writes the top-left position into outX/outY and returns true.
    /// On failure (no room left), returns false.
    [[nodiscard]] bool Place(uint32_t glyphW, uint32_t glyphH,
                             uint32_t& outX, uint32_t& outY) {
        // Would this glyph exceed the right edge of the current shelf?
        if (m_cursorX + glyphW + m_padding > m_atlasW) {
            // Start a new shelf below the current one.
            m_cursorX = m_padding;
            m_cursorY += m_shelfHeight + m_padding;
            m_shelfHeight = 0;
        }

        // Would this glyph exceed the bottom edge of the atlas?
        if (m_cursorY + glyphH + m_padding > m_atlasH) {
            return false;
        }

        outX = m_cursorX;
        outY = m_cursorY;

        m_cursorX += glyphW + m_padding;
        m_shelfHeight = std::max(m_shelfHeight, glyphH);
        return true;
    }

private:
    uint32_t m_atlasW;
    uint32_t m_atlasH;
    uint32_t m_padding;
    uint32_t m_cursorX;
    uint32_t m_cursorY;
    uint32_t m_shelfHeight;
};

// ─────────────────────────────────────────────────────────────────────────────
// GenerateSDFAtlas
// ─────────────────────────────────────────────────────────────────────────────

SDFAtlasResult GenerateSDFAtlas(const std::string& fontPath,
                                const SDFAtlasOptions& options) {
    SDFAtlasResult result;
    result.width    = options.atlasWidth;
    result.height   = options.atlasHeight;
    result.fontSize = options.fontSize;
    result.sdfRange = options.sdfRange;

    // ── Validate options ─────────────────────────────────────────────────
    if (options.atlasWidth == 0 || options.atlasHeight == 0) {
        result.error = "Atlas dimensions must be non-zero";
        return result;
    }
    if (options.fontSize <= 0.f) {
        result.error = "Font size must be positive";
        return result;
    }
    if (options.sdfRange <= 0.f) {
        result.error = "SDF range must be positive";
        return result;
    }
    if (options.firstCodepoint > options.lastCodepoint) {
        result.error = "firstCodepoint must be <= lastCodepoint";
        return result;
    }

    // ── Load font via FontManager ────────────────────────────────────────
    auto& fontManager = FontManager::Get();
    if (!fontManager.Init()) {
        result.error = "Failed to initialize FontManager/FreeType";
        return result;
    }

    // FontManager loads at the requested pixel size (needed for glyph metrics).
    const FontHandle fontHandle = fontManager.LoadFont(fontPath, options.fontSize);
    if (fontHandle == kInvalidFont) {
        result.error = "Failed to load font: " + fontPath;
        return result;
    }

    FT_Face ftFace = fontManager.GetFace(fontHandle);
    if (!ftFace) {
        result.error = "Failed to retrieve FT_Face for font: " + fontPath;
        return result;
    }

    // ── Wrap FT_Face for msdfgen ─────────────────────────────────────────
    // adoptFreetypeFont() creates an msdfgen FontHandle that references our
    // FT_Face without taking ownership. destroyFont() must still be called
    // but it will NOT call FT_Done_Face — FontManager owns the face.
    msdfgen::FontHandle* msdfFont = msdfgen::adoptFreetypeFont(ftFace);
    if (!msdfFont) {
        result.error = "msdfgen::adoptFreetypeFont() failed";
        return result;
    }

    // ── Get font metrics from msdfgen (in EM-normalized coordinates) ─────
    msdfgen::FontMetrics fontMetrics{};
    if (!msdfgen::getFontMetrics(fontMetrics, msdfFont,
                                 msdfgen::FONT_SCALING_EM_NORMALIZED)) {
        msdfgen::destroyFont(msdfFont);
        result.error = "Failed to get font metrics";
        return result;
    }

    // Scale from EM-normalized coordinates to pixel coordinates.
    // In EM-normalized space, 1.0 = 1 em. Multiply by fontSize to get pixels.
    const double emToPixels = static_cast<double>(options.fontSize);

    // The SDF range in EM-normalized space — needed for the msdfgen projection.
    const double sdfRangeEm = static_cast<double>(options.sdfRange) / emToPixels;

    // ── Allocate atlas (RGBA8, initialized to transparent black) ─────────
    const size_t pixelCount = static_cast<size_t>(options.atlasWidth) *
                              static_cast<size_t>(options.atlasHeight) * 4;
    result.pixels.resize(pixelCount, 0);

    ShelfPacker packer(options.atlasWidth, options.atlasHeight, options.padding);

    // ── Generate MSDF for each codepoint ─────────────────────────────────
    for (uint32_t cp = options.firstCodepoint; cp <= options.lastCodepoint; ++cp) {
        msdfgen::Shape shape;
        double advance = 0.0;

        // Load glyph shape in EM-normalized coordinates.
        if (!msdfgen::loadGlyph(shape, msdfFont, static_cast<msdfgen::unicode_t>(cp),
                                msdfgen::FONT_SCALING_EM_NORMALIZED, &advance)) {
            // Glyph not in font — skip silently (common for control chars).
            continue;
        }

        // Space character (U+0020) and similar whitespace have no contours.
        // We still record the advance but skip MSDF generation.
        if (shape.contours.empty()) {
            SDFGlyphInfo info{};
            info.codepoint = cp;
            info.advance = static_cast<float>(advance * emToPixels);
            result.glyphs.push_back(info);
            continue;
        }

        // Normalize the shape for distance field generation.
        shape.normalize();

        // Assign edge colors for multi-channel distance fields.
        // edgeColoringSimple is the standard robust algorithm for font glyphs.
        // angleThreshold of 3.0 radians (~172 degrees) is the msdfgen default.
        // edgeColoringByDistance can produce better results for complex shapes
        // but has edge cases with degenerate contours that cause crashes.
        msdfgen::edgeColoringSimple(shape, options.angleThreshold, 0);

        // Compute shape bounds in EM-normalized coordinates.
        const msdfgen::Shape::Bounds bounds = shape.getBounds();

        // Compute glyph bitmap dimensions in pixels.
        // We need enough pixels to cover the shape bounds plus SDF range on
        // each side (so the distance field extends beyond the glyph outline).
        const double glyphWidthEm  = bounds.r - bounds.l;
        const double glyphHeightEm = bounds.t - bounds.b;

        const int glyphW = static_cast<int>(
            std::ceil((glyphWidthEm + 2.0 * sdfRangeEm) * emToPixels));
        const int glyphH = static_cast<int>(
            std::ceil((glyphHeightEm + 2.0 * sdfRangeEm) * emToPixels));

        if (glyphW <= 0 || glyphH <= 0) {
            // Degenerate glyph (zero area). Record advance only.
            SDFGlyphInfo info{};
            info.codepoint = cp;
            info.advance = static_cast<float>(advance * emToPixels);
            result.glyphs.push_back(info);
            continue;
        }

        // ── Compute projection (modern SDFTransformation API) ─────────────
        //
        // The Projection maps from shape (EM-normalized) coordinates to pixel
        // coordinates in the per-glyph bitmap.
        //
        // scale: emToPixels in both axes (uniform scaling from EM to pixels)
        // translate: offset so that (bounds.l - sdfRangeEm) maps to pixel 0.
        //
        // Projection formula: pixel = shape * scale + translate
        //   For X: 0 = (bounds.l - sdfRangeEm) * emToPixels + translateX
        //          translateX = -(bounds.l - sdfRangeEm) * emToPixels
        //   For Y: 0 = (bounds.b - sdfRangeEm) * emToPixels + translateY
        //          translateY = -(bounds.b - sdfRangeEm) * emToPixels
        const msdfgen::Projection projection(
            msdfgen::Vector2(emToPixels, emToPixels),
            msdfgen::Vector2(
                -(bounds.l - sdfRangeEm) * emToPixels,
                -(bounds.b - sdfRangeEm) * emToPixels));

        // DistanceMapping converts signed distance (in pixel space after
        // projection) to [0,1] range. Range specifies the pixel distance
        // interval. DistanceMapping::inverse(Range) creates the mapping from
        // pixel-space distance to [0,1].
        const msdfgen::Range pxRange(static_cast<double>(options.sdfRange));
        const msdfgen::SDFTransformation transform(
            projection,
            msdfgen::DistanceMapping(pxRange));

        // ── Generate MSDF bitmap (3-channel float) ───────────────────────
        msdfgen::Bitmap<float, 3> msdfBitmap(glyphW, glyphH);
        msdfgen::generateMSDF(msdfBitmap, shape, transform);

        // ── Pack into atlas ──────────────────────────────────────────────
        uint32_t atlasX = 0;
        uint32_t atlasY = 0;
        if (!packer.Place(static_cast<uint32_t>(glyphW),
                          static_cast<uint32_t>(glyphH), atlasX, atlasY)) {
            UZ_LOG(SDFAtlas, Warning,
                   "Atlas full at codepoint U+{:04X} — some glyphs will be missing", cp);
            break;
        }

        // ── Copy MSDF bitmap to atlas RGBA8 ──────────────────────────────
        // msdfgen Bitmap pixel layout: operator()(x, y) returns float* to
        // N channels at column x, row y. Y=0 is the bottom row by default
        // (Y_UPWARD orientation). We flip vertically when copying to the
        // atlas so that Y=0 is the top row (standard texture convention).
        for (int y = 0; y < glyphH; ++y) {
            for (int x = 0; x < glyphW; ++x) {
                // Flip Y: msdfgen Y=0 is bottom, atlas Y=0 is top.
                const float* pixel = msdfBitmap(x, glyphH - 1 - y);

                // Clamp [0, 1] and convert to uint8.
                const auto toU8 = [](float v) -> uint8_t {
                    return static_cast<uint8_t>(
                        std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
                };

                const size_t dstIdx =
                    (static_cast<size_t>(atlasY + y) * options.atlasWidth +
                     static_cast<size_t>(atlasX + x)) *
                    4;

                result.pixels[dstIdx + 0] = toU8(pixel[0]); // R
                result.pixels[dstIdx + 1] = toU8(pixel[1]); // G
                result.pixels[dstIdx + 2] = toU8(pixel[2]); // B
                result.pixels[dstIdx + 3] = 255;            // A = opaque
            }
        }

        // ── Record glyph info ────────────────────────────────────────────
        // Bearing values: the SDF bitmap includes sdfRange padding around the
        // actual glyph outline. We compute bearings relative to the original
        // glyph bounds (excluding SDF padding).
        SDFGlyphInfo info{};
        info.codepoint = cp;
        info.atlasX = static_cast<float>(atlasX);
        info.atlasY = static_cast<float>(atlasY);
        info.atlasW = static_cast<float>(glyphW);
        info.atlasH = static_cast<float>(glyphH);
        // bearingX: horizontal distance from pen position to glyph left edge.
        // In EM-normalized coords, bounds.l is the left edge relative to origin.
        // Convert to pixels. Subtract sdfRange because the atlas region starts
        // sdfRange pixels to the left of the glyph edge.
        info.bearingX = static_cast<float>(bounds.l * emToPixels -
                                           options.sdfRange);
        // bearingY: vertical distance from baseline to glyph top edge.
        // bounds.t is the top edge in EM-normalized (Y-up) coords.
        // Add sdfRange because the atlas region extends sdfRange above the top.
        info.bearingY = static_cast<float>(bounds.t * emToPixels +
                                           options.sdfRange);
        info.advance = static_cast<float>(advance * emToPixels);
        result.glyphs.push_back(info);
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    // destroyFont does NOT call FT_Done_Face for adopted fonts.
    msdfgen::destroyFont(msdfFont);

    result.success = true;
    UZ_LOG(SDFAtlas, Info, "Generated MSDF atlas: {}x{}, {} glyphs, fontSize={}, sdfRange={}",
           options.atlasWidth, options.atlasHeight, result.glyphs.size(),
           options.fontSize, options.sdfRange);

    return result;
}

} // namespace uz
