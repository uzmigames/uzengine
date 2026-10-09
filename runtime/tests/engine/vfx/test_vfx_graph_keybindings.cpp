/// Unit tests for phase 9.3.0 — VFX graph keybindings.
///
/// @ref engine/vfx/editor/vfx_graph_keybindings.h

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_graph_keybindings.h"
#include "engine/vfx/editor/vfx_node_graph.h"

using namespace uz;

namespace {

VfxNodeGraph MakeTestGraph() {
    VfxNodeGraph g;
    g.nodes.push_back({"n0", "Spawn",     {0, 0}});
    g.nodes.push_back({"n1", "Gravity",   {100, 0}});
    g.nodes.push_back({"n2", "Billboard", {200, 0}});
    return g;
}

} // namespace

using K = VfxGraphKeybindingHandler::Key;

// ─────────────────────────────────────────────────────────────────────────────
// Unbound
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, UnboundIgnoresEvents) {
    VfxGraphKeybindingHandler h;
    EXPECT_FALSE(h.ProcessKeyEvent(K::C, true, false));
    EXPECT_EQ(h.CopyCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Copy / Paste
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, CtrlC_CopiesSelection) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n0");

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_TRUE(h.ProcessKeyEvent(K::C, true, false));
    EXPECT_EQ(h.CopyCount(), 1u);
}

TEST(VfxGraphKeybindings, CtrlC_NoSelectionNoOp) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_FALSE(h.ProcessKeyEvent(K::C, true, false));
    EXPECT_EQ(h.CopyCount(), 0u);
}

TEST(VfxGraphKeybindings, CtrlV_PastesFromClipboard) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n0");

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    // Copy first.
    h.ProcessKeyEvent(K::C, true, false);
    const size_t beforePaste = g.nodes.size();

    // Paste.
    EXPECT_TRUE(h.ProcessKeyEvent(K::V, true, false));
    EXPECT_EQ(h.PasteCount(), 1u);
    EXPECT_GT(g.nodes.size(), beforePaste); // new nodes added
}

TEST(VfxGraphKeybindings, CtrlV_EmptyClipboardNoOp) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_FALSE(h.ProcessKeyEvent(K::V, true, false));
    EXPECT_EQ(h.PasteCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Duplicate (Ctrl+D)
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, CtrlD_CopiesAndPastesInOneAction) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n1");

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    const size_t before = g.nodes.size();
    EXPECT_TRUE(h.ProcessKeyEvent(K::D, true, false));
    EXPECT_EQ(g.nodes.size(), before + 1); // one node duplicated
    EXPECT_EQ(h.CopyCount(), 1u);
    EXPECT_EQ(h.PasteCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Select all
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, CtrlA_SelectsAll) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_TRUE(h.ProcessKeyEvent(K::A, true, false));
    EXPECT_EQ(w.SelectionCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Delete
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, Delete_RemovesSelected) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n1");

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_TRUE(h.ProcessKeyEvent(K::Delete, false, false));
    EXPECT_EQ(g.nodes.size(), 2u);
    EXPECT_EQ(h.DeleteCount(), 1u);
}

TEST(VfxGraphKeybindings, Delete_EmptySelectionNoOp) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_FALSE(h.ProcessKeyEvent(K::Delete, false, false));
    EXPECT_EQ(h.DeleteCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Escape
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, Escape_ClearsSelection) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.SelectAll();

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_TRUE(h.ProcessKeyEvent(K::Escape, false, false));
    EXPECT_EQ(w.SelectionCount(), 0u);
}

TEST(VfxGraphKeybindings, Escape_CancelsWireDragFirst) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.SelectAll();
    w.BeginWireDrag("n0:out");

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_TRUE(h.ProcessKeyEvent(K::Escape, false, false));
    EXPECT_FALSE(w.IsWireDragActive());
    // First Escape cancels wire drag; selection remains.
    EXPECT_EQ(w.SelectionCount(), 3u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrow nudge
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, ArrowNudgesSelection) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.gridUnit = 16.0f;
    w.Select("n0");

    VfxGraphKeybindingHandler h;
    h.Bind(&w);

    EXPECT_TRUE(h.ProcessKeyEvent(K::Right, false, false));
    EXPECT_FLOAT_EQ(g.nodes[0].position.x, 16.0f);

    EXPECT_TRUE(h.ProcessKeyEvent(K::Down, false, false));
    EXPECT_FLOAT_EQ(g.nodes[0].position.y, 16.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Custom clipboard provider
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxGraphKeybindings, CustomProviderUsedForCopyPaste) {
    VfxNodeGraph g = MakeTestGraph();
    VfxNodeGraphWidget w;
    w.SetGraph(&g);
    w.Select("n0");

    VfxInProcessClipboard customClip;
    VfxGraphKeybindingHandler h;
    h.Bind(&w);
    h.SetClipboardProvider(&customClip);

    h.ProcessKeyEvent(K::C, true, false);
    EXPECT_FALSE(customClip.IsEmpty());
}

// ─────────────────────────────────────────────────────────────────────────────
// InProcessClipboard
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxInProcessClipboard, StartsEmpty) {
    VfxInProcessClipboard cb;
    EXPECT_TRUE(cb.IsEmpty());
    EXPECT_EQ(cb.Read(), "");
}

TEST(VfxInProcessClipboard, WriteRead) {
    VfxInProcessClipboard cb;
    cb.Write("hello");
    EXPECT_EQ(cb.Read(), "hello");
    EXPECT_FALSE(cb.IsEmpty());
}

TEST(VfxInProcessClipboard, Clear) {
    VfxInProcessClipboard cb;
    cb.Write("x");
    cb.Clear();
    EXPECT_TRUE(cb.IsEmpty());
}
