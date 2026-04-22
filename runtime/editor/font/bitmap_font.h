/// UzEngine Bitmap Font Atlas
///
/// Rasterizes a TrueType font into a GPU texture atlas for text rendering.
/// Uses stb_truetype to bake configurable Unicode codepoint ranges into an
/// R8 texture. Provides glyph metrics (advance, offset, UV coords) for quad
/// generation.
///
/// # Phase 13.2.0
/// Extended beyond ASCII 32–126. `BitmapFont::InitWithRanges` accepts a
/// vector of `GlyphRange` entries and bakes each into a single 1024x1024
/// atlas via `stbtt_PackFontRanges`. Default range set covers:
///
/// - ASCII printable       (U+0020 .. U+007E)
/// - Box Drawing           (U+2500 .. U+257F)
/// - Block Elements        (U+2580 .. U+259F)
/// - Geometric Shapes      (U+25A0 .. U+25FF)
/// - Arrows                (U+2190 .. U+21FF)
/// - Miscellaneous Symbols (U+2600 .. U+26FF)
/// - Dingbats              (U+2700 .. U+27BF)
///
/// Glyph lookup is keyed by codepoint (`uint32_t`). The legacy
/// `GetGlyph(char)` overload forwards to the codepoint path so every
/// phase-12 call site compiles unchanged.
///
/// # Usage
/// ```cpp
/// BitmapFont font;
/// font.Init(device, cmdList, uploadCtx, uz::ResolveDefaultMonoFont(), 16.f);
/// // or font.InitWithRanges(device, cmdList, uploadCtx, path, 16.f, ranges);
/// // ... use GetGlyph() to lay out text quads ...
/// font.Shutdown(device);
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Init/Shutdown on the render thread only.

#pragma once

#include "rhi.h"
#include "rhi_types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class UploadContext;

// ─────────────────────────────────────────────────────────────────────────────
// Glyph Info
// ─────────────────────────────────────────────────────────────────────────────

/// Metrics and UV coordinates for a single glyph in the atlas.
struct GlyphInfo {
    float u0 = 0.f, v0 = 0.f;       ///< Top-left UV in atlas.
    float u1 = 0.f, v1 = 0.f;       ///< Bottom-right UV in atlas.
    float xOffset = 0.f;            ///< Pixel offset from cursor to glyph left edge.
    float yOffset = 0.f;            ///< Pixel offset from baseline to glyph top edge.
    float width = 0.f;              ///< Glyph width in pixels.
    float height = 0.f;             ///< Glyph height in pixels.
    float xAdvance = 0.f;           ///< Horizontal advance to next glyph in pixels.
};

/// Inclusive Unicode codepoint range. `first` and `last` are both baked
/// (i.e. the range `[first, last]`).
struct GlyphRange {
    uint32_t first = 0;
    uint32_t last  = 0;

    [[nodiscard]] uint32_t Count() const noexcept {
        return (last >= first) ? (last - first + 1u) : 0u;
    }
};

/// Returns the curated default range set used by the VFX editor:
/// ASCII + Box Drawing + Block Elements + Geometric Shapes + Arrows
/// + Misc Symbols + Dingbats.
[[nodiscard]] const std::vector<GlyphRange>& GetDefaultGlyphRanges() noexcept;

/// Decodes a single UTF-8 codepoint from `bytes[i]` and advances `i`
/// past the consumed bytes. Returns U+FFFD on malformed input (and
/// advances `i` by one byte so decoding cannot spin forever). Exposed
/// publicly so tests and tools can share the exact logic used by
/// `TextRenderer::AddText`.
[[nodiscard]] uint32_t DecodeUtf8Codepoint(const std::string& bytes, size_t& i) noexcept;

// ─────────────────────────────────────────────────────────────────────────────
// BitmapFont
// ─────────────────────────────────────────────────────────────────────────────

/// Bitmap font atlas baked from a TrueType font file.
///
/// Covers one or more Unicode codepoint ranges. Each glyph is rasterized at
/// the requested pixel height into an R8 atlas texture and uploaded to GPU.
class BitmapFont {
public:
    BitmapFont() = default;

    /// Convenience overload — bakes ONLY printable ASCII (U+0020..U+007E).
    /// Retained for back-compat with phase-12 call sites; new code should
    /// prefer `InitWithRanges` + `GetDefaultGlyphRanges`.
    bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
              const std::string& fontPath, float pixelHeight);

    /// Bakes the font atlas covering every codepoint range in `ranges` and
    /// uploads it to the GPU.
    ///
    /// # Arguments
    /// * `device` — RHI device for texture/buffer creation.
    /// * `cmdList` — Command list for recording upload commands (must be recording).
    /// * `upload` — Upload context for staging buffer management.
    /// * `fontPath` — Absolute path to a .ttf font file.
    /// * `pixelHeight` — Desired glyph height in pixels.
    /// * `ranges` — Inclusive Unicode ranges to bake. Empty = ASCII only.
    ///
    /// # Returns
    /// `true` on success, `false` if the font cannot be loaded or the
    /// atlas cannot fit the baked ranges even at 1024x1024.
    ///
    /// # Side Effects
    /// - Creates an R8 GPU texture (atlas).
    /// - Records copy + barrier commands on `cmdList`.
    /// - Logs errors on failure via `EditorFont` log category.
    bool InitWithRanges(IRHIDevice* device, IRHICommandList* cmdList,
                        UploadContext& upload, const std::string& fontPath,
                        float pixelHeight,
                        const std::vector<GlyphRange>& ranges);

    /// Destroys the atlas texture. Must be called before device destruction.
    void Shutdown(IRHIDevice* device);

    /// Returns glyph info for a Unicode codepoint, or `nullptr` if the
    /// codepoint is not baked in the atlas.
    [[nodiscard]] const GlyphInfo* GetGlyph(uint32_t codepoint) const;

    /// Back-compat overload — resolves a signed-char argument as ASCII.
    /// Values in `[0x20, 0x7E]` return the same result as the codepoint
    /// overload; other values return nullptr.
    [[nodiscard]] const GlyphInfo* GetGlyph(char ch) const {
        return GetGlyph(static_cast<uint32_t>(static_cast<uint8_t>(ch)));
    }

    /// Returns `true` when the font has a glyph baked for `codepoint`.
    [[nodiscard]] bool HasGlyph(uint32_t codepoint) const {
        return m_glyphs.find(codepoint) != m_glyphs.end();
    }

    /// Returns the atlas texture handle for binding in shaders.
    [[nodiscard]] RHITextureHandle GetAtlasTexture() const { return m_atlas; }

    /// Returns the SRV descriptor index for bindless access.
    [[nodiscard]] uint32_t GetAtlasSRVIndex() const { return m_srvIndex; }

    /// Returns the font line height in pixels (ascent - descent + lineGap).
    [[nodiscard]] float GetLineHeight() const { return m_lineHeight; }

    /// Returns the pixel height the font was baked at.
    [[nodiscard]] float GetPixelHeight() const { return m_pixelHeight; }

    /// Returns the atlas width in pixels.
    [[nodiscard]] uint32_t GetAtlasWidth() const { return m_atlasWidth; }

    /// Returns the atlas height in pixels.
    [[nodiscard]] uint32_t GetAtlasHeight() const { return m_atlasHeight; }

    /// Returns the number of glyphs baked (across all ranges).
    [[nodiscard]] size_t GetGlyphCount() const { return m_glyphs.size(); }

private:
    std::unordered_map<uint32_t, GlyphInfo> m_glyphs;
    RHITextureHandle m_atlas{};
    uint32_t m_srvIndex = 0;
    uint32_t m_atlasWidth = 0;
    uint32_t m_atlasHeight = 0;
    float m_lineHeight = 0.f;
    float m_pixelHeight = 0.f;
};

} // namespace uz
