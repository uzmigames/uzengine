/// Unit tests for phase 14.15.0: emitter tree integration invariants.
///
/// The overlay wires `VfxTreeWidget` to the right panel and rebuilds
/// a root "Emitters" node + one child per emitter each frame. These
/// tests lock in the layout + click contract the overlay relies on.

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_tree_widget.h"

using namespace uz;

namespace {

void BuildOverviewTree(VfxTreeWidget& tree,
                       const std::vector<std::string>& names,
                       bool rootExpanded) {
    tree.Clear();
    VfxTreeNode root{};
    root.id = 0u;
    root.label = "Emitters";
    root.depth = 0u;
    root.hasChildren = true;
    root.expanded = rootExpanded;
    tree.AddNode(root);
    for (size_t i = 0; i < names.size(); ++i) {
        VfxTreeNode n{};
        n.id = static_cast<uint32_t>(i + 1);
        n.label = names[i];
        n.depth = 1u;
        n.icon = VfxUiIcon::Dot;
        n.expanded = true;
        n.hasChildren = false;
        tree.AddNode(n);
    }
}

} // namespace

TEST(TreeOverview, ExpandedRootShowsAllChildren) {
    VfxTreeWidget tree;
    BuildOverviewTree(tree, {"A", "B", "C"}, true);
    const auto layout = tree.ComputeLayout({0, 0, 200, 400});
    size_t rows = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::Row) ++rows;
    }
    EXPECT_EQ(rows, 4u);  // root + 3 children
}

TEST(TreeOverview, CollapsedRootHidesChildren) {
    VfxTreeWidget tree;
    BuildOverviewTree(tree, {"A", "B", "C"}, false);
    const auto layout = tree.ComputeLayout({0, 0, 200, 400});
    size_t rows = 0;
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::Row) ++rows;
    }
    EXPECT_EQ(rows, 1u);  // root only
}

TEST(TreeOverview, ChildNodeIdMapsToEmitterIndex) {
    VfxTreeWidget tree;
    BuildOverviewTree(tree, {"Fireball", "Smoke"}, true);
    // Second emitter should have id == 2 (index 1 + 1).
    const auto& nodes = tree.Nodes();
    ASSERT_EQ(nodes.size(), 3u);
    EXPECT_EQ(nodes[1].id, 1u);
    EXPECT_EQ(nodes[2].id, 2u);
    EXPECT_EQ(nodes[2].label, "Smoke");
}

TEST(TreeOverview, EmptyEmitterListShowsOnlyRoot) {
    VfxTreeWidget tree;
    BuildOverviewTree(tree, {}, true);
    EXPECT_EQ(tree.Count(), 1u);
}

TEST(TreeOverview, SelectionOfActiveEmitterUsesIdPlusOne) {
    VfxTreeWidget tree;
    BuildOverviewTree(tree, {"A", "B", "C"}, true);
    // Active emitter index 2 → selection id 3.
    tree.SetSelectedId(3u);
    EXPECT_EQ(tree.SelectedId(), 3u);
}

TEST(TreeOverview, ArrowHitOnRootDistinctFromChildHit) {
    VfxTreeWidget tree;
    BuildOverviewTree(tree, {"A"}, true);
    const auto layout = tree.ComputeLayout({0, 0, 200, 100});
    // Find root arrow.
    VfxStackRect rootArrow{};
    VfxStackRect childRow{};
    for (const auto& it : layout) {
        if (it.type == VfxTreeItemType::ExpandArrow && it.nodeId == 0u) {
            rootArrow = it.rect;
        }
        if (it.type == VfxTreeItemType::Row && it.nodeId == 1u) {
            childRow = it.rect;
        }
    }
    ASSERT_GT(rootArrow.width, 0.0f);
    ASSERT_GT(childRow.width,  0.0f);
    // Arrow hit type.
    const auto arrowHit = tree.HitTest(layout,
        rootArrow.x + rootArrow.width * 0.5f,
        rootArrow.y + rootArrow.height * 0.5f);
    EXPECT_EQ(arrowHit.type, VfxTreeItemType::ExpandArrow);
    EXPECT_EQ(arrowHit.nodeId, 0u);

    // Child row hit returns a Row with id 1.
    const auto childHit = tree.HitTest(layout,
        childRow.x + 190.0f,  // far right — well clear of any arrow region.
        childRow.y + childRow.height * 0.5f);
    EXPECT_EQ(childHit.type, VfxTreeItemType::Row);
    EXPECT_EQ(childHit.nodeId, 1u);
}
