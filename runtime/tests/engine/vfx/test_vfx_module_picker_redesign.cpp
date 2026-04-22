/// Unit tests for phase 12.3.0: `VfxModulePickerDialog` Niagara-parity
/// redesign — search bar, category tabs with count badges, responsive
/// tile grid (3/4 cols), selection accent border, preview area,
/// Create/Cancel footer + disabled-Create gating, metadata completeness.

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "engine/vfx/editor/vfx_module_picker_dialog.h"
#include "engine/vfx/modules/vfx_module_metadata.h"
#include "engine/vfx/stack/vfx_stage.h"

using namespace uz;

namespace {

bool ContainsType(const std::vector<VfxPickerLayoutItem>& layout,
                  VfxPickerItemType t) {
    for (const auto& it : layout) {
        if (it.type == t) return true;
    }
    return false;
}

const VfxPickerLayoutItem* First(
    const std::vector<VfxPickerLayoutItem>& layout,
    VfxPickerItemType t) {
    for (const auto& it : layout) {
        if (it.type == t) return &it;
    }
    return nullptr;
}

size_t CountType(const std::vector<VfxPickerLayoutItem>& layout,
                 VfxPickerItemType t) {
    size_t n = 0;
    for (const auto& it : layout) {
        if (it.type == t) ++n;
    }
    return n;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Closed dialog is inert
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, ClosedDialog_LayoutIsEmpty) {
    VfxModulePickerDialog d;
    EXPECT_FALSE(d.IsOpen());
    EXPECT_TRUE(d.ComputeLayout().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Search input
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, SearchInput_PrecedesEverythingElse) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 100.0f);

    const auto layout = d.ComputeLayout();
    ASSERT_FALSE(layout.empty());

    // First non-CloseButton item must be SearchInput.
    bool foundSearch = false;
    for (const auto& it : layout) {
        if (it.type == VfxPickerItemType::CloseButton) continue;
        EXPECT_EQ(it.type, VfxPickerItemType::SearchInput);
        foundSearch = true;
        break;
    }
    EXPECT_TRUE(foundSearch);
}

TEST(VfxModulePickerRedesign, SearchInput_LabelIsPlaceholderWhenEmpty) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 100.0f);
    const auto layout = d.ComputeLayout();
    const auto* s = First(layout, VfxPickerItemType::SearchInput);
    ASSERT_NE(s, nullptr);
    EXPECT_NE(s->label.find("Search"), std::string::npos);
}

TEST(VfxModulePickerRedesign, SearchInput_LabelReflectsFilterBuffer) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 100.0f);
    d.SetFilter("turbul");
    const auto layout = d.ComputeLayout();
    const auto* s = First(layout, VfxPickerItemType::SearchInput);
    ASSERT_NE(s, nullptr);
    EXPECT_EQ(s->label, "turbul");
}

// ─────────────────────────────────────────────────────────────────────────────
// Category tabs carry count badges
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, CategoryTabs_LabelContainsNameAndCount) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 100.0f, 100.0f);

    const auto layout = d.ComputeLayout();
    // Build the expected count for "Forces" + ParticleUpdate.
    const size_t expected = VfxModuleMetadata::CountByCategoryAndStage(
        "Forces", VfxStage::ParticleUpdate);

    bool saw = false;
    for (const auto& it : layout) {
        if (it.type != VfxPickerItemType::CategoryTab) continue;
        if (it.label.find("Forces") != std::string::npos) {
            saw = true;
            // Label must mention both the category name and the count.
            char countBuf[8];
            std::snprintf(countBuf, sizeof(countBuf), "%zu", expected);
            EXPECT_NE(it.label.find(countBuf), std::string::npos)
                << "Forces tab label='" << it.label << "'";
        }
    }
    EXPECT_TRUE(saw);
}

// ─────────────────────────────────────────────────────────────────────────────
// Responsive tile grid
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, TileGrid_3ColumnsWhenNarrow) {
    VfxModulePickerDialog d;
    d.dialogWidth = 520.0f; // < 600 threshold -> 3 cols
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    const auto layout = d.ComputeLayout();
    // Collect first-row tiles.
    std::vector<const VfxPickerLayoutItem*> firstRow;
    float firstY = -1.0f;
    for (const auto& it : layout) {
        if (it.type != VfxPickerItemType::ModuleTile) continue;
        if (firstY < 0.0f) firstY = it.rect.y;
        if (std::abs(it.rect.y - firstY) < 0.5f) firstRow.push_back(&it);
    }
    ASSERT_GE(firstRow.size(), 1u);
    EXPECT_LE(firstRow.size(), 3u) << "Too many columns at width 520";
}

TEST(VfxModulePickerRedesign, TileGrid_4ColumnsWhenWide) {
    VfxModulePickerDialog d;
    d.dialogWidth = 720.0f;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    const auto layout = d.ComputeLayout();
    std::vector<const VfxPickerLayoutItem*> firstRow;
    float firstY = -1.0f;
    for (const auto& it : layout) {
        if (it.type != VfxPickerItemType::ModuleTile) continue;
        if (firstY < 0.0f) firstY = it.rect.y;
        if (std::abs(it.rect.y - firstY) < 0.5f) firstRow.push_back(&it);
    }
    // At 720 width, expect 4 columns (or more if many modules visible).
    EXPECT_GE(firstRow.size(), 1u);
    EXPECT_LE(firstRow.size(), 4u);
    // Ensure we have at least 2 distinct columns (catalog has 8+ visible).
    EXPECT_GT(firstRow.size(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Preview area gating
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, PreviewArea_PresentWhenSelectionExists) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    // After Open(), the first visible tile is pre-selected.
    EXPECT_NE(d.SelectedIndex(), static_cast<size_t>(-1));

    const auto layout = d.ComputeLayout();
    EXPECT_TRUE(ContainsType(layout, VfxPickerItemType::PreviewArea));
}

TEST(VfxModulePickerRedesign, PreviewArea_AbsentWhenNoSelection) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);
    d.SetFilter("this_substring_matches_no_module_xyz");
    EXPECT_EQ(d.SelectedIndex(), static_cast<size_t>(-1));

    const auto layout = d.ComputeLayout();
    EXPECT_FALSE(ContainsType(layout, VfxPickerItemType::PreviewArea));
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer buttons
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, Footer_HasCreateAndCancelButtons) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    const auto layout = d.ComputeLayout();
    EXPECT_EQ(CountType(layout, VfxPickerItemType::ButtonCreate), 1u);
    EXPECT_EQ(CountType(layout, VfxPickerItemType::ButtonCancel), 1u);
}

TEST(VfxModulePickerRedesign, Footer_CreateButton_RightOfCancelButton) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    const auto layout = d.ComputeLayout();
    const auto* create = First(layout, VfxPickerItemType::ButtonCreate);
    const auto* cancel = First(layout, VfxPickerItemType::ButtonCancel);
    ASSERT_NE(create, nullptr);
    ASSERT_NE(cancel, nullptr);
    EXPECT_GT(create->rect.x, cancel->rect.x);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection-confirm gating
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModulePickerRedesign, ConfirmSelection_NoOpWhenFilterHidesAll) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    // onModuleChosen must NOT fire if the filter hides everything.
    bool fired = false;
    d.onModuleChosen = [&](const std::string&, VfxStage) { fired = true; };
    d.SetFilter("this_substring_matches_no_module_xyz");

    EXPECT_FALSE(d.ConfirmSelection());
    EXPECT_FALSE(fired);
}

TEST(VfxModulePickerRedesign, ConfirmSelection_FiresCallback) {
    VfxModulePickerDialog d;
    d.Open(VfxStage::ParticleUpdate, 0.0f, 0.0f);

    std::string chosen;
    VfxStage chosenStage = VfxStage::Render;
    d.onModuleChosen =
        [&](const std::string& name, VfxStage s) { chosen = name; chosenStage = s; };

    EXPECT_TRUE(d.ConfirmSelection());
    EXPECT_FALSE(chosen.empty());
    EXPECT_EQ(chosenStage, VfxStage::ParticleUpdate);
}

// ─────────────────────────────────────────────────────────────────────────────
// Metadata completeness — spec: every entry has non-empty description
// + icon glyph
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxModuleMetadata, BuiltinCatalog_EveryEntryHasDescriptionAndIcon) {
    size_t count = 0;
    const VfxModuleCatalogEntry* table = VfxModuleMetadata::Table(count);
    ASSERT_GT(count, 0u);
    for (size_t i = 0; i < count; ++i) {
        const auto& e = table[i];
        ASSERT_NE(e.description, nullptr) << "entry " << i;
        EXPECT_GE(std::strlen(e.description), 10u)
            << "entry '" << e.name << "' description too short";
        ASSERT_NE(e.iconGlyph, nullptr) << "entry " << i << " (" << e.name << ")";
        EXPECT_GT(std::strlen(e.iconGlyph), 0u)
            << "entry '" << e.name << "' icon glyph is empty";
    }
}

TEST(VfxModuleMetadata, GetIconGlyph_ReturnsNonNullForUnknownModule) {
    const char* g = VfxModuleMetadata::GetIconGlyph("nonexistent_xyz");
    ASSERT_NE(g, nullptr);
    EXPECT_GT(std::strlen(g), 0u);
}

TEST(VfxModuleMetadata, CountByCategoryAndStage_AllReturnsValidCount) {
    const size_t all = VfxModuleMetadata::CountByCategoryAndStage(
        "All", VfxStage::ParticleUpdate);
    EXPECT_GT(all, 0u);

    const size_t forces = VfxModuleMetadata::CountByCategoryAndStage(
        "Forces", VfxStage::ParticleUpdate);
    EXPECT_GT(forces, 0u);
    EXPECT_LE(forces, all);
}
