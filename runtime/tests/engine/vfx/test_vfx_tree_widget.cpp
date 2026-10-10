/// Unit tests for phase 14.4.0: `VfxTreeWidget`.
///
/// Covers:
///   - `ComputeLayout` emits one `Row` per visible node.
///   - Depth-N nodes are horizontally indented by `N * indentStep`.
///   - `HitTest` prioritises `ExpandArrow` over `Row`.
///   - Arrow click inside the 16-px region returns `ExpandArrow`
///     (fires `onToggleExpand`), not `Row` (which would fire
///     `onSelect`).
///   - Collapsed parents hide child rows from layout emission.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_tree_widget.h"

using namespace uz;

namespace {

VfxTreeNode MakeNode(uint32_t id, const char* label, uint32_t depth,
                     bool hasChildren, bool expanded = true) {
    VfxTreeNode n;
    n.id          = id;
    n.label       = label;
    n.depth       = depth;
    n.hasChildren = hasChildren;
    n.expanded    = expanded;
    return n;
}

} // namespace

TEST(VfxTreeWidget, LayoutEmitsOneRowPerVisibleNode) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Root", 0, true));
    w.AddNode(MakeNode(2, "ChildA", 1, false));
    w.AddNode(MakeNode(3, "ChildB", 1, false));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 300.0f, 400.0f});
    size_t rows = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::Row) ++rows;
    }
    EXPECT_EQ(rows, 3u);
}

TEST(VfxTreeWidget, CollapsedParentHidesChildren) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Root", 0, true, /*expanded=*/false));
    w.AddNode(MakeNode(2, "ChildA", 1, false));
    w.AddNode(MakeNode(3, "ChildB", 1, false));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 300.0f, 400.0f});
    size_t rows = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::Row) ++rows;
    }
    EXPECT_EQ(rows, 1u);  // only the collapsed root is visible.
}

TEST(VfxTreeWidget, ExpandArrowEmittedOnlyForNodesWithChildren) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Parent", 0, true));
    w.AddNode(MakeNode(2, "Leaf",   1, false));
    w.AddNode(MakeNode(3, "Parent2", 0, true));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 300.0f, 400.0f});
    size_t arrows = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::ExpandArrow) ++arrows;
    }
    EXPECT_EQ(arrows, 2u);
}

TEST(VfxTreeWidget, Depth2NodeIndents32Px) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Root",    0, true));
    w.AddNode(MakeNode(2, "Child",   1, true));
    w.AddNode(MakeNode(3, "GrandChild", 2, true));

    const auto layout = w.ComputeLayout({100.0f, 0.0f, 400.0f, 400.0f});
    // Find the arrow for id==3 (depth 2).
    for (const auto& it : layout) {
        if (it.type != VfxTreeItemType::ExpandArrow) continue;
        if (it.nodeId != 3u) continue;
        // Arrow x = panelBounds.x + depth*indentStep = 100 + 2*16 = 132.
        EXPECT_NEAR(it.rect.x, 132.0f, 0.01f);
        return;
    }
    FAIL() << "expand arrow for depth-2 node not found";
}

TEST(VfxTreeWidget, HitTestArrowTakesPriorityOverRow) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Parent", 0, true));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 300.0f, 400.0f});
    // Find the arrow rect.
    VfxStackRect arrowRect{};
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::ExpandArrow) {
            arrowRect = it.rect;
            break;
        }
    }
    ASSERT_GT(arrowRect.width, 0.0f);

    const float cx = arrowRect.x + arrowRect.width * 0.5f;
    const float cy = arrowRect.y + arrowRect.height * 0.5f;
    const auto r = w.HitTest(layout, cx, cy);
    EXPECT_EQ(r.type, VfxTreeItemType::ExpandArrow);
    EXPECT_EQ(r.nodeId, 1u);
}

TEST(VfxTreeWidget, HitTestRowOutsideArrow) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Parent", 0, true));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 300.0f, 400.0f});
    // Hit the far right edge of the row (well clear of arrow).
    const auto r = w.HitTest(layout, 290.0f, 10.0f);
    EXPECT_EQ(r.type, VfxTreeItemType::Row);
    EXPECT_EQ(r.nodeId, 1u);
}

TEST(VfxTreeWidget, SelectionStateRoundtrips) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(42, "Test", 0, false));
    w.SetSelectedId(42u);
    EXPECT_EQ(w.SelectedId(), 42u);
    w.SetHoveredId(42u);
    EXPECT_EQ(w.HoveredId(), 42u);
    w.Clear();
    EXPECT_EQ(w.Count(), 0u);
}

TEST(VfxTreeWidget, IndentGuideEmittedPerAncestorDepth) {
    VfxTreeWidget w;
    w.AddNode(MakeNode(1, "Root",  0, true));
    w.AddNode(MakeNode(2, "Child", 1, true));
    w.AddNode(MakeNode(3, "Grand", 2, false));

    const auto layout = w.ComputeLayout({0.0f, 0.0f, 400.0f, 400.0f});
    size_t guidesForGrand = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::IndentGuide && it.nodeId == 3u) {
            ++guidesForGrand;
        }
    }
    // Grandchild has 2 ancestors above → 2 indent guides.
    EXPECT_EQ(guidesForGrand, 2u);
}
