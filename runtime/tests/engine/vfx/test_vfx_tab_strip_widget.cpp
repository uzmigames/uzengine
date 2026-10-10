/// Unit tests for phase 14.2.0: `VfxTabStripWidget`.
///
/// Covers:
///   - `MeasureTab` returns at least `tabMinWidth` and scales with
///     label length.
///   - `ComputeLayout` emits one `Tab` item per entry + `Divider`
///     between adjacent tabs + `CloseIcon` item per closable tab.
///   - `HitTest` prioritises close-icon over tab body when they
///     overlap.
///   - Close icon hit-test is a separate region inside the tab rect.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_tab_strip_widget.h"

using namespace uz;

namespace {

VfxTabEntry MakeEntry(const char* label, bool closable = true,
                      VfxUiIcon icon = VfxUiIcon::None) {
    VfxTabEntry e;
    e.label    = label;
    e.icon     = icon;
    e.closable = closable;
    return e;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Measure
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTabStrip, MeasureTabHonoursMinWidth) {
    VfxTabStripWidget w;
    w.tabMinWidth = 80.0f;
    const float width = w.MeasureTab(MakeEntry("A"));
    EXPECT_GE(width, 80.0f);
}

TEST(VfxTabStrip, MeasureTabScalesWithLabelLength) {
    VfxTabStripWidget w;
    const float shortW = w.MeasureTab(MakeEntry("A"));
    const float longW  = w.MeasureTab(MakeEntry("ALongerTabLabel"));
    EXPECT_GT(longW, shortW);
}

TEST(VfxTabStrip, MeasureTabWithIconIsLargerThanWithout) {
    VfxTabStripWidget w;
    const float noIcon = w.MeasureTab(MakeEntry("AAAAAAAAAAAAAAA", true,
                                                  VfxUiIcon::None));
    const float withIcon = w.MeasureTab(MakeEntry("AAAAAAAAAAAAAAA", true,
                                                    VfxUiIcon::Dot));
    EXPECT_GT(withIcon, noIcon);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTabStrip, LayoutEmitsOneTabPerEntry) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    w.AddTab(MakeEntry("B"));
    w.AddTab(MakeEntry("C"));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 1000.0f, 30.0f});
    size_t tabs = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTabItemType::Tab) ++tabs;
    }
    EXPECT_EQ(tabs, 3u);
}

TEST(VfxTabStrip, LayoutEmitsDivider_BetweenAdjacentTabs) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    w.AddTab(MakeEntry("B"));
    w.AddTab(MakeEntry("C"));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 1000.0f, 30.0f});
    size_t dividers = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTabItemType::Divider) ++dividers;
    }
    // 3 tabs → 2 dividers between them (no trailing divider).
    EXPECT_EQ(dividers, 2u);
}

TEST(VfxTabStrip, LayoutEmitsCloseIconPerClosableTab) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("Closable", true));
    w.AddTab(MakeEntry("Fixed",    false));
    w.AddTab(MakeEntry("AlsoClosable", true));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 1000.0f, 30.0f});
    size_t closeIcons = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTabItemType::CloseIcon) ++closeIcons;
    }
    EXPECT_EQ(closeIcons, 2u);
}

TEST(VfxTabStrip, LayoutTabsFlowLeftToRight) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    w.AddTab(MakeEntry("B"));
    w.AddTab(MakeEntry("C"));

    const auto layout = w.ComputeLayout({100.0f, 0.0f, 1000.0f, 30.0f});
    float prevEndX = 100.0f;
    for (const auto& it : layout) {
        if (it.type != VfxTabItemType::Tab) continue;
        EXPECT_GE(it.rect.x, prevEndX);
        prevEndX = it.rect.x + it.rect.width;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTabStrip, HitTestMissOutsideStrip) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    const auto layout = w.ComputeLayout({0.0f, 0.0f, 1000.0f, 30.0f});
    const auto r = w.HitTest(layout, -10.0f, 15.0f);
    EXPECT_EQ(r.type, VfxTabItemType::None);
    EXPECT_EQ(r.tabIndex, static_cast<size_t>(-1));
}

TEST(VfxTabStrip, HitTestFindsTab) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    const auto layout = w.ComputeLayout({0.0f, 0.0f, 1000.0f, 30.0f});
    // Hit centre of first tab (minWidth=80 → centre at 40).
    const auto r = w.HitTest(layout, 40.0f, 14.0f);
    EXPECT_EQ(r.type, VfxTabItemType::Tab);
    EXPECT_EQ(r.tabIndex, 0u);
}

TEST(VfxTabStrip, HitTestCloseIconTakesPriorityOverTabBody) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    const auto layout = w.ComputeLayout({0.0f, 0.0f, 1000.0f, 30.0f});
    // Find the close icon rect.
    VfxStackRect closeRect{};
    for (const auto& it : layout) {
        if (it.type == VfxTabItemType::CloseIcon) {
            closeRect = it.rect;
            break;
        }
    }
    ASSERT_GT(closeRect.width, 0.0f);
    const float cx = closeRect.x + closeRect.width * 0.5f;
    const float cy = closeRect.y + closeRect.height * 0.5f;

    const auto r = w.HitTest(layout, cx, cy);
    EXPECT_EQ(r.type, VfxTabItemType::CloseIcon);
    EXPECT_EQ(r.tabIndex, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection state
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxTabStrip, SetActiveIndexClampedToCount) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    w.AddTab(MakeEntry("B"));
    w.SetActiveIndex(99);  // out of range — keeps previous value (0).
    EXPECT_LT(w.ActiveIndex(), 2u);
}

TEST(VfxTabStrip, ClearResetsTabs) {
    VfxTabStripWidget w;
    w.AddTab(MakeEntry("A"));
    w.AddTab(MakeEntry("B"));
    EXPECT_EQ(w.Count(), 2u);
    w.Clear();
    EXPECT_EQ(w.Count(), 0u);
}
