/// Unit tests for DX12 resource types and ResourcePool.
///
/// Tests DX12Pipeline, DX12Buffer, DX12Texture, DX12Shader, DX12Sampler
/// struct defaults and ResourcePool generational handle validation.

#include "dx12_resources.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// DX12Pipeline Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX12Pipeline, DefaultValues) {
    DX12Pipeline pipeline{};
    EXPECT_EQ(pipeline.pso, nullptr);
    EXPECT_EQ(pipeline.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12Buffer Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX12Buffer, DefaultValues) {
    DX12Buffer buffer{};
    EXPECT_EQ(buffer.allocation, nullptr);
    EXPECT_EQ(buffer.resource, nullptr);
    EXPECT_EQ(buffer.srvIndex, UINT32_MAX);
    EXPECT_EQ(buffer.uavIndex, UINT32_MAX);
    EXPECT_EQ(buffer.generation, 0u);
    EXPECT_EQ(buffer.mappedPtr, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12Texture Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX12Texture, DefaultValues) {
    DX12Texture texture{};
    EXPECT_EQ(texture.allocation, nullptr);
    EXPECT_EQ(texture.resource, nullptr);
    EXPECT_EQ(texture.srvIndex, UINT32_MAX);
    EXPECT_EQ(texture.uavIndex, UINT32_MAX);
    EXPECT_EQ(texture.rtvIndex, UINT32_MAX);
    EXPECT_EQ(texture.dsvIndex, UINT32_MAX);
    EXPECT_EQ(texture.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12Shader Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX12Shader, DefaultValues) {
    DX12Shader shader{};
    EXPECT_TRUE(shader.bytecode.empty());
    EXPECT_EQ(shader.stage, RHIShaderStage::Vertex);
    EXPECT_EQ(shader.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX12Sampler Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX12Sampler, DefaultValues) {
    DX12Sampler sampler{};
    EXPECT_EQ(sampler.samplerIndex, UINT32_MAX);
    EXPECT_EQ(sampler.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Generational Handle Validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool, AllocateReturnsValidSlot) {
    ResourcePool<DX12Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);
    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    DX12Pipeline* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->pso, nullptr);
}

TEST(ResourcePool, GetWithWrongGenerationReturnsNull) {
    ResourcePool<DX12Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0), nullptr);
}

TEST(ResourcePool, GetWithOutOfRangeIndexReturnsNull) {
    ResourcePool<DX12Pipeline> pool;
    EXPECT_EQ(pool.Get(999, 1), nullptr);
}

TEST(ResourcePool, FreeAndReallocate) {
    ResourcePool<DX12Pipeline> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    // Reallocate should reuse the freed slot
    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    // Old generation should be invalid
    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    // New generation should be valid
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool, MultipleAllocations) {
    ResourcePool<DX12Pipeline> pool;
    uint32_t gen0 = 0, gen1 = 0, gen2 = 0;
    uint32_t idx0 = pool.Allocate(gen0);
    uint32_t idx1 = pool.Allocate(gen1);
    uint32_t idx2 = pool.Allocate(gen2);

    EXPECT_EQ(idx0, 0u);
    EXPECT_EQ(idx1, 1u);
    EXPECT_EQ(idx2, 2u);

    EXPECT_NE(pool.Get(idx0, gen0), nullptr);
    EXPECT_NE(pool.Get(idx1, gen1), nullptr);
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool, ConstGet) {
    ResourcePool<DX12Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const DX12Pipeline* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->pso, nullptr);
}

TEST(ResourcePool, SlotsAccess) {
    ResourcePool<DX12Pipeline> pool;
    uint32_t gen = 0;
    pool.Allocate(gen);
    pool.Allocate(gen);

    EXPECT_EQ(pool.Slots().size(), 2u);

    const auto& constPool = pool;
    EXPECT_EQ(constPool.Slots().size(), 2u);
}
