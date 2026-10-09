/// Unit tests for phase 7.15.0 - VFX node-graph data model + .uzvfxgraph asset.
///
/// Covers:
///   - Pin-type enum round-trip + unknown rejection.
///   - Endpoint parser accepts `<node>:<pin>` and rejects malformed forms.
///   - AddNode enforces unique + non-empty id.
///   - AddWire rejects empty endpoints.
///   - RemoveNode drops the node + every wire touching it.
///   - RemoveWiresTouching removes only exact endpoint matches.
///   - .uzvfxgraph JSON save/load round-trip preserves nodes, wires, types.
///   - Malformed root / unknown format / duplicate ids rejected with warnings.
///
/// @ref phase7_7.15.0-vfx-node-graph-editor

#include <gtest/gtest.h>

#include "engine/vfx/editor/vfx_node_graph.h"

using namespace uz;

// ─── Pin type enum ─────────────────────────────────────────────────────────

TEST(VfxPinType, ToStringAllBranches) {
    EXPECT_STREQ(VfxPinTypeToString(VfxPinType::Float), "float");
    EXPECT_STREQ(VfxPinTypeToString(VfxPinType::Vec2),  "vec2");
    EXPECT_STREQ(VfxPinTypeToString(VfxPinType::Vec3),  "vec3");
    EXPECT_STREQ(VfxPinTypeToString(VfxPinType::Vec4),  "vec4");
    EXPECT_STREQ(VfxPinTypeToString(VfxPinType::Bool),  "bool");
    EXPECT_STREQ(VfxPinTypeToString(VfxPinType::Int),   "int");
}

TEST(VfxPinType, FromStringRoundTrip) {
    VfxPinType t;
    ASSERT_TRUE(VfxPinTypeFromString("float", t)); EXPECT_EQ(t, VfxPinType::Float);
    ASSERT_TRUE(VfxPinTypeFromString("vec3",  t)); EXPECT_EQ(t, VfxPinType::Vec3);
    ASSERT_TRUE(VfxPinTypeFromString("bool",  t)); EXPECT_EQ(t, VfxPinType::Bool);
    EXPECT_FALSE(VfxPinTypeFromString("bogus", t));
}

// ─── Endpoint parser ──────────────────────────────────────────────────────

TEST(VfxParseEndpoint, AcceptsNodePinForm) {
    std::string n, p;
    ASSERT_TRUE(VfxParseEndpoint("n0:spawnCount", n, p));
    EXPECT_EQ(n, "n0");
    EXPECT_EQ(p, "spawnCount");
}

TEST(VfxParseEndpoint, RejectsMissingColon) {
    std::string n, p;
    EXPECT_FALSE(VfxParseEndpoint("nocolon", n, p));
}

TEST(VfxParseEndpoint, RejectsEmptyHalf) {
    std::string n, p;
    EXPECT_FALSE(VfxParseEndpoint(":pin", n, p));
    EXPECT_FALSE(VfxParseEndpoint("node:", n, p));
}

// ─── Graph editing ─────────────────────────────────────────────────────────

TEST(VfxNodeGraph, AddNodeEnforcesUniqueId) {
    VfxNodeGraph g;
    VfxGraphNode a; a.id = "n0"; a.module = "Spawn";
    EXPECT_TRUE(g.AddNode(a));
    EXPECT_FALSE(g.AddNode(a));  // duplicate id rejected
    EXPECT_EQ(g.NodeCount(), 1u);
}

TEST(VfxNodeGraph, AddNodeRejectsEmptyId) {
    VfxNodeGraph g;
    VfxGraphNode a; a.module = "Spawn";  // id left empty
    EXPECT_FALSE(g.AddNode(a));
    EXPECT_EQ(g.NodeCount(), 0u);
}

TEST(VfxNodeGraph, AddWireRequiresBothEndpoints) {
    VfxNodeGraph g;
    VfxGraphWire w;
    EXPECT_FALSE(g.AddWire(w));  // both empty
    w.fromEndpoint = "n0:out";
    EXPECT_FALSE(g.AddWire(w));  // to empty
    w.toEndpoint = "n1:in";
    EXPECT_TRUE (g.AddWire(w));
    EXPECT_EQ(g.WireCount(), 1u);
}

TEST(VfxNodeGraph, RemoveNodeDropsNodeAndTouchingWires) {
    VfxNodeGraph g;
    VfxGraphNode a; a.id = "n0"; a.module = "Spawn";
    VfxGraphNode b; b.id = "n1"; b.module = "Color";
    g.AddNode(a); g.AddNode(b);
    VfxGraphWire w; w.fromEndpoint = "n0:out"; w.toEndpoint = "n1:in";
    g.AddWire(w);
    VfxGraphWire w2; w2.fromEndpoint = "n1:color"; w2.toEndpoint = "n0:tint";
    g.AddWire(w2);

    const uint32_t removed = g.RemoveNode("n0");
    EXPECT_EQ(removed, 3u);  // 1 node + 2 wires
    EXPECT_EQ(g.NodeCount(), 1u);
    EXPECT_EQ(g.WireCount(), 0u);
}

TEST(VfxNodeGraph, RemoveWiresTouchingMatchesExactEndpoint) {
    VfxNodeGraph g;
    VfxGraphWire w1; w1.fromEndpoint = "n0:spawnCount"; w1.toEndpoint = "n1:in";
    VfxGraphWire w2; w2.fromEndpoint = "n2:out";        w2.toEndpoint = "n0:spawnCount";
    VfxGraphWire w3; w3.fromEndpoint = "n3:out";        w3.toEndpoint = "n4:in";
    g.AddWire(w1); g.AddWire(w2); g.AddWire(w3);

    const uint32_t removed = g.RemoveWiresTouching("n0:spawnCount");
    EXPECT_EQ(removed, 2u);        // w1 + w2
    EXPECT_EQ(g.WireCount(), 1u);  // w3 survives
}

TEST(VfxNodeGraph, FindNodeReturnsNullForMissing) {
    VfxNodeGraph g;
    EXPECT_EQ(g.FindNode("ghost"), nullptr);
}

// ─── .uzvfxgraph JSON round-trip ───────────────────────────────────────────

TEST(VfxNodeGraphAsset, SaveLoadRoundTripPreservesEverything) {
    VfxNodeGraph src;
    VfxGraphNode a; a.id = "n0"; a.module = "Spawn";     a.position = Vec2{0,   0};
    VfxGraphNode b; b.id = "n1"; b.module = "ColorRamp"; b.position = Vec2{300, 0};
    VfxGraphNode c; c.id = "n2"; c.module = "Billboard"; c.position = Vec2{600, 0};
    src.AddNode(a); src.AddNode(b); src.AddNode(c);

    VfxGraphWire w1; w1.fromEndpoint = "n0:spawnCount"; w1.toEndpoint = "n1:particleCount"; w1.type = VfxPinType::Float;
    VfxGraphWire w2; w2.fromEndpoint = "n1:color";       w2.toEndpoint = "n2:color";         w2.type = VfxPinType::Vec4;
    src.AddWire(w1); src.AddWire(w2);

    const std::string json = VfxNodeGraphAsset::SaveToString(src);
    ASSERT_FALSE(json.empty());

    VfxNodeGraph dst;
    ASSERT_TRUE(VfxNodeGraphAsset::LoadFromString(json, dst));
    ASSERT_EQ(dst.NodeCount(), 3u);
    ASSERT_EQ(dst.WireCount(), 2u);
    EXPECT_EQ(dst.nodes[1].module, "ColorRamp");
    EXPECT_FLOAT_EQ(dst.nodes[2].position.x, 600.0f);
    EXPECT_EQ(dst.wires[0].type, VfxPinType::Float);
    EXPECT_EQ(dst.wires[1].type, VfxPinType::Vec4);
}

TEST(VfxNodeGraphAsset, RejectsUnknownFormat) {
    VfxNodeGraph dst;
    EXPECT_FALSE(VfxNodeGraphAsset::LoadFromString(R"({"format":"other"})", dst));
    EXPECT_FALSE(VfxNodeGraphAsset::LoadFromString("not json", dst));
}

TEST(VfxNodeGraphAsset, DuplicateIdInJsonIsSkipped) {
    const char* json = R"({
        "format": "uzvfxgraph",
        "version": 1,
        "nodes": [
            { "id": "n0", "module": "A" },
            { "id": "n0", "module": "B" }
        ]
    })";
    VfxNodeGraph dst;
    ASSERT_TRUE(VfxNodeGraphAsset::LoadFromString(json, dst));
    EXPECT_EQ(dst.NodeCount(), 1u);
    EXPECT_EQ(dst.nodes[0].module, "A");  // first-come-first-served
}

TEST(VfxNodeGraphAsset, UnknownPinTypeFallsBackToFloat) {
    const char* json = R"({
        "format": "uzvfxgraph",
        "version": 1,
        "nodes": [
            { "id": "n0", "module": "A" },
            { "id": "n1", "module": "B" }
        ],
        "wires": [
            { "from": "n0:out", "to": "n1:in", "type": "matrix4x4" }
        ]
    })";
    VfxNodeGraph dst;
    ASSERT_TRUE(VfxNodeGraphAsset::LoadFromString(json, dst));
    ASSERT_EQ(dst.WireCount(), 1u);
    EXPECT_EQ(dst.wires[0].type, VfxPinType::Float);
}
