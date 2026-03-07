// test_graph_ops_query.cpp -- Tests for GraphOps query operations.
//
// Covers:
//   - query_graph (empty, populated)
//   - query_node (exists, missing)
//   - find_nodes (by type, no match)
//   - list_node_types
//   - query_connection (connected, unconnected)

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"

using namespace uz::sg;

class GraphOpsQueryTest : public ::testing::Test {
protected:
    ShaderGraph graph_;
    GraphOps ops_{graph_};
};

// ============================================================================
// query_graph
// ============================================================================

TEST_F(GraphOpsQueryTest, QueryGraphEmpty) {
    auto r = ops_.query_graph();
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["nodeCount"], 0u);
    EXPECT_EQ(r.data["connectionCount"], 0u);
    EXPECT_TRUE(r.data["isValidDag"].get<bool>());
}

TEST_F(GraphOpsQueryTest, QueryGraphPopulated) {
    ops_.create_node("Constant3");
    ops_.create_node("Add");
    ops_.create_node("MaterialOutput");
    auto r = ops_.query_graph();
    EXPECT_EQ(r.data["nodeCount"], 3u);
    EXPECT_EQ(r.data["nodeIds"].size(), 3u);
}

// ============================================================================
// query_node
// ============================================================================

TEST_F(GraphOpsQueryTest, QueryNodeExists) {
    auto cr = ops_.create_node("Add", 50.f, 100.f);
    uint32_t id = cr.data["nodeId"].get<uint32_t>();

    auto r = ops_.query_node(id);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["id"], id);
    EXPECT_EQ(r.data["type"], "Add");
    EXPECT_FLOAT_EQ(r.data["x"].get<float>(), 50.f);
    EXPECT_EQ(r.data["inputs"].size(), 2u);
    EXPECT_EQ(r.data["outputs"].size(), 1u);
}

TEST_F(GraphOpsQueryTest, QueryNodeMissing) {
    auto r = ops_.query_node(999);
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsQueryTest, QueryNodeShowsConnection) {
    auto c = ops_.create_node("Constant3");
    auto a = ops_.create_node("Add");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t aid = a.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, aid, 0);

    auto r = ops_.query_node(aid);
    EXPECT_TRUE(r.data["inputs"][0]["connected"].get<bool>());
    EXPECT_EQ(r.data["inputs"][0]["sourceNode"], cid);
    EXPECT_FALSE(r.data["inputs"][1]["connected"].get<bool>());
}

// ============================================================================
// find_nodes
// ============================================================================

TEST_F(GraphOpsQueryTest, FindNodesByType) {
    ops_.create_node("Add");
    ops_.create_node("Add");
    ops_.create_node("Multiply");

    auto r = ops_.find_nodes("Add");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["nodeIds"].size(), 2u);
}

TEST_F(GraphOpsQueryTest, FindNodesNoMatch) {
    ops_.create_node("Add");
    auto r = ops_.find_nodes("Subtract");
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["nodeIds"].size(), 0u);
}

// ============================================================================
// list_node_types
// ============================================================================

TEST_F(GraphOpsQueryTest, ListNodeTypes) {
    auto r = ops_.list_node_types();
    EXPECT_TRUE(r.ok);
    EXPECT_GE(r.data["types"].size(), 20u);

    // Verify structure.
    auto& first = r.data["types"][0];
    EXPECT_TRUE(first.contains("name"));
    EXPECT_TRUE(first.contains("category"));
}

// ============================================================================
// query_connection
// ============================================================================

TEST_F(GraphOpsQueryTest, QueryConnectionConnected) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, oid, 0);

    auto r = ops_.query_connection(oid, 0);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.data["connected"].get<bool>());
    EXPECT_EQ(r.data["from"], cid);
}

TEST_F(GraphOpsQueryTest, QueryConnectionUnconnected) {
    auto o = ops_.create_node("MaterialOutput");
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    auto r = ops_.query_connection(oid, 0);
    EXPECT_TRUE(r.ok);
    EXPECT_FALSE(r.data["connected"].get<bool>());
}
