/// Unit tests for phase 14.8.0: menu accelerator underlines.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_editor_menu.h"

#include <cstring>

using namespace uz;

TEST(VfxMenuAccelerators, TopMenusAllHaveDefaultAcceleratorAtZero) {
    const size_t n = VfxMenuBarTopCount();
    for (size_t i = 0; i < n; ++i) {
        const VfxTopMenu* m = VfxMenuBarTopAt(i);
        ASSERT_NE(m, nullptr);
        EXPECT_GE(m->acceleratorIndex, 0);
        EXPECT_LT(m->acceleratorIndex,
                   static_cast<int>(std::strlen(m->label)));
    }
}

TEST(VfxMenuAccelerators, FileSaveAsHasExpectedAcceleratorIndex) {
    const VfxTopMenu* file = nullptr;
    for (size_t i = 0; i < VfxMenuBarTopCount(); ++i) {
        const VfxTopMenu* m = VfxMenuBarTopAt(i);
        if (std::strcmp(m->label, "File") == 0) { file = m; break; }
    }
    ASSERT_NE(file, nullptr);
    // "Save As" should have its accelerator on the 'A' (index 5).
    for (size_t i = 0; i < file->itemCount; ++i) {
        const auto& it = file->items[i];
        if (it.label == nullptr) continue;
        if (std::strcmp(it.label, "Save As") == 0) {
            EXPECT_EQ(it.acceleratorIndex, 5);
            EXPECT_EQ(it.label[it.acceleratorIndex], 'A');
            return;
        }
    }
    FAIL() << "Save As item not found";
}

TEST(VfxMenuAccelerators, FileExitHasAcceleratorOnX) {
    const VfxTopMenu* file = VfxMenuBarTopAt(0);
    ASSERT_NE(file, nullptr);
    for (size_t i = 0; i < file->itemCount; ++i) {
        const auto& it = file->items[i];
        if (it.label == nullptr) continue;
        if (std::strcmp(it.label, "Exit") == 0) {
            EXPECT_EQ(it.acceleratorIndex, 1);
            EXPECT_EQ(it.label[it.acceleratorIndex], 'x');
            return;
        }
    }
    FAIL() << "Exit item not found";
}

TEST(VfxMenuAccelerators, SeparatorItemsHaveNegativeAccelerator) {
    const VfxTopMenu* file = VfxMenuBarTopAt(0);
    ASSERT_NE(file, nullptr);
    bool foundSeparator = false;
    for (size_t i = 0; i < file->itemCount; ++i) {
        const auto& it = file->items[i];
        if (it.isSeparator) {
            EXPECT_LT(it.acceleratorIndex, 0);
            foundSeparator = true;
        }
    }
    EXPECT_TRUE(foundSeparator);
}

TEST(VfxMenuAccelerators, EditCutAcceleratorOnT) {
    const VfxTopMenu* edit = VfxMenuBarTopAt(1);
    ASSERT_NE(edit, nullptr);
    for (size_t i = 0; i < edit->itemCount; ++i) {
        const auto& it = edit->items[i];
        if (it.label == nullptr) continue;
        if (std::strcmp(it.label, "Cut") == 0) {
            EXPECT_EQ(it.acceleratorIndex, 2);
            EXPECT_EQ(it.label[it.acceleratorIndex], 't');
            return;
        }
    }
    FAIL() << "Cut item not found";
}
