/// Unit tests for DX11 resource types and ResourcePool.
///
/// Tests DX11Pipeline, DX11Buffer, DX11Texture, DX11Shader, DX11Sampler
/// struct defaults and ResourcePool generational handle validation.
///
/// All tests are CPU-only — no D3D11 device or GPU required.
/// ResourcePool is a pure data structure; pointer fields are verified
/// as nullptr without creating real COM objects.

#include "../../engine/rhi/dx11/dx11_resources.h"

#include <gtest/gtest.h>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// DX11Pipeline Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX11Pipeline, DefaultValues) {
    DX11Pipeline pipeline{};
    EXPECT_EQ(pipeline.inputLayout,  nullptr);
    EXPECT_EQ(pipeline.rasterizer,   nullptr);
    EXPECT_EQ(pipeline.depthStencil, nullptr);
    EXPECT_EQ(pipeline.blend,        nullptr);
    EXPECT_EQ(pipeline.topology,     D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    EXPECT_EQ(pipeline.generation,   0u);

    // Shader handles must default to invalid (index=0, generation=0)
    EXPECT_FALSE(pipeline.vertexShader.IsValid());
    EXPECT_FALSE(pipeline.pixelShader.IsValid());
    EXPECT_EQ(pipeline.vertexShader.index,      0u);
    EXPECT_EQ(pipeline.vertexShader.generation, 0u);
    EXPECT_EQ(pipeline.pixelShader.index,       0u);
    EXPECT_EQ(pipeline.pixelShader.generation,  0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX11Buffer Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX11Buffer, DefaultValues) {
    DX11Buffer buffer{};
    EXPECT_EQ(buffer.buffer,     nullptr);
    EXPECT_EQ(buffer.srv,        nullptr);
    EXPECT_EQ(buffer.uav,        nullptr);
    EXPECT_EQ(buffer.srvIndex,   UINT32_MAX);
    EXPECT_EQ(buffer.generation, 0u);
    EXPECT_EQ(buffer.mappedPtr,  nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX11Texture Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX11Texture, DefaultValues) {
    DX11Texture texture{};
    EXPECT_EQ(texture.texture,    nullptr);
    EXPECT_EQ(texture.srv,        nullptr);
    EXPECT_EQ(texture.rtv,        nullptr);
    EXPECT_EQ(texture.dsv,        nullptr);
    EXPECT_EQ(texture.uav,        nullptr);
    EXPECT_EQ(texture.srvIndex,   UINT32_MAX);
    EXPECT_EQ(texture.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX11Shader Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX11Shader, DefaultValues) {
    DX11Shader shader{};
    EXPECT_TRUE(shader.bytecode.empty());
    EXPECT_EQ(shader.vs,         nullptr);
    EXPECT_EQ(shader.ps,         nullptr);
    EXPECT_EQ(shader.cs,         nullptr);
    EXPECT_EQ(shader.stage,      RHIShaderStage::Vertex);
    EXPECT_EQ(shader.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DX11Sampler Defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(DX11Sampler, DefaultValues) {
    DX11Sampler sampler{};
    EXPECT_EQ(sampler.sampler,      nullptr);
    EXPECT_EQ(sampler.samplerIndex, UINT32_MAX);
    EXPECT_EQ(sampler.generation,   0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Generational Handle Validation (DX11Pipeline)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_DX11Pipeline, AllocateReturnsValidSlot) {
    ResourcePool<DX11Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    DX11Pipeline* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->inputLayout, nullptr);
    EXPECT_EQ(slot->generation, gen);
}

TEST(ResourcePool_DX11Pipeline, GetWithWrongGenerationReturnsNull) {
    ResourcePool<DX11Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0u),      nullptr);
}

TEST(ResourcePool_DX11Pipeline, GetWithOutOfRangeIndexReturnsNull) {
    ResourcePool<DX11Pipeline> pool;
    EXPECT_EQ(pool.Get(999u, 1u), nullptr);
}

TEST(ResourcePool_DX11Pipeline, FreeAndReallocate) {
    ResourcePool<DX11Pipeline> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    // Reallocate should reuse the freed slot
    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    // Old generation must be invalid
    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    // New generation must be valid
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool_DX11Pipeline, MultipleAllocations) {
    ResourcePool<DX11Pipeline> pool;
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

TEST(ResourcePool_DX11Pipeline, ConstGet) {
    ResourcePool<DX11Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const DX11Pipeline* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->inputLayout, nullptr);
    EXPECT_EQ(slot->generation,  gen);
}

TEST(ResourcePool_DX11Pipeline, SlotsAccess) {
    ResourcePool<DX11Pipeline> pool;
    uint32_t gen = 0;
    pool.Allocate(gen);
    pool.Allocate(gen);

    EXPECT_EQ(pool.Slots().size(), 2u);

    const auto& constPool = pool;
    EXPECT_EQ(constPool.Slots().size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — DX11Buffer
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_DX11Buffer, AllocateReturnsValidSlot) {
    ResourcePool<DX11Buffer> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    DX11Buffer* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->buffer,    nullptr);
    EXPECT_EQ(slot->srvIndex,  UINT32_MAX);
    EXPECT_EQ(slot->mappedPtr, nullptr);
    EXPECT_EQ(slot->generation, gen);
}

TEST(ResourcePool_DX11Buffer, GetWithWrongGenerationReturnsNull) {
    ResourcePool<DX11Buffer> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0u),      nullptr);
}

TEST(ResourcePool_DX11Buffer, FreeAndReallocate) {
    ResourcePool<DX11Buffer> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool_DX11Buffer, ConstGet) {
    ResourcePool<DX11Buffer> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const DX11Buffer* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->buffer,   nullptr);
    EXPECT_EQ(slot->srvIndex, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — DX11Texture
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_DX11Texture, AllocateReturnsValidSlot) {
    ResourcePool<DX11Texture> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    DX11Texture* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->texture,   nullptr);
    EXPECT_EQ(slot->srv,       nullptr);
    EXPECT_EQ(slot->rtv,       nullptr);
    EXPECT_EQ(slot->dsv,       nullptr);
    EXPECT_EQ(slot->uav,       nullptr);
    EXPECT_EQ(slot->srvIndex,  UINT32_MAX);
    EXPECT_EQ(slot->generation, gen);
}

TEST(ResourcePool_DX11Texture, GetWithWrongGenerationReturnsNull) {
    ResourcePool<DX11Texture> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0u),      nullptr);
}

TEST(ResourcePool_DX11Texture, FreeAndReallocate) {
    ResourcePool<DX11Texture> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool_DX11Texture, ConstGet) {
    ResourcePool<DX11Texture> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const DX11Texture* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->texture,  nullptr);
    EXPECT_EQ(slot->srvIndex, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — DX11Shader
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_DX11Shader, AllocateReturnsValidSlot) {
    ResourcePool<DX11Shader> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    DX11Shader* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->bytecode.empty());
    EXPECT_EQ(slot->vs,    nullptr);
    EXPECT_EQ(slot->ps,    nullptr);
    EXPECT_EQ(slot->cs,    nullptr);
    EXPECT_EQ(slot->stage, RHIShaderStage::Vertex);
    EXPECT_EQ(slot->generation, gen);
}

TEST(ResourcePool_DX11Shader, GetWithWrongGenerationReturnsNull) {
    ResourcePool<DX11Shader> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0u),      nullptr);
}

TEST(ResourcePool_DX11Shader, FreeAndReallocate) {
    ResourcePool<DX11Shader> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool_DX11Shader, ConstGet) {
    ResourcePool<DX11Shader> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const DX11Shader* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_TRUE(slot->bytecode.empty());
    EXPECT_EQ(slot->vs, nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — DX11Sampler
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_DX11Sampler, AllocateReturnsValidSlot) {
    ResourcePool<DX11Sampler> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(idx, 0u);
    EXPECT_GT(gen, 0u);

    DX11Sampler* slot = pool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->sampler,      nullptr);
    EXPECT_EQ(slot->samplerIndex, UINT32_MAX);
    EXPECT_EQ(slot->generation,   gen);
}

TEST(ResourcePool_DX11Sampler, GetWithWrongGenerationReturnsNull) {
    ResourcePool<DX11Sampler> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(pool.Get(idx, gen + 1), nullptr);
    EXPECT_EQ(pool.Get(idx, 0u),      nullptr);
}

TEST(ResourcePool_DX11Sampler, FreeAndReallocate) {
    ResourcePool<DX11Sampler> pool;
    uint32_t gen1 = 0;
    uint32_t idx1 = pool.Allocate(gen1);
    pool.Free(idx1);

    uint32_t gen2 = 0;
    uint32_t idx2 = pool.Allocate(gen2);
    EXPECT_EQ(idx2, idx1);
    EXPECT_GT(gen2, gen1);

    EXPECT_EQ(pool.Get(idx1, gen1), nullptr);
    EXPECT_NE(pool.Get(idx2, gen2), nullptr);
}

TEST(ResourcePool_DX11Sampler, GetWithOutOfRangeIndexReturnsNull) {
    ResourcePool<DX11Sampler> pool;
    EXPECT_EQ(pool.Get(999u, 1u), nullptr);
}

TEST(ResourcePool_DX11Sampler, ConstGet) {
    ResourcePool<DX11Sampler> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    const auto& constPool = pool;
    const DX11Sampler* slot = constPool.Get(idx, gen);
    ASSERT_NE(slot, nullptr);
    EXPECT_EQ(slot->sampler,      nullptr);
    EXPECT_EQ(slot->samplerIndex, UINT32_MAX);
}

TEST(ResourcePool_DX11Sampler, SlotsAccess) {
    ResourcePool<DX11Sampler> pool;
    uint32_t gen = 0;
    pool.Allocate(gen);
    pool.Allocate(gen);

    EXPECT_EQ(pool.Slots().size(), 2u);

    const auto& constPool = pool;
    EXPECT_EQ(constPool.Slots().size(), 2u);
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Cross-type isolation (free list does not bleed across types)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_Isolation, FreeListDoesNotBleedAcrossTypes) {
    ResourcePool<DX11Buffer>  bufferPool;
    ResourcePool<DX11Texture> texturePool;

    uint32_t bufGen = 0, texGen = 0;
    uint32_t bufIdx = bufferPool.Allocate(bufGen);
    uint32_t texIdx = texturePool.Allocate(texGen);

    bufferPool.Free(bufIdx);

    // texturePool is unaffected — slot 0 still valid
    EXPECT_NE(texturePool.Get(texIdx, texGen), nullptr);

    // After free + reallocate, old generation is stale
    uint32_t newGen = 0;
    uint32_t newIdx = bufferPool.Allocate(newGen);
    EXPECT_EQ(newIdx, bufIdx);         // reuses freed slot
    EXPECT_NE(newGen, bufGen);         // new generation differs
    EXPECT_EQ(bufferPool.Get(bufIdx, bufGen), nullptr);  // old handle invalid
}

// ─────────────────────────────────────────────────────────────────────────────
// ResourcePool — Generation zero is always invalid (never a live generation)
// ─────────────────────────────────────────────────────────────────────────────

TEST(ResourcePool_DX11Buffer, GenerationZeroAlwaysInvalid) {
    ResourcePool<DX11Buffer> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    // gen is now 1 (first allocation increments from 0 to 1)
    EXPECT_EQ(gen, 1u);
    // Explicitly passing generation=0 must always fail
    EXPECT_EQ(pool.Get(idx, 0u), nullptr);
}

TEST(ResourcePool_DX11Pipeline, GenerationZeroAlwaysInvalid) {
    ResourcePool<DX11Pipeline> pool;
    uint32_t gen = 0;
    uint32_t idx = pool.Allocate(gen);

    EXPECT_EQ(gen, 1u);
    EXPECT_EQ(pool.Get(idx, 0u), nullptr);
}
