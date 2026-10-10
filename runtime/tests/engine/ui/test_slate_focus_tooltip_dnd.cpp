/// Unit tests for FocusManager / TooltipManager / DragDropManager
/// (Phase 18 / Step 9).

#include "engine/ui/slate_box.h"
#include "engine/ui/slate_dragdrop.h"
#include "engine/ui/slate_focus.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_tooltip.h"
#include "engine/ui/slate_widget.h"

#include <gtest/gtest.h>

#include <memory>
#include <vector>

using namespace uz;
using namespace uz::ui;

namespace {

class FocusableLeaf final : public Widget {
public:
    int focusReceived{0};
    int focusLost{0};
    Vec2 Measure(Vec2 m) override { return m; }
    void Paint(SlateRenderer& /*r*/) override {}
    [[nodiscard]] bool IsFocusable() const noexcept override { return true; }
    void OnFocusReceived() override { ++focusReceived; }
    void OnFocusLost()     override { ++focusLost; }
};

class DropTarget final : public Widget {
public:
    int enters{0}, leaves{0}, drops{0};
    Vec2 Measure(Vec2 m) override { return m; }
    void Paint(SlateRenderer& /*r*/) override {}
    void OnDragEnter() override { ++enters; }
    void OnDragLeave() override { ++leaves; }
    EventReply OnDrop() override { ++drops; return EventReply::Handled; }
};

class TooltipOwner final : public Widget {
public:
    Vec2 Measure(Vec2 m) override { return m; }
    void Paint(SlateRenderer& /*r*/) override {}
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// FocusManager
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateFocus, SetFocusFiresHooksInOrder) {
    auto a = std::make_shared<FocusableLeaf>();
    auto b = std::make_shared<FocusableLeaf>();

    FocusManager fm;
    fm.SetFocus(a);
    EXPECT_EQ(a->focusReceived, 1);
    EXPECT_EQ(a->focusLost, 0);

    fm.SetFocus(b);
    EXPECT_EQ(a->focusLost, 1);
    EXPECT_EQ(b->focusReceived, 1);

    fm.SetFocus(nullptr);
    EXPECT_EQ(b->focusLost, 1);
    EXPECT_EQ(fm.GetFocus(), nullptr);
}

TEST(SlateFocus, SetFocusSameTargetIsNoOp) {
    auto a = std::make_shared<FocusableLeaf>();
    FocusManager fm;
    fm.SetFocus(a);
    fm.SetFocus(a);
    EXPECT_EQ(a->focusReceived, 1);
    EXPECT_EQ(a->focusLost, 0);
}

TEST(SlateFocus, CycleNextSkipsNonFocusable) {
    auto box = std::make_shared<HorizontalBox>();
    auto t1 = std::make_shared<TextBlock>(); // non-focusable
    auto f1 = std::make_shared<FocusableLeaf>();
    auto t2 = std::make_shared<TextBlock>(); // non-focusable
    auto f2 = std::make_shared<FocusableLeaf>();
    box->AddSlot(t1);
    box->AddSlot(f1);
    box->AddSlot(t2);
    box->AddSlot(f2);

    FocusManager fm;
    fm.SetRoot(box);
    auto first = fm.CycleNext();
    EXPECT_EQ(first, f1);
    auto second = fm.CycleNext();
    EXPECT_EQ(second, f2);
}

TEST(SlateFocus, CycleNextWrapsAround) {
    auto box = std::make_shared<HorizontalBox>();
    auto a = std::make_shared<FocusableLeaf>();
    auto b = std::make_shared<FocusableLeaf>();
    box->AddSlot(a);
    box->AddSlot(b);

    FocusManager fm;
    fm.SetRoot(box);
    fm.SetFocus(b);
    auto next = fm.CycleNext();
    EXPECT_EQ(next, a); // wraps around to first
}

TEST(SlateFocus, CyclePrevWalksBackwards) {
    auto box = std::make_shared<HorizontalBox>();
    auto a = std::make_shared<FocusableLeaf>();
    auto b = std::make_shared<FocusableLeaf>();
    auto c = std::make_shared<FocusableLeaf>();
    box->AddSlot(a);
    box->AddSlot(b);
    box->AddSlot(c);

    FocusManager fm;
    fm.SetRoot(box);
    fm.SetFocus(c);
    auto p = fm.CyclePrev();
    EXPECT_EQ(p, b);
    p = fm.CyclePrev();
    EXPECT_EQ(p, a);
    p = fm.CyclePrev();
    EXPECT_EQ(p, c); // wraps
}

TEST(SlateFocus, CycleEmptyTreeReturnsNull) {
    FocusManager fm;
    fm.SetRoot(nullptr);
    EXPECT_EQ(fm.CycleNext(), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// TooltipManager
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateTooltip, NoMaterializeBeforeDelay) {
    auto owner = std::make_shared<TooltipOwner>();
    int created = 0;
    TooltipManager tm;
    tm.Register(owner, [&created]() {
        ++created;
        return std::make_shared<TextBlock>();
    }, 0.5f);

    auto t = tm.Update(owner, Vec2{0, 0}, 0.3f);
    EXPECT_EQ(t, nullptr);
    EXPECT_EQ(created, 0);
}

TEST(SlateTooltip, MaterializesAfterDelay) {
    auto owner = std::make_shared<TooltipOwner>();
    int created = 0;
    TooltipManager tm;
    tm.Register(owner, [&created]() {
        ++created;
        return std::make_shared<TextBlock>();
    }, 0.5f);

    auto t = tm.Update(owner, Vec2{0, 0}, 0.6f);
    ASSERT_NE(t, nullptr);
    EXPECT_EQ(created, 1);
    EXPECT_EQ(tm.Active(), t);

    // Still hovering; factory should not be called again.
    tm.Update(owner, Vec2{0, 0}, 0.1f);
    EXPECT_EQ(created, 1);
}

TEST(SlateTooltip, HoverChangeDropsActiveTooltip) {
    auto a = std::make_shared<TooltipOwner>();
    auto b = std::make_shared<TooltipOwner>();
    TooltipManager tm;
    tm.Register(a, []() { return std::make_shared<TextBlock>(); }, 0.5f);

    tm.Update(a, Vec2{0, 0}, 0.6f); // tooltip materialised
    ASSERT_NE(tm.Active(), nullptr);

    tm.Update(b, Vec2{50, 50}, 0.0f); // hover changed
    EXPECT_EQ(tm.Active(), nullptr);
}

TEST(SlateTooltip, AnchorPosUpdates) {
    TooltipManager tm;
    tm.Update(nullptr, Vec2{100, 200}, 0.0f);
    EXPECT_FLOAT_EQ(tm.AnchorPos().x, 100.0f);
    EXPECT_FLOAT_EQ(tm.AnchorPos().y, 200.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// DragDropManager
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateDragDrop, DetectDoesNotEnterDragBelowThreshold) {
    auto src = std::make_shared<DropTarget>();
    DragDropManager dd;
    dd.BeginDetect(Vec2{100, 100}, src);

    bool dragging = dd.Update(Vec2{102, 101}, nullptr,
        [](Vec2) { return std::make_shared<DragOperation>(); });
    EXPECT_FALSE(dragging);
    EXPECT_FALSE(dd.IsDragging());
}

TEST(SlateDragDrop, DetectEntersDragOnceThresholdCrossed) {
    auto src = std::make_shared<DropTarget>();
    DragDropManager dd;
    dd.BeginDetect(Vec2{100, 100}, src);

    int onDragDetectedCalls = 0;
    bool dragging = dd.Update(Vec2{110, 100}, nullptr,
        [&onDragDetectedCalls](Vec2) {
            ++onDragDetectedCalls;
            auto op = std::make_shared<DragOperation>();
            op->typeTag = "vfx.module";
            return op;
        });
    EXPECT_TRUE(dragging);
    EXPECT_EQ(onDragDetectedCalls, 1);
    EXPECT_TRUE(dd.IsDragging());
    ASSERT_NE(dd.Active(), nullptr);
    EXPECT_EQ(dd.Active()->typeTag, "vfx.module");
}

TEST(SlateDragDrop, EnterLeaveOnTargetChange) {
    auto src = std::make_shared<DropTarget>();
    auto a = std::make_shared<DropTarget>();
    auto b = std::make_shared<DropTarget>();
    DragDropManager dd;
    dd.BeginDetect(Vec2{0, 0}, src);
    auto factory = [](Vec2) { return std::make_shared<DragOperation>(); };

    dd.Update(Vec2{20, 0}, a, factory);
    EXPECT_EQ(a->enters, 1);
    EXPECT_EQ(b->enters, 0);

    dd.Update(Vec2{40, 0}, b, factory);
    EXPECT_EQ(a->leaves, 1);
    EXPECT_EQ(b->enters, 1);
}

TEST(SlateDragDrop, EndDropFiresOnTarget) {
    auto src = std::make_shared<DropTarget>();
    auto target = std::make_shared<DropTarget>();
    DragDropManager dd;
    dd.BeginDetect(Vec2{0, 0}, src);
    dd.Update(Vec2{20, 0}, target,
        [](Vec2) { return std::make_shared<DragOperation>(); });

    bool dropped = dd.EndDrop(Vec2{20, 0}, target);
    EXPECT_TRUE(dropped);
    EXPECT_EQ(target->drops, 1);
    EXPECT_FALSE(dd.IsDragging());
}

TEST(SlateDragDrop, EndDropNoTargetClearsState) {
    auto src = std::make_shared<DropTarget>();
    DragDropManager dd;
    dd.BeginDetect(Vec2{0, 0}, src);
    dd.Update(Vec2{20, 0}, nullptr,
        [](Vec2) { return std::make_shared<DragOperation>(); });
    EXPECT_TRUE(dd.IsDragging());

    bool dropped = dd.EndDrop(Vec2{20, 0}, nullptr);
    EXPECT_FALSE(dropped);
    EXPECT_FALSE(dd.IsDragging());
}

TEST(SlateDragDrop, CancelDropsActiveStateWithoutFiring) {
    auto src = std::make_shared<DropTarget>();
    auto target = std::make_shared<DropTarget>();
    DragDropManager dd;
    dd.BeginDetect(Vec2{0, 0}, src);
    dd.Update(Vec2{20, 0}, target,
        [](Vec2) { return std::make_shared<DragOperation>(); });
    EXPECT_TRUE(dd.IsDragging());

    dd.Cancel();
    EXPECT_FALSE(dd.IsDragging());
    EXPECT_EQ(target->drops, 0);
    EXPECT_EQ(target->leaves, 1); // OnDragLeave still fires defensively
}
