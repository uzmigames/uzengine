/// Unit tests for constant buffer definitions and CBRingAllocator.
///
/// Validates struct sizes, alignment, field offsets, and ring allocator logic.

#include "constant_buffers.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// PerFrameCB layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(PerFrameCB, SizeIs768Bytes) {
    EXPECT_EQ(sizeof(PerFrameCB), 768u);
}

TEST(PerFrameCB, FieldOffsets) {
    EXPECT_EQ(offsetof(PerFrameCB, viewProj), 0u);
    EXPECT_EQ(offsetof(PerFrameCB, cameraPos), 64u);
    EXPECT_EQ(offsetof(PerFrameCB, timeParams), 80u);
}

TEST(PerFrameCB, DefaultInitialization) {
    PerFrameCB cb{};
    // Mat4 defaults to identity
    const auto* raw = reinterpret_cast<const float*>(&cb.viewProj);
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            float expected = (i == j) ? 1.0f : 0.0f;
            EXPECT_FLOAT_EQ(raw[i * 4 + j], expected);
        }
    }
    EXPECT_FLOAT_EQ(cb.cameraPos.x, 0.0f);
    EXPECT_FLOAT_EQ(cb.timeParams.x, 0.0f);
}

TEST(PerFrameCB, WriteAndReadBack) {
    PerFrameCB cb{};
    cb.cameraPos = {10.0f, 20.0f, 30.0f, 0.0f};
    cb.timeParams = {1.5f, 0.016f, 90.0f, 0.0f};

    EXPECT_FLOAT_EQ(cb.cameraPos.x, 10.0f);
    EXPECT_FLOAT_EQ(cb.cameraPos.z, 30.0f);
    EXPECT_FLOAT_EQ(cb.timeParams.x, 1.5f);
    EXPECT_FLOAT_EQ(cb.timeParams.y, 0.016f);
}

TEST(PerFrameCB, MemcpyRoundTrip) {
    PerFrameCB src{};
    src.cameraPos = {1.0f, 2.0f, 3.0f, 0.0f};
    src.timeParams = {100.0f, 0.016f, 6000.0f, 0.0f};

    uint8_t buffer[768];
    std::memcpy(buffer, &src, sizeof(src));

    PerFrameCB dst{};
    std::memcpy(&dst, buffer, sizeof(dst));

    EXPECT_FLOAT_EQ(dst.cameraPos.x, 1.0f);
    EXPECT_FLOAT_EQ(dst.cameraPos.z, 3.0f);
    EXPECT_FLOAT_EQ(dst.timeParams.x, 100.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// PerObjectCB layout
// ─────────────────────────────────────────────────────────────────────────────

TEST(PerObjectCB, SizeIs256Bytes) {
    EXPECT_EQ(sizeof(PerObjectCB), 256u);
}

TEST(PerObjectCB, FieldOffsets) {
    // Scalars first (offset 0): 6 uint32 indices + alphaClipThreshold + opacitySrvIndex = 32 bytes
    EXPECT_EQ(offsetof(PerObjectCB, albedoIndex),    0u);
    EXPECT_EQ(offsetof(PerObjectCB, normalIndex),    4u);
    EXPECT_EQ(offsetof(PerObjectCB, roughnessIndex), 8u);
    EXPECT_EQ(offsetof(PerObjectCB, metallicIndex), 12u);
    EXPECT_EQ(offsetof(PerObjectCB, aoIndex),       16u);
    EXPECT_EQ(offsetof(PerObjectCB, emissiveIndex), 20u);
    EXPECT_EQ(offsetof(PerObjectCB, alphaClipThreshold), 24u);
    EXPECT_EQ(offsetof(PerObjectCB, opacitySrvIndex), 28u);
    // Matrices after scalars (offset 32)
    EXPECT_EQ(offsetof(PerObjectCB, model),         32u);
    EXPECT_EQ(offsetof(PerObjectCB, normalMatrix),  96u);
    // Blend mode at end (offset 252)
    EXPECT_EQ(offsetof(PerObjectCB, materialFlags),    252u);
}

TEST(PerObjectCB, MaterialFlagsDefaults) {
    PerObjectCB cb{};
    EXPECT_EQ(cb.materialFlags, 0u);               // Opaque by default
    EXPECT_NEAR(cb.alphaClipThreshold, 0.3333f, 1e-4f); // default
    EXPECT_EQ(cb.opacitySrvIndex, 0u);         // No opacity texture by default
    EXPECT_FLOAT_EQ(cb.opacity, 1.0f);         // Fully opaque
}

TEST(PerObjectCB, DefaultInitialization) {
    PerObjectCB cb{};
    EXPECT_EQ(cb.albedoIndex, 0u);
    EXPECT_EQ(cb.normalIndex, 0u);
    EXPECT_EQ(cb.roughnessIndex, 0u);
    EXPECT_EQ(cb.metallicIndex, 0u);
    EXPECT_EQ(cb.aoIndex, 0u);
    EXPECT_EQ(cb.emissiveIndex, 0u);
    EXPECT_NEAR(cb.alphaClipThreshold, 0.3333f, 1e-4f);
    EXPECT_EQ(cb.opacitySrvIndex, 0u);
    EXPECT_EQ(cb.materialFlags, 0u);
}

TEST(PerObjectCB, WriteTextureIndices) {
    PerObjectCB cb{};
    cb.albedoIndex    = 1;
    cb.normalIndex    = 2;
    cb.roughnessIndex = 3;
    cb.metallicIndex  = 4;
    cb.aoIndex        = 5;
    EXPECT_EQ(cb.albedoIndex, 1u);
    EXPECT_EQ(cb.normalIndex, 2u);
    EXPECT_EQ(cb.roughnessIndex, 3u);
    EXPECT_EQ(cb.metallicIndex, 4u);
    EXPECT_EQ(cb.aoIndex, 5u);
}

TEST(PerObjectCB, MemcpyRoundTrip) {
    PerObjectCB src{};
    src.albedoIndex    = 10;
    src.normalIndex    = 20;
    src.roughnessIndex = 30;
    src.metallicIndex  = 40;
    src.aoIndex        = 50;

    uint8_t buffer[256];
    std::memcpy(buffer, &src, sizeof(src));

    PerObjectCB dst{};
    std::memcpy(&dst, buffer, sizeof(dst));
    EXPECT_EQ(dst.albedoIndex, 10u);
    EXPECT_EQ(dst.normalIndex, 20u);
    EXPECT_EQ(dst.roughnessIndex, 30u);
    EXPECT_EQ(dst.metallicIndex, 40u);
    EXPECT_EQ(dst.aoIndex, 50u);
}

// ─────────────────────────────────────────────────────────────────────────────
// CBAllocation struct
// ─────────────────────────────────────────────────────────────────────────────

TEST(CBAllocation, DefaultIsNull) {
    CBAllocation alloc{};
    EXPECT_EQ(alloc.cpuPtr, nullptr);
    EXPECT_EQ(alloc.gpuAddr, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// CBRingAllocator — argument validation (no device)
// ─────────────────────────────────────────────────────────────────────────────

TEST(CBRingAllocator, InitRejectsNullDevice) {
    CBRingAllocator ring;
    EXPECT_FALSE(ring.Init(nullptr, 3, 64 * 1024));
}

TEST(CBRingAllocator, InitRejectsZeroFrames) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    CBRingAllocator ring;
    EXPECT_FALSE(ring.Init(fakeDevice, 0, 64 * 1024));
}

TEST(CBRingAllocator, InitRejectsZeroBytes) {
    auto* fakeDevice = reinterpret_cast<IRHIDevice*>(0x1);
    CBRingAllocator ring;
    EXPECT_FALSE(ring.Init(fakeDevice, 3, 0));
}

TEST(CBRingAllocator, ShutdownOnUninitializedIsNoOp) {
    CBRingAllocator ring;
    ring.Shutdown(); // Should not crash
}

TEST(CBRingAllocator, DestructorOnUninitializedIsNoOp) {
    { CBRingAllocator ring; } // Should not crash
}
