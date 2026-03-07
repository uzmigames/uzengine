// test_shader_graph_manager.cpp -- Tests for ShaderGraphManager.
//
// Tests load, save, create_new, get, has, unload, reload, list operations
// using temporary files for file I/O tests.

#include <gtest/gtest.h>
#include "engine/renderer/shader_graph/shader_graph_manager.h"
#include "engine/core/platform.h"

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace uz::sg;

namespace {

// Helper: write a string to a temp file and return the path.
std::string write_temp_file(const std::string& content, const std::string& suffix = ".uzshader") {
    auto tmp = std::filesystem::temp_directory_path() / ("uztest_sgm_" + std::to_string(rand()) + suffix);
    std::ofstream ofs(tmp, std::ios::binary);
    ofs.write(content.data(), content.size());
    ofs.close();
    return tmp.string();
}

// Helper: create a valid .uzshader JSON string with a MaterialOutput node.
std::string make_valid_uzshader() {
    ShaderGraph graph;
    GraphOps ops(graph);
    ops.create_node("MaterialOutput", 400.f, 200.f);
    auto j = graph.serialize();
    return j.dump(2);
}

} // namespace

class ShaderGraphManagerTest : public ::testing::Test {
protected:
    ShaderGraphManager mgr_;
    std::vector<std::string> temp_files_;

    void TearDown() override {
        for (auto& f : temp_files_) {
            std::filesystem::remove(f);
        }
    }

    std::string create_temp(const std::string& content, const std::string& suffix = ".uzshader") {
        auto path = write_temp_file(content, suffix);
        temp_files_.push_back(path);
        return path;
    }
};

// ============================================================================
// create_new
// ============================================================================

TEST_F(ShaderGraphManagerTest, CreateNew) {
    auto path = mgr_.create_new("test_graph.uzshader");
    EXPECT_EQ(path, "test_graph.uzshader");
    EXPECT_TRUE(mgr_.has(path));

    auto* mg = mgr_.get(path);
    ASSERT_NE(mg, nullptr);
    EXPECT_EQ(mg->file_path, path);
    EXPECT_TRUE(mg->dirty);
    EXPECT_NE(mg->graph, nullptr);
    EXPECT_NE(mg->ops, nullptr);

    // Should have a MaterialOutput node.
    EXPECT_EQ(mg->graph->nodes().size(), 1u);
    EXPECT_EQ(mg->graph->nodes()[0]->type_name, "MaterialOutput");
}

TEST_F(ShaderGraphManagerTest, CreateNewOverwritesExisting) {
    mgr_.create_new("path.uzshader");
    auto* mg1 = mgr_.get("path.uzshader");
    auto* node1 = mg1->graph->nodes()[0].get();

    mgr_.create_new("path.uzshader");
    auto* mg2 = mgr_.get("path.uzshader");
    // Should be a fresh graph (different node pointer).
    EXPECT_NE(mg2->graph->nodes()[0].get(), node1);
}

// ============================================================================
// get / has
// ============================================================================

TEST_F(ShaderGraphManagerTest, GetNonExistent) {
    EXPECT_EQ(mgr_.get("nonexistent.uzshader"), nullptr);
}

TEST_F(ShaderGraphManagerTest, HasNonExistent) {
    EXPECT_FALSE(mgr_.has("nonexistent.uzshader"));
}

// ============================================================================
// unload
// ============================================================================

TEST_F(ShaderGraphManagerTest, Unload) {
    mgr_.create_new("a.uzshader");
    EXPECT_TRUE(mgr_.has("a.uzshader"));
    mgr_.unload("a.uzshader");
    EXPECT_FALSE(mgr_.has("a.uzshader"));
    EXPECT_EQ(mgr_.get("a.uzshader"), nullptr);
}

TEST_F(ShaderGraphManagerTest, UnloadNonExistent) {
    // Should not crash.
    mgr_.unload("does_not_exist.uzshader");
    EXPECT_FALSE(mgr_.has("does_not_exist.uzshader"));
}

// ============================================================================
// list
// ============================================================================

TEST_F(ShaderGraphManagerTest, ListEmpty) {
    EXPECT_TRUE(mgr_.list().empty());
}

TEST_F(ShaderGraphManagerTest, ListMultiple) {
    mgr_.create_new("a.uzshader");
    mgr_.create_new("b.uzshader");
    mgr_.create_new("c.uzshader");
    auto paths = mgr_.list();
    EXPECT_EQ(paths.size(), 3u);

    // All three should be present (order not guaranteed).
    std::sort(paths.begin(), paths.end());
    EXPECT_EQ(paths[0], "a.uzshader");
    EXPECT_EQ(paths[1], "b.uzshader");
    EXPECT_EQ(paths[2], "c.uzshader");
}

TEST_F(ShaderGraphManagerTest, ListAfterUnload) {
    mgr_.create_new("a.uzshader");
    mgr_.create_new("b.uzshader");
    mgr_.unload("a.uzshader");
    auto paths = mgr_.list();
    EXPECT_EQ(paths.size(), 1u);
    EXPECT_EQ(paths[0], "b.uzshader");
}

// ============================================================================
// save + load (file I/O round-trip)
// ============================================================================

TEST_F(ShaderGraphManagerTest, SaveAndLoad) {
    auto path = create_temp("", ".uzshader");

    // Create and save.
    mgr_.create_new(path);
    auto* mg = mgr_.get(path);
    EXPECT_TRUE(mg->dirty);
    EXPECT_TRUE(mgr_.save(path));

    mg = mgr_.get(path);
    EXPECT_FALSE(mg->dirty);

    // Unload and reload from disk.
    mgr_.unload(path);
    EXPECT_FALSE(mgr_.has(path));

    auto loaded = mgr_.load(path);
    EXPECT_EQ(loaded, path);
    EXPECT_TRUE(mgr_.has(path));

    auto* mg2 = mgr_.get(path);
    ASSERT_NE(mg2, nullptr);
    EXPECT_EQ(mg2->graph->nodes().size(), 1u);
    EXPECT_EQ(mg2->graph->nodes()[0]->type_name, "MaterialOutput");
    EXPECT_FALSE(mg2->dirty);
}

TEST_F(ShaderGraphManagerTest, SaveNonExistent) {
    EXPECT_FALSE(mgr_.save("nonexistent_graph.uzshader"));
}

TEST_F(ShaderGraphManagerTest, LoadValidFile) {
    auto content = make_valid_uzshader();
    auto path = create_temp(content);

    auto loaded = mgr_.load(path);
    EXPECT_EQ(loaded, path);
    auto* mg = mgr_.get(path);
    ASSERT_NE(mg, nullptr);
    EXPECT_EQ(mg->graph->nodes().size(), 1u);
}

TEST_F(ShaderGraphManagerTest, LoadNonExistentFile) {
    auto result = mgr_.load("C:/nonexistent/path/graph.uzshader");
    EXPECT_TRUE(result.empty());
}

TEST_F(ShaderGraphManagerTest, LoadInvalidJson) {
    auto path = create_temp("{ this is not valid json }}}");
    auto result = mgr_.load(path);
    EXPECT_TRUE(result.empty());
    EXPECT_FALSE(mgr_.has(path));
}

TEST_F(ShaderGraphManagerTest, LoadEmptyFile) {
    auto path = create_temp("");
    auto result = mgr_.load(path);
    EXPECT_TRUE(result.empty());
}

TEST_F(ShaderGraphManagerTest, LoadJsonWithNoNodes) {
    // Valid JSON but no nodes array — deserialize succeeds with empty graph.
    auto path = create_temp("{\"version\": 1}");
    auto result = mgr_.load(path);
    EXPECT_EQ(result, path);
    auto* mg = mgr_.get(path);
    ASSERT_NE(mg, nullptr);
    EXPECT_TRUE(mg->graph->nodes().empty());
}

// ============================================================================
// reload
// ============================================================================

TEST_F(ShaderGraphManagerTest, ReloadFromDisk) {
    auto content = make_valid_uzshader();
    auto path = create_temp(content);

    // Load initial.
    mgr_.load(path);
    EXPECT_TRUE(mgr_.has(path));

    // Reload should succeed (re-reads same file).
    EXPECT_TRUE(mgr_.reload(path));
    EXPECT_TRUE(mgr_.has(path));
    EXPECT_EQ(mgr_.get(path)->graph->nodes().size(), 1u);
}

TEST_F(ShaderGraphManagerTest, ReloadNonExistentFile) {
    // First create in-memory, then try to reload from non-existent disk path.
    mgr_.create_new("inmemory.uzshader");
    EXPECT_FALSE(mgr_.reload("inmemory.uzshader"));
    EXPECT_FALSE(mgr_.has("inmemory.uzshader"));
}

// ============================================================================
// dirty flag behavior
// ============================================================================

TEST_F(ShaderGraphManagerTest, DirtyAfterMutation) {
    auto content = make_valid_uzshader();
    auto path = create_temp(content);
    mgr_.load(path);

    auto* mg = mgr_.get(path);
    EXPECT_FALSE(mg->dirty);

    // Mutate the graph.
    mg->ops->create_node("Constant3");
    mg->dirty = true;  // Caller sets dirty.
    EXPECT_TRUE(mg->dirty);
}

TEST_F(ShaderGraphManagerTest, SaveClearsDirty) {
    auto path = create_temp("");
    mgr_.create_new(path);
    EXPECT_TRUE(mgr_.get(path)->dirty);
    EXPECT_TRUE(mgr_.save(path));
    EXPECT_FALSE(mgr_.get(path)->dirty);
}

// ============================================================================
// Graph content after save/load round-trip with multiple nodes
// ============================================================================

TEST_F(ShaderGraphManagerTest, RoundTripWithConnections) {
    auto path = create_temp("");

    mgr_.create_new(path);
    auto* mg = mgr_.get(path);

    // Add nodes and connect them.
    auto c = mg->ops->create_node("Constant3");
    uint32_t cid = c.data["nodeId"].get<uint32_t>();
    uint32_t oid = mg->graph->nodes()[0]->id;  // MaterialOutput.
    mg->ops->connect_pins(cid, 0, oid, 0);  // Constant3 → BaseColor.

    EXPECT_EQ(mg->graph->nodes().size(), 2u);
    EXPECT_EQ(mg->graph->connections().size(), 1u);

    // Save.
    EXPECT_TRUE(mgr_.save(path));

    // Reload.
    mgr_.unload(path);
    mgr_.load(path);

    auto* mg2 = mgr_.get(path);
    ASSERT_NE(mg2, nullptr);
    EXPECT_EQ(mg2->graph->nodes().size(), 2u);
    EXPECT_EQ(mg2->graph->connections().size(), 1u);
}
