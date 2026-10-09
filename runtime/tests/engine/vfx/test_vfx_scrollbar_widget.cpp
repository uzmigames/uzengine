/// Unit tests for phase 14.9.0: `VfxScrollBar`.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_scrollbar_widget.h"

using namespace uz;

TEST(VfxScrollBar, IdleWidthIs6) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetHovered(false);
    s.SetPressed(false);
    EXPECT_FLOAT_EQ(s.Width(), 6.0f);
}

TEST(VfxScrollBar, HoverWidthIs8) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetHovered(true);
    EXPECT_FLOAT_EQ(s.Width(), 8.0f);
}

TEST(VfxScrollBar, TrackRightEdge) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetHovered(false);
    const auto track = s.TrackRect();
    EXPECT_FLOAT_EQ(track.x,      240.0f - 6.0f);
    EXPECT_FLOAT_EQ(track.y,      0.0f);
    EXPECT_FLOAT_EQ(track.width,  6.0f);
    EXPECT_FLOAT_EQ(track.height, 400.0f);
}

TEST(VfxScrollBar, ThumbHeightProportional) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    // panelH / contentH = 0.5 → thumb height = 400 * 0.5 = 200
    EXPECT_FLOAT_EQ(s.ThumbHeight(), 200.0f);
}

TEST(VfxScrollBar, ThumbHeightMinimum20) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 50000);
    EXPECT_FLOAT_EQ(s.ThumbHeight(), 20.0f);
}

TEST(VfxScrollBar, ThumbYTracksScroll) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);  // contentH 800, panelH 400 → maxScroll 400
    s.SetScrollY(200);  // halfway.
    // thumbH = 200, travel = panelH - thumbH = 200
    // thumbY = 0 + (200/400) * 200 = 100
    const auto thumb = s.ThumbRect();
    EXPECT_FLOAT_EQ(thumb.y,      100.0f);
    EXPECT_FLOAT_EQ(thumb.height, 200.0f);
}

TEST(VfxScrollBar, NoNeedWhenContentFits) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 200);  // content < panel
    EXPECT_FALSE(s.NeedsScrollbar());
    EXPECT_FLOAT_EQ(s.MaxScroll(), 0.0f);
}

TEST(VfxScrollBar, ScrollWheelAdvances) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetScrollY(100);
    s.Scroll(-1.0f, 40.0f);  // wheel-down one notch.
    EXPECT_FLOAT_EQ(s.ScrollY(), 140.0f);
}

TEST(VfxScrollBar, ScrollYClampedToMax) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetScrollY(99999.0f);
    EXPECT_FLOAT_EQ(s.ScrollY(), 400.0f);  // maxScroll = contentH - panelH.
}

TEST(VfxScrollBar, ScrollYClampedToZero) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetScrollY(-50.0f);
    EXPECT_FLOAT_EQ(s.ScrollY(), 0.0f);
}

TEST(VfxScrollBar, DragConvertsPixelsToContent) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    // travel = 200, maxScroll = 400 → drag(50 px) == +100 scrollY.
    s.Drag(50.0f);
    EXPECT_FLOAT_EQ(s.ScrollY(), 100.0f);
}

TEST(VfxScrollBar, PageUpDownAdvancesByPanelHeight) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 2000);
    s.PageDown();
    EXPECT_FLOAT_EQ(s.ScrollY(), 400.0f);
    s.PageDown();
    EXPECT_FLOAT_EQ(s.ScrollY(), 800.0f);
    s.PageUp();
    EXPECT_FLOAT_EQ(s.ScrollY(), 400.0f);
}
