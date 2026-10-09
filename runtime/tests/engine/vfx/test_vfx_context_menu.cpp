/// Unit tests for phase 12.8.0: `VfxContextMenuWidget` lifecycle,
/// anchor flip, hover-to-submenu, Escape dismissal, click-outside,
/// disabled-row swallow.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_context_menu_widget.h"

using namespace uz;

namespace {

VfxContextMenuItem NormalItem(const char* label, std::function<void()> cb = {}) {
    VfxContextMenuItem it;
    it.label      = label;
    it.onActivate = std::move(cb);
    return it;
}

VfxContextMenuItem Separator() {
    VfxContextMenuItem it;
    it.isSeparator = true;
    return it;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Open / Close / Replace
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxContextMenu, DefaultClosed) {
    VfxContextMenuWidget w;
    EXPECT_FALSE(w.IsOpen());
    EXPECT_TRUE(w.ComputeLayout().empty());
}

TEST(VfxContextMenu, Open_SetsIsOpenAndItems) {
    VfxContextMenuWidget w;
    w.Open(10.0f, 20.0f,
            {NormalItem("A"), NormalItem("B"), NormalItem("C")});
    EXPECT_TRUE(w.IsOpen());
    EXPECT_EQ(w.Items().size(), 3u);
}

TEST(VfxContextMenu, Close_ClearsItems) {
    VfxContextMenuWidget w;
    w.Open(0.0f, 0.0f, {NormalItem("A")});
    w.Close();
    EXPECT_FALSE(w.IsOpen());
    EXPECT_TRUE(w.Items().empty());
    EXPECT_TRUE(w.ComputeLayout().empty());
}

TEST(VfxContextMenu, ReOpen_ReplacesItems) {
    VfxContextMenuWidget w;
    w.Open(0.0f, 0.0f, {NormalItem("A"), NormalItem("B"), NormalItem("C")});
    EXPECT_EQ(w.Items().size(), 3u);
    w.Open(0.0f, 0.0f, {NormalItem("X"), NormalItem("Y")});
    EXPECT_TRUE(w.IsOpen());
    EXPECT_EQ(w.Items().size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Anchor flip
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxContextMenu, Open_RightEdgeOverflow_FlipsLeft) {
    VfxContextMenuWidget w;
    w.panelWidth = 200.0f;
    // Anchor near the right edge so the menu would spill off-screen.
    w.Open(1260.0f, 10.0f, {NormalItem("A")}, 1280.0f, 720.0f);
    const auto bounds = w.Bounds();
    EXPECT_LT(bounds.x, 1260.0f)
        << "Expected menu to flip left of anchor when it would overflow";
    EXPECT_LE(bounds.x + bounds.width, 1280.0f + 0.01f);
}

TEST(VfxContextMenu, Open_BottomEdgeOverflow_FlipsUp) {
    VfxContextMenuWidget w;
    // 3 rows of rowHeight (22) + padding (4*2) = 74 px tall panel.
    w.Open(10.0f, 700.0f,
            {NormalItem("A"), NormalItem("B"), NormalItem("C")},
            1280.0f, 720.0f);
    const auto bounds = w.Bounds();
    EXPECT_LT(bounds.y, 700.0f)
        << "Expected menu to flip up when it would overflow the bottom";
}

TEST(VfxContextMenu, Open_FitsInViewport_StaysAtAnchor) {
    VfxContextMenuWidget w;
    w.Open(100.0f, 100.0f, {NormalItem("A")}, 1280.0f, 720.0f);
    const auto bounds = w.Bounds();
    EXPECT_FLOAT_EQ(bounds.x, 100.0f);
    EXPECT_FLOAT_EQ(bounds.y, 100.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Escape dismissal
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxContextMenu, ProcessKey_EscapeClosesMenu) {
    VfxContextMenuWidget w;
    w.Open(0.0f, 0.0f, {NormalItem("A")});
    EXPECT_TRUE(w.ProcessKey(VfxContextMenuWidget::Key::Escape));
    EXPECT_FALSE(w.IsOpen());
}

TEST(VfxContextMenu, ProcessKey_OtherKeysNotConsumed) {
    VfxContextMenuWidget w;
    w.Open(0.0f, 0.0f, {NormalItem("A")});
    EXPECT_FALSE(w.ProcessKey(VfxContextMenuWidget::Key::Z));
    EXPECT_TRUE(w.IsOpen());
}

// ─────────────────────────────────────────────────────────────────────────────
// Click activation + outside dismissal
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxContextMenu, ClickOnRow_FiresCallbackAndCloses) {
    VfxContextMenuWidget w;
    int calls = 0;
    w.Open(50.0f, 50.0f, {NormalItem("A", [&] { ++calls; })});
    // Find the row's rect.
    const auto layout = w.ComputeLayout();
    const VfxContextMenuLayoutItem* row = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxContextMenuItemType::Row) { row = &it; break; }
    }
    ASSERT_NE(row, nullptr);
    const float cx = row->rect.x + row->rect.width * 0.5f;
    const float cy = row->rect.y + row->rect.height * 0.5f;
    EXPECT_TRUE(w.ClickAt(cx, cy));
    EXPECT_EQ(calls, 1);
    EXPECT_FALSE(w.IsOpen());
}

TEST(VfxContextMenu, ClickOutside_DismissesMenu) {
    VfxContextMenuWidget w;
    w.Open(100.0f, 100.0f, {NormalItem("A")});
    EXPECT_TRUE(w.ClickAt(0.0f, 0.0f));
    EXPECT_FALSE(w.IsOpen());
}

TEST(VfxContextMenu, ClickOnDisabledRow_NoFireStaysOpen) {
    VfxContextMenuWidget w;
    int calls = 0;
    VfxContextMenuItem disabled = NormalItem("D", [&] { ++calls; });
    disabled.enabled = false;
    w.Open(50.0f, 50.0f, {disabled});
    const auto layout = w.ComputeLayout();
    const VfxContextMenuLayoutItem* row = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxContextMenuItemType::Row) { row = &it; break; }
    }
    ASSERT_NE(row, nullptr);
    const float cx = row->rect.x + row->rect.width * 0.5f;
    const float cy = row->rect.y + row->rect.height * 0.5f;
    EXPECT_TRUE(w.ClickAt(cx, cy));
    EXPECT_EQ(calls, 0);
    EXPECT_TRUE(w.IsOpen());
}

// ─────────────────────────────────────────────────────────────────────────────
// Separator handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxContextMenu, Separator_RendersAsDistinctLayoutItem) {
    VfxContextMenuWidget w;
    w.Open(0.0f, 0.0f,
            {NormalItem("A"), Separator(), NormalItem("B")});
    const auto layout = w.ComputeLayout();
    int rows = 0, seps = 0;
    for (const auto& it : layout) {
        if (it.type == VfxContextMenuItemType::Row) ++rows;
        if (it.type == VfxContextMenuItemType::Separator) ++seps;
    }
    EXPECT_EQ(rows, 2);
    EXPECT_EQ(seps, 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Submenu hover timing
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxContextMenu, Submenu_OpensAfterHoverDelay) {
    VfxContextMenuWidget w;
    VfxContextMenuItem parent = NormalItem("Tangent");
    parent.submenu.push_back(NormalItem("Auto"));
    parent.submenu.push_back(NormalItem("Linear"));
    w.Open(50.0f, 50.0f, {parent});

    const auto layout = w.ComputeLayout();
    const VfxContextMenuLayoutItem* row = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxContextMenuItemType::Row) { row = &it; break; }
    }
    ASSERT_NE(row, nullptr);

    // Hover the parent row.
    w.SetHover(row->rect.x + 10.0f, row->rect.y + 10.0f);
    EXPECT_EQ(w.HoveredIndex(), 0u);
    EXPECT_FALSE(w.IsSubmenuActive());

    // Tick for 100 ms — still below the 200 ms threshold.
    w.Tick(0.1f);
    EXPECT_FALSE(w.IsSubmenuActive());

    // Another 150 ms crosses the threshold.
    w.Tick(0.15f);
    EXPECT_TRUE(w.IsSubmenuActive());
}

TEST(VfxContextMenu, Submenu_HoverChangeResetsTimer) {
    VfxContextMenuWidget w;
    VfxContextMenuItem parent = NormalItem("P");
    parent.submenu.push_back(NormalItem("S"));
    w.Open(50.0f, 50.0f, {parent, NormalItem("Other")});

    const auto layout = w.ComputeLayout();
    const VfxContextMenuLayoutItem* p = nullptr;
    const VfxContextMenuLayoutItem* o = nullptr;
    for (const auto& it : layout) {
        if (it.type == VfxContextMenuItemType::Row) {
            if (it.itemIndex == 0u && p == nullptr) p = &it;
            else if (it.itemIndex == 1u && o == nullptr) o = &it;
        }
    }
    ASSERT_NE(p, nullptr);
    ASSERT_NE(o, nullptr);

    w.SetHover(p->rect.x + 10.0f, p->rect.y + 10.0f);
    w.Tick(0.15f);
    EXPECT_FALSE(w.IsSubmenuActive());
    // Move to the other row — timer must reset.
    w.SetHover(o->rect.x + 10.0f, o->rect.y + 10.0f);
    w.Tick(0.15f);
    EXPECT_FALSE(w.IsSubmenuActive());
}
