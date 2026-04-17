/// Unit tests for phase 10.4.0 — VFX node-graph editor widget.
///
/// @ref engine/vfx/editor/vfx_node_graph_widget.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_node_graph_widget.h"

using namespace uz;

namespace {

VfxNodeGraph MakeTestGraph() {
    VfxNodeGraph g;
    g.nodes.push_back({"n0", "Spawn",      {0, 0}});
    g.nodes.push_back({"n1", "ColorRamp",  {300, 0}});
    g.nodes.push_back({"n2", "Billboard",  {600, 0}});
    g.wires.push_back({"n0:out", "n1:in", VfxPinType::Float});
    g.wires.push_back({"n1:color", "n2:color", VfxPinType::Vec4});
    return g;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Setup + binding
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphWidget, StartsUnbound) {
    VfxNodeGraphWidget w;
    EXPECT_EQ(w.GetGraph(), nullptr);
    EXPECT_EQ(w.SelectionCount(), 0u);
}

TEST(VfxNodeGraphWidget, BindGraphResetsState) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n0");
    EXPECT_EQ(w.SelectionCount(), 1u);
    w.SetGraph(&g); // rebind resets selection
    EXPECT_EQ(w.SelectionCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphWidget, SelectClearsOthers) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n0");
    w.Select("n1");
    EXPECT_EQ(w.SelectionCount(), 1u);
    EXPECT_TRUE(w.IsSelected("n1"));
    EXPECT_FALSE(w.IsSelected("n0"));
}

TEST(VfxNodeGraphWidget, ToggleSelect) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.ToggleSelect("n0");
    w.ToggleSelect("n1");
    EXPECT_EQ(w.SelectionCount(), 2u);
    w.ToggleSelect("n0");
    EXPECT_EQ(w.SelectionCount(), 1u);
    EXPECT_TRUE(w.IsSelected("n1"));
}

TEST(VfxNodeGraphWidget, SelectAll) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.SelectAll();
    EXPECT_EQ(w.SelectionCount(), 3u);
}

TEST(VfxNodeGraphWidget, ClearSelection) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.SelectAll();
    w.ClearSelection();
    EXPECT_EQ(w.SelectionCount(), 0u);
}

TEST(VfxNodeGraphWidget, SelectionCallbackFires) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    int count = 0;
    w.onSelectionChanged = [&]() { ++count; };
    w.Select("n0");
    w.ToggleSelect("n1");
    w.ClearSelection();
    EXPECT_EQ(count, 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Node movement
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphWidget, MoveSelectionWritesBack) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n0");
    w.MoveSelection(50.0f, 25.0f);
    EXPECT_FLOAT_EQ(g.nodes[0].position.x, 50.0f);
    EXPECT_FLOAT_EQ(g.nodes[0].position.y, 25.0f);
    // Other nodes unaffected.
    EXPECT_FLOAT_EQ(g.nodes[1].position.x, 300.0f);
}

TEST(VfxNodeGraphWidget, MoveSelectionMultipleNodes) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.SelectAll();
    w.MoveSelection(10.0f, 0.0f);
    EXPECT_FLOAT_EQ(g.nodes[0].position.x, 10.0f);
    EXPECT_FLOAT_EQ(g.nodes[1].position.x, 310.0f);
    EXPECT_FLOAT_EQ(g.nodes[2].position.x, 610.0f);
}

TEST(VfxNodeGraphWidget, NudgeByGridUnit) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.gridUnit = 16.0f;
    w.Select("n0");
    w.NudgeSelection(1.0f, 0.0f);
    EXPECT_FLOAT_EQ(g.nodes[0].position.x, 16.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Deletion
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphWidget, DeleteSelectionRemovesNodesAndWires) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n1");
    const uint32_t removed = w.DeleteSelection();
    EXPECT_EQ(removed, 1u);
    EXPECT_EQ(g.nodes.size(), 2u); // n0 + n2 remain
    // Both wires touched n1 → both removed.
    EXPECT_EQ(g.wires.size(), 0u);
    EXPECT_EQ(w.SelectionCount(), 0u);
}

TEST(VfxNodeGraphWidget, DeleteMiddleNodeKeepsUnrelatedWires) {
    VfxNodeGraph g;
    g.nodes.push_back({"a", "A", {0, 0}});
    g.nodes.push_back({"b", "B", {100, 0}});
    g.nodes.push_back({"c", "C", {200, 0}});
    g.wires.push_back({"a:out", "c:in", VfxPinType::Float}); // skips b

    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("b");
    w.DeleteSelection();
    EXPECT_EQ(g.nodes.size(), 2u);
    EXPECT_EQ(g.wires.size(), 1u); // a→c wire survives
}

TEST(VfxNodeGraphWidget, DeleteEmptySelectionIsNoOp) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    const uint32_t removed = w.DeleteSelection();
    EXPECT_EQ(removed, 0u);
    EXPECT_EQ(g.nodes.size(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Wire drag
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphWidget, WireDragCreatesConnection) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    const size_t before = g.wires.size();
    w.BeginWireDrag("n0:out");
    EXPECT_TRUE(w.IsWireDragActive());
    const bool ok = w.EndWireDrag("n2:in2", VfxPinType::Float);
    EXPECT_TRUE(ok);
    EXPECT_FALSE(w.IsWireDragActive());
    EXPECT_EQ(g.wires.size(), before + 1);
}

TEST(VfxNodeGraphWidget, WireDragRejectsSameNode) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    const size_t before = g.wires.size();
    w.BeginWireDrag("n0:out");
    const bool ok = w.EndWireDrag("n0:in", VfxPinType::Float);
    EXPECT_FALSE(ok);
    EXPECT_EQ(g.wires.size(), before); // no wire added
}

TEST(VfxNodeGraphWidget, WireDragCancel) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.BeginWireDrag("n0:out");
    EXPECT_TRUE(w.IsWireDragActive());
    w.CancelWireDrag();
    EXPECT_FALSE(w.IsWireDragActive());
    EXPECT_TRUE(w.WireDragSource().empty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Rubber-band selection
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxNodeGraphWidget, RubberBandSelectsEnclosed) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.BeginRubberBand(-10.0f, -10.0f);
    w.UpdateRubberBand(350.0f, 50.0f);
    w.EndRubberBand();
    // n0 (0,0) and n1 (300,0) are inside; n2 (600,0) is not.
    EXPECT_EQ(w.SelectionCount(), 2u);
    EXPECT_TRUE(w.IsSelected("n0"));
    EXPECT_TRUE(w.IsSelected("n1"));
    EXPECT_FALSE(w.IsSelected("n2"));
}

TEST(VfxNodeGraphWidget, RubberBandRectNormalised) {
    VfxNodeGraphWidget w;
    w.BeginRubberBand(100.0f, 50.0f);
    w.UpdateRubberBand(10.0f, 150.0f); // drag leftward + downward
    float x, y, width, h;
    w.GetRubberBandRect(x, y, width, h);
    EXPECT_FLOAT_EQ(x, 10.0f);    // min
    EXPECT_FLOAT_EQ(y, 50.0f);     // min
    EXPECT_FLOAT_EQ(width, 90.0f); // abs delta
    EXPECT_FLOAT_EQ(h, 100.0f);    // abs delta
}
