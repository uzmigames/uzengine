/// ShaderHotReload Tests
///
/// Dirty flag management tests require no filesystem.
/// FileWatcher tests use a temporary directory that is cleaned up after each test.
/// Integration tests combine both subsystems.

#include "shader_hot_reload.h"
#include "shader_dependency_graph.h"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>

namespace uz {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Dirty Flag Management Tests (no filesystem needed)
// ─────────────────────────────────────────────────────────────────────────────

class ShaderHotReloadDirtyTest : public ::testing::Test {
protected:
    ShaderHotReload m_reload;
};

TEST_F(ShaderHotReloadDirtyTest, MarkDirtyAddsToSet) {
    m_reload.MarkDirty("/shaders/test.hlsl");
    EXPECT_EQ(m_reload.GetDirtyCount(), 1u);
}

TEST_F(ShaderHotReloadDirtyTest, IsDirtyReturnsFalseForUnknown) {
    EXPECT_FALSE(m_reload.IsDirty("/shaders/unknown.hlsl"));
}

TEST_F(ShaderHotReloadDirtyTest, IsDirtyReturnsTrueAfterMark) {
    m_reload.MarkDirty("/shaders/test.hlsl");
    EXPECT_TRUE(m_reload.IsDirty("/shaders/test.hlsl"));
}

TEST_F(ShaderHotReloadDirtyTest, ClearDirtyRemovesFromSet) {
    m_reload.MarkDirty("/shaders/test.hlsl");
    m_reload.ClearDirty("/shaders/test.hlsl");
    EXPECT_FALSE(m_reload.IsDirty("/shaders/test.hlsl"));
    EXPECT_EQ(m_reload.GetDirtyCount(), 0u);
}

TEST_F(ShaderHotReloadDirtyTest, ClearAllEmptiesSet) {
    m_reload.MarkDirty("/shaders/a.hlsl");
    m_reload.MarkDirty("/shaders/b.hlsl");
    m_reload.MarkDirty("/shaders/c.hlsl");
    m_reload.ClearAll();
    EXPECT_EQ(m_reload.GetDirtyCount(), 0u);
    EXPECT_FALSE(m_reload.IsDirty("/shaders/a.hlsl"));
}

TEST_F(ShaderHotReloadDirtyTest, GetDirtyShadersReturnsAllPaths) {
    m_reload.MarkDirty("/shaders/b.hlsl");
    m_reload.MarkDirty("/shaders/a.hlsl");
    auto dirty = m_reload.GetDirtyShaders();
    ASSERT_EQ(dirty.size(), 2u);
    // GetDirtyShaders returns sorted
    EXPECT_EQ(dirty[0], "/shaders/a.hlsl");
    EXPECT_EQ(dirty[1], "/shaders/b.hlsl");
}

TEST_F(ShaderHotReloadDirtyTest, GetDirtyCountCorrectness) {
    EXPECT_EQ(m_reload.GetDirtyCount(), 0u);
    m_reload.MarkDirty("/shaders/a.hlsl");
    EXPECT_EQ(m_reload.GetDirtyCount(), 1u);
    m_reload.MarkDirty("/shaders/b.hlsl");
    EXPECT_EQ(m_reload.GetDirtyCount(), 2u);
    m_reload.ClearDirty("/shaders/a.hlsl");
    EXPECT_EQ(m_reload.GetDirtyCount(), 1u);
}

TEST_F(ShaderHotReloadDirtyTest, MarkDirtyWithDepGraphPropagates) {
    ShaderDependencyGraph graph;
    // pbr.hlsl includes common.hlsli; lighting.hlsl includes common.hlsli
    graph.SetDependencies("/shaders/pbr.hlsl", {"/shaders/common.hlsli"});
    graph.SetDependencies("/shaders/lighting.hlsl", {"/shaders/common.hlsli"});

    m_reload.SetDependencyGraph(&graph);
    m_reload.MarkDirty("/shaders/common.hlsli");

    EXPECT_TRUE(m_reload.IsDirty("/shaders/common.hlsli"));
    EXPECT_TRUE(m_reload.IsDirty("/shaders/pbr.hlsl"));
    EXPECT_TRUE(m_reload.IsDirty("/shaders/lighting.hlsl"));
    EXPECT_EQ(m_reload.GetDirtyCount(), 3u);
}

TEST_F(ShaderHotReloadDirtyTest, MarkDirtyWithoutDepGraphOnlyMarksSelf) {
    // No dep graph set — transitive propagation disabled
    m_reload.MarkDirty("/shaders/common.hlsli");
    EXPECT_TRUE(m_reload.IsDirty("/shaders/common.hlsli"));
    EXPECT_EQ(m_reload.GetDirtyCount(), 1u);
}

TEST_F(ShaderHotReloadDirtyTest, DoubleMarkDirtyIsIdempotent) {
    m_reload.MarkDirty("/shaders/test.hlsl");
    m_reload.MarkDirty("/shaders/test.hlsl");
    EXPECT_EQ(m_reload.GetDirtyCount(), 1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// FileWatcher Tests (use temp directory)
// ─────────────────────────────────────────────────────────────────────────────

class FileWatcherTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "uz_test_filewatcher";
        // Clean up stale leftovers from a previous aborted run
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
        std::filesystem::create_directories(m_tempDir);
    }

    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }

    void WriteFile(const std::string& name, const std::string& content) {
        std::ofstream ofs(m_tempDir / name);
        ofs << content;
        ofs.close();
    }

    /// Touch a file by rewriting it with new content after a brief delay.
    /// The delay ensures the filesystem timestamp actually changes.
    void TouchFile(const std::string& name, const std::string& content) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ofstream ofs(m_tempDir / name);
        ofs << content;
        ofs.close();
    }

    std::filesystem::path m_tempDir;
    FileWatcher m_watcher;
};

TEST_F(FileWatcherTest, WatchOnNonExistentDirHandledGracefully) {
    m_watcher.Watch(std::filesystem::path("Z:/nonexistent/dir/12345"));
    EXPECT_FALSE(m_watcher.IsWatching());
}

TEST_F(FileWatcherTest, PollChangesReturnsEmptyOnNoChanges) {
    WriteFile("test.hlsl", "// shader");
    m_watcher.Watch(m_tempDir);
    auto changes = m_watcher.PollChanges();
    EXPECT_TRUE(changes.empty());
}

TEST_F(FileWatcherTest, PollChangesDetectsModifiedFile) {
    WriteFile("test.hlsl", "// v1");
    m_watcher.Watch(m_tempDir);

    TouchFile("test.hlsl", "// v2");

    auto changes = m_watcher.PollChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_NE(changes[0].find("test.hlsl"), std::string::npos);
}

TEST_F(FileWatcherTest, PollChangesDetectsNewFile) {
    WriteFile("existing.hlsl", "// existing");
    m_watcher.Watch(m_tempDir);

    WriteFile("new_shader.hlsl", "// new");

    auto changes = m_watcher.PollChanges();
    ASSERT_EQ(changes.size(), 1u);
    EXPECT_NE(changes[0].find("new_shader.hlsl"), std::string::npos);
}

TEST_F(FileWatcherTest, StopAllClearsWatchingState) {
    WriteFile("test.hlsl", "// shader");
    m_watcher.Watch(m_tempDir);
    EXPECT_TRUE(m_watcher.IsWatching());

    m_watcher.StopAll();
    EXPECT_FALSE(m_watcher.IsWatching());
}

TEST_F(FileWatcherTest, IsWatchingStateTracking) {
    EXPECT_FALSE(m_watcher.IsWatching());
    m_watcher.Watch(m_tempDir);
    EXPECT_TRUE(m_watcher.IsWatching());
    m_watcher.StopAll();
    EXPECT_FALSE(m_watcher.IsWatching());
}

TEST_F(FileWatcherTest, IgnoresNonShaderExtensions) {
    WriteFile("readme.txt", "not a shader");
    m_watcher.Watch(m_tempDir);

    WriteFile("readme.txt", "modified");

    auto changes = m_watcher.PollChanges();
    EXPECT_TRUE(changes.empty());
}

TEST_F(FileWatcherTest, WatchesAllShaderExtensions) {
    WriteFile("a.hlsl", "// hlsl");
    WriteFile("b.hlsli", "// hlsli");
    WriteFile("c.fx", "// fx");
    WriteFile("d.fxh", "// fxh");
    WriteFile("e.h", "// h");
    m_watcher.Watch(m_tempDir);

    // Touch all files
    TouchFile("a.hlsl", "// hlsl v2");
    TouchFile("b.hlsli", "// hlsli v2");
    TouchFile("c.fx", "// fx v2");
    TouchFile("d.fxh", "// fxh v2");
    TouchFile("e.h", "// h v2");

    auto changes = m_watcher.PollChanges();
    EXPECT_EQ(changes.size(), 5u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration Tests
// ─────────────────────────────────────────────────────────────────────────────

class ShaderHotReloadIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_tempDir = std::filesystem::temp_directory_path() / "uz_test_hotreload";
        // Clean up stale leftovers from a previous aborted run
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
        std::filesystem::create_directories(m_tempDir);
    }

    void TearDown() override {
        m_reload.Stop();
        std::error_code ec;
        std::filesystem::remove_all(m_tempDir, ec);
    }

    void WriteFile(const std::string& name, const std::string& content) {
        std::ofstream ofs(m_tempDir / name);
        ofs << content;
        ofs.close();
    }

    void TouchFile(const std::string& name, const std::string& content) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        std::ofstream ofs(m_tempDir / name);
        ofs << content;
        ofs.close();
    }

    std::filesystem::path m_tempDir;
    ShaderHotReload m_reload;
};

TEST_F(ShaderHotReloadIntegrationTest, StartPollDetectsDirtyShader) {
    WriteFile("test.hlsl", "// v1");
    m_reload.Start({m_tempDir});
    EXPECT_TRUE(m_reload.IsRunning());

    TouchFile("test.hlsl", "// v2");

    auto dirty = m_reload.Poll();
    ASSERT_EQ(dirty.size(), 1u);
    EXPECT_NE(dirty[0].find("test.hlsl"), std::string::npos);
    EXPECT_TRUE(m_reload.IsDirty(dirty[0]));
}

TEST_F(ShaderHotReloadIntegrationTest, StopSetsIsRunningFalse) {
    m_reload.Start({m_tempDir});
    EXPECT_TRUE(m_reload.IsRunning());
    m_reload.Stop();
    EXPECT_FALSE(m_reload.IsRunning());
}

TEST_F(ShaderHotReloadIntegrationTest, PollReturnsEmptyWhenNotRunning) {
    auto dirty = m_reload.Poll();
    EXPECT_TRUE(dirty.empty());
}

TEST_F(ShaderHotReloadIntegrationTest, PollWithDepGraphPropagates) {
    WriteFile("common.hlsli", "// common v1");
    WriteFile("pbr.hlsl", "// pbr");

    ShaderDependencyGraph graph;
    // Use normalized paths matching what FileWatcher will produce
    auto commonNorm = (m_tempDir / "common.hlsli").generic_string();
    auto pbrNorm = (m_tempDir / "pbr.hlsl").generic_string();
#ifdef _WIN32
    std::transform(commonNorm.begin(), commonNorm.end(), commonNorm.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::transform(pbrNorm.begin(), pbrNorm.end(), pbrNorm.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    graph.SetDependencies(pbrNorm, {commonNorm});

    m_reload.SetDependencyGraph(&graph);
    m_reload.Start({m_tempDir});

    TouchFile("common.hlsli", "// common v2");

    m_reload.Poll();
    EXPECT_TRUE(m_reload.IsDirty(commonNorm));
    EXPECT_TRUE(m_reload.IsDirty(pbrNorm));
}

} // namespace
} // namespace uz
