/// Unit tests for phase 13.1.0: `VfxIconAtlas`.
///
/// Covers the CPU-side contract of the procedural icon atlas — enum
/// stability, UV-rect table, and pixel-generator output. Does NOT
/// touch the GPU texture upload (that path is exercised by the
/// headless screenshot test and the overlay integration tests).
///
/// Scope:
///   - Atlas dimensions match the compile-time constants.
///   - `VfxGetIconUv(None)` returns a zero-area rect; every
///     enumerated icon returns a unit-square cell UV rect inside
///     the atlas bounds.
///   - UV rects are unique per icon (no two icons share a cell) and
///     each rect occupies a 32x32 cell with 4-px padding (i.e. a
///     24x24 drawn region in the atlas).
///   - `VfxGenerateIconAtlas` writes opaque-white-on-transparent:
///     at least one pixel per icon cell has RGBA = (255,255,255,255)
///     (the glyph centre), and the outer 2 pixels of each cell are
///     fully transparent (the 4-px padding is never drawn into).

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

using namespace uz;

namespace {

constexpr uint16_t FirstIcon() noexcept {
    return static_cast<uint16_t>(VfxUiIcon::Play);
}

constexpr uint16_t LastIcon() noexcept {
    return static_cast<uint16_t>(VfxUiIcon::Count) - 1u;
}

} // namespace

TEST(VfxIconAtlas, AtlasDimensionsAreStable) {
    EXPECT_EQ(kVfxIconAtlasWidth,  256u);
    EXPECT_EQ(kVfxIconAtlasHeight, 256u);
    EXPECT_EQ(kVfxIconAtlasCellPx,  32u);
    EXPECT_EQ(kVfxIconAtlasCols,    8u);
    EXPECT_EQ(kVfxIconAtlasRows,    8u);
    EXPECT_EQ(kVfxIconAtlasCols * kVfxIconAtlasCellPx, kVfxIconAtlasWidth);
    EXPECT_EQ(kVfxIconAtlasRows * kVfxIconAtlasCellPx, kVfxIconAtlasHeight);
}

TEST(VfxIconAtlas, NoneReturnsZeroAreaRect) {
    VfxIconUv uv = VfxGetIconUv(VfxUiIcon::None);
    EXPECT_EQ(uv.u0, uv.u1);
    EXPECT_EQ(uv.v0, uv.v1);
}

TEST(VfxIconAtlas, AllIconsHaveValidUvInsideAtlas) {
    for (uint16_t i = FirstIcon(); i <= LastIcon(); ++i) {
        SCOPED_TRACE(testing::Message() << "icon index=" << i);
        VfxIconUv uv = VfxGetIconUv(static_cast<VfxUiIcon>(i));
        EXPECT_GE(uv.u0, 0.0f);
        EXPECT_GE(uv.v0, 0.0f);
        EXPECT_LE(uv.u1, 1.0f);
        EXPECT_LE(uv.v1, 1.0f);
        EXPECT_LT(uv.u0, uv.u1);
        EXPECT_LT(uv.v0, uv.v1);
    }
}

TEST(VfxIconAtlas, UvRectsAreUniquePerIcon) {
    std::set<std::pair<int, int>> cells;
    for (uint16_t i = FirstIcon(); i <= LastIcon(); ++i) {
        VfxIconUv uv = VfxGetIconUv(static_cast<VfxUiIcon>(i));
        const int col = static_cast<int>(uv.u0 * kVfxIconAtlasWidth /
                                         kVfxIconAtlasCellPx);
        const int row = static_cast<int>(uv.v0 * kVfxIconAtlasHeight /
                                         kVfxIconAtlasCellPx);
        auto [_, inserted] = cells.insert({col, row});
        EXPECT_TRUE(inserted)
            << "duplicate cell (" << col << "," << row << ") for icon " << i;
    }
}

TEST(VfxIconAtlas, CellsSpan32x32WithUvWidth) {
    constexpr float kCellU = static_cast<float>(kVfxIconAtlasCellPx) /
                             static_cast<float>(kVfxIconAtlasWidth);
    constexpr float kCellV = static_cast<float>(kVfxIconAtlasCellPx) /
                             static_cast<float>(kVfxIconAtlasHeight);
    for (uint16_t i = FirstIcon(); i <= LastIcon(); ++i) {
        VfxIconUv uv = VfxGetIconUv(static_cast<VfxUiIcon>(i));
        EXPECT_NEAR(uv.u1 - uv.u0, kCellU, 1e-5f);
        EXPECT_NEAR(uv.v1 - uv.v0, kCellV, 1e-5f);
    }
}

TEST(VfxIconAtlas, GenerateFillsOpaqueWhiteInsideCells) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kVfxIconAtlasWidth) *
                                 kVfxIconAtlasHeight * 4u, 0xAA);
    VfxGenerateIconAtlas(pixels.data());

    for (uint16_t i = FirstIcon(); i <= LastIcon(); ++i) {
        SCOPED_TRACE(testing::Message() << "icon index=" << i);
        VfxIconUv uv = VfxGetIconUv(static_cast<VfxUiIcon>(i));
        const uint32_t cx = static_cast<uint32_t>(uv.u0 * kVfxIconAtlasWidth) +
                            kVfxIconAtlasCellPx / 2u;
        const uint32_t cy = static_cast<uint32_t>(uv.v0 * kVfxIconAtlasHeight) +
                            kVfxIconAtlasCellPx / 2u;

        // Sample a 16x16 window around the cell centre — at least one
        // pixel in that window must be opaque white (the glyph body).
        bool hasOpaqueWhite = false;
        for (int dy = -8; dy < 8 && !hasOpaqueWhite; ++dy) {
            for (int dx = -8; dx < 8 && !hasOpaqueWhite; ++dx) {
                const uint32_t px = cx + static_cast<uint32_t>(
                    static_cast<int32_t>(dx));
                const uint32_t py = cy + static_cast<uint32_t>(
                    static_cast<int32_t>(dy));
                const size_t idx = (static_cast<size_t>(py) *
                                    kVfxIconAtlasWidth + px) * 4u;
                if (pixels[idx + 0] == 0xFF && pixels[idx + 1] == 0xFF &&
                    pixels[idx + 2] == 0xFF && pixels[idx + 3] == 0xFF) {
                    hasOpaqueWhite = true;
                }
            }
        }
        EXPECT_TRUE(hasOpaqueWhite)
            << "expected at least one opaque-white pixel in cell centre";
    }
}

TEST(VfxIconAtlas, PaddingBorderIsFullyTransparent) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kVfxIconAtlasWidth) *
                                 kVfxIconAtlasHeight * 4u, 0xAA);
    VfxGenerateIconAtlas(pixels.data());

    // The outermost 2 pixels of every 32x32 cell should stay at alpha 0.
    // We probe row 0, col 0 of each cell.
    for (uint32_t row = 0; row < kVfxIconAtlasRows; ++row) {
        for (uint32_t col = 0; col < kVfxIconAtlasCols; ++col) {
            const uint32_t cellX = col * kVfxIconAtlasCellPx;
            const uint32_t cellY = row * kVfxIconAtlasCellPx;
            // Top-left padding pixel.
            const size_t idxTL = (static_cast<size_t>(cellY) *
                                  kVfxIconAtlasWidth + cellX) * 4u;
            EXPECT_EQ(pixels[idxTL + 3], 0x00)
                << "cell (" << col << "," << row << ") TL alpha must be 0";
            // Top-right padding pixel.
            const uint32_t trX = cellX + kVfxIconAtlasCellPx - 1u;
            const size_t idxTR = (static_cast<size_t>(cellY) *
                                  kVfxIconAtlasWidth + trX) * 4u;
            EXPECT_EQ(pixels[idxTR + 3], 0x00)
                << "cell (" << col << "," << row << ") TR alpha must be 0";
        }
    }
}

TEST(VfxIconAtlas, GeneratorHonoursAlphaOnTransparentBackground) {
    std::vector<uint8_t> pixels(static_cast<size_t>(kVfxIconAtlasWidth) *
                                 kVfxIconAtlasHeight * 4u, 0xAA);
    VfxGenerateIconAtlas(pixels.data());

    // Whatever pre-filler bytes were in the buffer MUST be overwritten
    // — the generator initialises the full buffer to zero-alpha and
    // paints glyphs on top. Spot-check: no pixel may retain the 0xAA
    // pre-fill in its alpha channel.
    size_t nonZeroAlpha = 0;
    size_t alphaAA = 0;
    for (size_t i = 3; i < pixels.size(); i += 4) {
        if (pixels[i] == 0xAA) ++alphaAA;
        if (pixels[i] != 0x00) ++nonZeroAlpha;
    }
    EXPECT_EQ(alphaAA, 0u)
        << "generator must clear alpha (found " << alphaAA << " stale bytes)";
    EXPECT_GT(nonZeroAlpha, 0u)
        << "expected at least some opaque glyph pixels";
}
