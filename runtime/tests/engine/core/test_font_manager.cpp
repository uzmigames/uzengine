/// UzEngine FontManager — Tests
///
/// Validates FreeType initialization, font loading, glyph metric extraction,
/// caching behavior, and error handling for invalid inputs.

#include "font_manager.h"

#include <gtest/gtest.h>

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Fixture — ensures Init/Shutdown around each test
// ─────────────────────────────────────────────────────────────────────────────

class FontManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto& fm = FontManager::Get();
        ASSERT_TRUE(fm.Init());
    }

    void TearDown() override {
        FontManager::Get().Shutdown();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Initialization
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, InitSucceeds) {
    // Init already called in SetUp — verify double-init is a no-op.
    EXPECT_TRUE(FontManager::Get().Init());
}

TEST_F(FontManagerTest, ShutdownAndReinit) {
    auto& fm = FontManager::Get();
    fm.Shutdown();
    EXPECT_TRUE(fm.Init());
}

// ─────────────────────────────────────────────────────────────────────────────
// Font Loading
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, LoadSystemFont) {
    auto& fm = FontManager::Get();

    // Consolas is available on all modern Windows installations.
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);
    EXPECT_NE(fm.GetFace(handle), nullptr);
}

TEST_F(FontManagerTest, LoadSameFontReturnsSameHandle) {
    auto& fm = FontManager::Get();

    const auto h1 = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    const auto h2 = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    EXPECT_EQ(h1, h2);
}

TEST_F(FontManagerTest, DifferentSizeGetsDifferentHandle) {
    auto& fm = FontManager::Get();

    const auto h1 = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    const auto h2 = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 16.0f);
    ASSERT_NE(h1, kInvalidFont);
    ASSERT_NE(h2, kInvalidFont);
    EXPECT_NE(h1, h2);
}

TEST_F(FontManagerTest, InvalidPathReturnsInvalidHandle) {
    auto& fm = FontManager::Get();

    const auto handle = fm.LoadFont("nonexistent_font_path.ttf", 32.0f);
    EXPECT_EQ(handle, kInvalidFont);
}

TEST_F(FontManagerTest, LoadBeforeInitFails) {
    auto& fm = FontManager::Get();
    fm.Shutdown();

    // Library is now null — load should fail gracefully.
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    EXPECT_EQ(handle, kInvalidFont);

    // Re-init for TearDown consistency.
    fm.Init();
}

// ─────────────────────────────────────────────────────────────────────────────
// Face Access
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, GetFaceInvalidHandle) {
    EXPECT_EQ(FontManager::Get().GetFace(kInvalidFont), nullptr);
    EXPECT_EQ(FontManager::Get().GetFace(9999), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Glyph Metrics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, GlyphMetricsForLatinA) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    GlyphMetrics metrics{};
    ASSERT_TRUE(fm.GetGlyphMetrics(handle, 'A', metrics));

    EXPECT_GT(metrics.glyphIndex, 0u);
    EXPECT_GT(metrics.advanceX, 0.f);
    EXPECT_GT(metrics.width, 0.f);
    EXPECT_GT(metrics.height, 0.f);
    // bearingY should be positive for 'A' (extends above baseline).
    EXPECT_GT(metrics.bearingY, 0.f);
}

TEST_F(FontManagerTest, GlyphMetricsForSpace) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    GlyphMetrics metrics{};
    ASSERT_TRUE(fm.GetGlyphMetrics(handle, ' ', metrics));

    // Space has advance but no visible glyph area.
    EXPECT_GT(metrics.advanceX, 0.f);
    EXPECT_FLOAT_EQ(metrics.width, 0.f);
    EXPECT_FLOAT_EQ(metrics.height, 0.f);
}

TEST_F(FontManagerTest, GlyphMetricsInvalidHandle) {
    GlyphMetrics metrics{};
    EXPECT_FALSE(FontManager::Get().GetGlyphMetrics(kInvalidFont, 'A', metrics));
}

TEST_F(FontManagerTest, GlyphMetricsNonExistentCodepoint) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    GlyphMetrics metrics{};
    // Private use area codepoint — unlikely to exist in Consolas.
    EXPECT_FALSE(fm.GetGlyphMetrics(handle, 0xF0000, metrics));
}

// ─────────────────────────────────────────────────────────────────────────────
// Font-Level Metrics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, LineHeightPositive) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    const float lineHeight = fm.GetLineHeight(handle);
    EXPECT_GT(lineHeight, 0.f);
    // For a 32px font, line height should be roughly in the 30-50 pixel range.
    EXPECT_GT(lineHeight, 20.f);
    EXPECT_LT(lineHeight, 80.f);
}

TEST_F(FontManagerTest, AscenderPositive) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    const float ascender = fm.GetAscender(handle);
    EXPECT_GT(ascender, 0.f);
    // Ascender should be less than the full line height.
    EXPECT_LT(ascender, fm.GetLineHeight(handle));
}

TEST_F(FontManagerTest, LineHeightInvalidHandle) {
    EXPECT_FLOAT_EQ(FontManager::Get().GetLineHeight(kInvalidFont), 0.f);
}

TEST_F(FontManagerTest, AscenderInvalidHandle) {
    EXPECT_FLOAT_EQ(FontManager::Get().GetAscender(kInvalidFont), 0.f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Batch ASCII Metrics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, ASCIIMetricsReturns95Glyphs) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    const auto ascii = fm.GetASCIIMetrics(handle);
    // Consolas contains all 95 printable ASCII characters.
    EXPECT_EQ(ascii.size(), 95u);

    // Every glyph should have a positive advance.
    for (const auto& m : ascii) {
        EXPECT_GT(m.advanceX, 0.f);
    }
}

TEST_F(FontManagerTest, ASCIIMetricsInvalidHandle) {
    const auto ascii = FontManager::Get().GetASCIIMetrics(kInvalidFont);
    EXPECT_TRUE(ascii.empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Monospace Consistency
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(FontManagerTest, ConsolasIsMonospace) {
    auto& fm = FontManager::Get();
    const auto handle = fm.LoadFont("C:/Windows/Fonts/consola.ttf", 32.0f);
    ASSERT_NE(handle, kInvalidFont);

    // In a monospace font, all glyphs should have the same advance width.
    GlyphMetrics mA{};
    GlyphMetrics mW{};
    GlyphMetrics mI{};
    ASSERT_TRUE(fm.GetGlyphMetrics(handle, 'A', mA));
    ASSERT_TRUE(fm.GetGlyphMetrics(handle, 'W', mW));
    ASSERT_TRUE(fm.GetGlyphMetrics(handle, 'i', mI));

    EXPECT_FLOAT_EQ(mA.advanceX, mW.advanceX);
    EXPECT_FLOAT_EQ(mA.advanceX, mI.advanceX);
}

} // namespace uz::test
