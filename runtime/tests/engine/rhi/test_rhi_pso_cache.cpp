/// Unit tests for PSO Cache — key computation determinism, in-memory cache
/// hit/miss/eviction, rehash on load factor, and hot-reload invalidation.

#include "rhi_pso_cache.h"
#include "rhi_types.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helper — builds a default RHIGraphicsPipelineDesc for testing
// ─────────────────────────────────────────────────────────────────────────────

namespace {

RHIGraphicsPipelineDesc MakeDefaultDesc() {
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader = {1, 1};
    desc.pixelShader = {2, 1};
    desc.topology = RHIPrimitiveTopology::TriangleList;
    desc.renderTargetCount = 1;
    desc.renderTargetFormats[0] = RHIFormat::RGBA8Unorm;
    desc.depthStencilFormat = RHIFormat::D32Float;
    desc.debugName = "test_pipeline";
    return desc;
}

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// PsoCacheKey — determinism and sensitivity tests
// ─────────────────────────────────────────────────────────────────────────────

TEST(PsoCacheKey, SameDescriptorProducesSameKey) {
    auto desc = MakeDefaultDesc();
    PsoCacheKey a = ComputePsoCacheKey(desc);
    PsoCacheKey b = ComputePsoCacheKey(desc);
    EXPECT_EQ(a, b);
    EXPECT_TRUE(a.IsValid());
}

TEST(PsoCacheKey, DifferentVertexShaderProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.vertexShader = {3, 1};
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentPixelShaderProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.pixelShader = {4, 2};
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentTopologyProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.topology = RHIPrimitiveTopology::LineList;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentCullModeProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.rasterizer.cullMode = RHICullMode::None;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentDepthEnableProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.depthStencil.depthEnable = false;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentBlendStateProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.renderTargetBlend[0].blendEnable = true;
    desc2.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentRTFormatProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.renderTargetFormats[0] = RHIFormat::RGBA16Float;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentRTCountProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.renderTargetCount = 3;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentDepthFormatProducesDifferentKey) {
    auto desc1 = MakeDefaultDesc();
    auto desc2 = MakeDefaultDesc();
    desc2.depthStencilFormat = RHIFormat::Unknown;
    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DifferentInputElementsProducesDifferentKey) {
    RHIInputElementDesc elements1[] = {
        {RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, 0},
        {RHIInputSemantic::Normal, 0, RHIFormat::RGB32Float, 0, 12},
    };
    RHIInputElementDesc elements2[] = {
        {RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, 0},
        {RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float, 0, 12},
    };

    auto desc1 = MakeDefaultDesc();
    desc1.inputElements = elements1;
    desc1.inputElementCount = 2;
    desc1.vertexStride = 24;

    auto desc2 = MakeDefaultDesc();
    desc2.inputElements = elements2;
    desc2.inputElementCount = 2;
    desc2.vertexStride = 20;

    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, BindlessVsSM5ProducesDifferentKey) {
    RHIInputElementDesc elements[] = {
        {RHIInputSemantic::Position, 0, RHIFormat::RGB32Float, 0, 0},
    };

    auto desc1 = MakeDefaultDesc();
    // SM6.6 bindless: no input elements
    desc1.inputElements = nullptr;
    desc1.inputElementCount = 0;

    auto desc2 = MakeDefaultDesc();
    // SM5: has input layout
    desc2.inputElements = elements;
    desc2.inputElementCount = 1;
    desc2.vertexStride = 12;

    EXPECT_NE(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DebugNameDoesNotAffectKey) {
    auto desc1 = MakeDefaultDesc();
    desc1.debugName = "pipeline_A";

    auto desc2 = MakeDefaultDesc();
    desc2.debugName = "pipeline_B";

    EXPECT_EQ(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, NullDebugNameSameAsNonNull) {
    auto desc1 = MakeDefaultDesc();
    desc1.debugName = nullptr;

    auto desc2 = MakeDefaultDesc();
    desc2.debugName = "some_name";

    EXPECT_EQ(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, DefaultConstructedDescProducesConsistentKey) {
    RHIGraphicsPipelineDesc desc1{};
    RHIGraphicsPipelineDesc desc2{};
    EXPECT_EQ(ComputePsoCacheKey(desc1), ComputePsoCacheKey(desc2));
}

TEST(PsoCacheKey, InvalidKeyHasZeroHash) {
    PsoCacheKey key = PsoCacheKey::Invalid();
    EXPECT_EQ(key.hash, 0u);
    EXPECT_FALSE(key.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// PsoCache — in-memory cache operations
// ─────────────────────────────────────────────────────────────────────────────

class PsoCacheTest : public ::testing::Test {
protected:
    void SetUp() override { cache.Init(16); }
    void TearDown() override { cache.Shutdown(); }

    PsoCache cache;
};

TEST_F(PsoCacheTest, InitSetsCapacity) {
    EXPECT_TRUE(cache.IsInitialized());
    EXPECT_EQ(cache.GetCapacity(), 16u);
    EXPECT_EQ(cache.GetEntryCount(), 0u);
}

TEST_F(PsoCacheTest, LookupMissReturnsInvalidHandle) {
    PsoCacheKey key{42};
    auto result = cache.Lookup(key);
    EXPECT_FALSE(result.IsValid());
}

TEST_F(PsoCacheTest, InsertAndLookupReturnsCorrectHandle) {
    PsoCacheKey key{123};
    RHIPipelineHandle handle{5, 1};
    cache.Insert(key, handle);
    auto result = cache.Lookup(key);
    EXPECT_TRUE(result.IsValid());
    EXPECT_EQ(result, handle);
}

TEST_F(PsoCacheTest, MultipleInsertsAndLookups) {
    PsoCacheKey keyA{100};
    PsoCacheKey keyB{200};
    PsoCacheKey keyC{300};
    RHIPipelineHandle handleA{1, 1};
    RHIPipelineHandle handleB{2, 1};
    RHIPipelineHandle handleC{3, 1};

    cache.Insert(keyA, handleA);
    cache.Insert(keyB, handleB);
    cache.Insert(keyC, handleC);

    EXPECT_EQ(cache.Lookup(keyA), handleA);
    EXPECT_EQ(cache.Lookup(keyB), handleB);
    EXPECT_EQ(cache.Lookup(keyC), handleC);
    EXPECT_EQ(cache.GetEntryCount(), 3u);
}

TEST_F(PsoCacheTest, InsertDuplicateKeyOverwritesHandle) {
    PsoCacheKey key{42};
    RHIPipelineHandle oldHandle{1, 1};
    RHIPipelineHandle newHandle{2, 2};

    cache.Insert(key, oldHandle);
    EXPECT_EQ(cache.Lookup(key), oldHandle);

    cache.Insert(key, newHandle);
    EXPECT_EQ(cache.Lookup(key), newHandle);
    EXPECT_EQ(cache.GetEntryCount(), 1u);
}

TEST_F(PsoCacheTest, ClearEvictsAllEntries) {
    cache.Insert({10}, {1, 1});
    cache.Insert({20}, {2, 1});
    cache.Insert({30}, {3, 1});
    EXPECT_EQ(cache.GetEntryCount(), 3u);

    cache.Clear();
    EXPECT_EQ(cache.GetEntryCount(), 0u);
    EXPECT_FALSE(cache.Lookup({10}).IsValid());
    EXPECT_FALSE(cache.Lookup({20}).IsValid());
    EXPECT_FALSE(cache.Lookup({30}).IsValid());
}

TEST_F(PsoCacheTest, InvalidateByShaderClearsAll) {
    cache.Insert({10}, {1, 1});
    cache.Insert({20}, {2, 1});
    cache.Insert({30}, {3, 1});

    RHIShaderHandle shader{1, 1};
    uint32_t evicted = cache.InvalidateByShader(shader);
    EXPECT_EQ(evicted, 3u);
    EXPECT_EQ(cache.GetEntryCount(), 0u);
}

TEST_F(PsoCacheTest, InvalidateEmptyCacheReturnsZero) {
    RHIShaderHandle shader{1, 1};
    uint32_t evicted = cache.InvalidateByShader(shader);
    EXPECT_EQ(evicted, 0u);
}

TEST_F(PsoCacheTest, LookupWithInvalidKeyReturnsInvalid) {
    cache.Insert({42}, {1, 1});
    PsoCacheKey invalidKey = PsoCacheKey::Invalid();
    EXPECT_FALSE(cache.Lookup(invalidKey).IsValid());
}

TEST_F(PsoCacheTest, InsertWithInvalidKeyIsIgnored) {
    PsoCacheKey invalidKey = PsoCacheKey::Invalid();
    cache.Insert(invalidKey, {1, 1});
    EXPECT_EQ(cache.GetEntryCount(), 0u);
}

TEST_F(PsoCacheTest, LoadFactorTracksCorrectly) {
    EXPECT_FLOAT_EQ(cache.GetLoadFactor(), 0.0F);

    cache.Insert({10}, {1, 1});
    EXPECT_FLOAT_EQ(cache.GetLoadFactor(), 1.0F / 16.0F);

    cache.Insert({20}, {2, 1});
    EXPECT_FLOAT_EQ(cache.GetLoadFactor(), 2.0F / 16.0F);
}

TEST_F(PsoCacheTest, RehashDoublesCapacityAndPreservesEntries) {
    // Fill to > 70% of 16 slots = need > 11 entries
    for (uint32_t i = 1; i <= 12; ++i) {
        cache.Insert({i * 100ULL}, {i, 1});
    }

    // Should have rehashed to 32
    EXPECT_EQ(cache.GetCapacity(), 32u);
    EXPECT_EQ(cache.GetEntryCount(), 12u);

    // All entries should still be findable
    for (uint32_t i = 1; i <= 12; ++i) {
        auto result = cache.Lookup({i * 100ULL});
        EXPECT_TRUE(result.IsValid());
        EXPECT_EQ(result.index, i);
    }
}

TEST_F(PsoCacheTest, ShutdownMakesCacheUnitialized) {
    cache.Shutdown();
    EXPECT_FALSE(cache.IsInitialized());
    EXPECT_EQ(cache.GetCapacity(), 0u);
    EXPECT_EQ(cache.GetEntryCount(), 0u);
}

TEST_F(PsoCacheTest, DoubleInitIsNoop) {
    cache.Init(64);
    // Capacity should remain 16 from SetUp, not change to 64
    EXPECT_EQ(cache.GetCapacity(), 16u);
}

TEST_F(PsoCacheTest, CapacityRoundsUpToPowerOfTwo) {
    PsoCache cache2;
    cache2.Init(10);
    EXPECT_EQ(cache2.GetCapacity(), 16u);
    cache2.Shutdown();

    PsoCache cache3;
    cache3.Init(17);
    EXPECT_EQ(cache3.GetCapacity(), 32u);
    cache3.Shutdown();

    PsoCache cache4;
    cache4.Init(1);
    EXPECT_EQ(cache4.GetCapacity(), 1u);
    cache4.Shutdown();
}

// ─────────────────────────────────────────────────────────────────────────────
// Integration — PsoCacheKey + PsoCache together
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(PsoCacheTest, FullWorkflowWithRealDescriptors) {
    auto desc = MakeDefaultDesc();
    PsoCacheKey key = ComputePsoCacheKey(desc);
    RHIPipelineHandle handle{10, 1};

    // Miss
    EXPECT_FALSE(cache.Lookup(key).IsValid());

    // Insert
    cache.Insert(key, handle);

    // Hit
    auto cached = cache.Lookup(key);
    EXPECT_TRUE(cached.IsValid());
    EXPECT_EQ(cached, handle);

    // Same descriptor, different debugName — still hit
    auto desc2 = desc;
    desc2.debugName = "different_name";
    PsoCacheKey key2 = ComputePsoCacheKey(desc2);
    EXPECT_EQ(key, key2);
    EXPECT_EQ(cache.Lookup(key2), handle);
}

TEST_F(PsoCacheTest, DifferentDescriptorsGetDifferentSlots) {
    auto descA = MakeDefaultDesc();
    descA.rasterizer.cullMode = RHICullMode::Back;

    auto descB = MakeDefaultDesc();
    descB.rasterizer.cullMode = RHICullMode::Front;

    PsoCacheKey keyA = ComputePsoCacheKey(descA);
    PsoCacheKey keyB = ComputePsoCacheKey(descB);

    RHIPipelineHandle handleA{1, 1};
    RHIPipelineHandle handleB{2, 1};

    cache.Insert(keyA, handleA);
    cache.Insert(keyB, handleB);

    EXPECT_EQ(cache.Lookup(keyA), handleA);
    EXPECT_EQ(cache.Lookup(keyB), handleB);
    EXPECT_EQ(cache.GetEntryCount(), 2u);
}
