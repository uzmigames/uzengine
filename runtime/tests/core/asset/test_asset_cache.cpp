/// UzEngine Asset Cache — Tests
///
/// Tests cache key derivation, store/lookup round-trip, invalidation,
/// statistics, and cleanup. Uses temp directories for isolation.

#include "asset_cache.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace uz::test {

// ─────────────────────────────────────────────────────────────────────────────
// Test fixture — creates and cleans up a temp cache directory
// ─────────────────────────────────────────────────────────────────────────────

class AssetCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_cacheDir = (fs::temp_directory_path() / "uz_test_asset_cache").string();
        fs::remove_all(m_cacheDir);
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(m_cacheDir, ec);
    }

    std::string m_cacheDir;
};

// ─────────────────────────────────────────────────────────────────────────────
// Hashing
// ─────────────────────────────────────────────────────────────────────────────

TEST(AssetCacheHashing, HashBytesIsDeterministic) {
    std::vector<uint8_t> data = {1, 2, 3, 4, 5};
    uint64_t h1 = AssetCache::HashBytes(data.data(), data.size());
    uint64_t h2 = AssetCache::HashBytes(data.data(), data.size());
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, 0u);
}

TEST(AssetCacheHashing, HashBytesDifferentDataDifferentHash) {
    std::vector<uint8_t> a = {1, 2, 3};
    std::vector<uint8_t> b = {4, 5, 6};
    EXPECT_NE(AssetCache::HashBytes(a.data(), a.size()),
              AssetCache::HashBytes(b.data(), b.size()));
}

TEST(AssetCacheHashing, HashStringIsDeterministic) {
    EXPECT_EQ(AssetCache::HashString("hello"), AssetCache::HashString("hello"));
    EXPECT_NE(AssetCache::HashString("hello"), AssetCache::HashString("world"));
}

TEST(AssetCacheHashing, ComputeKeyIsDeterministic) {
    uint64_t k1 = AssetCache::ComputeKey("shader", 1, 0xAABB, 0xCCDD);
    uint64_t k2 = AssetCache::ComputeKey("shader", 1, 0xAABB, 0xCCDD);
    EXPECT_EQ(k1, k2);
    EXPECT_NE(k1, 0u);
}

TEST(AssetCacheHashing, ComputeKeyChangesWithImporterName) {
    uint64_t k1 = AssetCache::ComputeKey("shader", 1, 0xAABB, 0xCCDD);
    uint64_t k2 = AssetCache::ComputeKey("texture", 1, 0xAABB, 0xCCDD);
    EXPECT_NE(k1, k2);
}

TEST(AssetCacheHashing, ComputeKeyChangesWithVersion) {
    uint64_t k1 = AssetCache::ComputeKey("shader", 1, 0xAABB, 0xCCDD);
    uint64_t k2 = AssetCache::ComputeKey("shader", 2, 0xAABB, 0xCCDD);
    EXPECT_NE(k1, k2);
}

TEST(AssetCacheHashing, ComputeKeyChangesWithSourceHash) {
    uint64_t k1 = AssetCache::ComputeKey("shader", 1, 0x1111, 0xCCDD);
    uint64_t k2 = AssetCache::ComputeKey("shader", 1, 0x2222, 0xCCDD);
    EXPECT_NE(k1, k2);
}

TEST(AssetCacheHashing, ComputeKeyChangesWithOptionsHash) {
    uint64_t k1 = AssetCache::ComputeKey("shader", 1, 0xAABB, 0x0001);
    uint64_t k2 = AssetCache::ComputeKey("shader", 1, 0xAABB, 0x0002);
    EXPECT_NE(k1, k2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Store / Lookup round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(AssetCacheTest, StoreAndLookupRoundTrip) {
    AssetCache cache(m_cacheDir);
    std::vector<uint8_t> data = {0xDE, 0xAD, 0xBE, 0xEF};
    uint64_t key = 0x123456789ABCDEF0;

    ASSERT_TRUE(cache.Store(key, data));
    auto result = cache.Lookup(key);

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, data);
}

TEST_F(AssetCacheTest, LookupMissReturnsNullopt) {
    AssetCache cache(m_cacheDir);
    auto result = cache.Lookup(0xDEAD);
    EXPECT_FALSE(result.has_value());
}

TEST_F(AssetCacheTest, ContainsReturnsTrueAfterStore) {
    AssetCache cache(m_cacheDir);
    uint64_t key = 0xAAAA;
    EXPECT_FALSE(cache.Contains(key));

    cache.Store(key, {1, 2, 3});
    EXPECT_TRUE(cache.Contains(key));
}

TEST_F(AssetCacheTest, StoreMultipleEntries) {
    AssetCache cache(m_cacheDir);

    cache.Store(0x0001, {0x01});
    cache.Store(0x0002, {0x02, 0x03});
    cache.Store(0x0003, {0x04, 0x05, 0x06});

    EXPECT_TRUE(cache.Contains(0x0001));
    EXPECT_TRUE(cache.Contains(0x0002));
    EXPECT_TRUE(cache.Contains(0x0003));

    auto r1 = cache.Lookup(0x0001);
    ASSERT_TRUE(r1.has_value());
    EXPECT_EQ(r1->size(), 1u);

    auto r3 = cache.Lookup(0x0003);
    ASSERT_TRUE(r3.has_value());
    EXPECT_EQ(r3->size(), 3u);
}

TEST_F(AssetCacheTest, StoreOverwritesExisting) {
    AssetCache cache(m_cacheDir);
    uint64_t key = 0xBBBB;

    cache.Store(key, {1, 2, 3});
    cache.Store(key, {4, 5});

    auto result = cache.Lookup(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(*result, (std::vector<uint8_t>{4, 5}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Remove
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(AssetCacheTest, RemoveDeletesEntry) {
    AssetCache cache(m_cacheDir);
    uint64_t key = 0xCCCC;

    cache.Store(key, {1});
    EXPECT_TRUE(cache.Contains(key));

    EXPECT_TRUE(cache.Remove(key));
    EXPECT_FALSE(cache.Contains(key));
}

TEST_F(AssetCacheTest, RemoveNonexistentReturnsFalse) {
    AssetCache cache(m_cacheDir);
    EXPECT_FALSE(cache.Remove(0xFFFF));
}

// ─────────────────────────────────────────────────────────────────────────────
// Statistics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(AssetCacheTest, EntryCountStartsAtZero) {
    AssetCache cache(m_cacheDir);
    EXPECT_EQ(cache.EntryCount(), 0u);
}

TEST_F(AssetCacheTest, EntryCountTracksStoresAndRemoves) {
    AssetCache cache(m_cacheDir);

    cache.Store(0x01, {1});
    cache.Store(0x02, {2});
    EXPECT_EQ(cache.EntryCount(), 2u);

    cache.Remove(0x01);
    EXPECT_EQ(cache.EntryCount(), 1u);
}

TEST_F(AssetCacheTest, TotalSizeBytesIsAccurate) {
    AssetCache cache(m_cacheDir);

    cache.Store(0x01, {1, 2, 3});       // 3 bytes
    cache.Store(0x02, {4, 5, 6, 7, 8}); // 5 bytes

    EXPECT_EQ(cache.TotalSizeBytes(), 8u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(AssetCacheTest, ClearRemovesAllEntries) {
    AssetCache cache(m_cacheDir);

    cache.Store(0x01, {1});
    cache.Store(0x02, {2});
    cache.Store(0x03, {3});
    EXPECT_EQ(cache.EntryCount(), 3u);

    cache.Clear();
    EXPECT_EQ(cache.EntryCount(), 0u);
    EXPECT_FALSE(cache.Contains(0x01));
}

TEST_F(AssetCacheTest, ClearOnEmptyCacheIsNoop) {
    AssetCache cache(m_cacheDir);
    cache.Clear();
    EXPECT_EQ(cache.EntryCount(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Invalidation scenarios
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(AssetCacheTest, VersionBumpProducesDifferentKey) {
    AssetCache cache(m_cacheDir);

    uint64_t sourceHash = AssetCache::HashString("float4 main() { return 1; }");
    uint64_t optionsHash = AssetCache::HashString("sm6_6");

    uint64_t keyV1 = AssetCache::ComputeKey("shader", 1, sourceHash, optionsHash);
    uint64_t keyV2 = AssetCache::ComputeKey("shader", 2, sourceHash, optionsHash);

    cache.Store(keyV1, {0x01});

    // V2 key is different — cache miss forces reimport
    EXPECT_TRUE(cache.Contains(keyV1));
    EXPECT_FALSE(cache.Contains(keyV2));
}

TEST_F(AssetCacheTest, SourceChangeProducesDifferentKey) {
    AssetCache cache(m_cacheDir);

    uint64_t srcA = AssetCache::HashString("float4 main() { return 0; }");
    uint64_t srcB = AssetCache::HashString("float4 main() { return 1; }");
    uint64_t opts = AssetCache::HashString("sm6_6");

    uint64_t keyA = AssetCache::ComputeKey("shader", 1, srcA, opts);
    uint64_t keyB = AssetCache::ComputeKey("shader", 1, srcB, opts);

    cache.Store(keyA, {0xAA});

    // Source changed — different key, cache miss
    EXPECT_TRUE(cache.Contains(keyA));
    EXPECT_FALSE(cache.Contains(keyB));
}

TEST_F(AssetCacheTest, OptionsChangeProducesDifferentKey) {
    AssetCache cache(m_cacheDir);

    uint64_t src = AssetCache::HashString("shader source");
    uint64_t optsA = AssetCache::HashString("sm6_6");
    uint64_t optsB = AssetCache::HashString("sm6_0");

    uint64_t keyA = AssetCache::ComputeKey("shader", 1, src, optsA);
    uint64_t keyB = AssetCache::ComputeKey("shader", 1, src, optsB);

    cache.Store(keyA, {0xBB});

    // Options changed — different key, cache miss
    EXPECT_TRUE(cache.Contains(keyA));
    EXPECT_FALSE(cache.Contains(keyB));
}

// ─────────────────────────────────────────────────────────────────────────────
// Edge cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(AssetCacheTest, StoreEmptyData) {
    AssetCache cache(m_cacheDir);
    uint64_t key = 0xEEEE;

    ASSERT_TRUE(cache.Store(key, {}));
    EXPECT_TRUE(cache.Contains(key));

    // Empty file → Lookup returns nullopt (size <= 0)
    auto result = cache.Lookup(key);
    EXPECT_FALSE(result.has_value());
}

TEST_F(AssetCacheTest, StoreLargeData) {
    AssetCache cache(m_cacheDir);
    std::vector<uint8_t> data(1024 * 64, 0xCC); // 64 KB
    uint64_t key = 0xDDDD;

    ASSERT_TRUE(cache.Store(key, data));
    auto result = cache.Lookup(key);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->size(), data.size());
    EXPECT_EQ(*result, data);
}

TEST_F(AssetCacheTest, CacheDirectoryAccessor) {
    AssetCache cache("/some/path");
    EXPECT_EQ(cache.CacheDirectory(), "/some/path");
}

} // namespace uz::test
