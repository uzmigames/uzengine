/// UzEngine Bitmap Font Atlas Implementation
///
/// Uses stb_truetype to bake a TrueType font into an R8 texture atlas.
/// The atlas is uploaded to the GPU via UploadContext for bindless access.

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
// Init
// ─────────────────────────────────────────────────────────────────────────────

bool BitmapFont::Init(IRHIDevice* device, IRHICommandList* cmdList, UploadContext& upload,
                      const std::string& fontPath, float pixelHeight) {
    m_pixelHeight = pixelHeight;

    // ── Load font file ───────────────────────────────────────────────────
    FILE* f = fopen(fontPath.c_str(), "rb");
    if (!f) {
        UZ_LOG(EditorFont, Error, "Failed to open font file: path='{}'", fontPath);
        return false;
    }
    fseek(f, 0, SEEK_END);
    long fileSize = ftell(f);
    fseek(f, 0, SEEK_SET);

    std::vector<uint8_t> fontData(static_cast<size_t>(fileSize));
    fread(fontData.data(), 1, fontData.size(), f);
    fclose(f);

    // ── Initialize stb_truetype ──────────────────────────────────────────
    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData.data(), 0)) {
        UZ_LOG(EditorFont, Error, "Failed to parse font: path='{}'", fontPath);
        return false;
    }

    float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelHeight);

    int ascent, descent, lineGap;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    m_lineHeight = (ascent - descent + lineGap) * scale;

    // ── Bake glyph atlas ─────────────────────────────────────────────────
    // Estimate atlas size: ~kGlyphCount glyphs at pixelHeight
    m_atlasWidth = 512;
    m_atlasHeight = 256;

    std::vector<stbtt_bakedchar> bakedChars(kGlyphCount);
    std::vector<uint8_t> atlasPixels(m_atlasWidth * m_atlasHeight, 0);

    int result = stbtt_BakeFontBitmap(fontData.data(), 0, pixelHeight,
        atlasPixels.data(), static_cast<int>(m_atlasWidth), static_cast<int>(m_atlasHeight),
        kFirstChar, kGlyphCount, bakedChars.data());

    if (result <= 0) {
        // Try larger atlas
        m_atlasWidth = 1024;
        m_atlasHeight = 512;
        atlasPixels.resize(m_atlasWidth * m_atlasHeight, 0);
        result = stbtt_BakeFontBitmap(fontData.data(), 0, pixelHeight,
            atlasPixels.data(), static_cast<int>(m_atlasWidth), static_cast<int>(m_atlasHeight),
            kFirstChar, kGlyphCount, bakedChars.data());
        if (result <= 0) {
            UZ_LOG(EditorFont, Error, "Font atlas bake failed: path='{}', pixelHeight={}",
                   fontPath, pixelHeight);
            return false;
        }
    }

    // ── Solid white pixel at (0,0) for rect rendering ──────────────────
    // Overlay rects use the text pipeline with UV=(0,0). Setting pixel (0,0)
    // to 0xFF ensures atlas.Sample().r == 1.0, so rect output = vertexColor.
    atlasPixels[0] = 0xFF;

    // ── Extract glyph metrics ────────────────────────────────────────────
    float invW = 1.f / static_cast<float>(m_atlasWidth);
    float invH = 1.f / static_cast<float>(m_atlasHeight);

    for (int i = 0; i < kGlyphCount; ++i) {
        const auto& bc = bakedChars[i];
        GlyphInfo& g = m_glyphs[i];
        g.u0 = static_cast<float>(bc.x0) * invW;
        g.v0 = static_cast<float>(bc.y0) * invH;
        g.u1 = static_cast<float>(bc.x1) * invW;
        g.v1 = static_cast<float>(bc.y1) * invH;
        g.xOffset = bc.xoff;
        g.yOffset = bc.yoff;
        g.width = static_cast<float>(bc.x1 - bc.x0);
        g.height = static_cast<float>(bc.y1 - bc.y0);
        g.xAdvance = bc.xadvance;
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

    // Get SRV index for bindless access
    m_srvIndex = device->GetTextureSRVIndex(m_atlas);

    // ── Upload atlas data ────────────────────────────────────────────────
    upload.UploadTexture(cmdList, m_atlas, atlasPixels.data(),
                         m_atlasWidth * m_atlasHeight);

    // Transition atlas from CopyDst (set by UploadTexture) to ShaderResource
    RHIBarrierDesc barrier{};
    barrier.texture = m_atlas;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    UZ_LOG(EditorFont, Info, "Font atlas created: path='{}', size={}x{}, pixelHeight={}, glyphs={}",
           fontPath, m_atlasWidth, m_atlasHeight, pixelHeight, kGlyphCount);
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
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph Lookup
// ─────────────────────────────────────────────────────────────────────────────

const GlyphInfo* BitmapFont::GetGlyph(char ch) const {
    int idx = static_cast<int>(ch) - kFirstChar;
    if (idx < 0 || idx >= kGlyphCount) return nullptr;
    return &m_glyphs[idx];
}

} // namespace uz
