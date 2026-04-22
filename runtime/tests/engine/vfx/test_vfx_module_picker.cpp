/// Unit tests for phase 11.3.0: `VfxModuleMetadata` catalog +
/// `VfxModulePickerDialog` state machine.
///
/// Covers:
///   - Catalog lookups: `Find`, `GetCategory`, `GetDescription`,
///     `IsValidInStage` for known and unknown module names.
///   - `FilterByCategoryAndStage` returns the expected subsets for each
///     built-in category / stage combination.
///   - Every entry in the built-in table has a non-null name, category,
///     description, and at least one valid stage.
///   - Dialog lifecycle: `Open` sets state + pre-selects, `Close` clears
///     `IsOpen`, `ConfirmSelection` only fires when a tile is picked.
///   - Category + filter narrow `VisibleModules`, selection snaps back
///     to index 0 when the target category/filter invalidates selection.
///   - Arrow navigation wraps; `SelectFirst`/`SelectLast` match.
///   - Layout emits tabs + filter box + tile rects; hit-test resolves
///     close button > tab > filter > tile precedence.
///   - Anchor nudge-on-screen-overflow.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_module_picker_dialog.h"
#include "engine/vfx/modules/vfx_module_metadata.h"
#include "engine/vfx/stack/vfx_stage.h"

#include <algorithm>
#include <string>
#include <vector>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Catalog
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModuleMetadata, Find_KnownReturnsEntry_UnknownReturnsNull) {
    const auto* grav = VfxModuleMetadata::Find("update");
    ASSERT_NE(grav, nullptr);
    EXPECT_STREQ(grav->category, "Simulation");

    EXPECT_EQ(VfxModuleMetadata::Find("not_a_module"), nullptr);
    EXPECT_EQ(VfxModuleMetadata::Find(nullptr), nullptr);
}

TEST(VfxModuleMetadata, GetCategory_UnknownReturnsCustom) {
    EXPECT_EQ(VfxModuleMetadata::GetCategory("sprite_render"), "Render");
    EXPECT_EQ(VfxModuleMetadata::GetCategory("unknown_user_mod"), "Custom");
}

TEST(VfxModuleMetadata, GetDescription_UnknownReturnsEmpty) {
    EXPECT_FALSE(VfxModuleMetadata::GetDescription("sprite_render").empty());
    EXPECT_TRUE(VfxModuleMetadata::GetDescription("unknown_user_mod").empty());
}

TEST(VfxModuleMetadata, IsValidInStage_KnownModuleRespectsMask) {
    // `update` is ParticleUpdate-only.
    EXPECT_TRUE(VfxModuleMetadata::IsValidInStage("update", VfxStage::ParticleUpdate));
    EXPECT_FALSE(VfxModuleMetadata::IsValidInStage("update", VfxStage::Render));
    EXPECT_FALSE(VfxModuleMetadata::IsValidInStage("update", VfxStage::SystemSpawn));

    // `sprite_render` is Render-only.
    EXPECT_TRUE(VfxModuleMetadata::IsValidInStage("sprite_render", VfxStage::Render));
    EXPECT_FALSE(VfxModuleMetadata::IsValidInStage("sprite_render",
                                                    VfxStage::ParticleUpdate));
}

TEST(VfxModuleMetadata, IsValidInStage_UnknownIsPermissive) {
    for (uint8_t i = 0; i < kVfxStageCount; ++i) {
        EXPECT_TRUE(VfxModuleMetadata::IsValidInStage("user_mod",
                                                       static_cast<VfxStage>(i)))
            << "Unknown modules should be permitted in every stage";
    }
}

TEST(VfxModuleMetadata, Categories_StartsWithAll_NoDuplicates) {
    const auto cats = VfxModuleMetadata::Categories();
    ASSERT_FALSE(cats.empty());
    EXPECT_EQ(cats.front(), "All");
    for (size_t i = 1; i < cats.size(); ++i) {
        for (size_t j = i + 1; j < cats.size(); ++j) {
            EXPECT_NE(cats[i], cats[j]) << "Duplicate category: " << cats[i];
        }
    }
}

TEST(VfxModuleMetadata, FilterByCategoryAndStage_RenderStageRenderCategory) {
    const auto names = VfxModuleMetadata::FilterByCategoryAndStage(
        "Render", VfxStage::Render);
    const std::vector<std::string> expected{
        "sprite_render", "light_render", "mesh_render"
    };
    EXPECT_EQ(names.size(), expected.size());
    for (const auto& e : expected) {
        EXPECT_NE(std::find(names.begin(), names.end(), e), names.end())
            << "Expected " << e << " in Render/Render";
    }
}

TEST(VfxModuleMetadata, FilterByCategoryAndStage_AllMatchesEverything) {
    const auto names = VfxModuleMetadata::FilterByCategoryAndStage(
        "All", VfxStage::ParticleUpdate);
    EXPECT_GT(names.size(), 5u);
    // A Render-only module must not appear here.
    EXPECT_EQ(std::find(names.begin(), names.end(), "sprite_render"),
              names.end());
}

TEST(VfxModuleMetadata, FilterByCategoryAndStage_IncompatibleStageYieldsEmpty) {
    // Render category in ParticleUpdate stage — no modules should match.
    const auto names = VfxModuleMetadata::FilterByCategoryAndStage(
        "Render", VfxStage::ParticleUpdate);
    EXPECT_TRUE(names.empty());
}

TEST(VfxModuleMetadata, TableConsistency_EveryEntryWellFormed) {
    size_t count = 0;
    const VfxModuleCatalogEntry* table = VfxModuleMetadata::Table(count);
    ASSERT_GT(count, 0u);
    for (size_t i = 0; i < count; ++i) {
        const auto& e = table[i];
        ASSERT_NE(e.name, nullptr)        << "Row " << i << " has null name";
        EXPECT_GT(std::string(e.name).size(), 0u);
        EXPECT_NE(e.category, nullptr);
        EXPECT_NE(e.description, nullptr);
        EXPECT_NE(e.validStageMask, 0u) << "Row " << e.name << " has no valid stage";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dialog lifecycle
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerDialog, DefaultClosed) {
    VfxModulePickerDialog d;
    EXPECT_FALSE(d.IsOpen());
    EXPECT_TRUE(d.ComputeLayout().empty());
}

TEST(VfxModulePickerDialog, Open_SetsState_PreselectsFirstTile) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 120.0f);
    EXPECT_TRUE(d.IsOpen());
    EXPECT_EQ(d.TargetStage(), VfxStage::ParticleUpdate);
    EXPECT_EQ(d.ActiveCategory(), "All");
    EXPECT_TRUE(d.Filter().empty());

    // At least one tile visible for ParticleUpdate → selection is 0.
    ASSERT_FALSE(d.VisibleModules().empty());
    EXPECT_EQ(d.SelectedIndex(), 0u);
}

TEST(VfxModulePickerDialog, Close_ClearsOpenFlag) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.Close();
    EXPECT_FALSE(d.IsOpen());
    EXPECT_TRUE(d.ComputeLayout().empty());
}

TEST(VfxModulePickerDialog, Open_ResetsFilterAndCategory) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.SetCategory("Forces");
    d.SetFilter("attract");
    d.Close();

    d.Open(VfxStage::ParticleUpdate, 50.0f, 60.0f);
    EXPECT_EQ(d.ActiveCategory(), "All");
    EXPECT_TRUE(d.Filter().empty());
}

TEST(VfxModulePickerDialog, Open_NudgesIntoScreenBounds) {
    VfxModulePickerDialog d;
    // Anchor beyond bottom-right → dialog slides back.
    d.Open(VfxStage::ParticleUpdate, 10000.0f, 10000.0f,
           /*screenW*/ 1280.0f, /*screenH*/ 720.0f);
    const VfxStackRect b = d.Bounds();
    EXPECT_LE(b.x + b.width, 1280.0f);
    EXPECT_LE(b.y + b.height, 720.0f);
    EXPECT_GE(b.x, 0.0f);
    EXPECT_GE(b.y, 0.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Category + filter narrowing
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerDialog, CategoryTab_NarrowsVisibleList) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    const size_t allCount = d.VisibleModules().size();

    d.SetCategory("Forces");
    const auto forces = d.VisibleModules();
    EXPECT_LT(forces.size(), allCount);
    EXPECT_GT(forces.size(), 0u);
    for (const auto& m : forces) {
        EXPECT_EQ(VfxModuleMetadata::GetCategory(m), "Forces");
    }
}

TEST(VfxModulePickerDialog, SetCategory_UnknownIsNoOp) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.SetCategory("BogusCategory");
    EXPECT_EQ(d.ActiveCategory(), "All");
}

TEST(VfxModulePickerDialog, Filter_CaseInsensitiveSubstringOnNameOrDesc) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    d.SetFilter("COLL");
    const auto hits = d.VisibleModules();
    ASSERT_FALSE(hits.empty());
    // Collision module should appear (name match).
    EXPECT_NE(std::find(hits.begin(), hits.end(), "collision"), hits.end());

    d.SetFilter("zzznope");
    EXPECT_TRUE(d.VisibleModules().empty());
    EXPECT_EQ(d.SelectedIndex(), static_cast<size_t>(-1));
}

TEST(VfxModulePickerDialog, SetCategoryOrFilter_ResetsSelectionToZero) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    d.SelectIndex(3);
    EXPECT_EQ(d.SelectedIndex(), 3u);

    d.SetCategory("Forces");
    EXPECT_EQ(d.SelectedIndex(), 0u);

    d.SelectIndex(1);
    d.SetFilter("attract");
    EXPECT_EQ(d.SelectedIndex(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Navigation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerDialog, NavigateSelection_WrapsBothWays) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.SetCategory("Forces");
    const size_t n = d.VisibleModules().size();
    ASSERT_GT(n, 1u);

    d.SelectFirst();
    EXPECT_EQ(d.SelectedIndex(), 0u);

    d.NavigateSelection(-1);
    EXPECT_EQ(d.SelectedIndex(), n - 1u);

    d.NavigateSelection(+1);
    EXPECT_EQ(d.SelectedIndex(), 0u);

    d.NavigateSelection(+2);
    EXPECT_EQ(d.SelectedIndex(), 2u % n);
}

TEST(VfxModulePickerDialog, SelectFirstAndLast) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    const size_t n = d.VisibleModules().size();
    ASSERT_GT(n, 1u);

    d.SelectLast();
    EXPECT_EQ(d.SelectedIndex(), n - 1u);
    d.SelectFirst();
    EXPECT_EQ(d.SelectedIndex(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Confirm
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerDialog, Confirm_FiresCallback_ClosesDialog) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    std::string chosen;
    VfxStage    chosenStage = VfxStage::SystemSpawn;
    d.onModuleChosen = [&](const std::string& n, VfxStage s) {
        chosen = n; chosenStage = s;
    };

    const std::string expected = d.VisibleModules()[d.SelectedIndex()];
    EXPECT_TRUE(d.ConfirmSelection());
    EXPECT_FALSE(d.IsOpen());
    EXPECT_EQ(chosen, expected);
    EXPECT_EQ(chosenStage, VfxStage::ParticleUpdate);
}

TEST(VfxModulePickerDialog, Confirm_NoVisibleTiles_DoesNothing) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.SetFilter("zzznope");

    int calls = 0;
    d.onModuleChosen = [&](const std::string&, VfxStage) { ++calls; };
    EXPECT_FALSE(d.ConfirmSelection());
    EXPECT_TRUE(d.IsOpen());
    EXPECT_EQ(calls, 0);
}

TEST(VfxModulePickerDialog, Confirm_WhenClosed_Noop) {
    VfxModulePickerDialog d;
    int calls = 0;
    d.onModuleChosen = [&](const std::string&, VfxStage) { ++calls; };
    EXPECT_FALSE(d.ConfirmSelection());
    EXPECT_EQ(calls, 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout + hit-test
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerDialog, Layout_EmitsTabsSearchAndTiles) {
    // Phase 12.3.0: FilterBox is superseded by SearchInput; ComputeLayout
    // emits the new item kind plus PreviewArea + Create/Cancel buttons.
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 100.0f);

    const auto layout = d.ComputeLayout();
    int tabs = 0, searchInputs = 0, tiles = 0, closeBtns = 0;
    int legacyFilters = 0;
    for (const auto& it : layout) {
        switch (it.type) {
        case VfxPickerItemType::CategoryTab:  ++tabs; break;
        case VfxPickerItemType::SearchInput:  ++searchInputs; break;
        case VfxPickerItemType::FilterBox:    ++legacyFilters; break;
        case VfxPickerItemType::ModuleTile:   ++tiles; break;
        case VfxPickerItemType::CloseButton:  ++closeBtns; break;
        default: break;  // PreviewArea, ButtonCreate, ButtonCancel are extras.
        }
    }
    EXPECT_EQ(closeBtns, 1);
    EXPECT_EQ(searchInputs, 1);
    EXPECT_EQ(legacyFilters, 0);  // 12.3.0 never emits FilterBox
    EXPECT_GT(tabs, 0);
    EXPECT_GT(tiles, 0);
}

TEST(VfxModulePickerDialog, HitTest_CloseButtonBeatsHeaderOverlap) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    const auto layout = d.ComputeLayout();
    const VfxPickerLayoutItem* close = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxPickerItemType::CloseButton) { close = &it; break; }
    }
    ASSERT_NE(close, nullptr);

    const float cx = close->rect.x + close->rect.width * 0.5f;
    const float cy = close->rect.y + close->rect.height * 0.5f;
    const auto hit = d.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxPickerItemType::CloseButton);
}

TEST(VfxModulePickerDialog, HitTest_TileClickReturnsIndexAndLabel) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.SetCategory("Forces");

    const auto layout = d.ComputeLayout();
    const VfxPickerLayoutItem* tile0 = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxPickerItemType::ModuleTile && it.index == 0u) {
            tile0 = &it; break;
        }
    }
    ASSERT_NE(tile0, nullptr);
    const float cx = tile0->rect.x + tile0->rect.width * 0.5f;
    const float cy = tile0->rect.y + tile0->rect.height * 0.5f;

    const auto hit = d.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxPickerItemType::ModuleTile);
    EXPECT_EQ(hit.index, 0u);
    EXPECT_EQ(hit.label, d.VisibleModules().front());
}

TEST(VfxModulePickerDialog, HitTest_CategoryTabReturnsLabel) {
    // Phase 12.3.0: labels embed the category name + count badge
    // ("Forces \xB7 4"). This test substring-matches the base category.
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    const auto layout = d.ComputeLayout();

    // Find any tab whose label starts with "Forces".
    const VfxPickerLayoutItem* tab = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxPickerItemType::CategoryTab &&
            it.label.rfind("Forces", 0) == 0u) {
            tab = &it; break;
        }
    }
    ASSERT_NE(tab, nullptr);
    const float cx = tab->rect.x + tab->rect.width * 0.5f;
    const float cy = tab->rect.y + tab->rect.height * 0.5f;
    const auto hit = d.HitTest(layout, cx, cy);
    EXPECT_EQ(hit.type, VfxPickerItemType::CategoryTab);
    EXPECT_EQ(hit.label.rfind("Forces", 0), 0u);
}

TEST(VfxModulePickerDialog, HitTest_OutsideDialogReturnsNone) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 100.0f);
    const auto layout = d.ComputeLayout();
    const auto hit = d.HitTest(layout, 5.0f, 5.0f); // far outside
    EXPECT_FALSE(hit.IsHit());
}
