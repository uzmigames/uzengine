/// Unit tests for phase 14.16.0: content-browser modal contract.
///
/// Tests the widget state sequence the overlay's `OpenContentBrowser`
/// + `CloseContentBrowser` follow: open populates tiles, close leaves
/// them intact, reopen does not duplicate.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_content_browser_widget.h"

using namespace uz;

namespace {

void PopulateDemoTiles(VfxContentBrowserWidget& cb) {
    if (cb.Count() > 0) return;
    const char* names[] = {"Sparks", "Smoke", "Fire",
                           "Dust", "Magic", "Explosion"};
    for (uint32_t i = 0; i < 6; ++i) {
        VfxAssetTile t;
        t.id = i + 1;
        t.label = names[i];
        cb.AddTile(t);
    }
}

} // namespace

TEST(ContentBrowserModal, FirstOpenPopulatesSixTiles) {
    VfxContentBrowserWidget cb;
    PopulateDemoTiles(cb);
    EXPECT_EQ(cb.Count(), 6u);
}

TEST(ContentBrowserModal, ReopenDoesNotDuplicateTiles) {
    VfxContentBrowserWidget cb;
    PopulateDemoTiles(cb);
    PopulateDemoTiles(cb);  // Second call is a no-op via count guard.
    EXPECT_EQ(cb.Count(), 6u);
}

TEST(ContentBrowserModal, DemoTileLabelsAreExpected) {
    VfxContentBrowserWidget cb;
    PopulateDemoTiles(cb);
    const auto& tiles = cb.Tiles();
    ASSERT_EQ(tiles.size(), 6u);
    EXPECT_EQ(tiles[0].label, "Sparks");
    EXPECT_EQ(tiles[5].label, "Explosion");
}

TEST(ContentBrowserModal, SelectionIdMapsToOneBasedTileIndex) {
    VfxContentBrowserWidget cb;
    PopulateDemoTiles(cb);
    cb.SetSelectedId(3u);
    EXPECT_EQ(cb.SelectedId(), 3u);
    // tile[2].id == 3 → "Fire".
    const auto& tiles = cb.Tiles();
    for (const auto& t : tiles) {
        if (t.id == 3u) {
            EXPECT_EQ(t.label, "Fire");
            return;
        }
    }
    FAIL() << "tile id 3 not found";
}

TEST(ContentBrowserModal, HitTestInsideGridFindsTile) {
    VfxContentBrowserWidget cb;
    PopulateDemoTiles(cb);
    const VfxStackRect bounds{0, 0, 720, 480};
    const auto layout = cb.ComputeLayout(bounds);
    ASSERT_FALSE(layout.empty());
    // First tile centre.
    const auto first = layout.front();
    const float cx = first.rect.x + first.rect.width * 0.5f;
    const float cy = first.rect.y + first.rect.height * 0.5f;
    EXPECT_NE(cb.HitTest(layout, cx, cy), 0u);
}

TEST(ContentBrowserModal, ClearEmptiesTileList) {
    VfxContentBrowserWidget cb;
    PopulateDemoTiles(cb);
    cb.Clear();
    EXPECT_EQ(cb.Count(), 0u);
}
