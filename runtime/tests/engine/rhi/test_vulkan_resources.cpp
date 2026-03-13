/// Unit tests for Vulkan resource types and ResourcePool.
///
/// Tests VulkanPipeline, VulkanBuffer, VulkanTexture, VulkanShader, VulkanSampler
/// struct defaults and ResourcePool generational handle validation.
///
/// All tests are CPU-only — no Vulkan instance or device is created.
/// VK_NO_PROTOTYPES prevents any Vulkan loader symbol references.

#define VK_NO_PROTOTYPES
#ifdef _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include "vulkan_resources.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// VulkanPipeline Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanPipeline, DefaultValues) {
    VulkanPipeline pipeline{};
    EXPECT_EQ(pipeline.pipeline, VK_NULL_HANDLE);
    EXPECT_EQ(pipeline.pipelineLayout, VK_NULL_HANDLE);
    EXPECT_EQ(pipeline.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VulkanBuffer Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanBuffer, DefaultValues) {
    VulkanBuffer buffer{};
    EXPECT_EQ(buffer.buffer, VK_NULL_HANDLE);
    EXPECT_EQ(buffer.allocation, nullptr);
    EXPECT_EQ(buffer.bufferView, VK_NULL_HANDLE);
    EXPECT_EQ(buffer.mappedPtr, nullptr);
    EXPECT_EQ(buffer.gpuAddress, 0u);
    EXPECT_EQ(buffer.srvIndex, UINT32_MAX);
    EXPECT_EQ(buffer.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VulkanTexture Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanTexture, DefaultValues) {
    VulkanTexture texture{};
    EXPECT_EQ(texture.image, VK_NULL_HANDLE);
    EXPECT_EQ(texture.allocation, nullptr);
    EXPECT_EQ(texture.imageView, VK_NULL_HANDLE);
    EXPECT_EQ(texture.currentLayout, VK_IMAGE_LAYOUT_UNDEFINED);
    EXPECT_EQ(texture.srvIndex, UINT32_MAX);
    EXPECT_EQ(texture.rtvIndex, UINT32_MAX);
    EXPECT_EQ(texture.dsvIndex, UINT32_MAX);
    EXPECT_EQ(texture.generation, 0u);
    EXPECT_TRUE(texture.ownsImage);
}

// ─────────────────────────────────────────────────────────────────────────────
// VulkanShader Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanShader, DefaultValues) {
    VulkanShader shader{};
    EXPECT_EQ(shader.module, VK_NULL_HANDLE);
    EXPECT_TRUE(shader.bytecode.empty());
    EXPECT_EQ(shader.stage, RHIShaderStage::Vertex);
    EXPECT_EQ(shader.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// VulkanSampler Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanSampler, DefaultValues) {
    VulkanSampler sampler{};
    EXPECT_EQ(sampler.sampler, VK_NULL_HANDLE);
    EXPECT_EQ(sampler.samplerIndex, UINT32_MAX);
    EXPECT_EQ(sampler.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Generational Handle Validation
// ─────────────────────────────────────────────────────────────────────────────

TEST(VulkanResourcePool, AllocateReturnsValidSlot) {
    ResourcePool<VulkanPipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);
    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    VulkanPipeline* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->pipeline, VK_NULL_HANDLE);
}

TEST(VulkanResourcePool, GetWithWrongGenerationReturnsNull) {
    ResourcePool<VulkanPipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0), nullptr);
}

TEST(VulkanResourcePool, GetWithOutOfRangeIndexReturnsNull) {
    ResourcePool<VulkanPipeline> pool;
    EXPECT_EQ(pool.Get(999, 1), nullptr);
}

TEST(VulkanResourcePool, FreeAndReallocate) {
    ResourcePool<VulkanPipeline> pool;
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

TEST(VulkanResourcePool, MultipleAllocations) {
    ResourcePool<VulkanPipeline> pool;
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

TEST(VulkanResourcePool, ConstGet) {
    ResourcePool<VulkanPipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const VulkanPipeline* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->pipeline, VK_NULL_HANDLE);
}

TEST(VulkanResourcePool, SlotsAccess) {
    ResourcePool<VulkanPipeline> pool;
    uint32_t gen = 0;
    pool.Allocate(gen);
    pool.Allocate(gen);

    EXPECT_EQ(pool.Slots().size(), 2u);

    const auto& constPool = pool;
    EXPECT_EQ(constPool.Slots().size(), 2u);
}
