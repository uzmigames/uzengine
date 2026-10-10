/// Unit tests for phase 14.1.0: UE5 Slate palette + typography
/// + radius + shadow token values.
///
/// Covers:
///   - UE5 five-step background ladder (`#0B0B0B → #1C1C1C →
///     #242424 → #2C2C2C → #383838`).
///   - UE5 blue `#0070E0` accent + `#FF7B00` orange accent.
///   - UE5 text ladder (`#E5E5E5 / #A0A0A0 / #707070 / #4A4A4A`).
///   - `cardBgSelected` == UE5 blue selection tint `#1C3A5E`.
///   - Typography scale tiers (menu 9 / body 10 / caption 11 /
///     header 12 / title 14).
///   - Radius scale includes new `none = 0` tier.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_theme.h"

using namespace uz;

namespace {

/// Tests whether `v` is within `tol` of `target` in every channel.
void ExpectNearColor(const Vec4& v, float r, float g, float b,
                     float tol = 0.01f) {
    EXPECT_NEAR(v.x, r, tol);
    EXPECT_NEAR(v.y, g, tol);
    EXPECT_NEAR(v.z, b, tol);
    EXPECT_NEAR(v.w, 1.0f, tol);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Background ladder
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, BackgroundBaseMatchesSlateDarkest) {
    // `#0B0B0B` = 0.043 / 0.043 / 0.043
    ExpectNearColor(VfxThemeColors().backgroundBase, 0.043f, 0.043f, 0.043f);
}

TEST(VfxEditorThemeV4Ue5, PanelBgMatchesSlatePanel) {
    // `#1C1C1C` = 0.110 / 0.110 / 0.110
    ExpectNearColor(VfxThemeColors().panelBg, 0.110f, 0.110f, 0.110f);
}

TEST(VfxEditorThemeV4Ue5, BarBgMatchesSlateHeader) {
    // `#242424` = 0.141 / 0.141 / 0.141
    ExpectNearColor(VfxThemeColors().barBg, 0.141f, 0.141f, 0.141f);
}

TEST(VfxEditorThemeV4Ue5, CardBgMatchesSlateRow) {
    // `#2C2C2C` = 0.173 / 0.173 / 0.173
    ExpectNearColor(VfxThemeColors().cardBg, 0.173f, 0.173f, 0.173f);
}

TEST(VfxEditorThemeV4Ue5, CardBgHoverMatchesSlateRowHover) {
    // `#383838` = 0.220 / 0.220 / 0.220
    ExpectNearColor(VfxThemeColors().cardBgHover, 0.220f, 0.220f, 0.220f);
}

TEST(VfxEditorThemeV4Ue5, LadderIsMonotonicallyAscending) {
    const auto& c = VfxThemeColors();
    EXPECT_LT(c.backgroundBase.x, c.panelBg.x);
    EXPECT_LT(c.panelBg.x,        c.barBg.x);
    EXPECT_LT(c.barBg.x,          c.cardBg.x);
    EXPECT_LT(c.cardBg.x,         c.cardBgHover.x);
}

// ─────────────────────────────────────────────────────────────────────────────
// Accents
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, AccentPrimaryIsUe5Blue) {
    // `#0070E0` = 0.000 / 0.439 / 0.878
    ExpectNearColor(VfxThemeColors().accentPrimary, 0.000f, 0.439f, 0.878f);
}

TEST(VfxEditorThemeV4Ue5, AccentOrangeIsUe5Orange) {
    // `#FF7B00` = 1.000 / 0.482 / 0.000
    ExpectNearColor(VfxThemeColors().accentOrange, 1.000f, 0.482f, 0.000f);
}

TEST(VfxEditorThemeV4Ue5, CardBgSelectedIsUe5SelectionBlue) {
    // `#1C3A5E` = 0.110 / 0.227 / 0.369
    ExpectNearColor(VfxThemeColors().cardBgSelected,
                    0.110f, 0.227f, 0.369f);
    // Blue-bias invariant for the selection tint.
    const auto& c = VfxThemeColors().cardBgSelected;
    EXPECT_LT(c.x, c.z);
    EXPECT_LT(c.y, c.z);
}

// ─────────────────────────────────────────────────────────────────────────────
// Text ladder
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, TextPrimaryMatchesUe5) {
    // `#E5E5E5` = 0.898 / 0.898 / 0.898
    ExpectNearColor(VfxThemeColors().textPrimary, 0.898f, 0.898f, 0.898f);
}

TEST(VfxEditorThemeV4Ue5, TextSecondaryMatchesUe5) {
    // `#A0A0A0` = 0.627 / 0.627 / 0.627
    ExpectNearColor(VfxThemeColors().textSecondary,
                    0.627f, 0.627f, 0.627f);
}

TEST(VfxEditorThemeV4Ue5, TextDimMatchesUe5) {
    // `#707070` = 0.439 / 0.439 / 0.439
    ExpectNearColor(VfxThemeColors().textDim, 0.439f, 0.439f, 0.439f);
}

TEST(VfxEditorThemeV4Ue5, TextDisabledMatchesUe5) {
    // `#4A4A4A` = 0.290 / 0.290 / 0.290
    ExpectNearColor(VfxThemeColors().textDisabled,
                    0.290f, 0.290f, 0.290f);
}

TEST(VfxEditorThemeV4Ue5, TextLadderMonotonicallyDescending) {
    const auto& c = VfxThemeColors();
    EXPECT_GT(c.textPrimary.x,   c.textSecondary.x);
    EXPECT_GT(c.textSecondary.x, c.textDim.x);
    EXPECT_GT(c.textDim.x,       c.textDisabled.x);
}

// ─────────────────────────────────────────────────────────────────────────────
// Borders
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, BordersMatchUe5) {
    // `#1A1A1A` subtle, `#404040` strong.
    ExpectNearColor(VfxThemeColors().borderSubtle, 0.102f, 0.102f, 0.102f);
    ExpectNearColor(VfxThemeColors().borderStrong, 0.251f, 0.251f, 0.251f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Typography
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, TypographyScaleTiers) {
    const auto& t = VfxThemeTypography();
    EXPECT_FLOAT_EQ(t.menuPx,     9.0f);
    EXPECT_FLOAT_EQ(t.bodyPx,    10.0f);
    EXPECT_FLOAT_EQ(t.captionPx, 11.0f);
    EXPECT_FLOAT_EQ(t.headerPx,  12.0f);
    EXPECT_FLOAT_EQ(t.titlePx,   14.0f);
    // Monotonically ascending.
    EXPECT_LT(t.menuPx,    t.bodyPx);
    EXPECT_LT(t.bodyPx,    t.captionPx);
    EXPECT_LT(t.captionPx, t.headerPx);
    EXPECT_LT(t.headerPx,  t.titlePx);
}

// ─────────────────────────────────────────────────────────────────────────────
// Radius
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, RadiusNoneTierIsZero) {
    EXPECT_FLOAT_EQ(VfxThemeRadius().none, 0.0f);
}

TEST(VfxEditorThemeV4Ue5, RadiusShrunkFromV3) {
    const auto& r = VfxThemeRadius();
    // UE5 uses tight corners — lg shrinks from 8 (V3) to 6.
    EXPECT_LE(r.lg, 6.0f);
    EXPECT_LE(r.md, 4.0f);
    EXPECT_LE(r.sm, 3.0f);
    EXPECT_LE(r.xs, 2.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Shadows
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxEditorThemeV4Ue5, ShadowsLighterThanV2) {
    const auto& s = VfxThemeShadows();
    // V4 UE5 caps shadow alpha at 0.50 (dialog); V2 used 0.62.
    EXPECT_LE(s.dialog.alpha, 0.50f);
    EXPECT_LE(s.popup.alpha,  0.40f);
    EXPECT_LE(s.hover.alpha,  0.20f);
}
