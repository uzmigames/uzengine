// test_command_executor.cpp -- Tests for CommandExecutor.
//
// Covers:
//   - Single command dispatch for each CommandType
//   - Invalid command types
//   - JSON parsing
//   - Query commands

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/commands/command_executor.h"
#include "engine/renderer/shader_graph/commands/graph_command.h"

using namespace uz::sg;

class CommandExecutorTest : public ::testing::Test {
protected:
    ShaderGraph graph_;
    GraphOps ops_{graph_};
    CommandExecutor exec_{ops_};
};

// ============================================================================
// Command type parsing
// ============================================================================

TEST(GraphCommandTest, TypeFromString) {
    EXPECT_EQ(command_type_from_string("ADD_NODE"), CommandType::ADD_NODE);
    EXPECT_EQ(command_type_from_string("DELETE_NODE"), CommandType::DELETE_NODE);
    EXPECT_EQ(command_type_from_string("CONNECT"), CommandType::CONNECT);
    EXPECT_EQ(command_type_from_string("DISCONNECT"), CommandType::DISCONNECT);
    EXPECT_EQ(command_type_from_string("SET_PARAM"), CommandType::SET_PARAM);
    EXPECT_EQ(command_type_from_string("INSERT_BETWEEN"), CommandType::INSERT_BETWEEN);
    EXPECT_EQ(command_type_from_string("REPLACE_NODE"), CommandType::REPLACE_NODE);
    EXPECT_EQ(command_type_from_string("EXPOSE_PARAM"), CommandType::EXPOSE_PARAM);
    EXPECT_EQ(command_type_from_string("COMPILE"), CommandType::COMPILE);
    EXPECT_EQ(command_type_from_string("QUERY"), CommandType::QUERY);
    EXPECT_EQ(command_type_from_string("BATCH"), CommandType::BATCH);
}

TEST(GraphCommandTest, TypeToString) {
    EXPECT_STREQ(command_type_to_string(CommandType::ADD_NODE), "ADD_NODE");
    EXPECT_STREQ(command_type_to_string(CommandType::BATCH), "BATCH");
}

TEST(GraphCommandTest, ParseCommand) {
    nlohmann::json j = {
        {"type", "ADD_NODE"},
        {"params", {{"nodeType", "Multiply"}, {"x", 100}, {"y", 200}}}
    };
    auto cmd = parse_command(j);
    EXPECT_EQ(cmd.type, CommandType::ADD_NODE);
    EXPECT_EQ(cmd.params["nodeType"], "Multiply");
}

TEST(GraphCommandTest, ParseBatchCommand) {
    nlohmann::json j = {
        {"type", "BATCH"},
        {"commands", {
            {{"type", "ADD_NODE"}, {"params", {{"nodeType", "Add"}}}},
            {{"type", "ADD_NODE"}, {"params", {{"nodeType", "Multiply"}}}}
        }}
    };
    auto cmd = parse_command(j);
    EXPECT_EQ(cmd.type, CommandType::BATCH);
    EXPECT_EQ(cmd.sub_commands.size(), 2u);
}

// ============================================================================
// Single command execution
// ============================================================================

TEST_F(CommandExecutorTest, AddNode) {
    GraphCommand cmd{CommandType::ADD_NODE, {{"nodeType", "Constant3"}}};
    auto r = exec_.execute(cmd);
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.data.contains("nodeId"));
    EXPECT_EQ(graph_.nodes().size(), 1u);
    EXPECT_FALSE(r.events.empty());
}

TEST_F(CommandExecutorTest, AddNodeInvalid) {
    GraphCommand cmd{CommandType::ADD_NODE, {{"nodeType", "NoSuchNode"}}};
    auto r = exec_.execute(cmd);
    EXPECT_FALSE(r.ok);
}

TEST_F(CommandExecutorTest, DeleteNode) {
    auto add_r = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Add"}}});
    uint32_t id = add_r.data["nodeId"].get<uint32_t>();

    auto r = exec_.execute({CommandType::DELETE_NODE, {{"nodeId", id}}});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(graph_.nodes().empty());
}

TEST_F(CommandExecutorTest, Connect) {
    auto c = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Constant3"}}});
    auto o = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "MaterialOutput"}}});
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    auto r = exec_.execute({CommandType::CONNECT,
        {{"from", cid}, {"fromOutput", 0}, {"to", oid}, {"toInput", 0}}});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(graph_.connections().size(), 1u);
}

TEST_F(CommandExecutorTest, Disconnect) {
    auto c = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Constant3"}}});
    auto o = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "MaterialOutput"}}});
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    exec_.execute({CommandType::CONNECT,
        {{"from", cid}, {"fromOutput", 0}, {"to", oid}, {"toInput", 0}}});

    auto r = exec_.execute({CommandType::DISCONNECT, {{"nodeId", oid}, {"inputIndex", 0}}});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(graph_.connections().empty());
}

TEST_F(CommandExecutorTest, SetParam) {
    auto c = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Constant1"}}});
    uint32_t id = c.data["nodeId"].get<uint32_t>();

    auto r = exec_.execute({CommandType::SET_PARAM,
        {{"nodeId", id}, {"key", "value"}, {"value", 2.5}}});
    EXPECT_TRUE(r.ok);
}

TEST_F(CommandExecutorTest, InsertBetween) {
    auto c = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Constant3"}}});
    auto o = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "MaterialOutput"}}});
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    exec_.execute({CommandType::CONNECT,
        {{"from", cid}, {"fromOutput", 0}, {"to", oid}, {"toInput", 0}}});

    auto r = exec_.execute({CommandType::INSERT_BETWEEN,
        {{"from", cid}, {"fromOutput", 0}, {"to", oid}, {"toInput", 0}, {"nodeType", "Multiply"}}});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(graph_.connections().size(), 2u);
}

TEST_F(CommandExecutorTest, ReplaceNode) {
    auto a = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Add"}}});
    uint32_t aid = a.data["nodeId"].get<uint32_t>();

    auto r = exec_.execute({CommandType::REPLACE_NODE,
        {{"nodeId", aid}, {"newType", "Multiply"}}});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(graph_.get_node(aid), nullptr);
}

TEST_F(CommandExecutorTest, ExposeParam) {
    auto o = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "MaterialOutput"}}});
    uint32_t oid = o.data["nodeId"].get<uint32_t>();

    auto r = exec_.execute({CommandType::EXPOSE_PARAM,
        {{"nodeId", oid}, {"inputIndex", 1}, {"paramName", "Metallic"}}});
    EXPECT_TRUE(r.ok);
}

TEST_F(CommandExecutorTest, QueryGraph) {
    exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Add"}}});
    auto r = exec_.execute({CommandType::QUERY, {{"query", "graph"}}});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["nodeCount"], 1u);
}

TEST_F(CommandExecutorTest, QueryNode) {
    auto a = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Add"}}});
    uint32_t id = a.data["nodeId"].get<uint32_t>();
    auto r = exec_.execute({CommandType::QUERY, {{"query", "node"}, {"nodeId", id}}});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["type"], "Add");
}

TEST_F(CommandExecutorTest, QueryFindNodes) {
    exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Add"}}});
    exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Add"}}});
    auto r = exec_.execute({CommandType::QUERY, {{"query", "findNodes"}, {"nodeType", "Add"}}});
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(r.data["nodeIds"].size(), 2u);
}

TEST_F(CommandExecutorTest, QueryNodeTypes) {
    auto r = exec_.execute({CommandType::QUERY, {{"query", "nodeTypes"}}});
    EXPECT_TRUE(r.ok);
    EXPECT_GE(r.data["types"].size(), 20u);
}

TEST_F(CommandExecutorTest, QueryConnection) {
    auto c = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "Constant3"}}});
    auto o = exec_.execute({CommandType::ADD_NODE, {{"nodeType", "MaterialOutput"}}});
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = o.data["nodeId"].get<uint32_t>();
    exec_.execute({CommandType::CONNECT,
        {{"from", cid}, {"fromOutput", 0}, {"to", oid}, {"toInput", 0}}});

    auto r = exec_.execute({CommandType::QUERY, {{"query", "connection"}, {"nodeId", oid}, {"inputIndex", 0}}});
    EXPECT_TRUE(r.ok);
    EXPECT_TRUE(r.data["connected"].get<bool>());
}

TEST_F(CommandExecutorTest, QueryUnknownType) {
    auto r = exec_.execute({CommandType::QUERY, {{"query", "bogus"}}});
    EXPECT_FALSE(r.ok);
}

TEST_F(CommandExecutorTest, CompileNoOutput) {
    auto r = exec_.execute({CommandType::COMPILE, {}});
    EXPECT_FALSE(r.ok);
    EXPECT_TRUE(r.data.contains("errors"));
}

// ============================================================================
// JSON-based execution
// ============================================================================

TEST_F(CommandExecutorTest, ExecuteJson) {
    nlohmann::json j = {
        {"type", "ADD_NODE"},
        {"params", {{"nodeType", "Multiply"}}}
    };
    auto r = exec_.execute_json(j);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(graph_.nodes().size(), 1u);
}

TEST_F(CommandExecutorTest, ExecuteJsonBatch) {
    nlohmann::json j = {
        {"type", "BATCH"},
        {"commands", {
            {{"type", "ADD_NODE"}, {"params", {{"nodeType", "Constant3"}}}},
            {{"type", "ADD_NODE"}, {"params", {{"nodeType", "MaterialOutput"}}}}
        }}
    };
    auto r = exec_.execute_json(j);
    EXPECT_TRUE(r.ok);
    EXPECT_EQ(graph_.nodes().size(), 2u);
}
