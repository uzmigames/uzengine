/// UzEngine MSDF Font Atlas Implementation — phase 13.4.0
///
/// Generates a single-channel SDF atlas via `stbtt_GetGlyphSDF`, packs
/// glyphs into a 1024x1024 R8 texture using a simple shelf packer,
/// and uploads to the GPU. The runtime shader samples the SDF and
/// uses `fwidth`-based AA for crisp text at any scale.

#include <stb_truetype.h>  // Definitions already provided by bitmap_font.cpp

#include "msdf_font.h"
#include "upload_context.h"
#include "log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(EditorMsdfFont);

namespace uz {

UZ_DEFINE_LOG_CATEGORY(EditorMsdfFont, Info);

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// SDF bake parameters. `onedge_value=128` + `pixel_dist_scale=32`
/// maps the edge to 128 and gives a ~4-pixel-wide transition band
/// (32 dist-units per SDF pixel × 128 range / 32 = 4 pixels). Good
/// for the editor scale range.
constexpr uint8_t kOnEdgeValue  = 128;
constexpr float   kPixelDistScale = 32.0f;
constexpr int     kPaddingPx      = 4;

/// Reads a TTF file into memory. Returns empty on failure.
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

/// Simple shelf packer — walks rows left-to-right, moves to a new
/// shelf on overflow. Returns `false` when the atlas is full.
struct ShelfPacker {
    uint32_t width;
    uint32_t height;
    uint32_t cursorX = 0;
    uint32_t cursorY = 0;
    uint32_t shelfHeight = 0;

    bool Place(uint32_t w, uint32_t h, uint32_t& outX, uint32_t& outY) {
        // Advance to next shelf if the current one cannot fit.
        if (cursorX + w > width) {
            cursorX = 0;
            cursorY += shelfHeight;
            shelfHeight = 0;
        }
        if (cursorY + h > height) return false;
        outX = cursorX;
        outY = cursorY;
        cursorX += w;
        shelfHeight = std::max(shelfHeight, h);
        return true;
    }
};

/// Writes `src` (width x height, one byte per pixel) into `dst` (the
/// atlas) at (`dstX`, `dstY`).
void BlitR8(uint8_t* dst, uint32_t dstPitch,
            const uint8_t* src, uint32_t w, uint32_t h,
            uint32_t dstX, uint32_t dstY) {
    for (uint32_t y = 0; y < h; ++y) {
        std::memcpy(dst + (dstY + y) * dstPitch + dstX,
                    src + y * w, w);
    }
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Bake
// ─────────────────────────────────────────────────────────────────────────────

bool MsdfFont::InitWithRanges(IRHIDevice* device, IRHICommandList* cmdList,
                              UploadContext& upload,
                              const std::string& fontPath,
                              float pixelHeight,
                              const std::vector<GlyphRange>& ranges) {
    m_pixelHeight = pixelHeight;
    m_glyphs.clear();

    std::vector<uint8_t> fontData = LoadFontBytes(fontPath);
    if (fontData.empty()) {
        UZ_LOG(EditorMsdfFont, Error, "Failed to open font file: path='{}'", fontPath);
        return false;
    }

    stbtt_fontinfo fontInfo;
    if (!stbtt_InitFont(&fontInfo, fontData.data(), 0)) {
        UZ_LOG(EditorMsdfFont, Error, "Failed to parse font: path='{}'", fontPath);
        return false;
    }
    const float scale = stbtt_ScaleForPixelHeight(&fontInfo, pixelHeight);
    int ascent = 0, descent = 0, lineGap = 0;
    stbtt_GetFontVMetrics(&fontInfo, &ascent, &descent, &lineGap);
    m_lineHeight = (ascent - descent + lineGap) * scale;
    m_sdfPadding = static_cast<float>(kPaddingPx);

    // Normalise ranges: empty → ASCII only.
    static const std::vector<GlyphRange> kAsciiOnly = { { 0x0020u, 0x007Eu } };
    const std::vector<GlyphRange>& eff = ranges.empty() ? kAsciiOnly : ranges;

    // Allocate atlas (R8, 1024x1024).
    m_atlasWidth  = 1024;
    m_atlasHeight = 1024;
    std::vector<uint8_t> pixels(static_cast<size_t>(m_atlasWidth) *
                                m_atlasHeight, 0);

    // Reserve pixel (0,0) as opaque so overlay rects stay compatible
    // (AddRect uses UV=(0,0) → sampler returns 1.0 for the SDF path).
    pixels[0] = 0xFF;

    ShelfPacker packer{ m_atlasWidth, m_atlasHeight };
    // Reserve pixel (0,0) for the solid-white overlay rect sampling
    // path. Advance the packer cursor past it so no glyph overlaps.
    {
        uint32_t rx = 0, ry = 0;
        packer.Place(1, 1, rx, ry);
    }

    size_t bakedCount = 0;
    size_t skippedNoGlyph = 0;
    size_t skippedNoFit   = 0;

    for (const auto& r : eff) {
        for (uint32_t cp = r.first; cp <= r.last; ++cp) {
            int glyphIdx = stbtt_FindGlyphIndex(&fontInfo, static_cast<int>(cp));
            if (glyphIdx == 0) {
                ++skippedNoGlyph;
                continue;
            }

            // Advance + side-bearing come from raw font metrics, not
            // the SDF bake (SDF gives no advance width).
            int advX = 0, lsb = 0;
            stbtt_GetGlyphHMetrics(&fontInfo, glyphIdx, &advX, &lsb);

            int w = 0, h = 0, xoff = 0, yoff = 0;
            unsigned char* sdf = stbtt_GetGlyphSDF(&fontInfo, scale, glyphIdx,
                                                   kPaddingPx,
                                                   kOnEdgeValue,
                                                   kPixelDistScale,
                                                   &w, &h, &xoff, &yoff);

            GlyphInfo info{};
            info.xAdvance = advX * scale;

            if (sdf == nullptr || w <= 0 || h <= 0) {
                // Glyph has no raster body (e.g. space). Record the
                // advance so text layout still positions the cursor
                // correctly, leave UV rect zero.
                if (info.xAdvance > 0.0f) {
                    m_glyphs[cp] = info;
                    ++bakedCount;
                }
                if (sdf) stbtt_FreeSDF(sdf, nullptr);
                continue;
            }

            uint32_t placeX = 0, placeY = 0;
            if (!packer.Place(static_cast<uint32_t>(w),
                              static_cast<uint32_t>(h),
                              placeX, placeY)) {
                stbtt_FreeSDF(sdf, nullptr);
                ++skippedNoFit;
                continue;
            }

            BlitR8(pixels.data(), m_atlasWidth, sdf,
                   static_cast<uint32_t>(w), static_cast<uint32_t>(h),
                   placeX, placeY);
            stbtt_FreeSDF(sdf, nullptr);

            const float invW = 1.f / static_cast<float>(m_atlasWidth);
            const float invH = 1.f / static_cast<float>(m_atlasHeight);
            info.u0 = static_cast<float>(placeX) * invW;
            info.v0 = static_cast<float>(placeY) * invH;
            info.u1 = static_cast<float>(placeX + w) * invW;
            info.v1 = static_cast<float>(placeY + h) * invH;
            info.xOffset = static_cast<float>(xoff);
            info.yOffset = static_cast<float>(yoff);
            info.width   = static_cast<float>(w);
            info.height  = static_cast<float>(h);

            m_glyphs[cp] = info;
            ++bakedCount;
        }
    }

    if (skippedNoFit > 0) {
        UZ_LOG(EditorMsdfFont, Warning,
               "MSDF atlas full: {} glyphs could not be packed at {}px",
               skippedNoFit, pixelHeight);
    }

    // ── Create GPU texture ────────────────────────────────────────────────
    RHITextureDesc texDesc{};
    texDesc.width = m_atlasWidth;
    texDesc.height = m_atlasHeight;
    texDesc.format = RHIFormat::R8Unorm;
    texDesc.usage = RHITextureUsage::ShaderResource;
    texDesc.debugName = "MsdfFontAtlas";

    m_atlas = device->CreateTexture(texDesc);
    if (!m_atlas.IsValid()) {
        UZ_LOG(EditorMsdfFont, Error, "Failed to create MSDF atlas texture");
        return false;
    }
    m_srvIndex = device->GetTextureSRVIndex(m_atlas);

    upload.UploadTexture(cmdList, m_atlas, pixels.data(),
                         static_cast<uint64_t>(m_atlasWidth) * m_atlasHeight);

    RHIBarrierDesc barrier{};
    barrier.texture = m_atlas;
    barrier.stateBefore = RHIResourceState::CopyDst;
    barrier.stateAfter = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&barrier, 1);

    UZ_LOG(EditorMsdfFont, Info,
           "MSDF atlas created: path='{}', size={}x{}, masterPx={}, "
           "ranges={}, baked={}, skippedNoGlyph={}, skippedNoFit={}",
           fontPath, m_atlasWidth, m_atlasHeight, pixelHeight,
           eff.size(), bakedCount, skippedNoGlyph, skippedNoFit);
    return true;
}

void MsdfFont::Shutdown(IRHIDevice* device) {
    if (m_atlas.IsValid()) {
        device->DestroyTexture(m_atlas);
        m_atlas = {};
    }
    m_glyphs.clear();
}

const GlyphInfo* MsdfFont::GetGlyph(uint32_t codepoint) const {
    auto it = m_glyphs.find(codepoint);
    if (it == m_glyphs.end()) return nullptr;
    return &it->second;
}

} // namespace uz
