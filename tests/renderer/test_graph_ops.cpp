// test_graph_ops.cpp -- Google Test coverage for GraphOps atomic operations + events.
//
// Covers:
//   - create_node (valid, invalid type)
//   - delete_node (exists, doesn't exist, auto-disconnect)
//   - connect_pins (valid, type mismatch, already connected, invalid pins)
//   - disconnect_pin (connected, unconnected)
//   - set_param
//   - Event emission for each mutation type

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/nodes/constant_nodes.h"
#include "engine/renderer/shader_graph/nodes/math_nodes.h"
#include "engine/renderer/shader_graph/nodes/output_node.h"
#include "engine/renderer/shader_graph/nodes/param_nodes.h"
#include "engine/renderer/shader_graph/nodes/texture_nodes.h"

using namespace uz::sg;

class GraphOpsTest : public ::testing::Test {
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
// create_node
// ============================================================================

TEST_F(GraphOpsTest, CreateNodeValid) {
    auto r = ops_.create_node("Add");
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.data.contains("nodeId"));
    EXPECT_EQ(graph_.nodes().size(), 1u);
    EXPECT_EQ(events_.size(), 1u);
    EXPECT_EQ(events_[0].type, GraphEventType::NodeAdded);
}

TEST_F(GraphOpsTest, CreateNodeWithPosition) {
    auto r = ops_.create_node("Constant1", 100.f, 200.f);
    EXPECT_TRUE(r.ok);
    auto* node = graph_.get_node(r.data["nodeId"].get<uint32_t>());
    EXPECT_FLOAT_EQ(node->pos_x, 100.f);
    EXPECT_FLOAT_EQ(node->pos_y, 200.f);
}

TEST_F(GraphOpsTest, CreateNodeInvalidType) {
    auto r = ops_.create_node("DoesNotExist");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.error.empty());
    EXPECT_TRUE(graph_.nodes().empty());
    EXPECT_TRUE(events_.empty());
}

// ============================================================================
// delete_node
// ============================================================================

TEST_F(GraphOpsTest, DeleteNodeValid) {
    auto r = ops_.create_node("Add");
    uint32_t id = r.data["nodeId"].get<uint32_t>();
    events_.clear();

    auto dr = ops_.delete_node(id);
    EXPECT_TRUE(dr.ok);
    EXPECT_TRUE(graph_.nodes().empty());
    EXPECT_EQ(events_.back().type, GraphEventType::NodeRemoved);
    EXPECT_EQ(events_.back().node_id, id);
}

TEST_F(GraphOpsTest, DeleteNodeNotFound) {
    auto r = ops_.delete_node(999);
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsTest, DeleteNodeAutoDisconnects) {
    auto c = ops_.create_node("Constant3");
    auto a = ops_.create_node("Add");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t aid = a.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(cid, 0, aid, 0);
    ops_.connect_pins(aid, 0, oid, 0);
    events_.clear();

    ops_.delete_node(aid);
    EXPECT_TRUE(graph_.connections().empty());
    // Should emit ConnectionRemoved events before NodeRemoved.
    bool found_conn_removed = false;
    for (auto& e : events_) {
        if (e.type == GraphEventType::ConnectionRemoved) found_conn_removed = true;
    }
    EXPECT_TRUE(found_conn_removed);
}

// ============================================================================
// connect_pins
// ============================================================================

TEST_F(GraphOpsTest, ConnectValid) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    events_.clear();

    auto r = ops_.connect_pins(cid, 0, oid, 0);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(graph_.connections().size(), 1u);
    EXPECT_EQ(events_.size(), 1u);
    EXPECT_EQ(events_[0].type, GraphEventType::ConnectionAdded);
}

TEST_F(GraphOpsTest, ConnectAutoDisconnects) {
    auto c1 = ops_.create_node("Constant3");
    auto c2 = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t c1id = c1.data["nodeId"].get<uint32_t>();
    uint32_t c2id = c2.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    ops_.connect_pins(c1id, 0, oid, 0);
    events_.clear();
    ops_.connect_pins(c2id, 0, oid, 0);

    // Should emit ConnectionRemoved for old, ConnectionAdded for new.
    EXPECT_EQ(events_.size(), 2u);
    EXPECT_EQ(events_[0].type, GraphEventType::ConnectionRemoved);
    EXPECT_EQ(events_[1].type, GraphEventType::ConnectionAdded);
    EXPECT_EQ(graph_.connections().size(), 1u);
}

TEST_F(GraphOpsTest, ConnectTypeMismatch) {
    auto t = ops_.create_node("TextureParam");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t tid = t.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    auto r = ops_.connect_pins(tid, 0, oid, 0);  // Texture2D -> Float3.
    EXPECT_FALSE(r.ok);
}

TEST_F(GraphOpsTest, ConnectInvalidPins) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    EXPECT_FALSE(ops_.connect_pins(cid, 99, oid, 0).ok);
    EXPECT_FALSE(ops_.connect_pins(cid, 0, oid, 99).ok);
    EXPECT_FALSE(ops_.connect_pins(999, 0, oid, 0).ok);
}

// ============================================================================
// disconnect_pin
// ============================================================================

TEST_F(GraphOpsTest, DisconnectValid) {
    auto c = ops_.create_node("Constant3");
    auto o = ops_.create_node("MaterialOutput");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    ops_.connect_pins(cid, 0, oid, 0);
    events_.clear();

    auto r = ops_.disconnect_pin(oid, 0);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(graph_.connections().empty());
    EXPECT_EQ(events_[0].type, GraphEventType::ConnectionRemoved);
}

TEST_F(GraphOpsTest, DisconnectNoConnection) {
    auto o = ops_.create_node("MaterialOutput");
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    auto r = ops_.disconnect_pin(oid, 0);
    EXPECT_FALSE(r.ok);
}

// ============================================================================
// set_param
// ============================================================================

TEST_F(GraphOpsTest, SetParamValid) {
    auto r = ops_.create_node("Constant1");
    uint32_t id = r.data["nodeId"].get<uint32_t>();
    events_.clear();

    auto sr = ops_.set_param(id, "value", 3.14);
    EXPECT_TRUE(sr.ok);
    EXPECT_EQ(events_.size(), 1u);
    EXPECT_EQ(events_[0].type, GraphEventType::ParamChanged);
    EXPECT_EQ(events_[0].key, "value");

    auto* node = static_cast<Constant1Node*>(graph_.get_node(id));
    EXPECT_FLOAT_EQ(node->value, 3.14f);
}

TEST_F(GraphOpsTest, SetParamNodeNotFound) {
    auto r = ops_.set_param(999, "value", 1.0);
    EXPECT_FALSE(r.ok);
}

// ============================================================================
// Event system
// ============================================================================

TEST_F(GraphOpsTest, NoListenerDoesNotCrash) {
    GraphOps ops2(graph_);
    auto r = ops2.create_node("Add");
    EXPECT_TRUE(r.ok);  // Should not crash without listener.
}

TEST_F(GraphOpsTest, EventDataContainsNodeId) {
    auto r = ops_.create_node("Add");
    EXPECT_EQ(events_[0].node_id, r.data["nodeId"].get<uint32_t>());
}
