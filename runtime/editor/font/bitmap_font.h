/// UzEngine Bitmap Font Atlas
///
/// Rasterizes a TrueType font into a GPU texture atlas for text rendering.
/// Uses stb_truetype to bake ASCII glyphs (32–126) into an R8 texture.
/// Provides glyph metrics (advance, offset, UV coords) for quad generation.
///
/// # Usage
/// ```cpp
/// BitmapFont font;
/// font.Init(device, cmdList, uploadCtx, "C:/Windows/Fonts/consola.ttf", 16.f);
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

namespace uz {

class UploadContext;

// ─────────────────────────────────────────────────────────────────────────────
// Glyph Info
// ─────────────────────────────────────────────────────────────────────────────

/// Metrics and UV coordinates for a single glyph in the atlas.
struct GlyphInfo {
    float u0, v0;       ///< Top-left UV in atlas.
    float u1, v1;       ///< Bottom-right UV in atlas.
    float xOffset;      ///< Pixel offset from cursor to glyph left edge.
    float yOffset;      ///< Pixel offset from baseline to glyph top edge.
    float width;        ///< Glyph width in pixels.
    float height;       ///< Glyph height in pixels.
    float xAdvance;     ///< Horizontal advance to next glyph in pixels.
};

// ─────────────────────────────────────────────────────────────────────────────
// BitmapFont
// ─────────────────────────────────────────────────────────────────────────────

/// Bitmap font atlas baked from a TrueType font file.
///
/// Covers printable ASCII range (32–126). Each glyph is rasterized at the
/// requested pixel height into an R8 atlas texture, uploaded to the GPU.
class BitmapFont {
public:
    BitmapFont() = default;

    /// Bakes the font atlas and uploads it to the GPU.
    ///
    /// # Arguments
    /// * `device` — RHI device for texture/buffer creation.
    /// * `cmdList` — Command list for recording upload commands (must be recording).
    /// * `upload` — Upload context for staging buffer management.
    /// * `fontPath` — Absolute path to a .ttf font file.
    /// * `pixelHeight` — Desired glyph height in pixels.
    ///
    /// # Returns
    /// `true` on success, `false` if the font file cannot be loaded.
    ///
    /// # Side Effects
    /// - Creates an R8 GPU texture (atlas).
    /// - Records copy commands on `cmdList`.
    /// - Logs errors on failure via `EditorFont` log category.
    bool Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
              const std::string& fontPath, float pixelHeight);

    /// Destroys the atlas texture. Must be called before device destruction.
    void Shutdown(IRHIDevice* device);

    /// Returns glyph info for a printable ASCII character.
    ///
    /// # Arguments
    /// * `ch` — ASCII character in range [32, 126].
    ///
    /// # Returns
    /// Pointer to GlyphInfo, or nullptr if `ch` is out of range.
    [[nodiscard]] const GlyphInfo* GetGlyph(char ch) const;

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

private:
    static constexpr int kFirstChar = 32;
    static constexpr int kLastChar = 126;
    static constexpr int kGlyphCount = kLastChar - kFirstChar + 1;

    GlyphInfo m_glyphs[kGlyphCount] = {};
    RHITextureHandle m_atlas{};
    uint32_t m_srvIndex = 0;
    uint32_t m_atlasWidth = 0;
    uint32_t m_atlasHeight = 0;
    float m_lineHeight = 0.f;
    float m_pixelHeight = 0.f;
};

} // namespace uz
