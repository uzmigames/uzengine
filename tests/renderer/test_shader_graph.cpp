// test_shader_graph.cpp -- Google Test coverage for ShaderGraph.
//
// Covers:
//   - Add/remove nodes, connect/disconnect
//   - DAG validation: valid DAG, simple cycle, diamond, self-loop, deep chain
//   - Type validation: compatible, incompatible, mixed
//   - Serialization round-trip
//   - Snapshot/restore

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/shader_graph.h"
#include <nlohmann/json.hpp>
#include <memory>

using namespace uz::sg;

// ============================================================================
// Test node implementations (minimal, for graph testing only)
// ============================================================================

// A node with 2 Float3 inputs and 1 Float3 output (like Add).
class TestAddNode : public ShaderNodeFixed<2> {
public:
    TestAddNode() { type_name = "TestAdd"; }

    std::span<const PinDesc> input_descs() const override {
        static const PinDesc descs[] = {
            {"A", ValueType::Scalar, false},
            {"B", ValueType::Scalar, false},
        };
        return descs;
    }

    std::span<const PinDesc> output_descs() const override {
        static const PinDesc descs[] = {
            {"Result", ValueType::Float3, false},
        };
        return descs;
    }

    int32_t compile(IShaderCompiler&, int32_t) override { return 0; }

    std::unique_ptr<ShaderNode> clone() const override {
        auto n = std::make_unique<TestAddNode>();
        n->id = id;
        n->type_name = type_name;
        n->pos_x = pos_x;
        n->pos_y = pos_y;
        n->inputs_ = inputs_;
        return n;
    }
};

// A node with 1 Float3 input and 0 outputs (like MaterialOutput).
class TestOutputNode : public ShaderNodeFixed<1> {
public:
    TestOutputNode() { type_name = "TestOutput"; }

    std::span<const PinDesc> input_descs() const override {
        static const PinDesc descs[] = {
            {"Color", ValueType::Float3, true},
        };
        return descs;
    }

    std::span<const PinDesc> output_descs() const override { return {}; }

    int32_t compile(IShaderCompiler&, int32_t) override { return 0; }

    std::unique_ptr<ShaderNode> clone() const override {
        auto n = std::make_unique<TestOutputNode>();
        n->id = id;
        n->type_name = type_name;
        n->pos_x = pos_x;
        n->pos_y = pos_y;
        n->inputs_ = inputs_;
        return n;
    }
};

// A node with 0 inputs and 1 Float3 output (like a Constant).
class TestConstantNode : public ShaderNodeFixed<0> {
public:
    TestConstantNode() { type_name = "TestConstant"; }

    std::span<const PinDesc> input_descs() const override { return {}; }

    std::span<const PinDesc> output_descs() const override {
        static const PinDesc descs[] = {
            {"Value", ValueType::Float3, false},
        };
        return descs;
    }

    int32_t compile(IShaderCompiler&, int32_t) override { return 0; }

    std::unique_ptr<ShaderNode> clone() const override {
        auto n = std::make_unique<TestConstantNode>();
        n->id = id;
        n->type_name = type_name;
        return n;
    }
};

// A node with Texture2D output (for type mismatch testing).
class TestTextureNode : public ShaderNodeFixed<0> {
public:
    TestTextureNode() { type_name = "TestTexture"; }

    std::span<const PinDesc> input_descs() const override { return {}; }

    std::span<const PinDesc> output_descs() const override {
        static const PinDesc descs[] = {
            {"Tex", ValueType::Texture2D, false},
        };
        return descs;
    }

    int32_t compile(IShaderCompiler&, int32_t) override { return 0; }

    std::unique_ptr<ShaderNode> clone() const override {
        auto n = std::make_unique<TestTextureNode>();
        n->id = id;
        n->type_name = type_name;
        return n;
    }
};

// Factory for deserialization tests.
static std::unique_ptr<ShaderNode> test_factory(const std::string& type) {
    if (type == "TestAdd") return std::make_unique<TestAddNode>();
    if (type == "TestOutput") return std::make_unique<TestOutputNode>();
    if (type == "TestConstant") return std::make_unique<TestConstantNode>();
    if (type == "TestTexture") return std::make_unique<TestTextureNode>();
    return nullptr;
}

// ============================================================================
// Test fixture
// ============================================================================

class ShaderGraphTest : public ::testing::Test {
protected:
    ShaderGraph graph_;

    uint32_t add_constant() {
        return graph_.add_node(std::make_unique<TestConstantNode>());
    }
    uint32_t add_add() {
        return graph_.add_node(std::make_unique<TestAddNode>());
    }
    uint32_t add_output() {
        return graph_.add_node(std::make_unique<TestOutputNode>());
    }
    uint32_t add_texture() {
        return graph_.add_node(std::make_unique<TestTextureNode>());
    }
};

// ============================================================================
// Node management
// ============================================================================

TEST_F(ShaderGraphTest, AddNodeReturnsIncrementingIds) {
    uint32_t id1 = add_constant();
    uint32_t id2 = add_constant();
    EXPECT_EQ(id1, 1u);
    EXPECT_EQ(id2, 2u);
}

TEST_F(ShaderGraphTest, AddNullNodeReturnsZero) {
    uint32_t id = graph_.add_node(nullptr);
    EXPECT_EQ(id, 0u);
    EXPECT_TRUE(graph_.nodes().empty());
}

TEST_F(ShaderGraphTest, GetNode) {
    uint32_t id = add_constant();
    EXPECT_NE(graph_.get_node(id), nullptr);
    EXPECT_EQ(graph_.get_node(id)->type_name, "TestConstant");
    EXPECT_EQ(graph_.get_node(999), nullptr);
}

TEST_F(ShaderGraphTest, GetNodeConst) {
    uint32_t id = add_constant();
    const auto& cg = graph_;
    EXPECT_NE(cg.get_node(id), nullptr);
    EXPECT_EQ(cg.get_node(999), nullptr);
}

TEST_F(ShaderGraphTest, RemoveNode) {
    uint32_t id = add_constant();
    EXPECT_EQ(graph_.nodes().size(), 1u);
    graph_.remove_node(id);
    EXPECT_EQ(graph_.nodes().size(), 0u);
}

TEST_F(ShaderGraphTest, RemoveNodeClearsOutputNode) {
    uint32_t id = add_output();
    graph_.set_output_node_id(id);
    EXPECT_EQ(graph_.output_node_id(), id);
    graph_.remove_node(id);
    EXPECT_EQ(graph_.output_node_id(), 0u);
}

TEST_F(ShaderGraphTest, RemoveNonexistentNodeIsSafe) {
    graph_.remove_node(999);  // Should not crash.
    EXPECT_TRUE(graph_.nodes().empty());
}

// ============================================================================
// Connection management
// ============================================================================

TEST_F(ShaderGraphTest, ConnectValid) {
    uint32_t c = add_constant();
    uint32_t o = add_output();
    EXPECT_TRUE(graph_.connect(c, 0, o, 0));
    EXPECT_EQ(graph_.connections().size(), 1u);

    // Verify NodeInput updated.
    auto* out_node = graph_.get_node(o);
    EXPECT_EQ(out_node->get_input(0).source_node_id, c);
    EXPECT_EQ(out_node->get_input(0).source_output_index, 0);
}

TEST_F(ShaderGraphTest, ConnectAutoDisconnectsExisting) {
    uint32_t c1 = add_constant();
    uint32_t c2 = add_constant();
    uint32_t o = add_output();
    EXPECT_TRUE(graph_.connect(c1, 0, o, 0));
    EXPECT_TRUE(graph_.connect(c2, 0, o, 0));
    EXPECT_EQ(graph_.connections().size(), 1u);  // Only newest connection.
    EXPECT_EQ(graph_.get_node(o)->get_input(0).source_node_id, c2);
}

TEST_F(ShaderGraphTest, ConnectInvalidSourceNode) {
    uint32_t o = add_output();
    EXPECT_FALSE(graph_.connect(999, 0, o, 0));
}

TEST_F(ShaderGraphTest, ConnectInvalidDestNode) {
    uint32_t c = add_constant();
    EXPECT_FALSE(graph_.connect(c, 0, 999, 0));
}

TEST_F(ShaderGraphTest, ConnectOutOfRangeOutputPin) {
    uint32_t c = add_constant();
    uint32_t o = add_output();
    EXPECT_FALSE(graph_.connect(c, 5, o, 0));
    EXPECT_FALSE(graph_.connect(c, -1, o, 0));
}

TEST_F(ShaderGraphTest, ConnectOutOfRangeInputPin) {
    uint32_t c = add_constant();
    uint32_t o = add_output();
    EXPECT_FALSE(graph_.connect(c, 0, o, 5));
    EXPECT_FALSE(graph_.connect(c, 0, o, -1));
}

TEST_F(ShaderGraphTest, ConnectTypeMismatch) {
    uint32_t t = add_texture();  // Texture2D output
    uint32_t o = add_output();   // Float3 input
    EXPECT_FALSE(graph_.connect(t, 0, o, 0));
}

TEST_F(ShaderGraphTest, ConnectSelfLoop) {
    uint32_t a = add_add();
    EXPECT_FALSE(graph_.connect(a, 0, a, 0));
}

TEST_F(ShaderGraphTest, Disconnect) {
    uint32_t c = add_constant();
    uint32_t o = add_output();
    graph_.connect(c, 0, o, 0);
    graph_.disconnect(o, 0);
    EXPECT_TRUE(graph_.connections().empty());
    EXPECT_EQ(graph_.get_node(o)->get_input(0).source_node_id, 0u);
}

TEST_F(ShaderGraphTest, DisconnectNonexistent) {
    graph_.disconnect(999, 0);  // Should not crash.
}

TEST_F(ShaderGraphTest, DisconnectAll) {
    uint32_t c1 = add_constant();
    uint32_t c2 = add_constant();
    uint32_t a = add_add();
    uint32_t o = add_output();
    graph_.connect(c1, 0, a, 0);
    graph_.connect(c2, 0, a, 1);
    graph_.connect(a, 0, o, 0);
    EXPECT_EQ(graph_.connections().size(), 3u);

    graph_.disconnect_all(a);
    EXPECT_EQ(graph_.connections().size(), 0u);
    EXPECT_EQ(graph_.get_node(o)->get_input(0).source_node_id, 0u);
}

TEST_F(ShaderGraphTest, FindConnection) {
    uint32_t c = add_constant();
    uint32_t o = add_output();
    graph_.connect(c, 0, o, 0);

    auto* conn = graph_.find_connection(o, 0);
    ASSERT_NE(conn, nullptr);
    EXPECT_EQ(conn->from_node, c);
    EXPECT_EQ(conn->to_node, o);

    EXPECT_EQ(graph_.find_connection(o, 1), nullptr);
    EXPECT_EQ(graph_.find_connection(999, 0), nullptr);
}

TEST_F(ShaderGraphTest, RemoveNodeDisconnectsConnections) {
    uint32_t c = add_constant();
    uint32_t a = add_add();
    uint32_t o = add_output();
    graph_.connect(c, 0, a, 0);
    graph_.connect(a, 0, o, 0);
    EXPECT_EQ(graph_.connections().size(), 2u);

    graph_.remove_node(a);
    EXPECT_TRUE(graph_.connections().empty());
    EXPECT_EQ(graph_.get_node(o)->get_input(0).source_node_id, 0u);
}

// ============================================================================
// DAG validation
// ============================================================================

TEST_F(ShaderGraphTest, ValidDagEmptyGraph) {
    EXPECT_TRUE(graph_.validate_dag());
}

TEST_F(ShaderGraphTest, ValidDagLinearChain) {
    uint32_t c = add_constant();
    uint32_t a = add_add();
    uint32_t o = add_output();
    graph_.connect(c, 0, a, 0);
    graph_.connect(a, 0, o, 0);
    EXPECT_TRUE(graph_.validate_dag());
}

TEST_F(ShaderGraphTest, ValidDagDiamond) {
    // c -> a1 -> o
    // c -> a2 -> (also feeds a1 input B)
    // This is a diamond pattern (DAG, not a cycle).
    uint32_t c = add_constant();
    uint32_t a1 = add_add();
    uint32_t a2 = add_add();
    uint32_t o = add_output();
    graph_.connect(c, 0, a1, 0);
    graph_.connect(c, 0, a2, 0);
    graph_.connect(a2, 0, a1, 1);
    graph_.connect(a1, 0, o, 0);
    EXPECT_TRUE(graph_.validate_dag());
}

TEST_F(ShaderGraphTest, ValidDagDeepChain) {
    // 10-node linear chain.
    uint32_t prev = add_constant();
    for (int i = 0; i < 9; ++i) {
        uint32_t next = (i < 8) ? add_add() : add_output();
        if (i < 8) {
            graph_.connect(prev, 0, next, 0);
        } else {
            graph_.connect(prev, 0, next, 0);
        }
        prev = next;
    }
    EXPECT_TRUE(graph_.validate_dag());
}

TEST_F(ShaderGraphTest, InvalidDagSimpleCycle) {
    // Manually create a cycle by directly modifying connections.
    // a1 -> a2 -> a1 (cycle).
    uint32_t a1 = add_add();
    uint32_t a2 = add_add();
    // We need to bypass type checking to create a cycle.
    // connect(a1, 0, a2, 0) and connect(a2, 0, a1, 0) both valid individually.
    graph_.connect(a1, 0, a2, 0);
    graph_.connect(a2, 0, a1, 0);
    EXPECT_FALSE(graph_.validate_dag());
}

TEST_F(ShaderGraphTest, ValidDagDisconnectedNodes) {
    add_constant();
    add_add();
    add_output();
    // No connections — still a valid DAG (trivially acyclic).
    EXPECT_TRUE(graph_.validate_dag());
}

// ============================================================================
// Type validation
// ============================================================================

TEST_F(ShaderGraphTest, ValidTypesCompatible) {
    uint32_t c = add_constant();  // Float3 output
    uint32_t o = add_output();    // Float3 input (via Scalar mask)
    graph_.connect(c, 0, o, 0);
    EXPECT_TRUE(graph_.validate_types());
    EXPECT_TRUE(graph_.get_errors().empty());
}

TEST_F(ShaderGraphTest, ValidTypesEmpty) {
    EXPECT_TRUE(graph_.validate_types());
}

TEST_F(ShaderGraphTest, ValidTypesNoConnectionsWithNodes) {
    add_constant();
    add_output();
    EXPECT_TRUE(graph_.validate_types());
}

// ============================================================================
// Serialization
// ============================================================================

TEST_F(ShaderGraphTest, SerializeEmpty) {
    auto j = graph_.serialize();
    EXPECT_EQ(j["version"], 1);
    EXPECT_TRUE(j["nodes"].empty());
    EXPECT_TRUE(j["connections"].empty());
}

TEST_F(ShaderGraphTest, SerializeRoundTrip) {
    uint32_t c = add_constant();
    uint32_t a = add_add();
    uint32_t o = add_output();
    graph_.set_output_node_id(o);
    graph_.connect(c, 0, a, 0);
    graph_.connect(a, 0, o, 0);

    auto j = graph_.serialize();
    auto restored = ShaderGraph::deserialize(j, test_factory);
    ASSERT_NE(restored, nullptr);

    EXPECT_EQ(restored->nodes().size(), 3u);
    EXPECT_EQ(restored->connections().size(), 2u);
    EXPECT_EQ(restored->output_node_id(), o);
}

TEST_F(ShaderGraphTest, SerializePreservesNodeData) {
    auto node = std::make_unique<TestConstantNode>();
    node->pos_x = 100.f;
    node->pos_y = 200.f;
    uint32_t id = graph_.add_node(std::move(node));

    auto j = graph_.serialize();
    auto restored = ShaderGraph::deserialize(j, test_factory);
    auto* n = restored->get_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->pos_x, 100.f);
    EXPECT_FLOAT_EQ(n->pos_y, 200.f);
    EXPECT_EQ(n->type_name, "TestConstant");
}

TEST_F(ShaderGraphTest, DeserializeUnknownNodeTypeSkipped) {
    nlohmann::json j = {
        {"version", 1},
        {"output_node_id", 0},
        {"nodes", {{{"id", 1}, {"type", "UnknownType"}, {"x", 0}, {"y", 0}, {"inputs", nlohmann::json::array()}}}},
        {"connections", nlohmann::json::array()}
    };
    auto restored = ShaderGraph::deserialize(j, test_factory);
    EXPECT_TRUE(restored->nodes().empty());
}

TEST_F(ShaderGraphTest, DeserializePreservesInputDefaults) {
    auto node = std::make_unique<TestAddNode>();
    node->get_input(0).default_value[0] = 1.5f;
    node->get_input(0).default_value[1] = 2.5f;
    uint32_t id = graph_.add_node(std::move(node));

    auto j = graph_.serialize();
    auto restored = ShaderGraph::deserialize(j, test_factory);
    auto* n = restored->get_node(id);
    ASSERT_NE(n, nullptr);
    EXPECT_FLOAT_EQ(n->get_input(0).default_value[0], 1.5f);
    EXPECT_FLOAT_EQ(n->get_input(0).default_value[1], 2.5f);
}

// ============================================================================
// Snapshot / Restore
// ============================================================================

TEST_F(ShaderGraphTest, SnapshotAndRestore) {
    uint32_t c = add_constant();
    uint32_t o = add_output();
    graph_.set_output_node_id(o);
    graph_.connect(c, 0, o, 0);

    auto snap = graph_.snapshot();
    EXPECT_EQ(graph_.nodes().size(), 2u);

    // Modify graph.
    add_add();
    EXPECT_EQ(graph_.nodes().size(), 3u);

    // Restore.
    graph_.restore(snap, test_factory);
    EXPECT_EQ(graph_.nodes().size(), 2u);
    EXPECT_EQ(graph_.connections().size(), 1u);
    EXPECT_EQ(graph_.output_node_id(), o);
}

// ============================================================================
// Connection struct equality
// ============================================================================

TEST_F(ShaderGraphTest, ConnectionEquality) {
    Connection a{1, 0, 2, 0};
    Connection b{1, 0, 2, 0};
    Connection c{1, 0, 3, 0};
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);
}

// ============================================================================
// ShaderNode serialization
// ============================================================================

TEST_F(ShaderGraphTest, NodeSerializeDeserialize) {
    TestAddNode node;
    node.id = 42;
    node.type_name = "TestAdd";
    node.pos_x = 10.f;
    node.pos_y = 20.f;
    node.get_input(0).default_value[0] = 3.14f;

    nlohmann::json j;
    node.serialize(j);

    TestAddNode restored;
    restored.id = j["id"].get<uint32_t>();
    restored.type_name = j["type"].get<std::string>();
    restored.deserialize(j);

    EXPECT_EQ(restored.id, 42u);
    EXPECT_EQ(restored.type_name, "TestAdd");
    EXPECT_FLOAT_EQ(restored.pos_x, 10.f);
    EXPECT_FLOAT_EQ(restored.pos_y, 20.f);
    EXPECT_FLOAT_EQ(restored.get_input(0).default_value[0], 3.14f);
}

TEST_F(ShaderGraphTest, NodeDeserializePartial) {
    // Deserialize with missing fields — should handle gracefully.
    nlohmann::json j = {{"id", 1}, {"type", "TestAdd"}};
    TestAddNode node;
    node.deserialize(j);  // No crash, pos defaults to 0.
    EXPECT_FLOAT_EQ(node.pos_x, 0.f);
}

// ============================================================================
// Edge cases
// ============================================================================

TEST_F(ShaderGraphTest, FanOutFromSingleOutput) {
    // One constant feeds two Add nodes.
    uint32_t c = add_constant();
    uint32_t a1 = add_add();
    uint32_t a2 = add_add();
    EXPECT_TRUE(graph_.connect(c, 0, a1, 0));
    EXPECT_TRUE(graph_.connect(c, 0, a2, 0));
    EXPECT_EQ(graph_.connections().size(), 2u);
    EXPECT_TRUE(graph_.validate_dag());
}

TEST_F(ShaderGraphTest, NextIdPreservedAfterRemove) {
    uint32_t id1 = add_constant();
    uint32_t id2 = add_constant();
    graph_.remove_node(id1);
    uint32_t id3 = add_constant();
    // IDs are monotonically increasing — removed IDs are NOT reused.
    EXPECT_GT(id3, id2);
}
