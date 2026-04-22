/// Unit tests for phase 13.5.0: Theme V2 token additions.
///
/// Covers:
///
///   - `VfxRadiusScale` carries four tiers (xs / sm / md / lg) with
///     the documented 2 / 4 / 6 / 8 px values — widgets consume
///     these by semantic role, never by raw literal.
///   - `VfxShadowScale` publishes three drop-shadow presets
///     (hover / popup / dialog) with monotonically increasing
///     offsetY + blur + alpha so deeper UI layers cast stronger
///     shadows.
///   - `VfxColorPalette::accentGlow` exists at an alpha < 0.25 so
///     hover tints composite as an overlay without washing the card
///     colour.
///   - Accessor shorthand functions (`VfxThemeRadius`,
///     `VfxThemeShadows`) return the same instance on repeat calls.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_theme.h"

using namespace uz;

TEST(VfxEditorThemeV2, RadiusScaleHasFiveTiers) {
    const VfxRadiusScale& r = VfxThemeRadius();
    // V4 UE5 values — panels square (`none=0`), tight corners
    // throughout.
    EXPECT_FLOAT_EQ(r.none, 0.0f);
    EXPECT_FLOAT_EQ(r.xs,   2.0f);
    EXPECT_FLOAT_EQ(r.sm,   3.0f);
    EXPECT_FLOAT_EQ(r.md,   4.0f);
    EXPECT_FLOAT_EQ(r.lg,   6.0f);
    // Semantic invariant: tiers are monotonically ascending.
    EXPECT_LT(r.none, r.xs);
    EXPECT_LT(r.xs,   r.sm);
    EXPECT_LT(r.sm,   r.md);
    EXPECT_LT(r.md,   r.lg);
}

TEST(VfxEditorThemeV2, ShadowScaleHasThreeTiers) {
    const VfxShadowScale& s = VfxThemeShadows();
    // Hover: subtle lift.
    EXPECT_GT(s.hover.blur, 0.0f);
    EXPECT_LT(s.hover.alpha, s.popup.alpha);
    // Popup: used by dropdowns + context menus.
    EXPECT_GE(s.popup.offsetY, s.hover.offsetY);
    EXPECT_GE(s.popup.blur,    s.hover.blur);
    EXPECT_GE(s.popup.alpha,   s.hover.alpha);
    // Dialog: modal panels — heaviest.
    EXPECT_GE(s.dialog.offsetY, s.popup.offsetY);
    EXPECT_GE(s.dialog.blur,    s.popup.blur);
    EXPECT_GE(s.dialog.alpha,   s.popup.alpha);
}

TEST(VfxEditorThemeV2, ShadowScaleValuesMatchDesignIntent) {
    const VfxShadowScale& s = VfxThemeShadows();
    // V4 UE5 intent — lighter shadows than V2.
    EXPECT_NEAR(s.hover.offsetY,   1.0f,  0.01f);
    EXPECT_NEAR(s.hover.blur,      2.0f,  0.01f);
    EXPECT_NEAR(s.popup.offsetY,   2.0f,  0.01f);
    EXPECT_NEAR(s.popup.blur,      6.0f,  0.01f);
    EXPECT_NEAR(s.dialog.offsetY,  4.0f,  0.01f);
    EXPECT_NEAR(s.dialog.blur,    12.0f,  0.01f);
}

TEST(VfxEditorThemeV2, AccentGlowIsSubtleOverlay) {
    const VfxColorPalette& c = VfxThemeColors();
    EXPECT_GT(c.accentGlow.w, 0.0f)
        << "accent glow must be visible";
    EXPECT_LT(c.accentGlow.w, 0.25f)
        << "accent glow must stay subtle so hovered card colour reads through";
}

TEST(VfxEditorThemeV2, RadiusShorthandReturnsSameInstance) {
    const VfxRadiusScale& a = VfxThemeRadius();
    const VfxRadiusScale& b = VfxThemeRadius();
    EXPECT_EQ(&a, &b);
}

TEST(VfxEditorThemeV2, ShadowShorthandReturnsSameInstance) {
    const VfxShadowScale& a = VfxThemeShadows();
    const VfxShadowScale& b = VfxThemeShadows();
    EXPECT_EQ(&a, &b);
}
