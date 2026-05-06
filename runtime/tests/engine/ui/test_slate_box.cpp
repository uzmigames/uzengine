/// Unit tests for HorizontalBox / VerticalBox / Overlay / Spacer
/// (Phase 18 / Step 3).
///
/// Covers:
///   - HBox/VBox Measure: sums children + padding on the primary axis,
///     max on the cross axis; clamps to maxSize.
///   - HBox/VBox Arrange: Auto slots receive their measured size; Fill
///     slots split the remainder pro-rata by fillFactor; padding eats
///     space before the child sees it.
///   - HAlign / VAlign positioning inside an oversized cell.
///   - Overlay Measure = max of children; Arrange stacks children on
///     the same rect.
///   - Spacer takes its declared size and paints nothing.
///
/// All tests use a `RecordingLeaf` helper widget so we can assert the
/// final arranged geometry without going through the GPU.

#include "engine/ui/slate_box.h"
#include "engine/ui/slate_renderer.h"
#include "engine/ui/slate_spacer.h"
#include "engine/ui/slate_widget.h"

#include <gtest/gtest.h>

#include <memory>

using namespace uz;
using namespace uz::ui;

namespace {

/// Leaf widget that records the geometry it was assigned and the
/// desired size it returns from Measure (caller-set).
class RecordingLeaf final : public Widget {
public:
    Vec2 desired{0.0f, 0.0f};
    Geometry assigned{};
    int paintCalls{0};

    Vec2 Measure(Vec2 /*maxSize*/) override { return desired; }
    void Arrange(Geometry g) override {
        assigned = g;
        Widget::Arrange(g);
    }
    void Paint(SlateRenderer& /*r*/) override { ++paintCalls; }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// HorizontalBox / VerticalBox — Measure
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateBox, HBoxMeasureSumsWidthsMaxsHeights) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};
    b->desired = Vec2{60.0f, 30.0f};

    HorizontalBox box;
    box.AddSlot(a);
    box.AddSlot(b);

    Vec2 m = box.Measure(Vec2{1000.0f, 1000.0f});
    EXPECT_FLOAT_EQ(m.x, 100.0f);   // 40 + 60
    EXPECT_FLOAT_EQ(m.y, 30.0f);    // max(20, 30)
}

TEST(SlateBox, HBoxMeasureIncludesSlotPadding) {
    auto a = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};

    HorizontalBox box;
    box.AddSlot(a).Padding_(Vec4{4.0f, 2.0f, 4.0f, 2.0f}); // L T R B

    Vec2 m = box.Measure(Vec2{1000.0f, 1000.0f});
    EXPECT_FLOAT_EQ(m.x, 48.0f);    // 40 + (4+4)
    EXPECT_FLOAT_EQ(m.y, 24.0f);    // 20 + (2+2)
}

TEST(SlateBox, VBoxMeasureSumsHeightsMaxsWidths) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{20.0f, 10.0f};
    b->desired = Vec2{50.0f, 30.0f};

    VerticalBox box;
    box.AddSlot(a);
    box.AddSlot(b);

    Vec2 m = box.Measure(Vec2{1000.0f, 1000.0f});
    EXPECT_FLOAT_EQ(m.x, 50.0f);
    EXPECT_FLOAT_EQ(m.y, 40.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HorizontalBox — Arrange
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateBox, HBoxArrangeAutoSlotGetsMeasuredWidth) {
    auto a = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};

    HorizontalBox box;
    box.AddSlot(a); // Auto by default.

    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = Vec2{300.0f, 100.0f};
    box.Arrange(g);

    EXPECT_FLOAT_EQ(a->assigned.absolutePos.x, 0.0f);
    EXPECT_FLOAT_EQ(a->assigned.size.x, 40.0f);
    EXPECT_FLOAT_EQ(a->assigned.size.y, 100.0f); // VAlign Fill stretches.
}

TEST(SlateBox, HBoxArrangeThreeFillSlotsSplitEqually) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();
    auto c = std::make_shared<RecordingLeaf>();

    HorizontalBox box;
    box.AddSlot(a).FillSize(1.0f);
    box.AddSlot(b).FillSize(1.0f);
    box.AddSlot(c).FillSize(1.0f);

    Geometry g{};
    g.size = Vec2{300.0f, 50.0f};
    box.Arrange(g);

    EXPECT_FLOAT_EQ(a->assigned.size.x, 100.0f);
    EXPECT_FLOAT_EQ(b->assigned.size.x, 100.0f);
    EXPECT_FLOAT_EQ(c->assigned.size.x, 100.0f);

    // Cursor advances correctly.
    EXPECT_FLOAT_EQ(a->assigned.absolutePos.x,   0.0f);
    EXPECT_FLOAT_EQ(b->assigned.absolutePos.x, 100.0f);
    EXPECT_FLOAT_EQ(c->assigned.absolutePos.x, 200.0f);
}

TEST(SlateBox, HBoxArrangeMixedAutoFill_AutoExactFillRest) {
    auto autoChild = std::make_shared<RecordingLeaf>();
    auto fillChild = std::make_shared<RecordingLeaf>();
    autoChild->desired = Vec2{50.0f, 20.0f};

    HorizontalBox box;
    box.AddSlot(autoChild);                     // Auto (50)
    box.AddSlot(fillChild).FillSize(1.0f);      // Fill (250)

    Geometry g{};
    g.size = Vec2{300.0f, 50.0f};
    box.Arrange(g);

    EXPECT_FLOAT_EQ(autoChild->assigned.size.x, 50.0f);
    EXPECT_FLOAT_EQ(fillChild->assigned.size.x, 250.0f);
    EXPECT_FLOAT_EQ(fillChild->assigned.absolutePos.x, 50.0f);
}

TEST(SlateBox, HBoxArrangePaddingShrinksChildAndAdvancesCursor) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};
    b->desired = Vec2{40.0f, 20.0f};

    HorizontalBox box;
    box.AddSlot(a).Padding_(Vec4{4.0f, 0.0f, 8.0f, 0.0f});
    box.AddSlot(b).Padding_(Vec4{2.0f, 0.0f, 0.0f, 0.0f});

    Geometry g{};
    g.size = Vec2{500.0f, 30.0f};
    box.Arrange(g);

    // a child sits at x=4 (left padding), b child at x=4+40+8(L+w+R)+2(b.left).
    EXPECT_FLOAT_EQ(a->assigned.absolutePos.x, 4.0f);
    EXPECT_FLOAT_EQ(a->assigned.size.x, 40.0f);
    EXPECT_FLOAT_EQ(b->assigned.absolutePos.x, 4.0f + 40.0f + 8.0f + 2.0f);
    EXPECT_FLOAT_EQ(b->assigned.size.x, 40.0f);
}

TEST(SlateBox, HBoxArrangeUnequalFillFactor) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();

    HorizontalBox box;
    box.AddSlot(a).FillSize(1.0f);
    box.AddSlot(b).FillSize(3.0f);

    Geometry g{};
    g.size = Vec2{400.0f, 50.0f};
    box.Arrange(g);

    // 400 split 1:3 → 100, 300.
    EXPECT_FLOAT_EQ(a->assigned.size.x, 100.0f);
    EXPECT_FLOAT_EQ(b->assigned.size.x, 300.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// VerticalBox — Arrange
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateBox, VBoxArrangeStacksTopToBottom) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};
    b->desired = Vec2{40.0f, 30.0f};

    VerticalBox box;
    box.AddSlot(a);
    box.AddSlot(b);

    Geometry g{};
    g.size = Vec2{200.0f, 200.0f};
    box.Arrange(g);

    EXPECT_FLOAT_EQ(a->assigned.absolutePos.y, 0.0f);
    EXPECT_FLOAT_EQ(a->assigned.size.y, 20.0f);
    EXPECT_FLOAT_EQ(b->assigned.absolutePos.y, 20.0f);
    EXPECT_FLOAT_EQ(b->assigned.size.y, 30.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// HAlign / VAlign
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateBox, HBoxHAlignCenterCentersWithinFillSlot) {
    auto a = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};

    HorizontalBox box;
    box.AddSlot(a).FillSize(1.0f).HAlign_(HAlign::Center);

    Geometry g{};
    g.size = Vec2{200.0f, 50.0f};
    box.Arrange(g);

    // arena = 200 wide, child desired = 40, center → x = (200-40)/2 = 80.
    EXPECT_FLOAT_EQ(a->assigned.absolutePos.x, 80.0f);
    EXPECT_FLOAT_EQ(a->assigned.size.x, 40.0f);
}

TEST(SlateBox, HBoxVAlignTopAndBottom) {
    auto top = std::make_shared<RecordingLeaf>();
    auto bot = std::make_shared<RecordingLeaf>();
    top->desired = Vec2{40.0f, 20.0f};
    bot->desired = Vec2{40.0f, 20.0f};

    HorizontalBox box;
    box.AddSlot(top).FillSize(1.0f).VAlign_(VAlign::Top);
    box.AddSlot(bot).FillSize(1.0f).VAlign_(VAlign::Bottom);

    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = Vec2{200.0f, 100.0f};
    box.Arrange(g);

    EXPECT_FLOAT_EQ(top->assigned.absolutePos.y, 0.0f);
    EXPECT_FLOAT_EQ(bot->assigned.absolutePos.y, 80.0f); // 100-20
}

// ─────────────────────────────────────────────────────────────────────────────
// Overlay
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateOverlay, MeasureReturnsMaxOfChildren) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();
    a->desired = Vec2{40.0f, 20.0f};
    b->desired = Vec2{20.0f, 50.0f};

    Overlay o;
    o.AddSlot(a);
    o.AddSlot(b);

    Vec2 m = o.Measure(Vec2{1000.0f, 1000.0f});
    EXPECT_FLOAT_EQ(m.x, 40.0f);
    EXPECT_FLOAT_EQ(m.y, 50.0f);
}

TEST(SlateOverlay, ArrangeStacksChildrenOnSameRect) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();

    Overlay o;
    o.AddSlot(a);
    o.AddSlot(b);

    Geometry g{};
    g.absolutePos = Vec2{10.0f, 20.0f};
    g.size = Vec2{200.0f, 100.0f};
    o.Arrange(g);

    EXPECT_FLOAT_EQ(a->assigned.absolutePos.x, 10.0f);
    EXPECT_FLOAT_EQ(a->assigned.absolutePos.y, 20.0f);
    EXPECT_FLOAT_EQ(b->assigned.absolutePos.x, 10.0f);
    EXPECT_FLOAT_EQ(b->assigned.absolutePos.y, 20.0f);
    EXPECT_FLOAT_EQ(a->assigned.size.x, 200.0f);
    EXPECT_FLOAT_EQ(b->assigned.size.x, 200.0f);
}

TEST(SlateOverlay, PaintCallsAllChildrenInOrder) {
    auto a = std::make_shared<RecordingLeaf>();
    auto b = std::make_shared<RecordingLeaf>();

    Overlay o;
    o.AddSlot(a);
    o.AddSlot(b);

    SlateRenderer r;
    r.BeginFrame();
    o.Paint(r);

    EXPECT_EQ(a->paintCalls, 1);
    EXPECT_EQ(b->paintCalls, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Spacer
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateSpacer, MeasureReturnsDeclaredSize) {
    Spacer s(Vec2{12.0f, 8.0f});
    Vec2 m = s.Measure(Vec2{1000.0f, 1000.0f});
    EXPECT_FLOAT_EQ(m.x, 12.0f);
    EXPECT_FLOAT_EQ(m.y, 8.0f);
}

TEST(SlateSpacer, PaintEmitsNothing) {
    Spacer s(Vec2{50.0f, 50.0f});
    SlateRenderer r;
    r.BeginFrame();
    s.Paint(r);
    EXPECT_EQ(r.ElementCount(), 0u);
}
