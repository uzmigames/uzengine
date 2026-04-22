/// UzEngine Bitmap Font Atlas Implementation
///
/// Uses stb_truetype to bake configurable Unicode codepoint ranges into an
/// R8 texture atlas. The atlas is uploaded to the GPU via UploadContext for
/// bindless access.

#define STB_TRUETYPE_IMPLEMENTATION
#include <stb_truetype.h>

#include "bitmap_font.h"
#include "upload_context.h"
#include "log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(EditorFont);

namespace uz {

UZ_DEFINE_LOG_CATEGORY(EditorFont, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Default range set — curated for the VFX editor (phase 13.2.0)
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// UTF-8 decode (shared between TextRenderer::AddText + tests)
// ─────────────────────────────────────────────────────────────────────────────

uint32_t DecodeUtf8Codepoint(const std::string& bytes, size_t& i) noexcept {
    const size_t n = bytes.size();
    if (i >= n) return 0xFFFDu;
    const uint8_t b0 = static_cast<uint8_t>(bytes[i]);

    // 1-byte ASCII.
    if (b0 < 0x80u) {
        ++i;
        return b0;
    }

    // Continuation byte at start → malformed, replace.
    if ((b0 & 0xC0u) == 0x80u) {
        ++i;
        return 0xFFFDu;
    }

    uint32_t cp = 0;
    size_t extra = 0;
    if ((b0 & 0xE0u) == 0xC0u)      { cp = b0 & 0x1Fu; extra = 1; }
    else if ((b0 & 0xF0u) == 0xE0u) { cp = b0 & 0x0Fu; extra = 2; }
    else if ((b0 & 0xF8u) == 0xF0u) { cp = b0 & 0x07u; extra = 3; }
    else { ++i; return 0xFFFDu; }

    if (i + 1 + extra > n) { ++i; return 0xFFFDu; }

    for (size_t k = 0; k < extra; ++k) {
        const uint8_t b = static_cast<uint8_t>(bytes[i + 1 + k]);
        if ((b & 0xC0u) != 0x80u) { ++i; return 0xFFFDu; }
        cp = (cp << 6) | (b & 0x3Fu);
    }

    i += 1 + extra;
    return cp;
}

const std::vector<GlyphRange>& GetDefaultGlyphRanges() noexcept {
    static const std::vector<GlyphRange> kDefaults = {
        { 0x0020u, 0x007Eu },  // ASCII printable
        { 0x2190u, 0x21FFu },  // Arrows
        { 0x2500u, 0x257Fu },  // Box Drawing
        { 0x2580u, 0x259Fu },  // Block Elements
        { 0x25A0u, 0x25FFu },  // Geometric Shapes
        { 0x2600u, 0x26FFu },  // Miscellaneous Symbols
        { 0x2700u, 0x27BFu },  // Dingbats
    };
    return kDefaults;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Loads an entire file into a byte buffer. Returns empty on failure.
std::vector<uint8_t> LoadFontBytes(const std::string& path) {
    std::vector<uint8_t> out;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return out;
    std::fseek(f, 0, SEEK_END);
    long sz = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (sz > 0) {
        out.resize(static_cast<size_t>(sz));
        std::fread(out.data(), 1, out.size(), f);
    }
    std::fclose(f);
    return out;
}

/// Sum of `Count()` across every range (total glyph slots needed).
size_t TotalGlyphs(const std::vector<GlyphRange>& ranges) {
    size_t n = 0;
    for (const auto& r : ranges) n += r.Count();
    return n;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Init (convenience — ASCII only)
// ─────────────────────────────────────────────────────────────────────────────

bool BitmapFont::Init(IRHIDevice* device, IRHICommandList* cmdList,
                      UploadContext& upload, const std::string& fontPath,
                      float pixelHeight) {
    // ASCII printable only — phase-12 back-compat.
    static const std::vector<GlyphRange> kAscii = { { 0x0020u, 0x007Eu } };
    return InitWithRanges(device, cmdList, upload, fontPath, pixelHeight, kAscii);
}

// ─────────────────────────────────────────────────────────────────────────────
// InitWithRanges (multi-range bake)
// ─────────────────────────────────────────────────────────────────────────────

bool BitmapFont::InitWithRanges(IRHIDevice* device, IRHICommandList* cmdList,
                                UploadContext& upload, const std::string& fontPath,
                                float pixelHeight,
                                const std::vector<GlyphRange>& ranges) {
    m_pixelHeight = pixelHeight;
    m_glyphs.clear();

    // ── Load font file ───────────────────────────────────────────────────
    std::vector<uint8_t> fontData = LoadFontBytes(fontPath);
    if (fontData.empty()) {
        UZ_LOG(EditorFont, Error, "Failed to open font file: path='{}'", fontPath);
        return false;
    }

    // ── stbtt font metrics ───────────────────────────────────────────────
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData.data(), 0)) {
        UZ_LOG(EditorFont, Error, "Failed to parse font: path='{}'", fontPath);
        return false;
    }
    float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    m_lineHeight = (ascent - descent + lineGap) * scale;

    // ── Normalise the range list (empty → ASCII fallback) ────────────────
    const std::vector<GlyphRange>* effectiveRanges = &ranges;
    static const std::vector<GlyphRange> kAsciiOnly = { { 0x0020u, 0x007Eu } };
    if (effectiveRanges->empty()) {
        effectiveRanges = &kAsciiOnly;
    }

    const size_t totalGlyphs = TotalGlyphs(*effectiveRanges);
    if (totalGlyphs == 0) {
        UZ_LOG(EditorFont, Error, "No glyphs requested for bake (empty ranges)");
        return false;
    }

    // ── Allocate 1024x1024 atlas ─────────────────────────────────────────
    m_atlasWidth  = 1024;
    m_atlasHeight = 1024;
    std::vector<uint8_t> atlasPixels(static_cast<size_t>(m_atlasWidth) *
                                     m_atlasHeight, 0);

    // ── Pack via stbtt_PackFontRanges ────────────────────────────────────
    stbtt_pack_context spc{};
    if (!stbtt_PackBegin(&spc, atlasPixels.data(),
                         static_cast<int>(m_atlasWidth),
                         static_cast<int>(m_atlasHeight),
                         0, 1, nullptr)) {
        UZ_LOG(EditorFont, Error, "stbtt_PackBegin failed ({}x{})",
               m_atlasWidth, m_atlasHeight);
        return false;
    }
    stbtt_PackSetSkipMissingCodepoints(&spc, 1);

    // Per-range chardata storage outlives the PackFontRanges call so we
    // can read back x0/y0/x1/y1 per glyph after baking completes.
    std::vector<std::vector<stbtt_packedchar>> perRangeChars(effectiveRanges->size());
    std::vector<stbtt_pack_range> stbRanges(effectiveRanges->size());

    for (size_t i = 0; i < effectiveRanges->size(); ++i) {
        const GlyphRange& r = (*effectiveRanges)[i];
        const uint32_t count = r.Count();
        perRangeChars[i].resize(count);
        stbRanges[i].font_size = pixelHeight;
        stbRanges[i].first_unicode_codepoint_in_range = static_cast<int>(r.first);
        stbRanges[i].array_of_unicode_codepoints = nullptr;
        stbRanges[i].num_chars = static_cast<int>(count);
        stbRanges[i].chardata_for_range = perRangeChars[i].data();
        stbRanges[i].h_oversample = 0;
        stbRanges[i].v_oversample = 0;
    }

    int packed = stbtt_PackFontRanges(&spc, fontData.data(), 0,
                                       stbRanges.data(),
                                       static_cast<int>(stbRanges.size()));
    stbtt_PackEnd(&spc);

    if (packed == 0) {
        UZ_LOG(EditorFont, Warning,
               "Font atlas pack overflowed some ranges at {}px — "
               "continuing with glyphs that fit", pixelHeight);
    }

    // ── Solid white pixel at (0,0) for rect rendering ────────────────────
    // Overlay rects use the text pipeline with UV=(0,0). Setting pixel (0,0)
    // to 0xFF ensures atlas.Sample().r == 1.0, so rect output = vertexColor.
    atlasPixels[0] = 0xFF;

    // ── Extract per-glyph metrics into codepoint-keyed map ───────────────
    const float invW = 1.f / static_cast<float>(m_atlasWidth);
    const float invH = 1.f / static_cast<float>(m_atlasHeight);

    size_t bakedCount = 0;
    for (size_t i = 0; i < effectiveRanges->size(); ++i) {
        const GlyphRange& r = (*effectiveRanges)[i];
        const uint32_t count = r.Count();
        for (uint32_t j = 0; j < count; ++j) {
            const stbtt_packedchar& bc = perRangeChars[i][j];
            // `skip_missing_codepoints=1` leaves the packedchar rect zero
            // AND xadvance=0 for glyphs the font does not cover. Glyphs
            // like space (U+0020) have a zero-size rect but a non-zero
            // xadvance — those must still be recorded so text layout
            // inserts the correct horizontal gap.
            const bool hasRect = (bc.x1 != bc.x0) && (bc.y1 != bc.y0);
            const bool hasAdvance = (bc.xadvance != 0.0f);
            if (!hasRect && !hasAdvance) continue;

            GlyphInfo g{};
            g.u0 = static_cast<float>(bc.x0) * invW;
            g.v0 = static_cast<float>(bc.y0) * invH;
            g.u1 = static_cast<float>(bc.x1) * invW;
            g.v1 = static_cast<float>(bc.y1) * invH;
            g.xOffset = bc.xoff;
            g.yOffset = bc.yoff;
            g.width   = static_cast<float>(bc.x1 - bc.x0);
            g.height  = static_cast<float>(bc.y1 - bc.y0);
            g.xAdvance = bc.xadvance;
            m_glyphs[r.first + j] = g;
            ++bakedCount;
        }
    }

    // ── Create GPU texture ───────────────────────────────────────────────
    RHITextureDesc texDesc{};
    texDesc.width = m_atlasWidth;
    texDesc.height = m_atlasHeight;
    texDesc.format = RHIFormat::R8Unorm;
    texDesc.usage = RHITextureUsage::ShaderResource;
    texDesc.debugName = "FontAtlas";

    m_atlas = device->CreateTexture(texDesc);
    if (!m_atlas.IsValid()) {
        UZ_LOG(EditorFont, Error, "Failed to create font atlas texture");
        return false;
    }

    m_srvIndex = device->GetTextureSRVIndex(m_atlas);

    // ── Upload atlas data ────────────────────────────────────────────────
    upload.UploadTexture(cmdList, m_atlas, atlasPixels.data(),
                         static_cast<uint64_t>(m_atlasWidth) * m_atlasHeight);

    RHIBarrierDesc barrier{};
    barrier.texture = m_atlas;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    UZ_LOG(EditorFont, Info,
           "Font atlas created: path='{}', size={}x{}, pixelHeight={}, "
           "ranges={}, requested={}, baked={}",
           fontPath, m_atlasWidth, m_atlasHeight, pixelHeight,
           effectiveRanges->size(), totalGlyphs, bakedCount);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void BitmapFont::Shutdown(IRHIDevice* device) {
    if (m_atlas.IsValid()) {
        device->DestroyTexture(m_atlas);
        m_atlas = {};
    }
    m_glyphs.clear();
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph Lookup
// ─────────────────────────────────────────────────────────────────────────────

const GlyphInfo* BitmapFont::GetGlyph(uint32_t codepoint) const {
    auto it = m_glyphs.find(codepoint);
    if (it == m_glyphs.end()) return nullptr;
    return &it->second;
}

} // namespace uz
