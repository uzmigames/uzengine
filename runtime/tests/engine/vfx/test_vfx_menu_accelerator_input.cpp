/// Unit tests for phase 14.12.0: menu accelerator input routing.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_menu.h"

#include <cstring>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// VfxMatchAccelerator
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxMenuAcceleratorInput, MatchesFile) {
    EXPECT_EQ(VfxMatchAccelerator('F'), 0);
    EXPECT_EQ(VfxMatchAccelerator('f'), 0);
}

TEST(VfxMenuAcceleratorInput, MatchesEdit) {
    EXPECT_EQ(VfxMatchAccelerator('E'), 1);
    EXPECT_EQ(VfxMatchAccelerator('e'), 1);
}

TEST(VfxMenuAcceleratorInput, MatchesView) {
    EXPECT_EQ(VfxMatchAccelerator('V'), 2);
}

TEST(VfxMenuAcceleratorInput, MatchesPresets) {
    EXPECT_EQ(VfxMatchAccelerator('P'), 3);
}

TEST(VfxMenuAcceleratorInput, MatchesHelp) {
    EXPECT_EQ(VfxMatchAccelerator('H'), 4);
}

TEST(VfxMenuAcceleratorInput, UnknownLetterReturnsMinusOne) {
    EXPECT_EQ(VfxMatchAccelerator('Z'), -1);
    EXPECT_EQ(VfxMatchAccelerator('Q'), -1);
    EXPECT_EQ(VfxMatchAccelerator('1'), -1);
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxMatchAcceleratorInMenu
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxMenuAcceleratorInput, FileMenuMatchesSaveAs) {
    const VfxTopMenu* file = VfxMenuBarTopAt(0);
    ASSERT_NE(file, nullptr);
    const int idx = VfxMatchAcceleratorInMenu('A', file->items,
                                                file->itemCount);
    ASSERT_GE(idx, 0);
    EXPECT_TRUE(std::strcmp(file->items[idx].label, "Save As") == 0);
}

TEST(VfxMenuAcceleratorInput, FileMenuMatchesExit) {
    const VfxTopMenu* file = VfxMenuBarTopAt(0);
    const int idx = VfxMatchAcceleratorInMenu('x', file->items,
                                                file->itemCount);
    ASSERT_GE(idx, 0);
    EXPECT_TRUE(std::strcmp(file->items[idx].label, "Exit") == 0);
}

TEST(VfxMenuAcceleratorInput, EditMenuMatchesCutOnT) {
    const VfxTopMenu* edit = VfxMenuBarTopAt(1);
    const int idx = VfxMatchAcceleratorInMenu('t', edit->items,
                                                edit->itemCount);
    ASSERT_GE(idx, 0);
    EXPECT_TRUE(std::strcmp(edit->items[idx].label, "Cut") == 0);
}

TEST(VfxMenuAcceleratorInput, NullPtrItemsReturnsMinusOne) {
    EXPECT_EQ(VfxMatchAcceleratorInMenu('A', nullptr, 0), -1);
    EXPECT_EQ(VfxMatchAcceleratorInMenu('A', nullptr, 10), -1);
}

TEST(VfxMenuAcceleratorInput, SeparatorsNeverMatch) {
    VfxMenuItem items[2];
    items[0].isSeparator = true;
    items[0].label = nullptr;
    items[0].acceleratorIndex = -1;
    items[1].isSeparator = false;
    items[1].enabled = false;
    items[1].label = "Disabled";
    items[1].acceleratorIndex = 0;
    EXPECT_EQ(VfxMatchAcceleratorInMenu('D', items, 2), -1);
}

TEST(VfxMenuAcceleratorInput, UnknownItemLetterReturnsMinusOne) {
    const VfxTopMenu* file = VfxMenuBarTopAt(0);
    EXPECT_EQ(VfxMatchAcceleratorInMenu('Z', file->items,
                                         file->itemCount), -1);
}
