/// Unit tests for phase 12.7.0: `vfx_editor_menu.h` top-menu data
/// tables + overlay-level tab scroll state.
///
/// The overlay's rendered bar requires GPU init, so these tests
/// target the pure data layer: menu/item counts, command enum IDs,
/// item ordering, hotkey hint presence, separator placement, disabled
/// state. Visual verification happens via the demo app in phase 12.10.

#include <gtest/gtest.h>

#include <cstring>
#include <set>
#include <string>

#include "engine/vfx/editor/vfx_editor_menu.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Top-menu structure
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxMenuBar, TopMenuCount_Is5) {
    EXPECT_EQ(VfxMenuBarTopCount(), 5u);
}

TEST(VfxMenuBar, TopMenuLabels_MatchNiagaraBar) {
    ASSERT_EQ(VfxMenuBarTopCount(), 5u);
    EXPECT_STREQ(VfxMenuBarTopAt(0)->label, "File");
    EXPECT_STREQ(VfxMenuBarTopAt(1)->label, "Edit");
    EXPECT_STREQ(VfxMenuBarTopAt(2)->label, "View");
    EXPECT_STREQ(VfxMenuBarTopAt(3)->label, "Presets");
    EXPECT_STREQ(VfxMenuBarTopAt(4)->label, "Help");
}

TEST(VfxMenuBar, TopAtOutOfRange_ReturnsNullptr) {
    EXPECT_EQ(VfxMenuBarTopAt(VfxMenuBarTopCount()), nullptr);
    EXPECT_EQ(VfxMenuBarTopAt(99), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-menu contents
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxMenuBar, FileMenu_HasCoreCommands) {
    const VfxTopMenu* m = VfxMenuBarTopAt(0);
    ASSERT_NE(m, nullptr);

    bool new_ = false, open = false, save = false, saveAs = false, exit = false;
    for (size_t i = 0; i < m->itemCount; ++i) {
        const auto& it = m->items[i];
        if (it.isSeparator) continue;
        switch (it.command) {
            case VfxMenuCommand::File_New:    new_ = true; break;
            case VfxMenuCommand::File_Open:   open = true; break;
            case VfxMenuCommand::File_Save:   save = true; break;
            case VfxMenuCommand::File_SaveAs: saveAs = true; break;
            case VfxMenuCommand::File_Exit:   exit = true; break;
            default: break;
        }
    }
    EXPECT_TRUE(new_);
    EXPECT_TRUE(open);
    EXPECT_TRUE(save);
    EXPECT_TRUE(saveAs);
    EXPECT_TRUE(exit);
}

TEST(VfxMenuBar, EditMenu_HasUndoRedoCutCopyPaste) {
    const VfxTopMenu* m = VfxMenuBarTopAt(1);
    ASSERT_NE(m, nullptr);

    std::set<VfxMenuCommand> seen;
    for (size_t i = 0; i < m->itemCount; ++i) {
        if (!m->items[i].isSeparator) seen.insert(m->items[i].command);
    }
    EXPECT_EQ(seen.count(VfxMenuCommand::Edit_Undo),  1u);
    EXPECT_EQ(seen.count(VfxMenuCommand::Edit_Redo),  1u);
    EXPECT_EQ(seen.count(VfxMenuCommand::Edit_Cut),   1u);
    EXPECT_EQ(seen.count(VfxMenuCommand::Edit_Copy),  1u);
    EXPECT_EQ(seen.count(VfxMenuCommand::Edit_Paste), 1u);
}

TEST(VfxMenuBar, ViewMenu_HasNiagaraModeCommand) {
    const VfxTopMenu* m = VfxMenuBarTopAt(2);
    ASSERT_NE(m, nullptr);
    bool found = false;
    for (size_t i = 0; i < m->itemCount; ++i) {
        if (m->items[i].command == VfxMenuCommand::View_NiagaraMode) {
            found = true;
            EXPECT_STREQ(m->items[i].label, "Niagara Mode");
            EXPECT_STREQ(m->items[i].hotkey, "F11");
        }
    }
    EXPECT_TRUE(found);
}

TEST(VfxMenuBar, HelpMenu_HasDocsAndAbout) {
    const VfxTopMenu* m = VfxMenuBarTopAt(4);
    ASSERT_NE(m, nullptr);
    std::set<VfxMenuCommand> seen;
    for (size_t i = 0; i < m->itemCount; ++i) seen.insert(m->items[i].command);
    EXPECT_EQ(seen.count(VfxMenuCommand::Help_Docs),  1u);
    EXPECT_EQ(seen.count(VfxMenuCommand::Help_About), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Hotkey hints + separators
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxMenuBar, SaveCommand_CarriesCtrlSHotkey) {
    const VfxTopMenu* m = VfxMenuBarTopAt(0);
    ASSERT_NE(m, nullptr);
    for (size_t i = 0; i < m->itemCount; ++i) {
        if (m->items[i].command == VfxMenuCommand::File_Save) {
            ASSERT_NE(m->items[i].hotkey, nullptr);
            EXPECT_STREQ(m->items[i].hotkey, "Ctrl+S");
            return;
        }
    }
    FAIL() << "File_Save entry not present";
}

TEST(VfxMenuBar, EditMenu_HasAtLeastOneSeparator) {
    const VfxTopMenu* m = VfxMenuBarTopAt(1);
    ASSERT_NE(m, nullptr);
    size_t seps = 0;
    for (size_t i = 0; i < m->itemCount; ++i) {
        if (m->items[i].isSeparator) ++seps;
    }
    EXPECT_GE(seps, 1u);
}

TEST(VfxMenuBar, Separator_CarriesNoneCommand) {
    size_t top = VfxMenuBarTopCount();
    for (size_t t = 0; t < top; ++t) {
        const VfxTopMenu* m = VfxMenuBarTopAt(t);
        for (size_t i = 0; i < m->itemCount; ++i) {
            if (m->items[i].isSeparator) {
                EXPECT_EQ(m->items[i].command, VfxMenuCommand::None)
                    << "Separator in " << m->label << " has non-None command";
            }
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Command enum — every non-separator item maps to a unique command
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxMenuBar, CommandEnum_EveryCommandIsUniqueAcrossAllMenus) {
    std::set<uint16_t> seen;
    const size_t top = VfxMenuBarTopCount();
    for (size_t t = 0; t < top; ++t) {
        const VfxTopMenu* m = VfxMenuBarTopAt(t);
        for (size_t i = 0; i < m->itemCount; ++i) {
            const auto& it = m->items[i];
            if (it.isSeparator) continue;
            // Presets placeholder carries None and has enabled=false.
            if (it.command == VfxMenuCommand::None) continue;
            const uint16_t key = static_cast<uint16_t>(it.command);
            ASSERT_EQ(seen.count(key), 0u)
                << "Duplicate command " << key << " in " << m->label;
            seen.insert(key);
        }
    }
    EXPECT_GE(seen.size(), 15u)
        << "Expected at least 15 distinct commands across all menus";
}
