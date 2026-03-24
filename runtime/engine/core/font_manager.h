#pragma once

/// UzEngine Font Manager — FreeType wrapper for font loading and glyph metrics.
///
/// Singleton that initializes FreeType once and caches loaded font faces.
/// Provides glyph outline extraction for msdfgen SDF atlas generation and
/// glyph metric queries for text layout.
///
/// @ref FreeType 2.13.3 — https://freetype.org/freetype2/docs/reference/

// Forward-declare FreeType opaque pointer types so consumers of this header
// do not need to include FreeType headers. Consumers that need the full
// FT_Face definition (e.g., msdfgen) include <ft2build.h> themselves.
typedef struct FT_LibraryRec_* FT_Library;
typedef struct FT_FaceRec_* FT_Face;

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GlyphMetrics — Per-glyph measurement data
// ─────────────────────────────────────────────────────────────────────────────

/// Glyph metrics extracted from a font face at a specific pixel size.
///
/// All values are in pixels (FreeType 26.6 fixed-point converted to float).
struct GlyphMetrics {
    uint32_t glyphIndex = 0; ///< FreeType glyph index (0 = .notdef)
    float advanceX = 0.f;    ///< Horizontal advance width in pixels
    float bearingX = 0.f;    ///< Left side bearing in pixels (origin to left edge)
    float bearingY = 0.f;    ///< Top side bearing in pixels (baseline to top edge)
    float width = 0.f;       ///< Glyph bounding box width in pixels
    float height = 0.f;      ///< Glyph bounding box height in pixels
};

// ─────────────────────────────────────────────────────────────────────────────
// FontHandle — Opaque ID for loaded fonts
// ─────────────────────────────────────────────────────────────────────────────

/// Opaque handle returned by LoadFont(). Pass to metric query functions.
using FontHandle = uint32_t;

/// Sentinel value indicating a failed or invalid font load.
static constexpr FontHandle kInvalidFont = UINT32_MAX;

// ─────────────────────────────────────────────────────────────────────────────
// FontManager — Singleton FreeType wrapper
// ─────────────────────────────────────────────────────────────────────────────

/// Singleton font manager wrapping FreeType.
///
/// Manages the FreeType library lifetime and caches loaded font faces.
/// NOT thread-safe — all access must occur on the same thread (typically
/// the render/game thread during atlas baking or text layout).
///
/// # Lifecycle
/// ```cpp
/// // At engine startup
/// uz::FontManager::Get().Init();
///
/// // During runtime
/// auto h = uz::FontManager::Get().LoadFont("content/fonts/mono.ttf", 32.f);
/// auto metrics = uz::FontManager::Get().GetASCIIMetrics(h);
///
/// // At engine shutdown
/// uz::FontManager::Get().Shutdown();
/// ```
class FontManager {
public:
    /// Returns the process-wide singleton instance (Meyer's singleton).
    static FontManager& Get();

    /// Initializes the FreeType library. Call once at engine startup.
    ///
    /// @returns true on success, false if FreeType initialization failed.
    ///          Subsequent calls after a successful init are no-ops (returns true).
    [[nodiscard]] bool Init();

    /// Shuts down FreeType and releases all loaded font faces.
    ///
    /// Safe to call multiple times. After shutdown, Init() can be called again.
    void Shutdown();

    /// Loads a font face from a file path at the specified pixel size.
    ///
    /// The face is cached by path+size — loading the same combination returns
    /// the same handle without re-reading the file.
    ///
    /// @param path       Filesystem path to the .ttf/.otf font file
    /// @param sizePixels Desired character size in pixels (passed to FT_Set_Pixel_Sizes)
    /// @returns A valid FontHandle, or kInvalidFont on failure.
    [[nodiscard]] FontHandle LoadFont(const std::string& path, float sizePixels);

    /// Returns the raw FT_Face for a loaded font (needed by msdfgen).
    ///
    /// @param handle Font handle from LoadFont()
    /// @returns FT_Face pointer, or nullptr if the handle is invalid.
    [[nodiscard]] FT_Face GetFace(FontHandle handle) const;

    /// Retrieves glyph metrics for a single Unicode codepoint.
    ///
    /// @param handle    Font handle from LoadFont()
    /// @param codepoint Unicode codepoint (e.g., 'A' = 0x41)
    /// @param out       Filled with metric data on success
    /// @returns true if the glyph was found and metrics were extracted.
    [[nodiscard]] bool GetGlyphMetrics(FontHandle handle, uint32_t codepoint,
                                       GlyphMetrics& out) const;

    /// Returns the line height in pixels (ascender - descender + line gap).
    ///
    /// This is the recommended vertical advance between baselines.
    [[nodiscard]] float GetLineHeight(FontHandle handle) const;

    /// Returns the ascender in pixels (baseline to top of tallest glyph).
    [[nodiscard]] float GetAscender(FontHandle handle) const;

    /// Extracts metrics for all printable ASCII codepoints (U+0020 through U+007E).
    ///
    /// Useful for pre-populating an SDF atlas with the basic Latin character set.
    /// Missing glyphs are silently skipped.
    [[nodiscard]] std::vector<GlyphMetrics> GetASCIIMetrics(FontHandle handle) const;

private:
    FontManager() = default;
    ~FontManager();

    FontManager(const FontManager&) = delete;
    FontManager& operator=(const FontManager&) = delete;
    FontManager(FontManager&&) = delete;
    FontManager& operator=(FontManager&&) = delete;

    /// Internal record for a loaded font face.
    struct LoadedFont {
        FT_Face face = nullptr;
        std::string path;
        float sizePixels = 0.f;
    };

    FT_Library m_library = nullptr;
    std::vector<LoadedFont> m_fonts;
    std::unordered_map<std::string, FontHandle> m_fontCache; ///< "path@size" -> handle
};

} // namespace uz
