/// Unit tests for Slate UI invalidation system (Phase 18 / Step 5).
///
/// Covers:
///   - Initial widget state has Layout|Paint dirty (first frame must
///     measure + paint).
///   - Layout invalidation bubbles up to the parent chain (HBox child →
///     HBox parent → root).
///   - Paint invalidation does NOT bubble (parent unaffected).
///   - ClearDirty resets the bit.
///   - Attribute<T>::operator=(literal) calls Invalidate(flag) on the
///     bound owner only when the value differs.
///   - Attribute<T>::operator=(lambda) does NOT auto-invalidate.

#include "engine/ui/slate_attribute.h"
#include "engine/ui/slate_box.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_widget.h"

#include <gtest/gtest.h>

#include <memory>

using namespace uz;
using namespace uz::ui;

namespace {

class Leaf final : public Widget {
public:
    Vec2 Measure(Vec2 m) override { return m; }
    void Paint(SlateRenderer& /*r*/) override {}
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Initial state
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateInvalidation, NewWidgetIsLayoutAndPaintDirty) {
    Leaf w;
    EXPECT_TRUE(w.IsDirty(InvalidationFlag::Layout));
    EXPECT_TRUE(w.IsDirty(InvalidationFlag::Paint));
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Visibility));
}

TEST(SlateInvalidation, ClearDirtyDropsTheBit) {
    Leaf w;
    w.ClearDirty(InvalidationFlag::Layout);
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Layout));
    EXPECT_TRUE(w.IsDirty(InvalidationFlag::Paint)); // untouched
}

// ─────────────────────────────────────────────────────────────────────────────
// Bubble rules
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateInvalidation, LayoutBubblesToParent) {
    auto child = std::make_shared<Leaf>();
    auto outer = std::make_shared<HorizontalBox>();
    outer->AddSlot(child);
    child->SetParent(outer);

    // Clear all dirty bits to start clean.
    child->ClearDirty(InvalidationFlag::Layout);
    child->ClearDirty(InvalidationFlag::Paint);
    outer->ClearDirty(InvalidationFlag::Layout);
    outer->ClearDirty(InvalidationFlag::Paint);

    child->Invalidate(InvalidationFlag::Layout);

    EXPECT_TRUE(child->IsDirty(InvalidationFlag::Layout));
    EXPECT_TRUE(outer->IsDirty(InvalidationFlag::Layout));
}

TEST(SlateInvalidation, PaintDoesNotBubble) {
    auto child = std::make_shared<Leaf>();
    auto outer = std::make_shared<HorizontalBox>();
    outer->AddSlot(child);
    child->SetParent(outer);

    child->ClearDirty(InvalidationFlag::Layout);
    child->ClearDirty(InvalidationFlag::Paint);
    outer->ClearDirty(InvalidationFlag::Layout);
    outer->ClearDirty(InvalidationFlag::Paint);

    child->Invalidate(InvalidationFlag::Paint);

    EXPECT_TRUE(child->IsDirty(InvalidationFlag::Paint));
    EXPECT_FALSE(outer->IsDirty(InvalidationFlag::Paint));
    EXPECT_FALSE(outer->IsDirty(InvalidationFlag::Layout));
}

TEST(SlateInvalidation, LayoutBubblesAcrossMultipleLevels) {
    auto leaf = std::make_shared<Leaf>();
    auto inner = std::make_shared<HorizontalBox>();
    auto outer = std::make_shared<VerticalBox>();
    inner->AddSlot(leaf);
    outer->AddSlot(inner);
    leaf->SetParent(inner);
    inner->SetParent(outer);

    leaf->ClearDirty(InvalidationFlag::Layout);
    inner->ClearDirty(InvalidationFlag::Layout);
    outer->ClearDirty(InvalidationFlag::Layout);

    leaf->Invalidate(InvalidationFlag::Layout);

    EXPECT_TRUE(leaf->IsDirty(InvalidationFlag::Layout));
    EXPECT_TRUE(inner->IsDirty(InvalidationFlag::Layout));
    EXPECT_TRUE(outer->IsDirty(InvalidationFlag::Layout));
}

// ─────────────────────────────────────────────────────────────────────────────
// Attribute auto-invalidation
// ─────────────────────────────────────────────────────────────────────────────

TEST(SlateInvalidation, AttributeLiteralChangeInvalidatesOwner) {
    Leaf w;
    Attribute<int> attr;
    attr.BindOwner(&w, InvalidationFlag::Paint);
    attr = 10;

    w.ClearDirty(InvalidationFlag::Paint);
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Paint));

    attr = 20; // value change → invalidate
    EXPECT_TRUE(w.IsDirty(InvalidationFlag::Paint));
}

TEST(SlateInvalidation, AttributeSameValueDoesNotInvalidate) {
    Leaf w;
    Attribute<int> attr;
    attr.BindOwner(&w, InvalidationFlag::Paint);
    attr = 42;

    w.ClearDirty(InvalidationFlag::Paint);
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Paint));

    attr = 42; // same value → no invalidate
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Paint));
}

TEST(SlateInvalidation, AttributeLambdaDoesNotAutoInvalidate) {
    Leaf w;
    Attribute<int> attr;
    attr.BindOwner(&w, InvalidationFlag::Paint);

    w.ClearDirty(InvalidationFlag::Paint);
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Paint));

    attr = std::function<int()>([]() { return 7; });
    EXPECT_FALSE(w.IsDirty(InvalidationFlag::Paint));
}

TEST(SlateInvalidation, AttributeOnlyLayoutFlagPropagatesToBoundOwner) {
    auto leaf = std::make_shared<Leaf>();
    auto outer = std::make_shared<HorizontalBox>();
    outer->AddSlot(leaf);
    leaf->SetParent(outer);

    Attribute<int> attr;
    attr.BindOwner(leaf.get(), InvalidationFlag::Layout);
    attr = 1;

    leaf->ClearDirty(InvalidationFlag::Layout);
    outer->ClearDirty(InvalidationFlag::Layout);

    attr = 2; // change → invalidate Layout (bubbles)
    EXPECT_TRUE(leaf->IsDirty(InvalidationFlag::Layout));
    EXPECT_TRUE(outer->IsDirty(InvalidationFlag::Layout));
}
