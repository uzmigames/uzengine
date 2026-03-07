// test_graph_ops_compound.cpp -- Tests for GraphOps compound operations.
//
// Covers:
//   - insert_between (valid, missing connection, type mismatch)
//   - replace_node (compatible pins, incompatible)
//   - expose_parameter (creates param node, wires correctly)

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/renderer/shader_graph/nodes/texture_nodes.h"

using namespace uz::sg;

class GraphOpsCompoundTest : public ::testing::Test {
protected:
    ShaderGraph graph_;
    GraphOps ops_{graph_};
    std::vector<GraphEvent> events_;

    void SetUp() override {
        ops_.set_event_listener([this](const GraphEvent& e) {
            events_.push_back(e);
        });
    }
};

// ============================================================================
// insert_between
// ============================================================================

TEST_F(GraphOpsCompoundTest, InsertBetweenValid) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, oid, 0);

    auto r = ops_.insert_between(cid, 0, oid, 0, "Multiply");
    EXPECT_TRUE(r.ok);
    uint32_t mid = r.data["nodeId"].get<uint32_t>();

    // Verify: Constant -> Multiply -> Output.
    EXPECT_EQ(graph_.connections().size(), 2u);
    auto* mul_node = graph_.get_node(mid);
    EXPECT_NE(mul_node, nullptr);
    EXPECT_EQ(mul_node->type_name, "Multiply");

    // Check wiring.
    auto* conn_to_mul = graph_.find_connection(mid, 0);
    ASSERT_NE(conn_to_mul, nullptr);
    EXPECT_EQ(conn_to_mul->from_node, cid);

    auto* conn_to_out = graph_.find_connection(oid, 0);
    ASSERT_NE(conn_to_out, nullptr);
    EXPECT_EQ(conn_to_out->from_node, mid);
}

TEST_F(GraphOpsCompoundTest, InsertBetweenNoConnection) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    // No connection exists.
    auto r = ops_.insert_between(cid, 0, oid, 0, "Multiply");
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsCompoundTest, InsertBetweenInvalidNodeType) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, oid, 0);

    auto r = ops_.insert_between(cid, 0, oid, 0, "NonExistentNode");
    EXPECT_FALSE(r.ok);
}

// ============================================================================
// replace_node
// ============================================================================

TEST_F(GraphOpsCompoundTest, ReplaceNodeCompatible) {
    auto c = ops_.create_node("Constant3");
    auto a = ops_.create_node("Add");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t aid = a.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, aid, 0);
    ops_.connect_pins(aid, 0, oid, 0);

    auto r = ops_.replace_node(aid, "Multiply");
    EXPECT_TRUE(r.ok);
    uint32_t new_id = r.data["nodeId"].get<uint32_t>();

    // Old node should be gone.
    EXPECT_EQ(graph_.get_node(aid), nullptr);

    // New node should have re-wired connections.
    auto* new_node = graph_.get_node(new_id);
    ASSERT_NE(new_node, nullptr);
    EXPECT_EQ(new_node->type_name, "Multiply");

    // Check wiring: Constant -> Multiply (new) -> Output.
    EXPECT_EQ(graph_.connections().size(), 2u);
}

TEST_F(GraphOpsCompoundTest, ReplaceNodeNotFound) {
    auto r = ops_.replace_node(999, "Multiply");
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsCompoundTest, ReplaceNodePreservesPosition) {
    auto c = ops_.create_node("Add", 150.f, 250.f);
    uint32_t cid = c.data["nodeId"].get<uint32_t>();

    auto r = ops_.replace_node(cid, "Multiply");
    EXPECT_TRUE(r.ok);
    auto* node = graph_.get_node(r.data["nodeId"].get<uint32_t>());
    EXPECT_FLOAT_EQ(node->pos_x, 150.f);
    EXPECT_FLOAT_EQ(node->pos_y, 250.f);
}

// ============================================================================
// expose_parameter
// ============================================================================

TEST_F(GraphOpsCompoundTest, ExposeParamFloat1) {
    auto o = ops_.create_node("MaterialOutput");
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    // Expose Metallic input (index 1, Float1) as parameter.
    auto r = ops_.expose_parameter(oid, 1, "Metallic");
    EXPECT_TRUE(r.ok);
    uint32_t pid = r.data["paramNodeId"].get<uint32_t>();

    auto* param = graph_.get_node(pid);
    ASSERT_NE(param, nullptr);
    EXPECT_EQ(param->type_name, "ScalarParam");

    // Should be connected.
    auto* conn = graph_.find_connection(oid, 1);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->from_node, pid);
}

TEST_F(GraphOpsCompoundTest, ExposeParamScalar) {
    auto o = ops_.create_node("MaterialOutput");
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    // Expose BaseColor input (index 0, Scalar — accepts any float width).
    auto r = ops_.expose_parameter(oid, 0, "BaseColor");
    EXPECT_TRUE(r.ok);
    auto* param = graph_.get_node(r.data["paramNodeId"].get<uint32_t>());
    // Scalar pin type → ScalarParam (Float1 is compatible with Scalar).
    EXPECT_EQ(param->type_name, "ScalarParam");
}

TEST_F(GraphOpsCompoundTest, ExposeParamUVInput) {
    auto ts = ops_.create_node("TextureSample");
    uint32_t tsid = ts.data["nodeId"].get<uint32_t>();

    // Expose UV input (index 1, Scalar — accepts any float width).
    auto r = ops_.expose_parameter(tsid, 1, "UVOffset");
    EXPECT_TRUE(r.ok);
    auto* param = graph_.get_node(r.data["paramNodeId"].get<uint32_t>());
    EXPECT_EQ(param->type_name, "ScalarParam");
}

TEST_F(GraphOpsCompoundTest, ExposeParamTexture) {
    auto ts = ops_.create_node("TextureSample");
    uint32_t tsid = ts.data["nodeId"].get<uint32_t>();

    // Expose Tex input (index 0, Texture2D).
    auto r = ops_.expose_parameter(tsid, 0, "AlbedoTex");
    EXPECT_TRUE(r.ok);
    auto* param = graph_.get_node(r.data["paramNodeId"].get<uint32_t>());
    EXPECT_EQ(param->type_name, "TextureParam");
}

TEST_F(GraphOpsCompoundTest, ExposeParamNodeNotFound) {
    auto r = ops_.expose_parameter(999, 0, "Test");
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsCompoundTest, ExposeParamInputOutOfRange) {
    auto o = ops_.create_node("MaterialOutput");
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    auto r = ops_.expose_parameter(oid, 99, "Test");
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsCompoundTest, ExposeParamDisconnectsExisting) {
    auto c = ops_.create_node("Constant1");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, oid, 1);  // Constant -> Metallic.

    // Expose should disconnect Constant and create ScalarParam.
    auto r = ops_.expose_parameter(oid, 1, "Metallic");
    EXPECT_TRUE(r.ok);
    auto* conn = graph_.find_connection(oid, 1);
    ASSERT_NE(conn, nullptr);
    EXPECT_NE(conn->from_node, cid);  // Now connected to param node.
}
