// test_rhi_common.cpp -- Unit tests for src/engine/renderer/rhi/rhi_common.c
//
// Tests the following without any D3D12 hardware dependency:
//   1. rhi() accessor and rhi_set_backend()
//   2. rhi_hash_bytes / rhi_hash_bytes32
//   3. rhi_make_pso_cache_key
//   4. PSO cache (init, find, insert, shutdown, invalidate_by_shader)
//
// The test binary links rhi_common.c only. rhi_dx12.c is NOT included.

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

extern "C" {
#include "engine/renderer/rhi/rhi.h"
}

// =============================================================================
// Helpers
// =============================================================================

// Build a minimal RhiPipelineDesc with the given shader bytes and rt_format.
static RhiPipelineDesc make_desc(const uint8_t *vs_data, size_t vs_size,
                                 const uint8_t *ps_data, size_t ps_size,
                                 RhiFormat rt_fmt = RHI_FORMAT_RGBA8_UNORM,
                                 uint32_t  rt_count = 1) {
    RhiPipelineDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.vertex_shader.spirv_data = vs_data;
    desc.vertex_shader.spirv_size = vs_size;
    desc.pixel_shader.spirv_data  = ps_data;
    desc.pixel_shader.spirv_size  = ps_size;
    desc.rt_formats[0]            = rt_fmt;
    desc.rt_count                 = rt_count;
    return desc;
}

// Build a unique RhiPsoCacheKey from a 64-bit "seed" so we can insert many
// distinct entries without actual shader data.
static RhiPsoCacheKey make_unique_key(uint64_t seed) {
    RhiPsoCacheKey key;
    memset(&key, 0, sizeof(key));
    key.vs_spirv_hash  = seed;
    key.ps_spirv_hash  = seed ^ 0xDEADBEEFCAFEBABEULL;
    key.rt_formats_hash = (uint32_t)(seed & 0xFFFFFFFFu);
    key.state_hash      = (uint32_t)(seed >> 32);
    return key;
}

// =============================================================================
// Fixture: PSO cache tests share global state — always init/shutdown
// =============================================================================

class PsoCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        rhi_pso_cache_init();
    }
    void TearDown() override {
        rhi_pso_cache_shutdown();
    }
};

// =============================================================================
// 1. rhi() accessor and rhi_set_backend()
// =============================================================================

TEST(RhiAccessor, InitiallyNull_BeforeSetBackend) {
    // Reset to a known-null state first (in case a previous test left it set).
    rhi_set_backend(nullptr);
    EXPECT_EQ(rhi(), nullptr);
}

TEST(RhiAccessor, SetAndGet_ReturnsSamePointer) {
    rhi_backend_t mock_backend;
    memset(&mock_backend, 0, sizeof(mock_backend));

    rhi_set_backend(&mock_backend);
    EXPECT_EQ(rhi(), &mock_backend);

    // Cleanup: leave accessor null for subsequent tests.
    rhi_set_backend(nullptr);
}

TEST(RhiAccessor, SetNull_AccessorBecomesNull) {
    rhi_backend_t mock_backend;
    memset(&mock_backend, 0, sizeof(mock_backend));

    rhi_set_backend(&mock_backend);
    ASSERT_EQ(rhi(), &mock_backend);

    rhi_set_backend(nullptr);
    EXPECT_EQ(rhi(), nullptr);
}

// =============================================================================
// 2. rhi_hash_bytes (64-bit FNV-1a)
// =============================================================================

TEST(RhiHash, EmptyInput_ReturnsFnvBasisValue) {
    // FNV-1a 64-bit basis is 14695981039346656037. Hash of zero bytes must be
    // deterministic and equal to the basis (loop body never executes).
    const uint64_t h1 = rhi_hash_bytes(nullptr, 0);
    const uint64_t h2 = rhi_hash_bytes(nullptr, 0);
    EXPECT_EQ(h1, h2) << "Hash of empty input must be deterministic";
    EXPECT_EQ(h1, 14695981039346656037ULL) << "FNV-1a 64-bit basis must be returned for empty input";
}

TEST(RhiHash, SameDataSameHash_Repeated) {
    const uint8_t data[] = {0x01, 0x02, 0x03, 0x04, 0x05};
    const uint64_t h1 = rhi_hash_bytes(data, sizeof(data));
    const uint64_t h2 = rhi_hash_bytes(data, sizeof(data));
    EXPECT_EQ(h1, h2) << "Same data must produce same 64-bit hash";
}

TEST(RhiHash, DifferentData_DifferentHash) {
    const uint8_t abc[] = {'a', 'b', 'c'};
    const uint8_t xyz[] = {'x', 'y', 'z'};
    const uint64_t h_abc = rhi_hash_bytes(abc, sizeof(abc));
    const uint64_t h_xyz = rhi_hash_bytes(xyz, sizeof(xyz));
    EXPECT_NE(h_abc, h_xyz) << "Different data must produce different hashes";
}

TEST(RhiHash, SensitiveToLength) {
    const uint8_t ab[] = {'a', 'b'};
    const uint64_t h2 = rhi_hash_bytes(ab, 2);
    const uint64_t h1 = rhi_hash_bytes(ab, 1);
    EXPECT_NE(h2, h1) << "Hash must differ when length differs";
}

TEST(RhiHash, SensitiveToByteOrder) {
    const uint8_t forward[]  = {'a', 'b'};
    const uint8_t backward[] = {'b', 'a'};
    const uint64_t hf = rhi_hash_bytes(forward,  sizeof(forward));
    const uint64_t hb = rhi_hash_bytes(backward, sizeof(backward));
    EXPECT_NE(hf, hb) << "Reversed byte order must produce a different hash";
}

TEST(RhiHash, SingleByte_AllValuesDistinct) {
    // Hash of single byte {0} must differ from single byte {1}
    const uint8_t b0 = 0x00;
    const uint8_t b1 = 0x01;
    const uint8_t bff = 0xFF;
    EXPECT_NE(rhi_hash_bytes(&b0,  1), rhi_hash_bytes(&b1,  1));
    EXPECT_NE(rhi_hash_bytes(&b1,  1), rhi_hash_bytes(&bff, 1));
    EXPECT_NE(rhi_hash_bytes(&b0,  1), rhi_hash_bytes(&bff, 1));
}

// =============================================================================
// 3. rhi_hash_bytes32 (32-bit FNV-1a)
// =============================================================================

TEST(RhiHash32, EmptyInput_ReturnsFnvBasisValue) {
    const uint32_t h1 = rhi_hash_bytes32(nullptr, 0);
    const uint32_t h2 = rhi_hash_bytes32(nullptr, 0);
    EXPECT_EQ(h1, h2) << "Hash of empty input must be deterministic";
    EXPECT_EQ(h1, 2166136261u) << "FNV-1a 32-bit basis must be returned for empty input";
}

TEST(RhiHash32, SameDataSameHash_Repeated) {
    const uint8_t data[] = {0x10, 0x20, 0x30};
    const uint32_t h1 = rhi_hash_bytes32(data, sizeof(data));
    const uint32_t h2 = rhi_hash_bytes32(data, sizeof(data));
    EXPECT_EQ(h1, h2) << "Same data must produce same 32-bit hash";
}

TEST(RhiHash32, DifferentData_DifferentHash) {
    const uint8_t a[] = {0xAA, 0xBB};
    const uint8_t b[] = {0xCC, 0xDD};
    EXPECT_NE(rhi_hash_bytes32(a, sizeof(a)), rhi_hash_bytes32(b, sizeof(b)));
}

TEST(RhiHash32, SensitiveToLength) {
    const uint8_t data[] = {0x42, 0x43};
    EXPECT_NE(rhi_hash_bytes32(data, 2), rhi_hash_bytes32(data, 1));
}

TEST(RhiHash32, SensitiveToByteOrder) {
    const uint8_t fwd[] = {0x11, 0x22};
    const uint8_t rev[] = {0x22, 0x11};
    EXPECT_NE(rhi_hash_bytes32(fwd, sizeof(fwd)), rhi_hash_bytes32(rev, sizeof(rev)));
}

// =============================================================================
// 4. rhi_hash_bytes vs rhi_hash_bytes32 — both deterministic for same input
// =============================================================================

TEST(RhiHash64vs32, BothDeterministicForSameInput) {
    const uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};

    const uint64_t h64a = rhi_hash_bytes(data,   sizeof(data));
    const uint64_t h64b = rhi_hash_bytes(data,   sizeof(data));
    const uint32_t h32a = rhi_hash_bytes32(data, sizeof(data));
    const uint32_t h32b = rhi_hash_bytes32(data, sizeof(data));

    EXPECT_EQ(h64a, h64b);
    EXPECT_EQ(h32a, h32b);
    // The 64-bit and 32-bit values need not be related — just each stable.
}

// =============================================================================
// 5. rhi_make_pso_cache_key
// =============================================================================

TEST(PsoCacheKey, SameInputProducesSameKey) {
    static const uint8_t vs[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t ps[] = {0x05, 0x06, 0x07, 0x08};

    RhiPipelineDesc d1 = make_desc(vs, sizeof(vs), ps, sizeof(ps));
    RhiPipelineDesc d2 = make_desc(vs, sizeof(vs), ps, sizeof(ps));

    RhiPsoCacheKey k1 = rhi_make_pso_cache_key(&d1);
    RhiPsoCacheKey k2 = rhi_make_pso_cache_key(&d2);

    EXPECT_EQ(k1.vs_spirv_hash,   k2.vs_spirv_hash);
    EXPECT_EQ(k1.ps_spirv_hash,   k2.ps_spirv_hash);
    EXPECT_EQ(k1.rt_formats_hash, k2.rt_formats_hash);
    EXPECT_EQ(k1.state_hash,      k2.state_hash);
}

TEST(PsoCacheKey, DifferentShaderBytes_DifferentKey) {
    static const uint8_t vs_a[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t vs_b[] = {0xAA, 0xBB, 0xCC, 0xDD};
    static const uint8_t ps[]   = {0x05, 0x06, 0x07, 0x08};

    RhiPipelineDesc d_a = make_desc(vs_a, sizeof(vs_a), ps, sizeof(ps));
    RhiPipelineDesc d_b = make_desc(vs_b, sizeof(vs_b), ps, sizeof(ps));

    RhiPsoCacheKey k_a = rhi_make_pso_cache_key(&d_a);
    RhiPsoCacheKey k_b = rhi_make_pso_cache_key(&d_b);

    EXPECT_NE(k_a.vs_spirv_hash, k_b.vs_spirv_hash)
        << "Different vertex shader bytes must produce different vs_spirv_hash";
}

TEST(PsoCacheKey, DifferentRtFormat_DifferentKey) {
    static const uint8_t vs[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t ps[] = {0x05, 0x06, 0x07, 0x08};

    RhiPipelineDesc d_rgba8  = make_desc(vs, sizeof(vs), ps, sizeof(ps), RHI_FORMAT_RGBA8_UNORM);
    RhiPipelineDesc d_rgba16 = make_desc(vs, sizeof(vs), ps, sizeof(ps), RHI_FORMAT_RGBA16_FLOAT);

    RhiPsoCacheKey k_rgba8  = rhi_make_pso_cache_key(&d_rgba8);
    RhiPsoCacheKey k_rgba16 = rhi_make_pso_cache_key(&d_rgba16);

    EXPECT_NE(k_rgba8.rt_formats_hash, k_rgba16.rt_formats_hash)
        << "Different rt_format must produce different rt_formats_hash";
}

TEST(PsoCacheKey, DifferentRtCount_DifferentStateHash) {
    static const uint8_t vs[] = {0x01, 0x02, 0x03, 0x04};
    static const uint8_t ps[] = {0x05, 0x06, 0x07, 0x08};

    RhiPipelineDesc d1 = make_desc(vs, sizeof(vs), ps, sizeof(ps), RHI_FORMAT_RGBA8_UNORM, 1);
    RhiPipelineDesc d2 = make_desc(vs, sizeof(vs), ps, sizeof(ps), RHI_FORMAT_RGBA8_UNORM, 2);

    RhiPsoCacheKey k1 = rhi_make_pso_cache_key(&d1);
    RhiPsoCacheKey k2 = rhi_make_pso_cache_key(&d2);

    EXPECT_NE(k1.state_hash, k2.state_hash)
        << "Different rt_count must produce different state_hash";
}

TEST(PsoCacheKey, NullShaderData_ProducesZeroShaderHash) {
    // When spirv_data is NULL or spirv_size is 0, the hash fields stay at 0
    // (memset initializes key to 0 and the if-guard skips hashing).
    RhiPipelineDesc desc;
    memset(&desc, 0, sizeof(desc));
    // Both shaders are null/zero-size.

    RhiPsoCacheKey key = rhi_make_pso_cache_key(&desc);

    EXPECT_EQ(key.vs_spirv_hash, 0u) << "Null VS data must leave vs_spirv_hash=0";
    EXPECT_EQ(key.ps_spirv_hash, 0u) << "Null PS data must leave ps_spirv_hash=0";
}

TEST(PsoCacheKey, PixelShaderOnly_DifferentFromVertexShaderOnly) {
    // Same bytes in vs vs ps should still produce different overall key structures
    // because vs_spirv_hash and ps_spirv_hash are stored in different fields.
    static const uint8_t shader[] = {0xAA, 0xBB, 0xCC};

    RhiPipelineDesc d_vs_only;
    memset(&d_vs_only, 0, sizeof(d_vs_only));
    d_vs_only.vertex_shader.spirv_data = shader;
    d_vs_only.vertex_shader.spirv_size = sizeof(shader);
    // ps is null

    RhiPipelineDesc d_ps_only;
    memset(&d_ps_only, 0, sizeof(d_ps_only));
    d_ps_only.pixel_shader.spirv_data = shader;
    d_ps_only.pixel_shader.spirv_size = sizeof(shader);
    // vs is null

    RhiPsoCacheKey k_vs = rhi_make_pso_cache_key(&d_vs_only);
    RhiPsoCacheKey k_ps = rhi_make_pso_cache_key(&d_ps_only);

    // vs-only has vs_hash set, ps-only has ps_hash set — keys must differ
    EXPECT_NE(k_vs.vs_spirv_hash, k_ps.vs_spirv_hash);
    EXPECT_NE(k_vs.ps_spirv_hash, k_ps.ps_spirv_hash);
}

// =============================================================================
// 6. PSO cache — lifecycle
// =============================================================================

TEST_F(PsoCacheTest, InitAndShutdown_NoInserts_NoCrash) {
    // SetUp() calls init, TearDown() calls shutdown.
    // Just reaching here without crashing is the assertion.
    SUCCEED();
}

TEST_F(PsoCacheTest, ReinitAfterShutdown_CacheIsEmpty) {
    // Insert an entry, shut down, re-init — find should return RHI_NULL.
    RhiPsoCacheKey key = make_unique_key(0xBEEF);
    const RhiPipeline pipeline = 999u;

    rhi_pso_cache_insert(&key, pipeline);
    ASSERT_EQ(rhi_pso_cache_find(&key), pipeline) << "Precondition: insert must work";

    rhi_pso_cache_shutdown();
    rhi_pso_cache_init();

    EXPECT_EQ(rhi_pso_cache_find(&key), RHI_NULL)
        << "After shutdown+reinit, previously inserted key must not be found";
}

// =============================================================================
// 7. PSO cache — find on empty cache
// =============================================================================

TEST_F(PsoCacheTest, FindEmpty_ReturnsNull) {
    RhiPsoCacheKey key = make_unique_key(0xABCD);
    EXPECT_EQ(rhi_pso_cache_find(&key), RHI_NULL)
        << "find on empty cache must return RHI_NULL";
}

// =============================================================================
// 8. PSO cache — basic insert + find
// =============================================================================

TEST_F(PsoCacheTest, InsertAndFind_ReturnsSamePipelineHandle) {
    RhiPsoCacheKey key = make_unique_key(0x1234);
    const RhiPipeline pso = 42u;

    rhi_pso_cache_insert(&key, pso);
    EXPECT_EQ(rhi_pso_cache_find(&key), pso)
        << "find after insert must return the inserted pipeline handle";
}

TEST_F(PsoCacheTest, FindMissingKey_ReturnsNull) {
    RhiPsoCacheKey present = make_unique_key(0x1111);
    RhiPsoCacheKey absent  = make_unique_key(0x2222);

    rhi_pso_cache_insert(&present, 7u);

    EXPECT_EQ(rhi_pso_cache_find(&absent), RHI_NULL)
        << "find with an uninserted key must return RHI_NULL";
}

// =============================================================================
// 9. PSO cache — double insert (update in-place)
// =============================================================================

TEST_F(PsoCacheTest, DoubleInsert_SameKey_FindReturnsLatestHandle) {
    RhiPsoCacheKey key = make_unique_key(0x5A5A);
    const RhiPipeline first  = 10u;
    const RhiPipeline second = 20u;

    rhi_pso_cache_insert(&key, first);
    ASSERT_EQ(rhi_pso_cache_find(&key), first) << "Precondition: first insert must work";

    rhi_pso_cache_insert(&key, second);
    EXPECT_EQ(rhi_pso_cache_find(&key), second)
        << "Second insert with same key must update the stored pipeline handle";
}

// =============================================================================
// 10. PSO cache — multiple distinct entries
// =============================================================================

TEST_F(PsoCacheTest, MultipleEntries_AllFindable) {
    // Insert 10 distinct key→pipeline pairs, verify all are findable afterward.
    constexpr int kCount = 10;
    RhiPsoCacheKey keys[kCount];
    RhiPipeline    pipelines[kCount];

    for (int i = 0; i < kCount; i++) {
        keys[i]      = make_unique_key(static_cast<uint64_t>(i) * 0x100 + 1);
        pipelines[i] = static_cast<RhiPipeline>(i + 100);
        rhi_pso_cache_insert(&keys[i], pipelines[i]);
    }

    for (int i = 0; i < kCount; i++) {
        EXPECT_EQ(rhi_pso_cache_find(&keys[i]), pipelines[i])
            << "Entry " << i << " must be findable after inserting 10 entries";
    }
}

// =============================================================================
// 11. PSO cache — near-capacity stress (250 entries in a 256-slot table)
// =============================================================================

TEST_F(PsoCacheTest, NearCapacity_250Entries_AllFindable) {
    // 250 distinct entries leaves 6 free slots — table must not corrupt.
    constexpr int kCount = 250;

    // Build keys with seeds spaced far apart in hash space to minimise clustering.
    static RhiPsoCacheKey keys[kCount];
    static RhiPipeline    pipelines[kCount];

    for (int i = 0; i < kCount; i++) {
        // Multiply by a large prime to scatter insertion points across the 256 slots.
        keys[i]      = make_unique_key(static_cast<uint64_t>(i) * 6364136223846793005ULL + 1442695040888963407ULL);
        pipelines[i] = static_cast<RhiPipeline>(i + 1); // never 0 (= RHI_NULL)
        rhi_pso_cache_insert(&keys[i], pipelines[i]);
    }

    for (int i = 0; i < kCount; i++) {
        EXPECT_EQ(rhi_pso_cache_find(&keys[i]), pipelines[i])
            << "Entry " << i << " must be findable after 250 insertions";
    }
}

// =============================================================================
// 12. PSO cache — invalidate_by_shader
// =============================================================================

TEST_F(PsoCacheTest, InvalidateByShader_MatchingVsHash_EntriesBecomeMissing) {
    // Insert 3 entries all sharing a specific vs_spirv_hash, then invalidate.
    const uint64_t shared_vs_hash = 0xAAAAAAAABBBBBBBBULL;

    RhiPsoCacheKey keys[3];
    for (int i = 0; i < 3; i++) {
        memset(&keys[i], 0, sizeof(keys[i]));
        keys[i].vs_spirv_hash  = shared_vs_hash;
        // Vary ps_hash so each key is unique in the table.
        keys[i].ps_spirv_hash  = static_cast<uint64_t>(i) * 0x1111111111111111ULL + 1;
        keys[i].rt_formats_hash = static_cast<uint32_t>(i * 7 + 1);
        keys[i].state_hash      = static_cast<uint32_t>(i * 13 + 2);

        rhi_pso_cache_insert(&keys[i], static_cast<RhiPipeline>(i + 100));
    }

    // Verify all 3 are present before invalidation.
    for (int i = 0; i < 3; i++) {
        ASSERT_NE(rhi_pso_cache_find(&keys[i]), RHI_NULL)
            << "Precondition: key " << i << " must be present before invalidation";
    }

    rhi_pso_cache_invalidate_by_shader(shared_vs_hash);

    // All 3 must now be gone.
    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(rhi_pso_cache_find(&keys[i]), RHI_NULL)
            << "Key " << i << " must be evicted after invalidate_by_shader(vs_hash)";
    }
}

TEST_F(PsoCacheTest, InvalidateByShader_MatchingPsHash_EntriesBecomeMissing) {
    const uint64_t shared_ps_hash = 0xCCCCCCCCDDDDDDDDULL;

    RhiPsoCacheKey keys[3];
    for (int i = 0; i < 3; i++) {
        memset(&keys[i], 0, sizeof(keys[i]));
        keys[i].vs_spirv_hash  = static_cast<uint64_t>(i) * 0x2222222222222222ULL + 1;
        keys[i].ps_spirv_hash  = shared_ps_hash;
        keys[i].rt_formats_hash = static_cast<uint32_t>(i * 11 + 3);
        keys[i].state_hash      = static_cast<uint32_t>(i * 17 + 5);

        rhi_pso_cache_insert(&keys[i], static_cast<RhiPipeline>(i + 200));
    }

    for (int i = 0; i < 3; i++) {
        ASSERT_NE(rhi_pso_cache_find(&keys[i]), RHI_NULL)
            << "Precondition: key " << i << " must be present before invalidation";
    }

    rhi_pso_cache_invalidate_by_shader(shared_ps_hash);

    for (int i = 0; i < 3; i++) {
        EXPECT_EQ(rhi_pso_cache_find(&keys[i]), RHI_NULL)
            << "Key " << i << " must be evicted after invalidate_by_shader(ps_hash)";
    }
}

TEST_F(PsoCacheTest, InvalidateByShader_OtherEntriesUnaffected) {
    // Insert one "target" entry (will be invalidated) and one "survivor" entry.
    const uint64_t target_vs_hash   = 0xEEEEEEEEFFFFFFFFULL;
    const uint64_t survivor_vs_hash = 0x1234567890ABCDEFULL;

    RhiPsoCacheKey target_key;
    memset(&target_key, 0, sizeof(target_key));
    target_key.vs_spirv_hash  = target_vs_hash;
    target_key.ps_spirv_hash  = 0x0101010101010101ULL;
    target_key.rt_formats_hash = 0xAAAA0001u;
    target_key.state_hash      = 0xBBBB0001u;

    RhiPsoCacheKey survivor_key;
    memset(&survivor_key, 0, sizeof(survivor_key));
    survivor_key.vs_spirv_hash  = survivor_vs_hash;
    survivor_key.ps_spirv_hash  = 0x0202020202020202ULL;
    survivor_key.rt_formats_hash = 0xAAAA0002u;
    survivor_key.state_hash      = 0xBBBB0002u;

    rhi_pso_cache_insert(&target_key,   500u);
    rhi_pso_cache_insert(&survivor_key, 501u);

    ASSERT_EQ(rhi_pso_cache_find(&target_key),   500u);
    ASSERT_EQ(rhi_pso_cache_find(&survivor_key), 501u);

    rhi_pso_cache_invalidate_by_shader(target_vs_hash);

    EXPECT_EQ(rhi_pso_cache_find(&target_key), RHI_NULL)
        << "Target entry must be evicted";
    EXPECT_EQ(rhi_pso_cache_find(&survivor_key), 501u)
        << "Survivor entry must remain in cache after invalidation of unrelated shader";
}

TEST_F(PsoCacheTest, InvalidateByShader_HashNotPresent_CacheUnchanged) {
    // Invalidating a hash that was never inserted must not corrupt the cache.
    RhiPsoCacheKey key = make_unique_key(0xCAFE);
    rhi_pso_cache_insert(&key, 77u);

    // Invalidate a completely different hash.
    rhi_pso_cache_invalidate_by_shader(0xDEADBEEFDEADBEEFULL);

    EXPECT_EQ(rhi_pso_cache_find(&key), 77u)
        << "Invalidating an absent hash must not disturb existing entries";
}

TEST_F(PsoCacheTest, InvalidateByShader_EmptyCache_NoCrash) {
    // Invalidating when cache is empty must not crash.
    rhi_pso_cache_invalidate_by_shader(0xFFFFFFFFFFFFFFFFULL);
    SUCCEED();
}
