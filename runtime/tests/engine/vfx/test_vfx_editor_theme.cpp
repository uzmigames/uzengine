/// Unit tests for phase 12.1.0: `VfxEditorTheme` palette + scales +
/// icon glyph table + `VfxContrastRatio` helper.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_editor_theme.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// VfxTheme() — single source of truth
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorTheme, VfxThemeReturnsSameInstance) {
    const VfxEditorTheme& a = VfxTheme();
    const VfxEditorTheme& b = VfxTheme();
    EXPECT_EQ(&a, &b);
}

TEST(VfxEditorTheme, ColorAccessorDelegatesToTheme) {
    EXPECT_EQ(&VfxThemeColors(), &VfxTheme().colors);
    EXPECT_EQ(&VfxThemeSpacing(), &VfxTheme().spacing);
    EXPECT_EQ(&VfxThemeTypography(), &VfxTheme().typography);
    EXPECT_EQ(&VfxThemeRadius(), &VfxTheme().radius);
}

// ─────────────────────────────────────────────────────────────────────────────
// Palette sanity — every color is a finite non-degenerate Vec4
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorTheme, AllColorsAreFiniteUnitRange) {
    auto check = [](const Vec4& v, const char* name) {
        EXPECT_GE(v.x, 0.0f) << name;
        EXPECT_LE(v.x, 1.0f) << name;
        EXPECT_GE(v.y, 0.0f) << name;
        EXPECT_LE(v.y, 1.0f) << name;
        EXPECT_GE(v.z, 0.0f) << name;
        EXPECT_LE(v.z, 1.0f) << name;
        EXPECT_GE(v.w, 0.0f) << name;
        EXPECT_LE(v.w, 1.0f) << name;
    };
    const auto& c = VfxThemeColors();
    check(c.backgroundBase, "backgroundBase");
    check(c.panelBg,        "panelBg");
    check(c.panelBgAlt,     "panelBgAlt");
    check(c.cardBg,         "cardBg");
    check(c.cardBgHover,    "cardBgHover");
    check(c.cardBgSelected, "cardBgSelected");
    check(c.textPrimary,    "textPrimary");
    check(c.textSecondary,  "textSecondary");
    check(c.textDim,        "textDim");
    check(c.accentPrimary,  "accentPrimary");
    check(c.accentMuted,    "accentMuted");
    check(c.stateSuccess,   "stateSuccess");
    check(c.stateWarning,   "stateWarning");
    check(c.stateError,     "stateError");
    check(c.stateInfo,      "stateInfo");
}

// ─────────────────────────────────────────────────────────────────────────────
// Spacing / typography / radius scales have strictly ordered values
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorTheme, SpacingScaleIsMonotonic) {
    const auto& s = VfxThemeSpacing();
    EXPECT_LT(s.xs, s.sm);
    EXPECT_LT(s.sm, s.md);
    EXPECT_LT(s.md, s.lg);
    EXPECT_LT(s.lg, s.xl);
    EXPECT_LT(s.xl, s.xxl);
}

TEST(VfxEditorTheme, TypographyScaleIsMonotonic) {
    const auto& t = VfxThemeTypography();
    EXPECT_LT(t.captionPx, t.bodyPx);
    EXPECT_LT(t.bodyPx,    t.headerPx);
    EXPECT_LT(t.headerPx,  t.titlePx);
    // codePx sits between captionPx and bodyPx (monospace choice)
    EXPECT_LE(t.captionPx, t.codePx);
    EXPECT_LE(t.codePx,    t.bodyPx);
}

TEST(VfxEditorTheme, RadiusScaleIsMonotonic) {
    const auto& r = VfxThemeRadius();
    EXPECT_LT(r.sm, r.md);
    EXPECT_LT(r.md, r.lg);
}

// ─────────────────────────────────────────────────────────────────────────────
// WCAG contrast guarantees
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorTheme, ContrastRatio_IdenticalColorsIsOne) {
    const Vec4 c{0.5f, 0.5f, 0.5f, 1.0f};
    EXPECT_NEAR(VfxContrastRatio(c, c), 1.0f, 1e-4f);
}

TEST(VfxEditorTheme, ContrastRatio_WhiteOnBlackIs21) {
    const Vec4 white{1.0f, 1.0f, 1.0f, 1.0f};
    const Vec4 black{0.0f, 0.0f, 0.0f, 1.0f};
    EXPECT_NEAR(VfxContrastRatio(white, black), 21.0f, 0.1f);
}

TEST(VfxEditorTheme, ContrastRatio_Symmetric) {
    const Vec4 a{0.3f, 0.3f, 0.4f, 1.0f};
    const Vec4 b{0.9f, 0.9f, 0.9f, 1.0f};
    EXPECT_FLOAT_EQ(VfxContrastRatio(a, b), VfxContrastRatio(b, a));
}

TEST(VfxEditorTheme, TextPrimaryOnPanelBg_MeetsWcagAA) {
    const auto& c = VfxThemeColors();
    EXPECT_GE(VfxContrastRatio(c.textPrimary, c.panelBg), 4.5f);
}

TEST(VfxEditorTheme, AccentPrimaryOnPanelBg_MeetsMinContrast) {
    const auto& c = VfxThemeColors();
    EXPECT_GE(VfxContrastRatio(c.accentPrimary, c.panelBg), 3.0f);
}

TEST(VfxEditorTheme, TextDimOnPanelBg_MeetsMinContrast) {
    const auto& c = VfxThemeColors();
    EXPECT_GE(VfxContrastRatio(c.textDim, c.panelBg), 3.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Icon glyph table
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorIcons, TableHasExpectedCount) {
    // Spec says >= 30 glyphs; implementation ships more.
    EXPECT_GE(kVfxIconCount, 30);
}

TEST(VfxEditorIcons, EveryGlyphIsNonEmptyCString) {
    const VfxIconEntry* table = VfxAllIcons();
    for (int i = 0; i < kVfxIconCount; ++i) {
        ASSERT_NE(table[i].glyph, nullptr) << "entry " << i << " name=" << table[i].name;
        EXPECT_GT(std::strlen(table[i].glyph), 0u)
            << "entry " << i << " name=" << table[i].name;
        EXPECT_NE(table[i].name, nullptr) << "entry " << i;
    }
}

TEST(VfxEditorIcons, EveryGlyphIsAsciiPrintable) {
    // BitmapFont bakes ASCII 32-126 only. Any non-ASCII glyph would
    // render as '?'.
    const VfxIconEntry* table = VfxAllIcons();
    for (int i = 0; i < kVfxIconCount; ++i) {
        const char* g = table[i].glyph;
        for (const char* p = g; *p != '\0'; ++p) {
            const unsigned char c = static_cast<unsigned char>(*p);
            EXPECT_GE(c, 32u) << "entry " << table[i].name << " byte 0x" << std::hex << int(c);
            EXPECT_LE(c, 126u) << "entry " << table[i].name << " byte 0x" << std::hex << int(c);
        }
    }
}

TEST(VfxEditorIcons, TransportIconsAreDistinct) {
    // Visually important that play/pause/stop etc. are not accidentally
    // aliased to the same string.
    const std::string play(VFX_ICON_PLAY);
    const std::string pause(VFX_ICON_PAUSE);
    const std::string stop(VFX_ICON_STOP);
    const std::string stepFwd(VFX_ICON_STEP_FWD);
    const std::string stepBack(VFX_ICON_STEP_BACK);
    EXPECT_NE(play, pause);
    EXPECT_NE(pause, stop);
    EXPECT_NE(stepFwd, stepBack);
    EXPECT_NE(play, stepFwd);
}
