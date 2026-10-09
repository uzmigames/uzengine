/// Unit tests for Slate UI Brush + StyleSet + StyleStack
/// (Phase 18 / Step 6).
///
/// Covers:
///   - Brush is trivially copyable; equality works.
///   - StyleSet missing key returns the supplied fallback.
///   - StyleStack push/pop semantics; later pushes override; missing
///     keys fall through to the next set.
///   - claude.dark contains all of the design's required keys.

#include "engine/ui/slate_brush.h"
#include "engine/ui/slate_style.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace uz;
using namespace uz::ui;

// ─────────────────────────────────────────────────────────────────────────────
// Brush
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateBrush, IsTriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<Brush>);
    Brush a = MakeSolidColor(Color{1, 0, 0, 1}, 4.0f);
    Brush b = a;
    EXPECT_EQ(a, b);
}

TEST(SlateBrush, MakeSolidColorPopulatesFields) {
    Brush b = MakeSolidColor(Color{0.2f, 0.3f, 0.4f, 1.0f}, 6.0f);
    EXPECT_EQ(b.kind, BrushKind::SolidColor);
    EXPECT_FLOAT_EQ(b.tintColor.x, 0.2f);
    EXPECT_FLOAT_EQ(b.cornerRadius, 6.0f);
    EXPECT_FLOAT_EQ(b.borderThickness, 0.0f);
    EXPECT_FALSE(b.HasBorder());
    EXPECT_FALSE(b.HasShadow());
}

TEST(SlateBrush, MakeBorderedReportsBorder) {
    Brush b = MakeBordered(Color{0, 0, 0, 1}, 2.0f, Color{1, 1, 1, 1}, 3.0f);
    EXPECT_TRUE(b.HasBorder());
    EXPECT_FLOAT_EQ(b.borderThickness, 2.0f);
    EXPECT_FLOAT_EQ(b.cornerRadius, 3.0f);
}

TEST(SlateBrush, MakeShadowedReportsShadow) {
    Brush b = MakeShadowed(Color{0, 0, 0, 1}, Vec2{0, 2}, 4.0f, Color{0, 0, 0, 0.5f});
    EXPECT_TRUE(b.HasShadow());
    EXPECT_FLOAT_EQ(b.shadowBlur, 4.0f);
}

TEST(SlateBrush, EqualityDistinguishesValues) {
    Brush a = MakeSolidColor(Color{1, 0, 0, 1}, 4.0f);
    Brush b = MakeSolidColor(Color{0, 1, 0, 1}, 4.0f);
    EXPECT_NE(a, b);

    Brush c = MakeSolidColor(Color{1, 0, 0, 1}, 4.0f);
    EXPECT_EQ(a, c);
}

// ─────────────────────────────────────────────────────────────────────────────
// StyleSet
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateStyle, GetColorFallbackOnMissing) {
    StyleSet s;
    s.SetColor("accent", Color{0, 0.5f, 1, 1});
    EXPECT_FLOAT_EQ(s.GetColor("accent").y, 0.5f);

    Color fallback = Color{1, 0, 1, 1};
    Color got = s.GetColor("missing-key", fallback);
    EXPECT_FLOAT_EQ(got.x, 1.0f);
    EXPECT_FLOAT_EQ(got.y, 0.0f);
    EXPECT_FLOAT_EQ(got.z, 1.0f);
}

TEST(SlateStyle, HasColorAndCount) {
    StyleSet s;
    EXPECT_FALSE(s.HasColor("a"));
    EXPECT_EQ(s.ColorCount(), 0u);
    s.SetColor("a", Color{});
    s.SetColor("b", Color{});
    EXPECT_TRUE(s.HasColor("a"));
    EXPECT_EQ(s.ColorCount(), 2u);
}

TEST(SlateStyle, BrushRoundtrip) {
    StyleSet s;
    Brush b = MakeBordered(Color{0.1f, 0.1f, 0.1f, 1}, 1, Color{1, 1, 1, 1}, 2);
    s.SetBrush("panel.body", b);
    EXPECT_TRUE(s.HasBrush("panel.body"));
    EXPECT_EQ(s.GetBrush("panel.body"), b);
}

// ─────────────────────────────────────────────────────────────────────────────
// StyleStack
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateStyleStack, PushOverridesAndPopRestores) {
    auto base = std::make_shared<StyleSet>();
    base->SetColor("accent", Color{0, 0, 1, 1}); // blue

    auto override_ = std::make_shared<StyleSet>();
    override_->SetColor("accent", Color{1, 0, 0, 1}); // red

    StyleStack stack;
    stack.Push(base);
    EXPECT_FLOAT_EQ(stack.GetColor("accent").z, 1.0f); // blue

    stack.Push(override_);
    EXPECT_FLOAT_EQ(stack.GetColor("accent").x, 1.0f); // red

    stack.Pop();
    EXPECT_FLOAT_EQ(stack.GetColor("accent").z, 1.0f); // blue back
}

TEST(SlateStyleStack, FallthroughForMissingKeys) {
    auto base = std::make_shared<StyleSet>();
    base->SetColor("fg.0", Color{0.9f, 0.9f, 0.9f, 1});

    auto over = std::make_shared<StyleSet>();
    over->SetColor("accent", Color{1, 0, 0, 1}); // does NOT define fg.0

    StyleStack stack;
    stack.Push(base);
    stack.Push(over);

    // accent comes from over.
    EXPECT_FLOAT_EQ(stack.GetColor("accent").x, 1.0f);
    // fg.0 falls through to base.
    EXPECT_FLOAT_EQ(stack.GetColor("fg.0").x, 0.9f);
}

TEST(SlateStyleStack, MissingEverywhereReturnsFallback) {
    StyleStack stack;
    stack.Push(std::make_shared<StyleSet>()); // empty
    Color fb{1, 1, 0, 1};
    EXPECT_FLOAT_EQ(stack.GetColor("nope", fb).y, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// claude.dark builtin completeness
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateClaudeDark, ContainsAllRequiredColorKeys) {
    auto ss = CreateClaudeDarkStyleSet();
    const std::vector<std::string> required = {
        "bg.0", "bg.1", "bg.2", "bg.3", "bg.4",
        "line.0", "line.1", "line.2",
        "fg.0", "fg.1", "fg.2", "fg.3",
        "accent", "accent.soft", "accent.line",
        "warn", "ok", "err",
        "magenta", "purple", "cyan",
        "axis.x", "axis.y", "axis.z",
    };
    for (const auto& k : required) {
        EXPECT_TRUE(ss->HasColor(k)) << "missing color key: " << k;
    }
}

TEST(SlateClaudeDark, ContainsAllRequiredBrushKeys) {
    auto ss = CreateClaudeDarkStyleSet();
    const std::vector<std::string> required = {
        "panel.head", "panel.body",
        "tab.active", "tab.inactive",
        "button.primary", "button.ghost",
    };
    for (const auto& k : required) {
        EXPECT_TRUE(ss->HasBrush(k)) << "missing brush key: " << k;
    }
}

TEST(SlateClaudeDark, BgZeroIsDarkest) {
    auto ss = CreateClaudeDarkStyleSet();
    Color bg0 = ss->GetColor("bg.0");
    Color bg4 = ss->GetColor("bg.4");
    // bg.4 should be measurably brighter than bg.0 (deeper bg = darker).
    const float lum0 = 0.299f * bg0.x + 0.587f * bg0.y + 0.114f * bg0.z;
    const float lum4 = 0.299f * bg4.x + 0.587f * bg4.y + 0.114f * bg4.z;
    EXPECT_LT(lum0, lum4);
}

TEST(SlateClaudeDark, AccentSoftHasReducedAlpha) {
    auto ss = CreateClaudeDarkStyleSet();
    Color accent = ss->GetColor("accent");
    Color soft   = ss->GetColor("accent.soft");
    EXPECT_FLOAT_EQ(accent.w, 1.0f);
    EXPECT_LT(soft.w, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Global Styles() singleton
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateStyleStack, GlobalSinglehasClaudeDarkOnFirstUse) {
    EXPECT_GE(Styles().Depth(), 1u);
    Color accent = Styles().GetColor("accent", Color{1, 0, 1, 1});
    EXPECT_FALSE(accent.x == 1.0f && accent.y == 0.0f && accent.z == 1.0f)
        << "Global Styles() did not return claude.dark accent";
}
