/// Unit tests for phase 14.7.0: `VfxContentBrowserWidget`.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_content_browser_widget.h"

using namespace uz;

namespace {

VfxAssetTile MakeTile(uint32_t id, const char* label) {
    VfxAssetTile t;
    t.id = id;
    t.label = label;
    t.kindTint = { 0.6f, 0.3f, 0.85f, 1.0f };
    return t;
}

} // namespace

TEST(VfxContentBrowser, ColumnCountAtNarrowPanel) {
    VfxContentBrowserWidget w;
    // 320 px wide: cols = floor((320 - 8) / (96 + 8)) = 3.
    EXPECT_EQ(w.ColumnCount(320.0f), 3u);
}

TEST(VfxContentBrowser, ColumnCountAtWidePanel) {
    VfxContentBrowserWidget w;
    // 1200 px wide: cols = floor((1200 - 8) / 104) = 11.
    EXPECT_EQ(w.ColumnCount(1200.0f), 11u);
}

TEST(VfxContentBrowser, ColumnCountMinimumOne) {
    VfxContentBrowserWidget w;
    // Even 50 px panel still returns 1 column.
    EXPECT_EQ(w.ColumnCount(50.0f), 1u);
}

TEST(VfxContentBrowser, TileTotalHeightSum) {
    VfxContentBrowserWidget w;
    EXPECT_FLOAT_EQ(w.TileTotalHeight(), 96.0f + 24.0f);
}

TEST(VfxContentBrowser, LayoutFlowsRowByRow) {
    VfxContentBrowserWidget w;
    for (uint32_t i = 1; i <= 5; ++i) {
        w.AddTile(MakeTile(i, "Asset"));
    }
    // 320 panel → 3 columns → tile 4 starts a new row.
    const auto layout = w.ComputeLayout({0.0f, 0.0f, 320.0f, 500.0f});
    // Filter to Tile items and check that tile index 4 (id=4) has
    // y > tile index 1.
    VfxStackRect tile1{}, tile4{};
    for (const auto& it : layout) {
        if (it.type != VfxAssetItemType::Tile) continue;
        if (it.tileId == 1u) tile1 = it.rect;
        if (it.tileId == 4u) tile4 = it.rect;
    }
    EXPECT_GT(tile4.y, tile1.y);
}

TEST(VfxContentBrowser, HitTestInsideTile) {
    VfxContentBrowserWidget w;
    w.AddTile(MakeTile(42, "X"));
    const auto layout = w.ComputeLayout({0.0f, 0.0f, 320.0f, 500.0f});
    // First tile centre.
    const float cx = 8.0f + 48.0f;   // gap + thumbSize/2
    const float cy = 8.0f + 60.0f;   // gap + tileHeight/2
    EXPECT_EQ(w.HitTest(layout, cx, cy), 42u);
}

TEST(VfxContentBrowser, HitTestMissesOutsideTile) {
    VfxContentBrowserWidget w;
    w.AddTile(MakeTile(42, "X"));
    const auto layout = w.ComputeLayout({0.0f, 0.0f, 320.0f, 500.0f});
    EXPECT_EQ(w.HitTest(layout, 999.0f, 999.0f), 0u);
}

TEST(VfxContentBrowser, DragThresholdFiresAboveSix) {
    VfxContentBrowserWidget w;
    w.BeginMouseDown(1u, 100.0f, 100.0f);
    EXPECT_FALSE(w.MouseMove(102.0f, 102.0f)); // ~2.8 px — below threshold.
    EXPECT_FALSE(w.IsDragActive());
    const bool fired = w.MouseMove(110.0f, 110.0f); // ~14 px — above.
    EXPECT_TRUE(fired);
    EXPECT_TRUE(w.IsDragActive());
}

TEST(VfxContentBrowser, DragFiresOnlyOnce) {
    VfxContentBrowserWidget w;
    w.BeginMouseDown(1u, 100.0f, 100.0f);
    EXPECT_TRUE(w.MouseMove(120.0f, 120.0f));
    // Subsequent moves do NOT fire again — drag is "already started".
    EXPECT_FALSE(w.MouseMove(130.0f, 130.0f));
}

TEST(VfxContentBrowser, MouseUpReturnsTileId) {
    VfxContentBrowserWidget w;
    w.BeginMouseDown(77u, 0.0f, 0.0f);
    EXPECT_EQ(w.EndMouseUp(), 77u);
    EXPECT_EQ(w.EndMouseUp(), 0u);  // already consumed.
}

TEST(VfxContentBrowser, SelectionRoundTrips) {
    VfxContentBrowserWidget w;
    w.AddTile(MakeTile(5, "X"));
    w.SetSelectedId(5u);
    EXPECT_EQ(w.SelectedId(), 5u);
}
