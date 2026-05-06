/// Unit tests for Slate UI Widget base + Attribute<T> + TextBlock
/// (Phase 18 / Step 2).
///
/// Covers:
///   - Attribute<T>: literal value, lambda binding, IsBound, Unbind, Get
///     re-evaluation per call when bound.
///   - Widget::Arrange stores geometry; subclasses without Arrange override
///     inherit the default behaviour.
///   - TextBlock::Measure scales linearly with text length and font size,
///     and respects the maxSize clamp.
///   - TextBlock::Paint emits a single Text DrawElement positioned at the
///     widget's absolute geometry, with the current Text + Color values.

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_renderer.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_widget.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

using namespace uz;
using namespace uz::ui;

// ─────────────────────────────────────────────────────────────────────────────
// Attribute<T>
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateAttribute, LiteralAssignmentReturnsValue) {
    Attribute<int> a;
    a = 42;
    EXPECT_EQ(a.Get(), 42);
    EXPECT_FALSE(a.IsBound());
}

TEST(SlateAttribute, ImplicitConstructionFromLiteral) {
    Attribute<std::string> a("hello");
    EXPECT_EQ(a.Get(), "hello");
    EXPECT_FALSE(a.IsBound());
}

TEST(SlateAttribute, LambdaIsReevaluatedPerGet) {
    int counter = 0;
    Attribute<int> a;
    a = std::function<int()>([&counter]() { return ++counter; });

    EXPECT_TRUE(a.IsBound());
    EXPECT_EQ(a.Get(), 1);
    EXPECT_EQ(a.Get(), 2);
    EXPECT_EQ(a.Get(), 3);
    EXPECT_EQ(counter, 3);
}

TEST(SlateAttribute, LiteralAfterLambdaClearsBinding) {
    Attribute<int> a;
    a = std::function<int()>([]() { return 7; });
    EXPECT_TRUE(a.IsBound());

    a = 99;
    EXPECT_FALSE(a.IsBound());
    EXPECT_EQ(a.Get(), 99);
}

TEST(SlateAttribute, UnbindFallsBackToLiteral) {
    Attribute<int> a(11);
    a = std::function<int()>([]() { return 22; });
    EXPECT_EQ(a.Get(), 22);

    a.Unbind();
    EXPECT_FALSE(a.IsBound());
    EXPECT_EQ(a.Get(), 11);
}

// ─────────────────────────────────────────────────────────────────────────────
// Widget::Arrange default behaviour
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Minimal Widget subclass that does nothing in Paint; used to exercise
/// the base-class lifecycle.
class NoopLeaf final : public Widget {
public:
    Vec2 Measure(Vec2 maxSize) override { return Vec2{maxSize.x, maxSize.y}; }
    void Paint(SlateRenderer& /*r*/) override {}
};

} // namespace

TEST(SlateWidget, ArrangeStoresGeometry) {
    NoopLeaf w;
    Geometry g{};
    g.absolutePos = Vec2{10.0f, 20.0f};
    g.size = Vec2{100.0f, 50.0f};
    g.dpiScale = 2.0f;
    w.Arrange(g);

    const Geometry& got = w.GetGeometry();
    EXPECT_FLOAT_EQ(got.absolutePos.x, 10.0f);
    EXPECT_FLOAT_EQ(got.absolutePos.y, 20.0f);
    EXPECT_FLOAT_EQ(got.size.x, 100.0f);
    EXPECT_FLOAT_EQ(got.size.y, 50.0f);
    EXPECT_FLOAT_EQ(got.dpiScale, 2.0f);
}

TEST(SlateWidget, GeometryToRectMatchesAbsolutePosAndSize) {
    NoopLeaf w;
    Geometry g{};
    g.absolutePos = Vec2{5.0f, 5.0f};
    g.size = Vec2{20.0f, 30.0f};
    w.Arrange(g);
    Rect r = w.GetGeometry().ToRect();
    EXPECT_FLOAT_EQ(r.pos.x, 5.0f);
    EXPECT_FLOAT_EQ(r.size.x, 20.0f);
    EXPECT_FLOAT_EQ(r.size.y, 30.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextBlock — Measure
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateTextBlock, MeasureScalesWithLengthAndSize) {
    auto t = std::make_shared<TextBlock>();
    t->Text = "abcd";        // 4 chars
    t->FontSize = 12.0f;

    Vec2 m = t->Measure(Vec2{1000.0f, 1000.0f});
    const float expectedW =
        4.0f * 12.0f * TextBlock::kApproxGlyphAdvanceRatio;
    EXPECT_FLOAT_EQ(m.x, expectedW);
    EXPECT_FLOAT_EQ(m.y, 14.0f); // size + 2 leading
}

TEST(SlateTextBlock, MeasureRespectsMaxSizeClamp) {
    auto t = std::make_shared<TextBlock>();
    t->Text = std::string(1000, 'x');  // huge string
    t->FontSize = 12.0f;

    Vec2 m = t->Measure(Vec2{200.0f, 50.0f});
    EXPECT_FLOAT_EQ(m.x, 200.0f);
    EXPECT_FLOAT_EQ(m.y, 14.0f); // height unaffected (within max)
}

TEST(SlateTextBlock, MeasureLambdaTextIsResolvedOnGet) {
    auto t = std::make_shared<TextBlock>();
    int  pulls = 0;
    t->Text = std::function<std::string()>([&pulls]() {
        ++pulls;
        return std::string("abc"); // 3 chars
    });
    t->FontSize = 10.0f;

    Vec2 m = t->Measure(Vec2{1000.0f, 1000.0f});
    EXPECT_FLOAT_EQ(m.x, 3.0f * 10.0f * TextBlock::kApproxGlyphAdvanceRatio);
    EXPECT_GE(pulls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextBlock — Paint
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateTextBlock, PaintEmitsSingleTextElementAtArrangedGeometry) {
    auto t = std::make_shared<TextBlock>();
    t->Text = "hi";
    t->TextColor = Color{1.0f, 0.0f, 0.0f, 1.0f};
    t->FontSize = 16.0f;

    Geometry g{};
    g.absolutePos = Vec2{100.0f, 200.0f};
    g.size = Vec2{40.0f, 18.0f};
    t->Arrange(g);

    SlateRenderer r;
    r.BeginFrame();
    t->Paint(r);

    ASSERT_EQ(r.ElementCount(), 1u);
    const DrawElement& e = r.Elements()[0];
    EXPECT_EQ(e.kind, DrawKind::Text);
    EXPECT_FLOAT_EQ(e.rect.pos.x, 100.0f);
    EXPECT_FLOAT_EQ(e.rect.pos.y, 200.0f);
    EXPECT_FLOAT_EQ(e.rect.size.x, 40.0f);
    EXPECT_FLOAT_EQ(e.color.x, 1.0f);
    EXPECT_FLOAT_EQ(e.color.y, 0.0f);
    EXPECT_EQ(e.text, "hi");
    EXPECT_FLOAT_EQ(e.fontSize, 16.0f);
    EXPECT_FALSE(e.msdf);
}

TEST(SlateTextBlock, PaintReflectsCurrentLambdaText) {
    auto t = std::make_shared<TextBlock>();
    int counter = 0;
    t->Text = std::function<std::string()>([&counter]() {
        ++counter;
        return std::string("frame=") + std::to_string(counter);
    });

    Geometry g{};
    g.size = Vec2{100.0f, 14.0f};
    t->Arrange(g);

    SlateRenderer r;
    r.BeginFrame();
    t->Paint(r);
    ASSERT_EQ(r.ElementCount(), 1u);
    EXPECT_EQ(r.Elements()[0].text, "frame=1");

    r.BeginFrame();
    t->Paint(r);
    ASSERT_EQ(r.ElementCount(), 1u);
    EXPECT_EQ(r.Elements()[0].text, "frame=2");
}
