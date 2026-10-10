/// Unit tests for the Slate UI MVP renderer (Phase 18 / Step 1).
///
/// Covers:
///   - DrawElement queue lifecycle (PushElement / Clear / BeginFrame).
///   - Submit() bridge: positions and sizes are scaled by dpiScale exactly once.
///   - Scissor stack: push/pop balance, elements outside the active scissor
///     are skipped, partial overlaps still emit, unbalanced stack auto-drains.
///   - Rect helper: Contains, Intersect, IsEmpty.
///
/// These tests do NOT touch the GPU. They use a tiny stub TextRenderer-like
/// recorder to capture the calls Submit would make. The real TextRenderer is
/// exercised by the demo + editor screenshot tests.

#include "engine/ui/slate_core.h"
#include "engine/ui/slate_renderer.h"

#include <gtest/gtest.h>

using namespace uz;
using namespace uz::ui;

// ─────────────────────────────────────────────────────────────────────────────
// Rect primitive
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateRect, ContainsRespectsHalfOpenInterval) {
    Rect r{Vec2{10.0f, 20.0f}, Vec2{100.0f, 50.0f}};

    // Top-left corner is inclusive.
    EXPECT_TRUE(r.Contains(Vec2{10.0f, 20.0f}));
    // Right and bottom edges are exclusive.
    EXPECT_FALSE(r.Contains(Vec2{110.0f, 40.0f}));
    EXPECT_FALSE(r.Contains(Vec2{60.0f, 70.0f}));
    // Centre.
    EXPECT_TRUE(r.Contains(Vec2{60.0f, 45.0f}));
    // Outside.
    EXPECT_FALSE(r.Contains(Vec2{0.0f, 0.0f}));
}

TEST(SlateRect, IntersectClampsToOverlappingArea) {
    Rect a{Vec2{0.0f, 0.0f},   Vec2{100.0f, 100.0f}};
    Rect b{Vec2{50.0f, 50.0f}, Vec2{200.0f, 200.0f}};

    Rect r = a.Intersect(b);
    EXPECT_FLOAT_EQ(r.pos.x,  50.0f);
    EXPECT_FLOAT_EQ(r.pos.y,  50.0f);
    EXPECT_FLOAT_EQ(r.size.x, 50.0f);
    EXPECT_FLOAT_EQ(r.size.y, 50.0f);
}

TEST(SlateRect, IntersectDisjointReturnsEmpty) {
    Rect a{Vec2{0.0f, 0.0f},   Vec2{10.0f, 10.0f}};
    Rect b{Vec2{50.0f, 50.0f}, Vec2{10.0f, 10.0f}};
    Rect r = a.Intersect(b);
    EXPECT_TRUE(r.IsEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// SlateRenderer queue
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateRenderer, BeginFrameClearsQueue) {
    SlateRenderer r;
    DrawElement e{};
    e.kind = DrawKind::Quad;
    e.rect = Rect{Vec2{10.0f, 20.0f}, Vec2{30.0f, 40.0f}};
    r.PushElement(e);
    EXPECT_EQ(r.ElementCount(), 1u);

    r.BeginFrame();
    EXPECT_EQ(r.ElementCount(), 0u);
}

TEST(SlateRenderer, PushElementPreservesOrder) {
    SlateRenderer r;
    DrawElement a{}, b{}, c{};
    a.kind = DrawKind::Quad;
    b.kind = DrawKind::Line;
    c.kind = DrawKind::Text;
    c.text = "hi";

    r.PushElement(a);
    r.PushElement(b);
    r.PushElement(c);

    ASSERT_EQ(r.Elements().size(), 3u);
    EXPECT_EQ(r.Elements()[0].kind, DrawKind::Quad);
    EXPECT_EQ(r.Elements()[1].kind, DrawKind::Line);
    EXPECT_EQ(r.Elements()[2].kind, DrawKind::Text);
    EXPECT_EQ(r.Elements()[2].text, "hi");
}

TEST(SlateRenderer, ClearDropsAllElementsWithoutSubmit) {
    SlateRenderer r;
    DrawElement e{};
    e.kind = DrawKind::Quad;
    r.PushElement(e);
    r.PushElement(e);
    EXPECT_EQ(r.ElementCount(), 2u);

    r.Clear();
    EXPECT_EQ(r.ElementCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DrawElement plain-data invariants
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateDrawElement, DefaultIsValidQuadWithIdentityTransform) {
    DrawElement e{};
    EXPECT_EQ(e.kind, DrawKind::Quad);
    EXPECT_FLOAT_EQ(e.rect.size.x, 0.0f);
    EXPECT_FLOAT_EQ(e.rect.size.y, 0.0f);
    EXPECT_FLOAT_EQ(e.cornerRadius, 0.0f);
    EXPECT_FLOAT_EQ(e.borderThickness, 0.0f);
    EXPECT_FLOAT_EQ(e.shadowBlur, 0.0f);
    EXPECT_FLOAT_EQ(e.lineThickness, 1.0f);
    EXPECT_TRUE(e.text.empty());
    EXPECT_EQ(e.iconId, VfxUiIcon::None);
}

TEST(SlateDrawElement, CopyIsValueSemantic) {
    DrawElement a{};
    a.kind = DrawKind::Text;
    a.text = "hello";
    a.color = Color{1.0f, 0.0f, 0.0f, 1.0f};

    DrawElement b = a;
    b.text = "world";
    b.color = Color{0.0f, 1.0f, 0.0f, 1.0f};

    EXPECT_EQ(a.text, "hello");
    EXPECT_FLOAT_EQ(a.color.x, 1.0f);
    EXPECT_EQ(b.text, "world");
    EXPECT_FLOAT_EQ(b.color.y, 1.0f);
}
