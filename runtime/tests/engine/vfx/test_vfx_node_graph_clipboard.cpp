/// Unit tests for phase 8.6.0 — VFX node-graph clipboard Copy / Paste.
///
/// Validates the subgraph serialisation + paste-with-id-remap path:
///   - Copy selects only chosen nodes + internal wires (drops dangling).
///   - Paste round-trips a copy into the same or a different graph.
///   - Id collisions append `_copy`, `_copy2`, … suffixes.
///   - Wire endpoints rewritten to match the renamed nodes.
///   - Malformed JSON + wrong format marker leave the target untouched.
///   - PasteMany produces distinct ids across repeated paste operations.
///
/// @ref phase8_8.6.0-vfx-editor-clipboard-select

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_node_graph.h"

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Copy
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxClipboardCopy, EmptySelection_ProducesValidEmptySnippet) {
    VfxNodeGraph g;
    g.AddNode({"n0", "Spawn", {}});

    const std::string snippet = VfxNodeGraphClipboard::Copy(g, {});
    EXPECT_NE(snippet.find("\"uzvfxclip\""), std::string::npos);
    // No nodes copied.
    EXPECT_NE(snippet.find("\"nodes\": []"), std::string::npos);
}

TEST(VfxClipboardCopy, SelectsOnlyMatchingNodes) {
    VfxNodeGraph g;
    g.AddNode({"keep",  "Spawn",  {1.0f, 2.0f}});
    g.AddNode({"drop",  "Update", {3.0f, 4.0f}});
    g.AddNode({"keep2", "Render", {5.0f, 6.0f}});

    const std::string snippet = VfxNodeGraphClipboard::Copy(g, {"keep", "keep2"});
    EXPECT_NE(snippet.find("\"keep\""),  std::string::npos);
    EXPECT_NE(snippet.find("\"keep2\""), std::string::npos);
    EXPECT_EQ(snippet.find("\"drop\""),  std::string::npos);
}

TEST(VfxClipboardCopy, DropsWiresThatEscapeSelection) {
    VfxNodeGraph g;
    g.AddNode({"a", "Spawn",  {}});
    g.AddNode({"b", "Update", {}});
    g.AddNode({"c", "Render", {}});
    g.AddWire({"a:out", "b:in", VfxPinType::Float}); // both selected
    g.AddWire({"b:out", "c:in", VfxPinType::Vec3});  // escapes

    const std::string snippet = VfxNodeGraphClipboard::Copy(g, {"a", "b"});
    // Internal wire preserved.
    EXPECT_NE(snippet.find("\"a:out\""), std::string::npos);
    EXPECT_NE(snippet.find("\"b:in\""),  std::string::npos);
    // Escaping wire dropped.
    EXPECT_EQ(snippet.find("\"b:out\""), std::string::npos);
    EXPECT_EQ(snippet.find("\"c:in\""),  std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Paste — happy path
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxClipboardPaste, RoundTripIntoEmptyGraphPreservesStructure) {
    VfxNodeGraph src;
    src.AddNode({"n0", "Spawn",  {1.0f, 2.0f}});
    src.AddNode({"n1", "Update", {}});
    src.AddWire({"n0:out", "n1:in", VfxPinType::Vec3});

    const std::string snippet = VfxNodeGraphClipboard::Copy(src, {"n0", "n1"});

    VfxNodeGraph dst;
    auto added = VfxNodeGraphClipboard::Paste(dst, snippet);
    ASSERT_EQ(added.size(), 2u);
    EXPECT_EQ(added[0], "n0");
    EXPECT_EQ(added[1], "n1");
    EXPECT_EQ(dst.NodeCount(), 2u);
    EXPECT_EQ(dst.WireCount(), 1u);
    ASSERT_NE(dst.FindNode("n0"), nullptr);
    EXPECT_FLOAT_EQ(dst.FindNode("n0")->position.x, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Paste — id collision renaming
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxClipboardPaste, CollidingIdsGet_CopySuffix) {
    VfxNodeGraph src;
    src.AddNode({"alpha", "Spawn", {}});
    src.AddNode({"beta",  "Update", {}});
    src.AddWire({"alpha:out", "beta:in", VfxPinType::Float});
    const std::string snippet = VfxNodeGraphClipboard::Copy(src, {"alpha", "beta"});

    // Destination already owns "alpha" and "beta".
    VfxNodeGraph dst;
    dst.AddNode({"alpha", "Spawn", {}});
    dst.AddNode({"beta",  "Update", {}});

    auto added = VfxNodeGraphClipboard::Paste(dst, snippet);
    ASSERT_EQ(added.size(), 2u);
    EXPECT_EQ(added[0], "alpha_copy");
    EXPECT_EQ(added[1], "beta_copy");
    EXPECT_EQ(dst.NodeCount(), 4u);

    // Wire endpoint must reference the renamed nodes.
    ASSERT_EQ(dst.wires.size(), 1u);
    EXPECT_EQ(dst.wires[0].fromEndpoint, "alpha_copy:out");
    EXPECT_EQ(dst.wires[0].toEndpoint,   "beta_copy:in");
}

TEST(VfxClipboardPaste, MultiplePastesEscalateSuffix) {
    VfxNodeGraph src;
    src.AddNode({"node", "Spawn", {}});
    const std::string snippet = VfxNodeGraphClipboard::Copy(src, {"node"});

    VfxNodeGraph dst;
    dst.AddNode({"node", "Spawn", {}});

    auto a = VfxNodeGraphClipboard::Paste(dst, snippet);
    ASSERT_EQ(a.size(), 1u); EXPECT_EQ(a[0], "node_copy");
    auto b = VfxNodeGraphClipboard::Paste(dst, snippet);
    ASSERT_EQ(b.size(), 1u); EXPECT_EQ(b[0], "node_copy2");
    auto c = VfxNodeGraphClipboard::Paste(dst, snippet);
    ASSERT_EQ(c.size(), 1u); EXPECT_EQ(c[0], "node_copy3");
    EXPECT_EQ(dst.NodeCount(), 4u);
}

TEST(VfxClipboardPaste, PasteManyProducesDistinctIds) {
    VfxNodeGraph src;
    src.AddNode({"node", "Spawn", {}});
    const std::string snippet = VfxNodeGraphClipboard::Copy(src, {"node"});

    VfxNodeGraph dst;
    dst.AddNode({"node", "Spawn", {}});

    auto added = VfxNodeGraphClipboard::PasteMany(dst, snippet, 3);
    ASSERT_EQ(added.size(), 3u);
    EXPECT_EQ(added[0], "node_copy");
    EXPECT_EQ(added[1], "node_copy2");
    EXPECT_EQ(added[2], "node_copy3");
}

// ─────────────────────────────────────────────────────────────────────────────
// Error handling
// ─────────────────────────────────────────────────────────────────────────────

TEST(VfxClipboardPaste, MalformedJsonLeavesTargetUntouched) {
    VfxNodeGraph dst;
    dst.AddNode({"existing", "Spawn", {}});
    auto added = VfxNodeGraphClipboard::Paste(dst, "{ broken");
    EXPECT_TRUE(added.empty());
    EXPECT_EQ(dst.NodeCount(), 1u);
}

TEST(VfxClipboardPaste, WrongFormatMarkerRejected) {
    // Valid JSON but not a clipboard snippet.
    const char* json = R"({ "format": "uzvfxgraph", "nodes": [], "wires": [] })";
    VfxNodeGraph dst;
    dst.AddNode({"existing", "Spawn", {}});
    auto added = VfxNodeGraphClipboard::Paste(dst, json);
    EXPECT_TRUE(added.empty());
    EXPECT_EQ(dst.NodeCount(), 1u);
}

TEST(VfxClipboardPaste, NodeMissingModuleSkipped) {
    const char* json = R"({
        "format": "uzvfxclip", "version": 1,
        "nodes": [ { "id": "bad", "module": "" }, { "id": "good", "module": "Spawn" } ],
        "wires": []
    })";
    VfxNodeGraph dst;
    auto added = VfxNodeGraphClipboard::Paste(dst, json);
    ASSERT_EQ(added.size(), 1u);
    EXPECT_EQ(added[0], "good");
}
