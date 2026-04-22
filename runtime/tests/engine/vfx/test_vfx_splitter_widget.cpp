/// Unit tests for phase 15.2.0: `VfxSplitterWidget`.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_splitter_widget.h"

using namespace uz;

TEST(VfxSplitter, HitTestInsideBounds) {
    VfxSplitterWidget s;
    const VfxStackRect bounds{640, 30, 6, 500};
    EXPECT_TRUE(s.HitTest(642, 200, bounds));
}

TEST(VfxSplitter, HitTestOutsideBounds) {
    VfxSplitterWidget s;
    const VfxStackRect bounds{640, 30, 6, 500};
    EXPECT_FALSE(s.HitTest(700, 200, bounds));
    EXPECT_FALSE(s.HitTest(640, 10,  bounds));
}

TEST(VfxSplitter, HitTestBoundaryInclusiveLeftExclusiveRight) {
    VfxSplitterWidget s;
    const VfxStackRect bounds{100, 0, 6, 500};
    EXPECT_TRUE(s.HitTest(100, 10, bounds)); // left edge inclusive.
    EXPECT_FALSE(s.HitTest(106, 10, bounds)); // right edge exclusive.
}

TEST(VfxSplitter, BeginDragSetsDraggingTrue) {
    VfxSplitterWidget s;
    EXPECT_FALSE(s.IsDragging());
    s.BeginDrag(100.0f);
    EXPECT_TRUE(s.IsDragging());
    s.EndDrag();
    EXPECT_FALSE(s.IsDragging());
}

TEST(VfxSplitter, UpdateDragReturnsFrameLocalDelta) {
    VfxSplitterWidget s;
    s.BeginDrag(100.0f);
    EXPECT_FLOAT_EQ(s.UpdateDrag(110.0f), 10.0f);
    // Second call: anchor now at 110 → delta 5.
    EXPECT_FLOAT_EQ(s.UpdateDrag(115.0f),  5.0f);
    // Drag backward: negative delta.
    EXPECT_FLOAT_EQ(s.UpdateDrag(108.0f), -7.0f);
}

TEST(VfxSplitter, UpdateDragWhenNotDraggingReturnsZero) {
    VfxSplitterWidget s;
    EXPECT_FLOAT_EQ(s.UpdateDrag(500.0f), 0.0f);
}

TEST(VfxSplitter, EndDragResetsAnchor) {
    VfxSplitterWidget s;
    s.BeginDrag(100.0f);
    s.UpdateDrag(150.0f);
    s.EndDrag();
    EXPECT_FALSE(s.IsDragging());
    // Subsequent BeginDrag uses the new anchor.
    s.BeginDrag(200.0f);
    EXPECT_FLOAT_EQ(s.UpdateDrag(210.0f), 10.0f);
}

TEST(VfxSplitter, HoverFlagRoundTrips) {
    VfxSplitterWidget s;
    EXPECT_FALSE(s.IsHovered());
    s.SetHovered(true);
    EXPECT_TRUE(s.IsHovered());
    s.SetHovered(false);
    EXPECT_FALSE(s.IsHovered());
}
