/// Tests for ShaderCache — disk-based bytecode storage.

#include "shader_cache.h"
#include <gtest/gtest.h>
#include <filesystem>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture
// ─────────────────────────────────────────────────────────────────────────────

class ShaderCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_cacheDir = "test_shader_cache_tmp";
        fs::remove_all(m_cacheDir);
    }

    void TearDown() override {
        fs::remove_all(m_cacheDir);
    }

    std::vector<uint8_t> MakeBytecode(const std::string& content) {
        return std::vector<uint8_t>(content.begin(), content.end());
    }

    std::string m_cacheDir;
};

// ─────────────────────────────────────────────────────────────────────────────
// Store / Lookup
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, StoreAndLookup) {
    uz::ShaderCache cache(m_cacheDir);

    auto bytecode = MakeBytecode("DXIL_BYTECODE_DATA");
    EXPECT_TRUE(cache.Store("dx12_sm66", 0xDEADBEEF12345678ULL, bytecode, "/engine/pbr.hlsl"));

    auto result = cache.Lookup("dx12_sm66", 0xDEADBEEF12345678ULL);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, bytecode);
}

TEST_F(ShaderCacheTest, LookupMissReturnsNullopt) {
    uz::ShaderCache cache(m_cacheDir);
    auto result = cache.Lookup("dx12_sm66", 0x1234);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ShaderCacheTest, ContainsReturnsTrueAfterStore) {
    uz::ShaderCache cache(m_cacheDir);

    auto bytecode = MakeBytecode("data");
    cache.Store("dx12_sm66", 42, bytecode, "/test.hlsl");

    EXPECT_TRUE(cache.Contains("dx12_sm66", 42));
    EXPECT_FALSE(cache.Contains("dx12_sm66", 99));
    EXPECT_FALSE(cache.Contains("vulkan_spirv16", 42));
}

// ─────────────────────────────────────────────────────────────────────────────
// Remove
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, RemoveDeletesEntry) {
    uz::ShaderCache cache(m_cacheDir);

    auto bytecode = MakeBytecode("data");
    cache.Store("dx12_sm66", 42, bytecode, "/test.hlsl");
    EXPECT_TRUE(cache.Contains("dx12_sm66", 42));

    cache.Remove("dx12_sm66", 42);
    EXPECT_FALSE(cache.Contains("dx12_sm66", 42));
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple Subdirectories
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, DifferentSubdirsAreIndependent) {
    uz::ShaderCache cache(m_cacheDir);

    auto bytecode1 = MakeBytecode("dxil_data");
    auto bytecode2 = MakeBytecode("spirv_data");

    cache.Store("dx12_sm66", 100, bytecode1, "/test.hlsl");
    cache.Store("vulkan_spirv16", 100, bytecode2, "/test.hlsl");

    auto r1 = cache.Lookup("dx12_sm66", 100);
    auto r2 = cache.Lookup("vulkan_spirv16", 100);

    ASSERT_TRUE(r1.has_value());
    ASSERT_TRUE(r2.has_value());
    EXPECT_EQ(*r1, bytecode1);
    EXPECT_EQ(*r2, bytecode2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Statistics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, EntryCountTracksStored) {
    uz::ShaderCache cache(m_cacheDir);
    EXPECT_EQ(cache.GetEntryCount(), 0u);

    cache.Store("dx12_sm66", 1, MakeBytecode("a"), "/a.hlsl");
    cache.Store("dx12_sm66", 2, MakeBytecode("bb"), "/b.hlsl");
    cache.Store("vulkan_spirv16", 3, MakeBytecode("ccc"), "/c.hlsl");
    EXPECT_EQ(cache.GetEntryCount(), 3u);
}

TEST_F(ShaderCacheTest, TotalSizeMatchesBytecodeLength) {
    uz::ShaderCache cache(m_cacheDir);

    cache.Store("dx12_sm66", 1, MakeBytecode("12345"), "/a.hlsl");
    cache.Store("dx12_sm66", 2, MakeBytecode("67890"), "/b.hlsl");

    EXPECT_EQ(cache.GetTotalSizeBytes(), 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, ClearRemovesAllEntries) {
    uz::ShaderCache cache(m_cacheDir);

    cache.Store("dx12_sm66", 1, MakeBytecode("a"), "/a.hlsl");
    cache.Store("dx12_sm66", 2, MakeBytecode("b"), "/b.hlsl");
    EXPECT_EQ(cache.GetEntryCount(), 2u);

    cache.Clear();
    EXPECT_EQ(cache.GetEntryCount(), 0u);
    EXPECT_FALSE(cache.Contains("dx12_sm66", 1));
}

// ─────────────────────────────────────────────────────────────────────────────
// GetSubdirectory
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, GetSubdirectoryFormatsCorrectly) {
    EXPECT_EQ(uz::ShaderCache::GetSubdirectory("dx12", "sm66"), "dx12_sm66");
    EXPECT_EQ(uz::ShaderCache::GetSubdirectory("vulkan", "spirv16"), "vulkan_spirv16");
}

// ─────────────────────────────────────────────────────────────────────────────
// Large Bytecode
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(ShaderCacheTest, StoreAndRetrieveLargeBytecode) {
    uz::ShaderCache cache(m_cacheDir);

    // Simulate a realistic shader bytecode size (64 KB)
    std::vector<uint8_t> largeBytecode(65536);
    for (size_t i = 0; i < largeBytecode.size(); ++i) {
        largeBytecode[i] = static_cast<uint8_t>(i & 0xFF);
    }

    cache.Store("dx12_sm66", 0xCAFEBABEULL, largeBytecode, "/big_shader.hlsl");

    auto result = cache.Lookup("dx12_sm66", 0xCAFEBABEULL);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), largeBytecode.size());
    EXPECT_EQ(*result, largeBytecode);
}
