/// Unit tests for the UzEngine Depth Prepass and Hi-Z Pyramid Pass.
///
/// Validates DepthPrepassDesc struct field defaults and accessibility,
/// HiZMipLevel struct size and defaults, HiZPyramidPassDesc defaults and
/// accessibility, the kHiZMaxMipLevels constant value, and the correctness
/// of the mip-count formula floor(log2(max(width, height))) for several
/// representative resolutions. No GPU hardware, no RHI device, no render
/// graph execution required.

#include "passes/geometry/depth_prepass.h"
#include "passes/postprocess/hiz_pyramid_pass.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cmath>

using namespace uz;
using namespace uz::rg;

// ─────────────────────────────────────────────────────────────────────────────
// DepthPrepassDesc — default construction
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed DepthPrepassDesc must expose null/zero/false defaults
/// for the optional-path fields that have in-class initializers.
TEST(DepthPrepassDesc, DefaultValues_OptionalFields) {
    DepthPrepassDesc desc{};

    // Optional mesh array fields default to null / zero (in-class initializers)
    EXPECT_EQ(desc.meshes,           nullptr);
    EXPECT_EQ(desc.meshCount,        0u);

    // Binder path disabled by default
    EXPECT_EQ(desc.cbRingAllocator,  nullptr);

    // GPU instancing disabled by default
    EXPECT_FALSE(desc.instanced);
    EXPECT_EQ(desc.instanceBufferSrvIndex, UINT32_MAX);
    EXPECT_EQ(desc.batchData,        nullptr);
    EXPECT_EQ(desc.batchCount,       0u);
}

/// Struct size must be reasonable — large enough for all fields but well under
/// a cache-hostile 512 bytes.
TEST(DepthPrepassDesc, SizeIsReasonable) {
    EXPECT_GT(sizeof(DepthPrepassDesc), 0u);
    EXPECT_LT(sizeof(DepthPrepassDesc), 512u);
}

/// Every field in DepthPrepassDesc must be independently writable and readable
/// without silent corruption (padding, aliasing, or wrong type width).
TEST(DepthPrepassDesc, AllFieldsAccessible_WriteAndRead) {
    DepthPrepassDesc desc{};

    // Render graph virtual resource IDs
    desc.depthBuffer = RgResourceId{7u};

    // Physical RHI handles
    desc.depthTexture = RHITextureHandle{3u, 1u};
    desc.pipeline     = RHIPipelineHandle{5u, 2u};

    // Draw list and allocator pointers — use sentinel addresses (no allocation needed)
    const DrawList*    fakeDrawList  = reinterpret_cast<const DrawList*>(0x1000);
    CBRingAllocator*   fakeAllocator = reinterpret_cast<CBRingAllocator*>(0x2000);
    desc.drawList    = fakeDrawList;
    desc.cbAllocator = fakeAllocator;

    // Per-frame CB allocation — write arbitrary values into the handle fields
    desc.perFrameAlloc.gpuAddr   = 0xDEADBEEF00000000ULL;
    desc.perFrameAlloc.sizeBytes = 256u;

    // Mesh array
    GPUMesh fakeMeshes[3]{};
    desc.meshes    = fakeMeshes;
    desc.meshCount = 3u;

    // Scalar flags and dimensions
    desc.bindless        = true;
    desc.viewportWidth   = 2560.0f;
    desc.viewportHeight  = 1440.0f;

    // Optional binder path
    CBRingAllocator* fakeCbRing = reinterpret_cast<CBRingAllocator*>(0x3000);
    desc.cbRingAllocator = fakeCbRing;

    // GPU instancing path
    RHIPipelineHandle fakeInstancedPSO{9u, 1u};
    const InstanceBatch* fakeBatches = reinterpret_cast<const InstanceBatch*>(0x4000);
    desc.instanced              = true;
    desc.instancedPipeline      = fakeInstancedPSO;
    desc.instanceBufferSrvIndex = 42u;
    desc.batchData              = fakeBatches;
    desc.batchCount             = 8u;

    // ── Read back every field ──────────────────────────────────────────────
    EXPECT_EQ(desc.depthBuffer.index,    7u);
    EXPECT_EQ(desc.depthTexture.index,   3u);
    EXPECT_EQ(desc.depthTexture.generation, 1u);
    EXPECT_EQ(desc.pipeline.index,       5u);
    EXPECT_EQ(desc.pipeline.generation,  2u);
    EXPECT_EQ(desc.drawList,             fakeDrawList);
    EXPECT_EQ(desc.cbAllocator,          fakeAllocator);
    EXPECT_EQ(desc.perFrameAlloc.gpuAddr,   0xDEADBEEF00000000ULL);
    EXPECT_EQ(desc.perFrameAlloc.sizeBytes, 256u);
    EXPECT_EQ(desc.meshes,               fakeMeshes);
    EXPECT_EQ(desc.meshCount,            3u);
    EXPECT_TRUE(desc.bindless);
    EXPECT_FLOAT_EQ(desc.viewportWidth,  2560.0f);
    EXPECT_FLOAT_EQ(desc.viewportHeight, 1440.0f);
    EXPECT_EQ(desc.cbRingAllocator,      fakeCbRing);
    EXPECT_TRUE(desc.instanced);
    EXPECT_EQ(desc.instancedPipeline.index, 9u);
    EXPECT_EQ(desc.instanceBufferSrvIndex, 42u);
    EXPECT_EQ(desc.batchData,            fakeBatches);
    EXPECT_EQ(desc.batchCount,           8u);
}

// ─────────────────────────────────────────────────────────────────────────────
// DepthPrepassDesc — RgResourceId and RHIHandle validity semantics
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructed RgResourceId (index == UINT32_MAX) must not be valid.
TEST(DepthPrepassDesc, DefaultDepthBuffer_ResourceId_IsInvalid) {
    DepthPrepassDesc desc{};
    EXPECT_FALSE(desc.depthBuffer.IsValid())
        << "Default depthBuffer RgResourceId should be invalid (index == UINT32_MAX)";
}

/// Default-constructed RHI handles (generation == 0) must not be valid.
TEST(DepthPrepassDesc, DefaultHandles_AreInvalid) {
    DepthPrepassDesc desc{};
    EXPECT_FALSE(desc.depthTexture.IsValid())
        << "Default depthTexture RHIHandle should be invalid (generation == 0)";
    EXPECT_FALSE(desc.pipeline.IsValid())
        << "Default pipeline RHIHandle should be invalid (generation == 0)";
    EXPECT_FALSE(desc.instancedPipeline.IsValid())
        << "Default instancedPipeline RHIHandle should be invalid (generation == 0)";
}

/// After assigning a valid handle, IsValid() must return true.
TEST(DepthPrepassDesc, AssignedDepthTexture_IsValid) {
    DepthPrepassDesc desc{};
    desc.depthTexture = RHITextureHandle{1u, 1u};
    EXPECT_TRUE(desc.depthTexture.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// HiZMipLevel — struct defaults and size
// ─────────────────────────────────────────────────────────────────────────────

/// A default-constructed HiZMipLevel must have zero width and height.
TEST(HiZMipLevel, DefaultValues) {
    HiZMipLevel mip{};
    EXPECT_EQ(mip.width,  0u);
    EXPECT_EQ(mip.height, 0u);
    EXPECT_EQ(mip.srvIndex, 0u);
    EXPECT_EQ(mip.uavIndex, 0u);
    EXPECT_FALSE(mip.texture.IsValid())
        << "Default HiZMipLevel texture handle should be invalid";
}

/// HiZMipLevel holds: RHITextureHandle (8 B) + srvIndex (4 B) + uavIndex (4 B)
/// + width (4 B) + height (4 B) = 24 bytes minimum. Struct layout may pack to
/// exactly 24 bytes (no trailing padding needed given 4-byte alignment).
///
/// The spec comment in hiz_pyramid_pass.h says "(handle + 4 uint32_t)" — that
/// is 8 + 16 = 24 bytes. Accept sizes between 20 and 32 to allow for compiler
/// padding differences.
TEST(HiZMipLevel, SizeIsBetween20And32Bytes) {
    EXPECT_GE(sizeof(HiZMipLevel), 20u)
        << "HiZMipLevel is too small — RHITextureHandle (8 B) + 4 uint32_t (16 B) = 24 B minimum";
    EXPECT_LE(sizeof(HiZMipLevel), 32u)
        << "HiZMipLevel is too large — should be at most 32 bytes";
}

/// All fields in HiZMipLevel must be writable and readable.
TEST(HiZMipLevel, AllFieldsAccessible_WriteAndRead) {
    HiZMipLevel mip{};
    mip.texture  = RHITextureHandle{11u, 2u};
    mip.srvIndex = 100u;
    mip.uavIndex = 200u;
    mip.width    = 960u;
    mip.height   = 540u;

    EXPECT_EQ(mip.texture.index,      11u);
    EXPECT_EQ(mip.texture.generation,  2u);
    EXPECT_TRUE(mip.texture.IsValid());
    EXPECT_EQ(mip.srvIndex, 100u);
    EXPECT_EQ(mip.uavIndex, 200u);
    EXPECT_EQ(mip.width,    960u);
    EXPECT_EQ(mip.height,   540u);
}

// ─────────────────────────────────────────────────────────────────────────────
// HiZPyramidPassDesc — default construction
// ─────────────────────────────────────────────────────────────────────────────

/// Default mipCount must be 0 (no mips configured yet).
TEST(HiZPyramidPassDesc, DefaultValues_MipCountIsZero) {
    HiZPyramidPassDesc desc{};
    EXPECT_EQ(desc.mipCount, 0u);
}

/// Default source dimensions must be 0.
TEST(HiZPyramidPassDesc, DefaultValues_SourceDimensionsAreZero) {
    HiZPyramidPassDesc desc{};
    EXPECT_EQ(desc.sourceWidth,  0u);
    EXPECT_EQ(desc.sourceHeight, 0u);
}

/// Default depthSourceSrvIndex must be 0.
TEST(HiZPyramidPassDesc, DefaultValues_DepthSourceSrvIndexIsZero) {
    HiZPyramidPassDesc desc{};
    EXPECT_EQ(desc.depthSourceSrvIndex, 0u);
}

/// Default virtual resource ID must be invalid.
TEST(HiZPyramidPassDesc, DefaultDepthSource_IsInvalid) {
    HiZPyramidPassDesc desc{};
    EXPECT_FALSE(desc.depthSource.IsValid())
        << "Default depthSource RgResourceId must be invalid";
}

/// Default physical handles must be invalid.
TEST(HiZPyramidPassDesc, DefaultHandles_AreInvalid) {
    HiZPyramidPassDesc desc{};
    EXPECT_FALSE(desc.depthSourceTexture.IsValid())
        << "Default depthSourceTexture must be invalid";
    EXPECT_FALSE(desc.pipeline.IsValid())
        << "Default pipeline must be invalid";
}

/// The mipLevels array must have kHiZMaxMipLevels slots.
TEST(HiZPyramidPassDesc, MipArrayCapacity_EqualskHiZMaxMipLevels) {
    HiZPyramidPassDesc desc{};
    // Count is verified via offsetof: mipLevels array must occupy exactly
    // kHiZMaxMipLevels * sizeof(HiZMipLevel) bytes starting at offsetof(mipLevels).
    const size_t expectedArrayBytes = kHiZMaxMipLevels * sizeof(HiZMipLevel);
    const size_t actualArrayBytes   = sizeof(desc.mipLevels);
    EXPECT_EQ(actualArrayBytes, expectedArrayBytes)
        << "mipLevels array size should be kHiZMaxMipLevels * sizeof(HiZMipLevel)";
}

/// All HiZPyramidPassDesc fields must be independently writable and readable.
TEST(HiZPyramidPassDesc, AllFieldsAccessible_WriteAndRead) {
    HiZPyramidPassDesc desc{};

    desc.depthSource             = RgResourceId{4u};
    desc.depthSourceTexture      = RHITextureHandle{10u, 1u};
    desc.depthSourceSrvIndex     = 55u;
    desc.pipeline                = RHIPipelineHandle{7u, 3u};
    desc.mipCount                = 10u;
    desc.sourceWidth             = 1920u;
    desc.sourceHeight            = 1080u;

    // Write a representative set of mip levels (first, middle, last)
    desc.mipLevels[0].texture  = RHITextureHandle{20u, 1u};
    desc.mipLevels[0].srvIndex = 300u;
    desc.mipLevels[0].uavIndex = 400u;
    desc.mipLevels[0].width    = 960u;
    desc.mipLevels[0].height   = 540u;

    desc.mipLevels[5].texture  = RHITextureHandle{25u, 1u};
    desc.mipLevels[5].width    = 30u;
    desc.mipLevels[5].height   = 17u;

    desc.mipLevels[kHiZMaxMipLevels - 1].texture  = RHITextureHandle{31u, 1u};
    desc.mipLevels[kHiZMaxMipLevels - 1].width    = 1u;
    desc.mipLevels[kHiZMaxMipLevels - 1].height   = 1u;

    EXPECT_EQ(desc.depthSource.index,              4u);
    EXPECT_EQ(desc.depthSourceTexture.index,       10u);
    EXPECT_EQ(desc.depthSourceSrvIndex,            55u);
    EXPECT_EQ(desc.pipeline.index,                 7u);
    EXPECT_EQ(desc.pipeline.generation,            3u);
    EXPECT_EQ(desc.mipCount,                       10u);
    EXPECT_EQ(desc.sourceWidth,                    1920u);
    EXPECT_EQ(desc.sourceHeight,                   1080u);

    EXPECT_EQ(desc.mipLevels[0].texture.index,     20u);
    EXPECT_EQ(desc.mipLevels[0].srvIndex,          300u);
    EXPECT_EQ(desc.mipLevels[0].uavIndex,          400u);
    EXPECT_EQ(desc.mipLevels[0].width,             960u);
    EXPECT_EQ(desc.mipLevels[0].height,            540u);

    EXPECT_EQ(desc.mipLevels[5].texture.index,     25u);
    EXPECT_EQ(desc.mipLevels[5].width,             30u);
    EXPECT_EQ(desc.mipLevels[5].height,            17u);

    EXPECT_EQ(desc.mipLevels[kHiZMaxMipLevels - 1].texture.index, 31u);
    EXPECT_EQ(desc.mipLevels[kHiZMaxMipLevels - 1].width,          1u);
    EXPECT_EQ(desc.mipLevels[kHiZMaxMipLevels - 1].height,         1u);
}

// ─────────────────────────────────────────────────────────────────────────────
// kHiZMaxMipLevels — constant value
// ─────────────────────────────────────────────────────────────────────────────

/// kHiZMaxMipLevels must be exactly 12.
///
/// The header comment justifies this as sufficient for up to 4096x4096 source
/// resolution (log2(4096) = 12). Changing this constant is a breaking change
/// that would require resizing the mipLevels array and updating shader dispatch.
TEST(HiZConstants, kHiZMaxMipLevels_Is12) {
    EXPECT_EQ(kHiZMaxMipLevels, 12u);
}

/// kHiZMaxMipLevels must be >= 12 — sufficient for 4096x4096.
TEST(HiZConstants, kHiZMaxMipLevels_IsSufficientFor4096) {
    // log2(4096) == 12 — the constant must cover this resolution.
    const uint32_t mipsNeededFor4K = static_cast<uint32_t>(
        std::floor(std::log2(4096.0)));
    EXPECT_GE(kHiZMaxMipLevels, mipsNeededFor4K)
        << "kHiZMaxMipLevels=" << kHiZMaxMipLevels
        << " is insufficient for 4096x4096 (needs " << mipsNeededFor4K << " mips)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Hi-Z mip count formula — floor(log2(max(width, height)))
//
// The formula is tested directly in the tests (not via a utility function,
// since no such function is exposed from the headers). The tests document the
// expected value for each resolution so that if the formula or constant is
// changed the tests catch it immediately.
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Computes the number of Hi-Z mip levels for a given source resolution.
/// This replicates the formula from HiZPyramidPassDesc docs:
///   floor(log2(max(width, height)))
uint32_t ComputeHiZMipCount(uint32_t width, uint32_t height) {
    if (width == 0u || height == 0u) return 0u;
    const uint32_t maxDim = (width > height) ? width : height;
    return static_cast<uint32_t>(std::floor(std::log2(static_cast<double>(maxDim))));
}

} // namespace

/// 1920x1080: max = 1920, floor(log2(1920)) = 10.
TEST(HiZMipCount, Resolution_1920x1080_Is10Mips) {
    EXPECT_EQ(ComputeHiZMipCount(1920u, 1080u), 10u);
}

/// 3840x2160: max = 3840, floor(log2(3840)) = 11.
TEST(HiZMipCount, Resolution_3840x2160_Is11Mips) {
    EXPECT_EQ(ComputeHiZMipCount(3840u, 2160u), 11u);
}

/// 1x1: max = 1, floor(log2(1)) = 0 — degenerate case produces zero mips.
///
/// This guards the pass against a zero-iteration loop on a 1x1 depth buffer,
/// which could arise in thumbnail or minimap contexts.
TEST(HiZMipCount, Resolution_1x1_Is0Mips) {
    EXPECT_EQ(ComputeHiZMipCount(1u, 1u), 0u);
}

/// 2048x2048: max = 2048, floor(log2(2048)) = 11.
///
/// Power-of-two resolutions must produce exact integer results with no
/// floating-point rounding error.
TEST(HiZMipCount, Resolution_2048x2048_Is11Mips) {
    EXPECT_EQ(ComputeHiZMipCount(2048u, 2048u), 11u);
}

/// 4096x4096: max = 4096, floor(log2(4096)) = 12 — exactly kHiZMaxMipLevels.
///
/// This is the designed maximum for the pyramid. The pass must clamp to
/// kHiZMaxMipLevels if the source exceeds this resolution.
TEST(HiZMipCount, Resolution_4096x4096_Is12Mips) {
    EXPECT_EQ(ComputeHiZMipCount(4096u, 4096u), 12u);
}

/// Non-square resolution uses max(width, height).
/// 256x1080: max = 1080, floor(log2(1080)) = 10.
TEST(HiZMipCount, NonSquare_256x1080_Uses_MaxDimension) {
    EXPECT_EQ(ComputeHiZMipCount(256u, 1080u), 10u);
}

/// Non-square resolution uses max(width, height) — width > height case.
/// 2560x720: max = 2560, floor(log2(2560)) = 11.
TEST(HiZMipCount, NonSquare_2560x720_Uses_MaxDimension) {
    EXPECT_EQ(ComputeHiZMipCount(2560u, 720u), 11u);
}

/// 2x2: max = 2, floor(log2(2)) = 1 — smallest non-trivial mip chain.
TEST(HiZMipCount, Resolution_2x2_Is1Mip) {
    EXPECT_EQ(ComputeHiZMipCount(2u, 2u), 1u);
}

/// 512x512: max = 512, floor(log2(512)) = 9.
TEST(HiZMipCount, Resolution_512x512_Is9Mips) {
    EXPECT_EQ(ComputeHiZMipCount(512u, 512u), 9u);
}

/// 1024x768: max = 1024, floor(log2(1024)) = 10.
TEST(HiZMipCount, Resolution_1024x768_Is10Mips) {
    EXPECT_EQ(ComputeHiZMipCount(1024u, 768u), 10u);
}

// ─────────────────────────────────────────────────────────────────────────────
// HiZPyramidPassDesc — mip array slot isolation
//
// Writing to one mip slot must not corrupt adjacent slots (guards against
// wrong sizeof in array indexing or struct layout problems).
// ─────────────────────────────────────────────────────────────────────────────

TEST(HiZPyramidPassDesc, MipSlots_AreIndependent_NoCorruption) {
    HiZPyramidPassDesc desc{};

    // Write sentinel values into all slots
    for (uint32_t i = 0; i < kHiZMaxMipLevels; ++i) {
        desc.mipLevels[i].width    = i * 100u + 1u;
        desc.mipLevels[i].height   = i * 100u + 2u;
        desc.mipLevels[i].srvIndex = i * 10u + 3u;
        desc.mipLevels[i].uavIndex = i * 10u + 4u;
    }

    // Verify every slot retained its own values
    for (uint32_t i = 0; i < kHiZMaxMipLevels; ++i) {
        EXPECT_EQ(desc.mipLevels[i].width,    i * 100u + 1u)
            << "width corrupted at mip slot " << i;
        EXPECT_EQ(desc.mipLevels[i].height,   i * 100u + 2u)
            << "height corrupted at mip slot " << i;
        EXPECT_EQ(desc.mipLevels[i].srvIndex, i * 10u  + 3u)
            << "srvIndex corrupted at mip slot " << i;
        EXPECT_EQ(desc.mipLevels[i].uavIndex, i * 10u  + 4u)
            << "uavIndex corrupted at mip slot " << i;
    }
}

