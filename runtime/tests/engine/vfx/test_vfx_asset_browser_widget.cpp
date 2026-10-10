/// Unit tests for phase 10.3.0 — VFX asset browser widget.
///
/// @ref engine/vfx/editor/vfx_asset_browser_widget.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_asset_browser_widget.h"

using namespace uz;

namespace {

std::vector<VfxBrowserAssetEntry> MakeTestEntries() {
    return {
        {"Fireball", "vfx/fireball.uzvfxsys", "", {"fire", "magic"}},
        {"Smoke",    "vfx/smoke.uzvfxsys",    "", {"ambient"}},
        {"Sparks",   "vfx/sparks.uzfx",        "", {"fire", "impact"}},
        {"Rain",     "vfx/rain.uzfx",           "", {"weather"}},
    };
}

} // namespace

TEST(VfxAssetBrowserWidget, StartsEmpty) {
    VfxAssetBrowserWidget w;
    EXPECT_EQ(w.TotalAssetCount(), 0u);
    EXPECT_EQ(w.VisibleTileCount(), 0u);
    EXPECT_TRUE(w.SelectedAssetPath().empty());
}

TEST(VfxAssetBrowserWidget, RefreshPopulatesFromDataSource) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    EXPECT_EQ(w.TotalAssetCount(), 4u);
    EXPECT_EQ(w.VisibleTileCount(), 4u); // no filter
}

TEST(VfxAssetBrowserWidget, FilterByName) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    w.SetFilter("fire");
    EXPECT_EQ(w.VisibleTileCount(), 1u); // "Fireball" matches
}

TEST(VfxAssetBrowserWidget, FilterByNameCaseInsensitive) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    w.SetFilter("SMOKE");
    EXPECT_EQ(w.VisibleTileCount(), 1u);
}

TEST(VfxAssetBrowserWidget, FilterByTag) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    w.SetFilter("impact");
    EXPECT_EQ(w.VisibleTileCount(), 1u); // Sparks has "impact" tag
}

TEST(VfxAssetBrowserWidget, EmptyFilterShowsAll) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    w.SetFilter("fire");
    EXPECT_EQ(w.VisibleTileCount(), 1u);
    w.SetFilter("");
    EXPECT_EQ(w.VisibleTileCount(), 4u);
}

TEST(VfxAssetBrowserWidget, SelectAsset) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();

    std::string selected;
    w.onSelectionChanged = [&](const std::string& path) { selected = path; };

    w.SelectAsset("vfx/smoke.uzvfxsys");
    EXPECT_EQ(w.SelectedAssetPath(), "vfx/smoke.uzvfxsys");
    EXPECT_EQ(selected, "vfx/smoke.uzvfxsys");
}

TEST(VfxAssetBrowserWidget, SelectUnknownIsNoOp) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    w.SelectAsset("vfx/nonexistent.uzfx");
    EXPECT_TRUE(w.SelectedAssetPath().empty());
}

TEST(VfxAssetBrowserWidget, ClearSelection) {
    VfxAssetBrowserWidget w;
    w.SetDataSource(MakeTestEntries);
    w.Refresh();
    w.SelectAsset("vfx/rain.uzfx");
    EXPECT_FALSE(w.SelectedAssetPath().empty());
    w.ClearSelection();
    EXPECT_TRUE(w.SelectedAssetPath().empty());
}

TEST(VfxAssetBrowserWidget, FilterBufferMutable) {
    VfxAssetBrowserWidget w;
    w.FilterBuffer() = "test";
    EXPECT_EQ(w.GetFilter(), "test");
}

TEST(VfxAssetBrowserWidget, ScrollOffset) {
    VfxAssetBrowserWidget w;
    EXPECT_FLOAT_EQ(w.ScrollOffset(), 0.0f);
    w.SetScrollOffset(42.5f);
    EXPECT_FLOAT_EQ(w.ScrollOffset(), 42.5f);
}
