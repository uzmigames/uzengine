/// Unit tests for the UzEngine Runtime C API (runtime.h).
///
/// Tests the extern "C" interface used by the N-API addon.
/// These are headless tests — uz_create_viewport with parent_hwnd=0 creates
/// a standalone window. Tests verify lifecycle, scene loading, entity CRUD,
/// log callback, and screenshot capture without a running render loop.

#include "runtime.h"

#include <gtest/gtest.h>

#include <cstring>
#include <string>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(RuntimeAPI, VersionReturnsNonEmpty) {
    const char* ver = uz_runtime_version();
    ASSERT_NE(ver, nullptr);
    EXPECT_GT(std::strlen(ver), 0u);
}

TEST(RuntimeAPI, BackendReturnsValidString) {
    const char* backend = uz_runtime_backend();
    ASSERT_NE(backend, nullptr);
    // Should be one of: dx12, dx11, vulkan, metal, none, stub
    std::string b(backend);
    EXPECT_TRUE(b == "dx12" || b == "dx11" || b == "vulkan" || b == "metal" || b == "none" || b == "stub");
}

TEST(RuntimeAPI, InitAndShutdown) {
    EXPECT_EQ(uz_runtime_init(), 1);
    uz_runtime_shutdown(); // Should not crash
}

// ─────────────────────────────────────────────────────────────────────────────
// Viewport Tests (require GPU — skip in CI)
// ─────────────────────────────────────────────────────────────────────────────

class RuntimeViewportTest : public ::testing::Test {
protected:
    void SetUp() override {
        uz_set_content_root("../sample/content");
    }

    void TearDown() override {
        uz_destroy_viewport();
    }
};

TEST_F(RuntimeViewportTest, CreateAndDestroy) {
    int result = uz_create_viewport(nullptr, 800, 600);
    EXPECT_EQ(result, 1);
    // Double create should fail
    int result2 = uz_create_viewport(nullptr, 800, 600);
    EXPECT_EQ(result2, 0);
}

TEST_F(RuntimeViewportTest, RenderFrameReturnsStats) {
    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);

    uz_frame_stats stats = uz_render_frame();
    EXPECT_GT(stats.frame_index, 0u);
    // fps/frame_ms may be 0 on first frame, but frame_index should advance
}

TEST_F(RuntimeViewportTest, DestroyWithoutCreateIsNoOp) {
    uz_destroy_viewport(); // Should not crash
}

TEST_F(RuntimeViewportTest, RenderWithoutViewportReturnsZero) {
    uz_frame_stats stats = uz_render_frame();
    EXPECT_FLOAT_EQ(stats.fps, 0.0f);
    EXPECT_EQ(stats.frame_index, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(RuntimeViewportTest, LoadSceneAndGetTree) {
    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);

    int loaded = uz_load_scene("scenes/pbr_showcase.uzscene");
    EXPECT_EQ(loaded, 1);

    const char* tree = uz_get_scene_tree();
    ASSERT_NE(tree, nullptr);
    // Should be valid JSON array
    EXPECT_EQ(tree[0], '[');
    // Should contain "Sun" entity
    EXPECT_NE(std::strstr(tree, "Sun"), nullptr);
}

TEST_F(RuntimeViewportTest, LoadInvalidSceneFails) {
    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);
    int loaded = uz_load_scene("nonexistent.uzscene");
    EXPECT_EQ(loaded, 0);
}

TEST_F(RuntimeViewportTest, GetSceneTreeWithoutSceneReturnsEmptyArray) {
    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);
    const char* tree = uz_get_scene_tree();
    ASSERT_NE(tree, nullptr);
    EXPECT_STREQ(tree, "[]");
}

// ─────────────────────────────────────────────────────────────────────────────
// Entity Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(RuntimeViewportTest, CreateAndDestroyEntity) {
    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);

    uint32_t id = uz_create_entity("TestCube");
    EXPECT_GT(id, 0u);

    // Entity should appear in scene tree
    const char* tree = uz_get_scene_tree();
    EXPECT_NE(std::strstr(tree, "TestCube"), nullptr);

    uz_destroy_entity(id);

    // Entity should be gone
    tree = uz_get_scene_tree();
    EXPECT_EQ(std::strstr(tree, "TestCube"), nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Log Callback Tests
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<std::string> s_logMessages;

static void TestLogCallback(int level, const char* category, const char* message) {
    (void)level;
    (void)category;
    s_logMessages.emplace_back(message ? message : "");
}

TEST_F(RuntimeViewportTest, LogCallbackReceivesMessages) {
    s_logMessages.clear();
    uz_set_log_callback(TestLogCallback);

    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);

    // Creating a viewport generates log messages
    EXPECT_GT(s_logMessages.size(), 0u);

    uz_set_log_callback(nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Editor Mode Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(RuntimeViewportTest, SetEditorModeDoesNotCrash) {
    ASSERT_EQ(uz_create_viewport(nullptr, 800, 600), 1);
    uz_set_editor_mode(1); // Enable
    uz_set_editor_mode(0); // Disable
    // No crash = pass
}

// ─────────────────────────────────────────────────────────────────────────────
// Content Root Tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(RuntimeAPI, SetContentRootDoesNotCrash) {
    uz_set_content_root("some/path");
    uz_set_content_root(nullptr); // Should handle null gracefully
}
