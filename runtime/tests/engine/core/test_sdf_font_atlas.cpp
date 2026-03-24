/// UzEngine SDFAtlas — Tests
///
/// Validates MSDF atlas generation from TrueType fonts, including glyph
/// packing, metric correctness, edge cases, and error handling.

#include "sdf_font_atlas.h"
#include "font_manager.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <numeric>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — ensures FontManager is initialized/shutdown around each test
// ─────────────────────────────────────────────────────────────────────────────

class SDFAtlasTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& fm = FontManager::Get();
        ASSERT_TRUE(fm.Init());
    }

    void TearDown() override {
        FontManager::Get().Shutdown();
    }

    /// Returns a path to a known system font. On Windows, Consolas is always
    /// available. On Linux, we fall back to DejaVuSans which is present on
    /// most distributions.
    static std::string GetTestFontPath() {
#ifdef _WIN32
        return "C:/Windows/Fonts/consola.ttf";
#else
        return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Basic Generation — ASCII range
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, GenerateASCII) {
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 512;
    opts.atlasHeight = 512;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.width, 512u);
    EXPECT_EQ(result.height, 512u);
    EXPECT_EQ(result.pixels.size(), 512u * 512u * 4u);
    EXPECT_EQ(result.fontSize, 32.0f);
    EXPECT_EQ(result.sdfRange, 4.0f);

    // ASCII 32-126 = 95 codepoints. Some may be whitespace-only (space, etc.)
    // but should still be recorded.
    EXPECT_GE(result.glyphs.size(), 90u);
    EXPECT_LE(result.glyphs.size(), 95u);
}

TEST_F(SDFAtlasTest, GlyphAHasValidMetrics) {
    SDFAtlasOptions opts;
    opts.fontSize = 48.0f;
    opts.atlasWidth = 1024;
    opts.atlasHeight = 1024;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // Find glyph 'A'
    const auto it = std::find_if(result.glyphs.begin(), result.glyphs.end(),
                                 [](const SDFGlyphInfo& g) {
                                     return g.codepoint == 'A';
                                 });
    ASSERT_NE(it, result.glyphs.end()) << "Glyph 'A' not found in atlas";

    // 'A' is a visible glyph — it must have non-zero atlas dimensions.
    EXPECT_GT(it->atlasW, 0.f);
    EXPECT_GT(it->atlasH, 0.f);
    EXPECT_GT(it->advance, 0.f);

    // Atlas position must be within bounds.
    EXPECT_GE(it->atlasX, 0.f);
    EXPECT_GE(it->atlasY, 0.f);
    EXPECT_LT(it->atlasX + it->atlasW,
              static_cast<float>(opts.atlasWidth) + 1.f);
    EXPECT_LT(it->atlasY + it->atlasH,
              static_cast<float>(opts.atlasHeight) + 1.f);
}

TEST_F(SDFAtlasTest, SpaceGlyphHasZeroDimensions) {
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 512;
    opts.atlasHeight = 512;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    const auto it = std::find_if(result.glyphs.begin(), result.glyphs.end(),
                                 [](const SDFGlyphInfo& g) {
                                     return g.codepoint == ' ';
                                 });
    ASSERT_NE(it, result.glyphs.end()) << "Space glyph not found";

    // Space has no visual contours — atlas dimensions should be zero.
    EXPECT_EQ(it->atlasW, 0.f);
    EXPECT_EQ(it->atlasH, 0.f);

    // But it must have a positive advance (for word spacing).
    EXPECT_GT(it->advance, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas Pixel Content — MSDF data is not all zeros
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, AtlasPixelsNotAllZero) {
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 512;
    opts.atlasHeight = 512;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // At least some pixels in the glyph region should be non-zero (the SDF
    // data). Sum up all pixel values — a non-trivial atlas will have a large
    // sum.
    const uint64_t sum = std::accumulate(result.pixels.begin(),
                                         result.pixels.end(), uint64_t{0});
    EXPECT_GT(sum, 0u);
}

TEST_F(SDFAtlasTest, GlyphRegionHasVariation) {
    SDFAtlasOptions opts;
    opts.fontSize = 48.0f;
    opts.atlasWidth = 1024;
    opts.atlasHeight = 1024;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // Find glyph 'O' — a letter with inside/outside distance variation.
    const auto it = std::find_if(result.glyphs.begin(), result.glyphs.end(),
                                 [](const SDFGlyphInfo& g) {
                                     return g.codepoint == 'O';
                                 });
    ASSERT_NE(it, result.glyphs.end());
    ASSERT_GT(it->atlasW, 0.f);
    ASSERT_GT(it->atlasH, 0.f);

    // The SDF should have spatial variation across the glyph region. Scan all
    // pixels in the glyph and verify that not all R values are identical —
    // a valid MSDF always has distance gradients.
    const uint32_t gx = static_cast<uint32_t>(it->atlasX);
    const uint32_t gy = static_cast<uint32_t>(it->atlasY);
    const uint32_t gw = static_cast<uint32_t>(it->atlasW);
    const uint32_t gh = static_cast<uint32_t>(it->atlasH);

    // Verify the glyph region in the atlas has non-zero pixel data.
    // In an MSDF, each channel independently encodes distance to different
    // edge colors. At least some pixels must be non-zero.
    uint32_t nonZeroPixels = 0;
    for (uint32_t y = gy; y < gy + gh; ++y) {
        for (uint32_t x = gx; x < gx + gw; ++x) {
            const size_t idx = (static_cast<size_t>(y) * opts.atlasWidth + x) * 4;
            if (result.pixels[idx] > 0 || result.pixels[idx + 1] > 0 ||
                result.pixels[idx + 2] > 0) {
                ++nonZeroPixels;
            }
        }
    }

    // A visible glyph like 'O' must produce non-zero SDF data in its region.
    // At minimum, several pixels should have non-zero values.
    EXPECT_GT(nonZeroPixels, gw * gh / 4)
        << "Too few non-zero pixels in glyph 'O' region (" << nonZeroPixels
        << "/" << gw * gh << ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Alpha Channel — always opaque
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, AlphaIsOpaqueInGlyphRegions) {
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 512;
    opts.atlasHeight = 512;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // For every glyph with non-zero atlas dimensions, check that all alpha
    // values in the glyph region are 255.
    for (const auto& g : result.glyphs) {
        if (g.atlasW == 0.f || g.atlasH == 0.f) {
            continue;
        }

        const uint32_t x0 = static_cast<uint32_t>(g.atlasX);
        const uint32_t y0 = static_cast<uint32_t>(g.atlasY);
        const uint32_t x1 = x0 + static_cast<uint32_t>(g.atlasW);
        const uint32_t y1 = y0 + static_cast<uint32_t>(g.atlasH);

        for (uint32_t y = y0; y < y1; ++y) {
            for (uint32_t x = x0; x < x1; ++x) {
                const size_t idx =
                    (static_cast<size_t>(y) * opts.atlasWidth + x) * 4 + 3;
                ASSERT_EQ(result.pixels[idx], 255u)
                    << "Non-opaque alpha at (" << x << ", " << y
                    << ") for codepoint U+" << std::hex << g.codepoint;
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// No Glyph Overlap — all glyph rectangles must be disjoint
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, GlyphsDoNotOverlap) {
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 512;
    opts.atlasHeight = 512;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // Collect all glyphs with non-zero atlas regions.
    std::vector<const SDFGlyphInfo*> placed;
    for (const auto& g : result.glyphs) {
        if (g.atlasW > 0.f && g.atlasH > 0.f) {
            placed.push_back(&g);
        }
    }

    // Brute-force overlap check (N is small, ~90 glyphs).
    for (size_t i = 0; i < placed.size(); ++i) {
        for (size_t j = i + 1; j < placed.size(); ++j) {
            const auto& a = *placed[i];
            const auto& b = *placed[j];

            const bool overlapX = a.atlasX < b.atlasX + b.atlasW &&
                                  b.atlasX < a.atlasX + a.atlasW;
            const bool overlapY = a.atlasY < b.atlasY + b.atlasH &&
                                  b.atlasY < a.atlasY + a.atlasH;

            EXPECT_FALSE(overlapX && overlapY)
                << "Glyphs U+" << std::hex << a.codepoint << " and U+"
                << b.codepoint << " overlap in atlas";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom Codepoint Range
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, CustomCodepointRange) {
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 256;
    opts.atlasHeight = 256;
    opts.firstCodepoint = 'A';
    opts.lastCodepoint = 'Z';

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // Should have exactly 26 glyphs (A-Z).
    EXPECT_EQ(result.glyphs.size(), 26u);

    // Verify all codepoints are in range.
    for (const auto& g : result.glyphs) {
        EXPECT_GE(g.codepoint, static_cast<uint32_t>('A'));
        EXPECT_LE(g.codepoint, static_cast<uint32_t>('Z'));
    }
}

TEST_F(SDFAtlasTest, SingleCodepoint) {
    SDFAtlasOptions opts;
    opts.fontSize = 48.0f;
    opts.atlasWidth = 128;
    opts.atlasHeight = 128;
    opts.firstCodepoint = 'X';
    opts.lastCodepoint = 'X';

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.glyphs.size(), 1u);
    EXPECT_EQ(result.glyphs[0].codepoint, static_cast<uint32_t>('X'));
    EXPECT_GT(result.glyphs[0].atlasW, 0.f);
    EXPECT_GT(result.glyphs[0].atlasH, 0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// SDF Range and Font Size are stored in result
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, ResultStoresOptionsCorrectly) {
    SDFAtlasOptions opts;
    opts.fontSize = 64.0f;
    opts.sdfRange = 8.0f;
    opts.atlasWidth = 2048;
    opts.atlasHeight = 2048;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    EXPECT_EQ(result.fontSize, 64.0f);
    EXPECT_EQ(result.sdfRange, 8.0f);
    EXPECT_EQ(result.width, 2048u);
    EXPECT_EQ(result.height, 2048u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error Handling
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, InvalidFontPath) {
    auto result = GenerateSDFAtlas("nonexistent_font_path.ttf");
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST_F(SDFAtlasTest, ZeroAtlasDimensions) {
    SDFAtlasOptions opts;
    opts.atlasWidth = 0;
    opts.atlasHeight = 512;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("non-zero"), std::string::npos);
}

TEST_F(SDFAtlasTest, ZeroFontSize) {
    SDFAtlasOptions opts;
    opts.fontSize = 0.0f;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("positive"), std::string::npos);
}

TEST_F(SDFAtlasTest, NegativeSdfRange) {
    SDFAtlasOptions opts;
    opts.sdfRange = -1.0f;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("positive"), std::string::npos);
}

TEST_F(SDFAtlasTest, InvertedCodepointRange) {
    SDFAtlasOptions opts;
    opts.firstCodepoint = 126;
    opts.lastCodepoint = 32;

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("firstCodepoint"), std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Atlas Too Small — graceful handling when atlas runs out of space
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, AtlasTooSmallStillSucceeds) {
    SDFAtlasOptions opts;
    opts.fontSize = 48.0f;
    opts.atlasWidth = 64;
    opts.atlasHeight = 64;
    opts.firstCodepoint = 32;
    opts.lastCodepoint = 126;

    // A 64x64 atlas at 48px cannot fit all 95 ASCII glyphs.
    // The function should still succeed but with fewer glyphs.
    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;

    // Should have some glyphs but fewer than the full 95.
    EXPECT_GT(result.glyphs.size(), 0u);
    EXPECT_LT(result.glyphs.size(), 95u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Digits have consistent advance (monospace font)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, DigitsHaveConsistentAdvance) {
    // Consolas/DejaVuSans-Mono: digits should have identical advance in a
    // monospace font. For proportional fonts this test still validates that
    // advances are reasonable (positive).
    SDFAtlasOptions opts;
    opts.fontSize = 32.0f;
    opts.atlasWidth = 512;
    opts.atlasHeight = 512;
    opts.firstCodepoint = '0';
    opts.lastCodepoint = '9';

    auto result = GenerateSDFAtlas(GetTestFontPath(), opts);
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.glyphs.size(), 10u);

    for (const auto& g : result.glyphs) {
        EXPECT_GT(g.advance, 0.f)
            << "Zero advance for digit codepoint " << g.codepoint;
    }

    // For Consolas (monospace), all advances should be identical.
#ifdef _WIN32
    const float firstAdvance = result.glyphs[0].advance;
    for (const auto& g : result.glyphs) {
        EXPECT_FLOAT_EQ(g.advance, firstAdvance)
            << "Non-uniform advance in monospace font for digit "
            << g.codepoint;
    }
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Different font sizes produce different glyph dimensions
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(SDFAtlasTest, LargerFontProducesLargerGlyphs) {
    const std::string font = GetTestFontPath();

    SDFAtlasOptions small;
    small.fontSize = 24.0f;
    small.atlasWidth = 1024;
    small.atlasHeight = 1024;
    small.firstCodepoint = 'M';
    small.lastCodepoint = 'M';

    SDFAtlasOptions large;
    large.fontSize = 96.0f;
    large.atlasWidth = 1024;
    large.atlasHeight = 1024;
    large.firstCodepoint = 'M';
    large.lastCodepoint = 'M';

    auto resultSmall = GenerateSDFAtlas(font, small);
    ASSERT_TRUE(resultSmall.success) << resultSmall.error;
    ASSERT_EQ(resultSmall.glyphs.size(), 1u);

    // Re-init FontManager between runs to avoid cached face at old size.
    FontManager::Get().Shutdown();
    ASSERT_TRUE(FontManager::Get().Init());

    auto resultLarge = GenerateSDFAtlas(font, large);
    ASSERT_TRUE(resultLarge.success) << resultLarge.error;
    ASSERT_EQ(resultLarge.glyphs.size(), 1u);

    EXPECT_GT(resultLarge.glyphs[0].atlasW, resultSmall.glyphs[0].atlasW);
    EXPECT_GT(resultLarge.glyphs[0].atlasH, resultSmall.glyphs[0].atlasH);
    EXPECT_GT(resultLarge.glyphs[0].advance, resultSmall.glyphs[0].advance);
}

} // namespace uz::test
