/// Tests for ShaderSourceManager — source loading, include resolution, content hashing.

#include "shader_source_manager.h"
#include "shader_vfs.h"
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class ShaderSourceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Use a unique dir name per test instance to avoid cross-test contamination
        m_testDir = "test_shader_src_mgr_tmp_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
        fs::create_directories(m_testDir + "/engine");
        fs::create_directories(m_testDir + "/game");

        WriteFile(m_testDir + "/engine/common.hlsli",
            "// common definitions\n"
            "float PI = 3.14159;\n");

        WriteFile(m_testDir + "/engine/brdf.hlsli",
            "#include \"common.hlsli\"\n"
            "// BRDF functions\n");

        WriteFile(m_testDir + "/engine/pbr.hlsl",
            "#include \"common.hlsli\"\n"
            "#include \"brdf.hlsli\"\n"
            "// PBR shader body\n");

        WriteFile(m_testDir + "/engine/standalone.hlsl",
            "// no includes\nfloat4 main() { return 0; }\n");

        WriteFile(m_testDir + "/game/custom.hlsl",
            "#include \"/engine/common.hlsli\"\n"
            "// custom game shader\n");

        m_vfs.Mount("/engine", m_testDir + "/engine");
        m_vfs.Mount("/game", m_testDir + "/game");
    }

    void TearDown() override {
        m_vfs.Clear();
        std::error_code ec;
        fs::remove_all(m_testDir, ec);
    }

    static void WriteFile(const std::string& path, const std::string& content) {
        std::ofstream f(path);
        f << content;
    }

    std::string m_testDir;
    uz::ShaderVFS m_vfs;
};

// ─────────────────────────────────────────────────────────────────────────────
// Basic Loading
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderSourceManagerTest, LoadStandaloneShader) {
    uz::ShaderSourceManager mgr(m_vfs);

    auto info = mgr.LoadSource("/engine/standalone.hlsl");
    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->virtualPath, "/engine/standalone.hlsl");
    EXPECT_TRUE(info->includes.empty());
    EXPECT_NE(info->contentHash, 0u);
    EXPECT_FALSE(info->source.empty());
}

TEST_F(ShaderSourceManagerTest, LoadMissingShaderReturnsNullopt) {
    uz::ShaderSourceManager mgr(m_vfs);
    auto info = mgr.LoadSource("/engine/nonexistent.hlsl");
    EXPECT_FALSE(info.has_value());
}

// ─────────────────────────────────────────────────────────────────────────────
// Include Resolution
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderSourceManagerTest, ResolvesRelativeInclude) {
    uz::ShaderSourceManager mgr(m_vfs);

    auto info = mgr.LoadSource("/engine/brdf.hlsli");
    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->includes.size(), 1u);
    EXPECT_EQ(info->includes[0], "/engine/common.hlsli");
}

TEST_F(ShaderSourceManagerTest, ResolvesAbsoluteInclude) {
    uz::ShaderSourceManager mgr(m_vfs);

    auto info = mgr.LoadSource("/game/custom.hlsl");
    ASSERT_TRUE(info.has_value());
    ASSERT_EQ(info->includes.size(), 1u);
    EXPECT_EQ(info->includes[0], "/engine/common.hlsli");
}

TEST_F(ShaderSourceManagerTest, DeduplicatesIncludes) {
    uz::ShaderSourceManager mgr(m_vfs);

    // pbr.hlsl includes common.hlsli AND brdf.hlsli (which also includes common.hlsli)
    auto info = mgr.LoadSource("/engine/pbr.hlsl");
    ASSERT_TRUE(info.has_value());

    // common.hlsli should appear only once despite being included by both pbr and brdf
    int commonCount = 0;
    for (const auto& inc : info->includes) {
        if (inc == "/engine/common.hlsli") ++commonCount;
    }
    EXPECT_EQ(commonCount, 1);

    // Should have both unique includes
    EXPECT_EQ(info->includes.size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Content Hashing
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderSourceManagerTest, SameSourceProducesSameHash) {
    uz::ShaderSourceManager mgr(m_vfs);

    auto info1 = mgr.LoadSource("/engine/standalone.hlsl");
    mgr.InvalidateAll();
    auto info2 = mgr.LoadSource("/engine/standalone.hlsl");

    ASSERT_TRUE(info1.has_value());
    ASSERT_TRUE(info2.has_value());
    EXPECT_EQ(info1->contentHash, info2->contentHash);
}

TEST_F(ShaderSourceManagerTest, DifferentSourcesProduceDifferentHashes) {
    uz::ShaderSourceManager mgr(m_vfs);

    auto info1 = mgr.LoadSource("/engine/standalone.hlsl");
    auto info2 = mgr.LoadSource("/engine/pbr.hlsl");

    ASSERT_TRUE(info1.has_value());
    ASSERT_TRUE(info2.has_value());
    EXPECT_NE(info1->contentHash, info2->contentHash);
}

TEST_F(ShaderSourceManagerTest, ChangingIncludeChangesHash) {
    uz::ShaderSourceManager mgr(m_vfs);

    auto info1 = mgr.LoadSource("/engine/pbr.hlsl");
    ASSERT_TRUE(info1.has_value());
    uint64_t hash1 = info1->contentHash;

    // Modify an included file
    WriteFile(m_testDir + "/engine/common.hlsli", "// MODIFIED common\nfloat PI = 3.14;\n");

    // Invalidate and reload
    mgr.InvalidateAll();
    auto info2 = mgr.LoadSource("/engine/pbr.hlsl");
    ASSERT_TRUE(info2.has_value());

    EXPECT_NE(info2->contentHash, hash1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Identity Hash (with defines and target)
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderSourceManagerTest, IdentityHashDiffersWithDefines) {
    uz::ShaderSourceManager mgr(m_vfs);

    uint64_t hash1 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {}, uz::ShaderTarget::DXIL_SM6_6, 1);
    uint64_t hash2 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {"USE_PBR"}, uz::ShaderTarget::DXIL_SM6_6, 1);

    EXPECT_NE(hash1, hash2);
}

TEST_F(ShaderSourceManagerTest, IdentityHashDiffersWithTarget) {
    uz::ShaderSourceManager mgr(m_vfs);

    uint64_t hash1 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {}, uz::ShaderTarget::DXIL_SM6_6, 1);
    uint64_t hash2 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {}, uz::ShaderTarget::SPIRV_1_6, 1);

    EXPECT_NE(hash1, hash2);
}

TEST_F(ShaderSourceManagerTest, IdentityHashDiffersWithCompilerVersion) {
    uz::ShaderSourceManager mgr(m_vfs);

    uint64_t hash1 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {}, uz::ShaderTarget::DXIL_SM6_6, 1);
    uint64_t hash2 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {}, uz::ShaderTarget::DXIL_SM6_6, 2);

    EXPECT_NE(hash1, hash2);
}

TEST_F(ShaderSourceManagerTest, IdentityHashDefineOrderDoesNotMatter) {
    uz::ShaderSourceManager mgr(m_vfs);

    uint64_t hash1 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {"A", "B", "C"}, uz::ShaderTarget::DXIL_SM6_6, 1);
    uint64_t hash2 = mgr.ComputeIdentityHash(
        "/engine/standalone.hlsl", {"C", "A", "B"}, uz::ShaderTarget::DXIL_SM6_6, 1);

    EXPECT_EQ(hash1, hash2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Cache Behavior
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderSourceManagerTest, CachesLoadedSources) {
    uz::ShaderSourceManager mgr(m_vfs);

    mgr.LoadSource("/engine/standalone.hlsl");
    EXPECT_EQ(mgr.GetCacheSize(), 1u);

    mgr.LoadSource("/engine/standalone.hlsl");
    EXPECT_EQ(mgr.GetCacheSize(), 1u);
}

TEST_F(ShaderSourceManagerTest, InvalidateRemovesCacheEntry) {
    uz::ShaderSourceManager mgr(m_vfs);

    mgr.LoadSource("/engine/standalone.hlsl");
    EXPECT_EQ(mgr.GetCacheSize(), 1u);

    mgr.Invalidate("/engine/standalone.hlsl");
    EXPECT_EQ(mgr.GetCacheSize(), 0u);
}

TEST_F(ShaderSourceManagerTest, InvalidateAllClearsCache) {
    uz::ShaderSourceManager mgr(m_vfs);

    mgr.LoadSource("/engine/standalone.hlsl");
    mgr.LoadSource("/engine/pbr.hlsl");
    EXPECT_EQ(mgr.GetCacheSize(), 2u);

    mgr.InvalidateAll();
    EXPECT_EQ(mgr.GetCacheSize(), 0u);
}
