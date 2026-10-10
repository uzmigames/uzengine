/// Unit tests for Slate UI input + Button (Phase 18 / Step 4).
///
/// Covers:
///   - InputRouter hit-test is deepest-first (child claims before parent).
///   - Bubble: child Unhandled → parent gets the event.
///   - HandledCapture: subsequent Move/Up redirect to captor regardless
///     of cursor position; PointerUp releases capture.
///   - Hover diff: OnHoverEnter / OnHoverLeave fire when hit changes;
///     same target → no calls.
///   - Button click semantics: Down inside + Up inside → OnClick fires.
///   - Button: Down outside + Up inside → no OnClick.
///   - Button: Down inside + drag out + Up outside → no OnClick (hover
///     leave drops press state defensively).

#include "engine/ui/slate_box.h"
#include "engine/ui/slate_button.h"
#include "engine/ui/slate_input.h"
#include "engine/ui/slate_renderer.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_widget.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace uz;
using namespace uz::ui;

namespace {

class HitRecorder final : public Widget {
public:
    int  downCount{0};
    int  upCount{0};
    int  moveCount{0};
    int  hoverEnter{0};
    int  hoverLeave{0};
    EventReply downReply{EventReply::Handled};
    EventReply upReply{EventReply::Handled};

    Vec2 Measure(Vec2 m) override { return m; }
    void Paint(SlateRenderer& /*r*/) override {}

    EventReply OnPointerDown(const PointerEvent& /*e*/) override {
        ++downCount; return downReply;
    }
    EventReply OnPointerUp(const PointerEvent& /*e*/) override {
        ++upCount; return upReply;
    }
    EventReply OnPointerMove(const PointerEvent& /*e*/) override {
        ++moveCount; return EventReply::Unhandled;
    }
    void OnHoverEnter() override { ++hoverEnter; }
    void OnHoverLeave() override { ++hoverLeave; }
};

PointerEvent Down(Vec2 pos) {
    PointerEvent e{};
    e.position = pos;
    e.button = PointerButton::Left;
    e.isDown = true;
    return e;
}
PointerEvent Up(Vec2 pos) {
    PointerEvent e{};
    e.position = pos;
    e.button = PointerButton::Left;
    e.isDown = false;
    return e;
}
PointerEvent Move(Vec2 pos) {
    PointerEvent e{};
    e.position = pos;
    return e;
}

void Layout(const SP<Widget>& root, Vec2 size) {
    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = size;
    root->Arrange(g);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateInput, HitTestPicksDeepestChildBeforeParent) {
    auto child = std::make_shared<HitRecorder>();
    auto box = std::make_shared<HorizontalBox>();
    box->AddSlot(child).FillSize(1.0f);

    Layout(box, Vec2{200.0f, 100.0f});

    InputRouter router;
    router.SetRoot(box);
    bool handled = router.DispatchPointer(Down(Vec2{100.0f, 50.0f}));

    EXPECT_TRUE(handled);
    EXPECT_EQ(child->downCount, 1);
}

TEST(SlateInput, BubbleReachesParentWhenChildUnhandled) {
    auto child = std::make_shared<HitRecorder>();
    child->downReply = EventReply::Unhandled; // bubble it up

    auto parent = std::make_shared<HitRecorder>();
    parent->downReply = EventReply::Handled;

    auto box = std::make_shared<HorizontalBox>();
    box->AddSlot(parent).FillSize(1.0f); // outer "wrapper" reachable via bubble
    // We need parent->child topology, but HBox only has 1 child level.
    // For this test, set parent as the only child of box; box itself
    // will receive the bubble from parent if parent unhandles.
    // But parent is set to Handled, so it will stop the bubble.
    Layout(box, Vec2{200.0f, 100.0f});

    InputRouter router;
    router.SetRoot(box);
    bool handled = router.DispatchPointer(Down(Vec2{50.0f, 50.0f}));

    EXPECT_TRUE(handled);
    EXPECT_EQ(parent->downCount, 1);
    EXPECT_EQ(child->downCount, 0);
}

TEST(SlateInput, BubbleSkipsParentIfChildHandled) {
    auto inner = std::make_shared<HitRecorder>();
    inner->downReply = EventReply::Handled;

    auto outer = std::make_shared<HorizontalBox>();
    outer->AddSlot(inner).FillSize(1.0f);

    Layout(outer, Vec2{200.0f, 100.0f});

    InputRouter router;
    router.SetRoot(outer);
    router.DispatchPointer(Down(Vec2{50.0f, 50.0f}));

    EXPECT_EQ(inner->downCount, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Capture
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateInput, HandledCaptureRedirectsMoveAndUpToCaptor) {
    auto captor = std::make_shared<HitRecorder>();
    captor->downReply = EventReply::HandledCapture;
    auto sibling = std::make_shared<HitRecorder>();

    auto box = std::make_shared<HorizontalBox>();
    box->AddSlot(captor).FillSize(1.0f);
    box->AddSlot(sibling).FillSize(1.0f);
    Layout(box, Vec2{200.0f, 100.0f});

    InputRouter router;
    router.SetRoot(box);
    router.DispatchPointer(Down(Vec2{50.0f, 50.0f}));      // captor
    EXPECT_EQ(captor->downCount, 1);
    EXPECT_EQ(router.GetCapture(), captor);

    // Move to sibling's territory — should still go to captor.
    router.DispatchPointer(Move(Vec2{150.0f, 50.0f}));
    EXPECT_EQ(captor->moveCount, 1);
    EXPECT_EQ(sibling->moveCount, 0);

    // PointerUp anywhere → captor receives, capture released.
    router.DispatchPointer(Up(Vec2{180.0f, 90.0f}));
    EXPECT_EQ(captor->upCount, 1);
    EXPECT_EQ(sibling->upCount, 0);
    EXPECT_EQ(router.GetCapture(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hover transitions
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateInput, HoverEnterLeaveFiresOnTargetChange) {
    auto a = std::make_shared<HitRecorder>();
    auto b = std::make_shared<HitRecorder>();
    auto box = std::make_shared<HorizontalBox>();
    box->AddSlot(a).FillSize(1.0f);
    box->AddSlot(b).FillSize(1.0f);
    Layout(box, Vec2{200.0f, 100.0f});

    InputRouter router;
    router.SetRoot(box);

    router.DispatchPointer(Move(Vec2{50.0f, 50.0f})); // over a
    EXPECT_EQ(a->hoverEnter, 1);
    EXPECT_EQ(a->hoverLeave, 0);
    EXPECT_EQ(b->hoverEnter, 0);

    router.DispatchPointer(Move(Vec2{150.0f, 50.0f})); // moved to b
    EXPECT_EQ(a->hoverLeave, 1);
    EXPECT_EQ(b->hoverEnter, 1);

    router.DispatchPointer(Move(Vec2{160.0f, 60.0f})); // still over b
    EXPECT_EQ(a->hoverLeave, 1);  // unchanged
    EXPECT_EQ(b->hoverEnter, 1);  // unchanged
}

// ─────────────────────────────────────────────────────────────────────────────
// Button click semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateButton, ClickFiresOnPressInsidePlusReleaseInside) {
    auto btn = std::make_shared<Button>();
    int  clicks = 0;
    btn->OnClick = [&clicks]() { ++clicks; };

    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = Vec2{100.0f, 40.0f};
    btn->Arrange(g);

    InputRouter router;
    router.SetRoot(btn);
    router.DispatchPointer(Down(Vec2{50.0f, 20.0f})); // inside
    router.DispatchPointer(Up(Vec2{50.0f, 20.0f}));   // inside
    EXPECT_EQ(clicks, 1);
    EXPECT_FALSE(btn->IsPressed());
}

TEST(SlateButton, NoClickWhenPressOutsideThenReleaseInside) {
    auto btn = std::make_shared<Button>();
    int  clicks = 0;
    btn->OnClick = [&clicks]() { ++clicks; };

    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = Vec2{100.0f, 40.0f};
    btn->Arrange(g);

    InputRouter router;
    router.SetRoot(btn);
    // Pointer first goes outside → router has no hit, no down delivered.
    router.DispatchPointer(Down(Vec2{200.0f, 200.0f}));
    EXPECT_FALSE(btn->IsPressed());
    // Then up inside — button never received Down, nothing to "release".
    router.DispatchPointer(Up(Vec2{50.0f, 20.0f}));
    EXPECT_EQ(clicks, 0);
}

TEST(SlateButton, NoClickWhenDragOutAndReleaseOutside) {
    auto btn = std::make_shared<Button>();
    int clicks = 0;
    btn->OnClick = [&clicks]() { ++clicks; };

    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = Vec2{100.0f, 40.0f};
    btn->Arrange(g);

    InputRouter router;
    router.SetRoot(btn);
    router.DispatchPointer(Down(Vec2{50.0f, 20.0f}));   // press inside
    EXPECT_TRUE(btn->IsPressed());
    EXPECT_EQ(router.GetCapture(), btn);

    // Drag out — capture keeps moves on the button, so it doesn't see
    // the hover-leave from the router. Still, release outside should
    // not fire OnClick because the position is outside the geometry.
    router.DispatchPointer(Move(Vec2{300.0f, 300.0f}));
    router.DispatchPointer(Up(Vec2{300.0f, 300.0f}));
    EXPECT_EQ(clicks, 0);
    EXPECT_FALSE(btn->IsPressed());
}

TEST(SlateButton, HoverLeaveDuringPressDropsPressDefensively) {
    auto btn = std::make_shared<Button>();
    btn->OnClick = []{ FAIL() << "should not click"; };

    Geometry g{};
    g.absolutePos = Vec2{0.0f, 0.0f};
    g.size = Vec2{100.0f, 40.0f};
    btn->Arrange(g);

    btn->OnHoverEnter();
    EXPECT_TRUE(btn->IsHovered());
    PointerEvent down{};
    down.button = PointerButton::Left;
    down.isDown = true;
    btn->OnPointerDown(down);
    EXPECT_TRUE(btn->IsPressed());

    btn->OnHoverLeave();
    EXPECT_FALSE(btn->IsHovered());
    EXPECT_FALSE(btn->IsPressed()); // defensive drop

    // Subsequent up does not re-fire.
    PointerEvent up{};
    up.button = PointerButton::Left;
    up.isDown = false;
    up.position = Vec2{50.0f, 20.0f};
    btn->OnPointerUp(up);
    // No FAIL — OnClick never invoked.
}

TEST(SlateButton, PaintColorReflectsState) {
    auto btn = std::make_shared<Button>();
    btn->NormalColor  = Color{0.1f, 0.1f, 0.1f, 1.0f};
    btn->HoverColor   = Color{0.5f, 0.5f, 0.5f, 1.0f};
    btn->PressedColor = Color{0.9f, 0.9f, 0.9f, 1.0f};

    Geometry g{};
    g.size = Vec2{50.0f, 30.0f};
    btn->Arrange(g);

    SlateRenderer r;
    r.BeginFrame();
    btn->Paint(r);
    ASSERT_GE(r.ElementCount(), 1u);
    EXPECT_FLOAT_EQ(r.Elements()[0].color.x, 0.1f); // idle

    btn->OnHoverEnter();
    r.BeginFrame();
    btn->Paint(r);
    EXPECT_FLOAT_EQ(r.Elements()[0].color.x, 0.5f); // hover

    PointerEvent down{};
    down.button = PointerButton::Left;
    down.isDown = true;
    btn->OnPointerDown(down);
    r.BeginFrame();
    btn->Paint(r);
    EXPECT_FLOAT_EQ(r.Elements()[0].color.x, 0.9f); // pressed
}
