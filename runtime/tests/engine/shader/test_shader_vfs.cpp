/// Tests for ShaderVFS — virtual filesystem for shader source files.

#include "shader_vfs.h"
#include <gtest/gtest.h>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — creates temporary shader files on disk
// ─────────────────────────────────────────────────────────────────────────────

class ShaderVFSTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_testDir = "test_shader_vfs_tmp";
        fs::create_directories(m_testDir + "/engine");
        fs::create_directories(m_testDir + "/game");

        WriteFile(m_testDir + "/engine/common.hlsli", "// common header\n");
        WriteFile(m_testDir + "/engine/pbr.hlsl", "// pbr shader\n#include \"common.hlsli\"\n");
        WriteFile(m_testDir + "/game/custom.hlsl", "// custom shader\n");
    }

    void TearDown() override {
        fs::remove_all(m_testDir);
    }

    static void WriteFile(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        f << content;
    }

    std::string m_testDir;
};

// ─────────────────────────────────────────────────────────────────────────────
// Mount / Unmount
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderVFSTest, MountAddsEntry) {
    uz::ShaderVFS vfs;
    EXPECT_EQ(vfs.GetMountCount(), 0u);

    vfs.Mount("/engine", m_testDir + "/engine");
    EXPECT_EQ(vfs.GetMountCount(), 1u);

    vfs.Mount("/game", m_testDir + "/game");
    EXPECT_EQ(vfs.GetMountCount(), 2u);
}

TEST_F(ShaderVFSTest, UnmountRemovesMatchingEntries) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");
    vfs.Mount("/game", m_testDir + "/game");
    EXPECT_EQ(vfs.GetMountCount(), 2u);

    vfs.Unmount("/engine");
    EXPECT_EQ(vfs.GetMountCount(), 1u);
    EXPECT_EQ(vfs.GetMounts()[0].virtualPrefix, "/game");
}

TEST_F(ShaderVFSTest, ClearRemovesAllMounts) {
    uz::ShaderVFS vfs;
    vfs.Mount("/a", "a");
    vfs.Mount("/b", "b");
    vfs.Clear();
    EXPECT_EQ(vfs.GetMountCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resolution
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderVFSTest, ResolveBasicPath) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");

    auto result = vfs.Resolve("/engine/pbr.hlsl");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, m_testDir + "/engine/pbr.hlsl");
}

TEST_F(ShaderVFSTest, ResolveNoMatchReturnsNullopt) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");

    auto result = vfs.Resolve("/game/custom.hlsl");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ShaderVFSTest, ResolveLastMountWins) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", "first_dir");
    vfs.Mount("/engine", "second_dir");

    auto result = vfs.Resolve("/engine/test.hlsl");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, "second_dir/test.hlsl");
}

TEST_F(ShaderVFSTest, ResolveDoesNotMatchPartialPrefix) {
    uz::ShaderVFS vfs;
    vfs.Mount("/eng", m_testDir);

    // "/engine/pbr.hlsl" should NOT match "/eng" because "i" is not "/"
    auto result = vfs.Resolve("/engine/pbr.hlsl");
    EXPECT_FALSE(result.has_value());
}

TEST_F(ShaderVFSTest, ResolveNormalizesBackslashes) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "\\engine");

    auto result = vfs.Resolve("/engine/pbr.hlsl");
    ASSERT_TRUE(result.has_value());
    // Backslashes should be normalized to forward slashes
    EXPECT_TRUE(result->find('\\') == std::string::npos);
}

// ─────────────────────────────────────────────────────────────────────────────
// Exists / ReadFile
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderVFSTest, ExistsReturnsTrueForRealFile) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");

    EXPECT_TRUE(vfs.Exists("/engine/common.hlsli"));
    EXPECT_FALSE(vfs.Exists("/engine/nonexistent.hlsl"));
}

TEST_F(ShaderVFSTest, ExistsReturnsFalseForUnmountedPath) {
    uz::ShaderVFS vfs;
    EXPECT_FALSE(vfs.Exists("/engine/common.hlsli"));
}

TEST_F(ShaderVFSTest, ReadFileReturnsContent) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");

    auto content = vfs.ReadFile("/engine/common.hlsli");
    ASSERT_TRUE(content.has_value());
    // Content contains "// common header" — exact line ending depends on platform
    EXPECT_TRUE(content->find("// common header") != std::string::npos);
    EXPECT_FALSE(content->empty());
}

TEST_F(ShaderVFSTest, ReadFileReturnsNulloptForMissing) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");

    auto content = vfs.ReadFile("/engine/missing.hlsl");
    EXPECT_FALSE(content.has_value());
}

TEST_F(ShaderVFSTest, ReadFileReturnsNulloptForUnresolved) {
    uz::ShaderVFS vfs;
    auto content = vfs.ReadFile("/nomount/file.hlsl");
    EXPECT_FALSE(content.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple Mounts
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderVFSTest, MultipleMountsResolveCorrectly) {
    uz::ShaderVFS vfs;
    vfs.Mount("/engine", m_testDir + "/engine");
    vfs.Mount("/game", m_testDir + "/game");

    auto engineResult = vfs.Resolve("/engine/pbr.hlsl");
    auto gameResult = vfs.Resolve("/game/custom.hlsl");

    ASSERT_TRUE(engineResult.has_value());
    ASSERT_TRUE(gameResult.has_value());
    EXPECT_EQ(*engineResult, m_testDir + "/engine/pbr.hlsl");
    EXPECT_EQ(*gameResult, m_testDir + "/game/custom.hlsl");
}
