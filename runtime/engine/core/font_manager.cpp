#include "font_manager.h"

#include <ft2build.h>
#include FT_FREETYPE_H

#include "../logging/log.h"

#include <cmath>
#include <cstdio>

UZ_DEFINE_LOG_CATEGORY(Font, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────

FontManager& FontManager::Get() {
    static FontManager instance;
    return instance;
}

FontManager::~FontManager() {
    Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool FontManager::Init() {
    if (m_library) {
        return true; // Already initialized
    }

    const FT_Error err = FT_Init_FreeType(&m_library);
    if (err != 0) {
        UZ_LOG(Font, Error, "FreeType initialization failed (FT_Error={})", err);
        return false;
    }

    FT_Int major = 0;
    FT_Int minor = 0;
    FT_Int patch = 0;
    FT_Library_Version(m_library, &major, &minor, &patch);
    UZ_LOG(Font, Info, "FreeType {}.{}.{} initialized", major, minor, patch);
    return true;
}

void FontManager::Shutdown() {
    // Release all font faces before destroying the library.
    for (auto& font : m_fonts) {
        if (font.face) {
            FT_Done_Face(font.face);
            font.face = nullptr;
        }
    }
    m_fonts.clear();
    m_fontCache.clear();

    if (m_library) {
        FT_Done_FreeType(m_library);
        m_library = nullptr;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Font Loading
// ─────────────────────────────────────────────────────────────────────────────

FontHandle FontManager::LoadFont(const std::string& path, float sizePixels) {
    // Build cache key from path and pixel size. Using %.1f so that 32.0 and
    // 32.0 always produce the same key, but 32.0 and 32.5 differ.
    char key[512];
    std::snprintf(key, sizeof(key), "%s@%.1f", path.c_str(), sizePixels);

    const auto it = m_fontCache.find(key);
    if (it != m_fontCache.end()) {
        return it->second;
    }

    if (!m_library) {
        UZ_LOG(Font, Error, "FontManager not initialized — call Init() first");
        return kInvalidFont;
    }

    FT_Face face = nullptr;
    const FT_Error err = FT_New_Face(m_library, path.c_str(), 0, &face);
    if (err != 0) {
        UZ_LOG(Font, Error, "Failed to load font '{}' (FT_Error={})", path, err);
        return kInvalidFont;
    }

    // Set character size. FreeType's pixel-size setter accepts unsigned int.
    const auto pixelHeight = static_cast<FT_UInt>(std::round(sizePixels));
    FT_Set_Pixel_Sizes(face, 0, pixelHeight);

    const auto handle = static_cast<FontHandle>(m_fonts.size());
    m_fonts.push_back({face, path, sizePixels});
    m_fontCache[key] = handle;

    UZ_LOG(Font, Info, "Loaded font '{}' at {}px (handle={})", path, sizePixels, handle);
    return handle;
}

// ─────────────────────────────────────────────────────────────────────────────
// Face Access
// ─────────────────────────────────────────────────────────────────────────────

FT_Face FontManager::GetFace(FontHandle handle) const {
    if (handle >= static_cast<FontHandle>(m_fonts.size())) {
        return nullptr;
    }
    return m_fonts[handle].face;
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph Metrics
// ─────────────────────────────────────────────────────────────────────────────

bool FontManager::GetGlyphMetrics(FontHandle handle, uint32_t codepoint,
                                  GlyphMetrics& out) const {
    if (handle >= static_cast<FontHandle>(m_fonts.size())) {
        return false;
    }

    FT_Face face = m_fonts[handle].face;
    if (!face) {
        return false;
    }

    // FT_Get_Char_Index returns 0 (.notdef) when the codepoint is not in the font.
    const FT_UInt glyphIdx = FT_Get_Char_Index(face, codepoint);
    if (glyphIdx == 0) {
        return false;
    }

    // Load glyph metrics without rendering a bitmap — we only need the outline
    // metrics for SDF generation and text layout.
    const FT_Error err = FT_Load_Glyph(face, glyphIdx, FT_LOAD_NO_BITMAP);
    if (err != 0) {
        return false;
    }

    // FreeType stores metrics in 26.6 fixed-point format (1/64th of a pixel).
    const FT_GlyphSlot slot = face->glyph;
    out.glyphIndex = glyphIdx;
    out.advanceX = static_cast<float>(slot->advance.x) / 64.0f;
    out.bearingX = static_cast<float>(slot->metrics.horiBearingX) / 64.0f;
    out.bearingY = static_cast<float>(slot->metrics.horiBearingY) / 64.0f;
    out.width = static_cast<float>(slot->metrics.width) / 64.0f;
    out.height = static_cast<float>(slot->metrics.height) / 64.0f;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Font-Level Metrics
// ─────────────────────────────────────────────────────────────────────────────

float FontManager::GetLineHeight(FontHandle handle) const {
    if (handle >= static_cast<FontHandle>(m_fonts.size())) {
        return 0.f;
    }

    const FT_Face face = m_fonts[handle].face;
    if (!face || !face->size) {
        return 0.f;
    }

    // face->size->metrics.height is the recommended baseline-to-baseline
    // distance in 26.6 fixed-point pixels.
    return static_cast<float>(face->size->metrics.height) / 64.0f;
}

float FontManager::GetAscender(FontHandle handle) const {
    if (handle >= static_cast<FontHandle>(m_fonts.size())) {
        return 0.f;
    }

    const FT_Face face = m_fonts[handle].face;
    if (!face || !face->size) {
        return 0.f;
    }

    // face->size->metrics.ascender is the distance from baseline to the
    // highest point of any glyph, in 26.6 fixed-point pixels.
    return static_cast<float>(face->size->metrics.ascender) / 64.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch Metrics
// ─────────────────────────────────────────────────────────────────────────────

std::vector<GlyphMetrics> FontManager::GetASCIIMetrics(FontHandle handle) const {
    std::vector<GlyphMetrics> result;
    result.reserve(95); // U+0020 (' ') through U+007E ('~') inclusive = 95 codepoints

    for (uint32_t cp = 32; cp <= 126; ++cp) {
        GlyphMetrics m{};
        if (GetGlyphMetrics(handle, cp, m)) {
            result.push_back(m);
        }
    }
    return result;
}

} // namespace uz
