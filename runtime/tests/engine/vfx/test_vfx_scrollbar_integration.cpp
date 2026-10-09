/// Unit tests for phase 14.11.0: scrollbar integration invariants.
///
/// The overlay owns four `VfxScrollBar` members (stack / params /
/// picker / overview) that produce geometry + scrollY. These tests
/// lock in the contract the overlay relies on when feeding them
/// each frame — primarily that `NeedsScrollbar` gates rendering and
/// that the contract is consistent across every mounted scrollbar.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_scrollbar_widget.h"

using namespace uz;

TEST(VfxScrollbarIntegration, NoOverflowHidesScrollbar) {
    // Every overlay site wraps its `Render` in `if (contentH >
    // panel.h)`. This test locks in the underlying predicate so the
    // overlay's gate matches the widget's `NeedsScrollbar`.
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 200);
    EXPECT_FALSE(s.NeedsScrollbar());
}

TEST(VfxScrollbarIntegration, OverflowShowsScrollbar) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 401);
    EXPECT_TRUE(s.NeedsScrollbar());
}

TEST(VfxScrollbarIntegration, HoverWidensThumb) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 800);
    s.SetHovered(false);
    const float idleW = s.Width();
    s.SetHovered(true);
    const float hoverW = s.Width();
    EXPECT_GT(hoverW, idleW);
}

TEST(VfxScrollbarIntegration, ContentExactlyFittingHidesScrollbar) {
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 400);  // content == panel.
    EXPECT_FALSE(s.NeedsScrollbar());
}

TEST(VfxScrollbarIntegration, WheelDeltaFeedsScroll) {
    // Overlay feeds `-wheel * 40` through `Scroll`; verify the
    // arithmetic matches the host's assumption.
    VfxScrollBar s;
    s.SetView({0, 0, 240, 400}, 2000);
    s.SetScrollY(0);
    s.Scroll(-2.0f, 40.0f); // two notches down.
    EXPECT_FLOAT_EQ(s.ScrollY(), 80.0f);
}

TEST(VfxScrollbarIntegration, FourScrollbarsAreIndependent) {
    // Overlay holds four separate instances (stack / params / picker
    // / overview). Each maintains its own state.
    VfxScrollBar a, b, c, d;
    a.SetView({0, 0, 240, 400}, 800);
    b.SetView({0, 0, 240, 400}, 800);
    c.SetView({0, 0, 240, 400}, 800);
    d.SetView({0, 0, 240, 400}, 800);

    a.SetScrollY(100);
    b.SetScrollY(200);
    c.SetScrollY(300);
    d.SetScrollY(400);

    EXPECT_FLOAT_EQ(a.ScrollY(), 100.0f);
    EXPECT_FLOAT_EQ(b.ScrollY(), 200.0f);
    EXPECT_FLOAT_EQ(c.ScrollY(), 300.0f);
    EXPECT_FLOAT_EQ(d.ScrollY(), 400.0f);
}
