/// Tests for TextureLoadResult struct defaults, ComputeTextureMipCount, and TextureLoader null-safety.
///
/// These are pure CPU tests — no GPU device or command list is allocated.
/// They verify struct field defaults, mip count computation correctness,
/// and that null-pointer arguments produce safe, well-defined results
/// (empty/invalid output, no crash).

#include <gtest/gtest.h>

#include "texture_loader.h"
#include "rhi_types.h"

#include <cstdint>
#include <limits>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoadResult — struct field defaults
// ─────────────────────────────────────────────────────────────────────────────

/// TextureLoadResult default-constructed handle must be invalid (generation==0).
TEST(TextureLoadResult, Default_Handle_IsInvalid) {
    TextureLoadResult r;
    EXPECT_FALSE(r.handle.IsValid())
        << "Default TextureLoadResult handle must not be valid";
}

/// TextureLoadResult default srvIndex must be UINT32_MAX (sentinel for 'not assigned').
TEST(TextureLoadResult, Default_SrvIndex_IsMaxUint32) {
    TextureLoadResult r;
    EXPECT_EQ(r.srvIndex, UINT32_MAX)
        << "Default srvIndex must equal UINT32_MAX";
}

/// TextureLoadResult default width must be 0.
TEST(TextureLoadResult, Default_Width_IsZero) {
    TextureLoadResult r;
    EXPECT_EQ(r.width, 0u);
}

/// TextureLoadResult default height must be 0.
TEST(TextureLoadResult, Default_Height_IsZero) {
    TextureLoadResult r;
    EXPECT_EQ(r.height, 0u);
}

/// TextureLoadResult default mipLevels must be 0.
TEST(TextureLoadResult, Default_MipLevels_IsZero) {
    TextureLoadResult r;
    EXPECT_EQ(r.mipLevels, 0u);
}

/// Two default-constructed TextureLoadResult values must compare identically.
TEST(TextureLoadResult, TwoDefaults_AreEqual) {
    TextureLoadResult a;
    TextureLoadResult b;
    EXPECT_EQ(a.handle.index,      b.handle.index);
    EXPECT_EQ(a.handle.generation, b.handle.generation);
    EXPECT_EQ(a.srvIndex,          b.srvIndex);
    EXPECT_EQ(a.width,             b.width);
    EXPECT_EQ(a.height,            b.height);
    EXPECT_EQ(a.mipLevels,         b.mipLevels);
}

/// Handle index field must be 0 on a default result.
TEST(TextureLoadResult, Default_Handle_IndexIsZero) {
    TextureLoadResult r;
    EXPECT_EQ(r.handle.index, 0u);
}

/// Handle generation field must be 0 on a default result.
TEST(TextureLoadResult, Default_Handle_GenerationIsZero) {
    TextureLoadResult r;
    EXPECT_EQ(r.handle.generation, 0u);
}

/// A manually constructed invalid result must match the all-default state.
TEST(TextureLoadResult, ManualInvalidHandle_MatchesDefault) {
    TextureLoadResult r;
    r.handle = RHITextureHandle::Invalid();

    EXPECT_FALSE(r.handle.IsValid());
    EXPECT_EQ(r.handle.index,      0u);
    EXPECT_EQ(r.handle.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoadResult — value assignment round-trips
// ─────────────────────────────────────────────────────────────────────────────

/// Assigned width and height are preserved without truncation.
TEST(TextureLoadResult, WidthHeight_Assignment_RoundTrip) {
    TextureLoadResult r;
    r.width  = 1920u;
    r.height = 1080u;
    EXPECT_EQ(r.width,  1920u);
    EXPECT_EQ(r.height, 1080u);
}

/// srvIndex can be assigned any uint32_t value and is retrieved correctly.
TEST(TextureLoadResult, SrvIndex_Assignment_RoundTrip) {
    TextureLoadResult r;
    r.srvIndex = 42u;
    EXPECT_EQ(r.srvIndex, 42u);
}

/// srvIndex at maximum valid value (UINT32_MAX - 1) round-trips correctly.
TEST(TextureLoadResult, SrvIndex_NearMaxValue_RoundTrip) {
    TextureLoadResult r;
    r.srvIndex = UINT32_MAX - 1u;
    EXPECT_EQ(r.srvIndex, UINT32_MAX - 1u);
}

/// Zero-width texture result — width field stores 0 without modification.
TEST(TextureLoadResult, ZeroWidth_StoresCorrectly) {
    TextureLoadResult r;
    r.width = 0u;
    EXPECT_EQ(r.width, 0u);
}

/// Maximum uint32_t width round-trips without overflow.
TEST(TextureLoadResult, MaxWidth_RoundTrips) {
    TextureLoadResult r;
    r.width = UINT32_MAX;
    EXPECT_EQ(r.width, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::Load — null device returns invalid result
// ─────────────────────────────────────────────────────────────────────────────

/// Load with null device must return an invalid result without crashing.
TEST(TextureLoader, Load_NullDevice_ReturnsInvalidResult) {
    TextureLoadResult result = TextureLoader::Load(nullptr, nullptr, "any_path.png");

    EXPECT_FALSE(result.handle.IsValid())
        << "Load with null device must return invalid handle";
    EXPECT_EQ(result.srvIndex, UINT32_MAX)
        << "Load with null device must leave srvIndex as UINT32_MAX";
    EXPECT_EQ(result.width,  0u) << "Load with null device must leave width as 0";
    EXPECT_EQ(result.height, 0u) << "Load with null device must leave height as 0";
}

/// Load with null device and empty path must return invalid result.
TEST(TextureLoader, Load_NullDevice_EmptyPath_ReturnsInvalidResult) {
    TextureLoadResult result = TextureLoader::Load(nullptr, nullptr, "");

    EXPECT_FALSE(result.handle.IsValid());
    EXPECT_EQ(result.srvIndex, UINT32_MAX);
    EXPECT_EQ(result.width,  0u);
    EXPECT_EQ(result.height, 0u);
}

/// Load with null device and non-existent path must not crash.
TEST(TextureLoader, Load_NullDevice_NonExistentPath_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        TextureLoader::Load(nullptr, nullptr, "does_not_exist.png");
    });
}

/// Load with null device and null cmdList returns invalid handle index.
TEST(TextureLoader, Load_NullDevice_HandleIndex_IsZero) {
    TextureLoadResult result = TextureLoader::Load(nullptr, nullptr, "test.png");
    EXPECT_EQ(result.handle.index, 0u);
}

/// Load with null device and null cmdList returns invalid handle generation.
TEST(TextureLoader, Load_NullDevice_HandleGeneration_IsZero) {
    TextureLoadResult result = TextureLoader::Load(nullptr, nullptr, "test.png");
    EXPECT_EQ(result.handle.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoader::LoadFromMemory — null device returns invalid result
// ─────────────────────────────────────────────────────────────────────────────

/// LoadFromMemory with null device must return an invalid result without crashing.
TEST(TextureLoader, LoadFromMemory_NullDevice_ReturnsInvalidResult) {
    const uint8_t kFakeBytes[] = {0xFF, 0xD8, 0xFF, 0xE0}; // JPEG magic bytes stub
    TextureLoadResult result = TextureLoader::LoadFromMemory(
        nullptr, nullptr, kFakeBytes, sizeof(kFakeBytes), "test");

    EXPECT_FALSE(result.handle.IsValid())
        << "LoadFromMemory with null device must return invalid handle";
    EXPECT_EQ(result.srvIndex, UINT32_MAX);
    EXPECT_EQ(result.width,  0u);
    EXPECT_EQ(result.height, 0u);
}

/// LoadFromMemory with null device and null data must not crash.
TEST(TextureLoader, LoadFromMemory_NullDevice_NullData_DoesNotCrash) {
    EXPECT_NO_FATAL_FAILURE({
        TextureLoader::LoadFromMemory(nullptr, nullptr, nullptr, 0, "test");
    });
}

/// LoadFromMemory with null device, zero size must return invalid handle.
TEST(TextureLoader, LoadFromMemory_NullDevice_ZeroSize_ReturnsInvalidHandle) {
    TextureLoadResult result = TextureLoader::LoadFromMemory(
        nullptr, nullptr, nullptr, 0, "zero_size");

    EXPECT_FALSE(result.handle.IsValid());
    EXPECT_EQ(result.srvIndex, UINT32_MAX);
}

/// LoadFromMemory with null device must leave width and height as zero.
TEST(TextureLoader, LoadFromMemory_NullDevice_WidthHeightAreZero) {
    const uint8_t kBytes[] = {0x89, 0x50, 0x4E, 0x47}; // PNG magic bytes stub
    TextureLoadResult result = TextureLoader::LoadFromMemory(
        nullptr, nullptr, kBytes, sizeof(kBytes), "png_stub");

    EXPECT_EQ(result.width,  0u);
    EXPECT_EQ(result.height, 0u);
}

/// LoadFromMemory with null device returns zero handle index.
TEST(TextureLoader, LoadFromMemory_NullDevice_HandleIndex_IsZero) {
    const uint8_t kBytes[] = {0x01};
    TextureLoadResult result = TextureLoader::LoadFromMemory(
        nullptr, nullptr, kBytes, 1, "single_byte");

    EXPECT_EQ(result.handle.index, 0u);
}

/// LoadFromMemory with null device returns zero handle generation.
TEST(TextureLoader, LoadFromMemory_NullDevice_HandleGeneration_IsZero) {
    const uint8_t kBytes[] = {0x01};
    TextureLoadResult result = TextureLoader::LoadFromMemory(
        nullptr, nullptr, kBytes, 1, "single_byte");

    EXPECT_EQ(result.handle.generation, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// RHITextureHandle — handle type validity semantics used by TextureLoadResult
// ─────────────────────────────────────────────────────────────────────────────

/// RHITextureHandle::Invalid() must not be valid.
TEST(RHITextureHandle, Invalid_IsNotValid) {
    auto h = RHITextureHandle::Invalid();
    EXPECT_FALSE(h.IsValid());
}

/// RHITextureHandle with generation=0 must not be valid.
TEST(RHITextureHandle, ZeroGeneration_IsNotValid) {
    RHITextureHandle h;
    h.index      = 99u;
    h.generation = 0u;
    EXPECT_FALSE(h.IsValid());
}

/// RHITextureHandle with generation != 0 must be valid.
TEST(RHITextureHandle, NonZeroGeneration_IsValid) {
    RHITextureHandle h;
    h.index      = 1u;
    h.generation = 1u;
    EXPECT_TRUE(h.IsValid());
}

/// Two identical RHITextureHandle values must compare equal.
TEST(RHITextureHandle, EqualHandles_CompareEqual) {
    RHITextureHandle a{1u, 2u};
    RHITextureHandle b{1u, 2u};
    EXPECT_EQ(a, b);
}

/// Two different RHITextureHandle values must compare not equal.
TEST(RHITextureHandle, DifferentHandles_CompareNotEqual) {
    RHITextureHandle a{1u, 2u};
    RHITextureHandle b{1u, 3u};
    EXPECT_NE(a, b);
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeTextureMipCount — mip level computation
// ─────────────────────────────────────────────────────────────────────────────

/// 1x1 texture has exactly 1 mip level.
TEST(ComputeTextureMipCount, OneByOne_Returns1) {
    EXPECT_EQ(ComputeTextureMipCount(1, 1), 1u);
}

/// 2x2 texture has 2 mip levels (2x2 + 1x1).
TEST(ComputeTextureMipCount, TwoByTwo_Returns2) {
    EXPECT_EQ(ComputeTextureMipCount(2, 2), 2u);
}

/// 4x4 texture has 3 mip levels (4x4 + 2x2 + 1x1).
TEST(ComputeTextureMipCount, FourByFour_Returns3) {
    EXPECT_EQ(ComputeTextureMipCount(4, 4), 3u);
}

/// 256x256 texture has 9 mip levels.
TEST(ComputeTextureMipCount, SquarePow2_256) {
    EXPECT_EQ(ComputeTextureMipCount(256, 256), 9u);
}

/// 1024x1024 texture has 11 mip levels (log2(1024) + 1 = 11).
TEST(ComputeTextureMipCount, SquarePow2_1024) {
    EXPECT_EQ(ComputeTextureMipCount(1024, 1024), 11u);
}

/// 2048x2048 texture has 12 mip levels.
TEST(ComputeTextureMipCount, SquarePow2_2048) {
    EXPECT_EQ(ComputeTextureMipCount(2048, 2048), 12u);
}

/// 4096x4096 texture has 13 mip levels.
TEST(ComputeTextureMipCount, SquarePow2_4096) {
    EXPECT_EQ(ComputeTextureMipCount(4096, 4096), 13u);
}

/// 16384x16384 texture has 15 mip levels (log2(16384) + 1 = 15).
TEST(ComputeTextureMipCount, Max16K) {
    EXPECT_EQ(ComputeTextureMipCount(16384, 16384), 15u);
}

/// Non-square texture: mip count based on largest dimension.
TEST(ComputeTextureMipCount, NonSquare_WidthLarger) {
    // 1024x512: max(1024,512) = 1024, log2(1024) + 1 = 11
    EXPECT_EQ(ComputeTextureMipCount(1024, 512), 11u);
}

/// Non-square texture: height larger.
TEST(ComputeTextureMipCount, NonSquare_HeightLarger) {
    // 512x2048: max(512,2048) = 2048, log2(2048) + 1 = 12
    EXPECT_EQ(ComputeTextureMipCount(512, 2048), 12u);
}

/// Non-power-of-two: 1920x1080 (common resolution).
TEST(ComputeTextureMipCount, NonPow2_1920x1080) {
    // max(1920,1080) = 1920. 2^10 = 1024, 2^11 = 2048.
    // floor(log2(1920)) + 1 = 10 + 1 = 11
    EXPECT_EQ(ComputeTextureMipCount(1920, 1080), 11u);
}

/// Non-power-of-two: 3x5.
TEST(ComputeTextureMipCount, NonPow2_3x5) {
    // max(3,5) = 5. floor(log2(5)) + 1 = 2 + 1 = 3
    EXPECT_EQ(ComputeTextureMipCount(3, 5), 3u);
}

/// Asymmetric: 1x1024.
TEST(ComputeTextureMipCount, Asymmetric_1x1024) {
    // max(1,1024) = 1024, log2(1024) + 1 = 11
    EXPECT_EQ(ComputeTextureMipCount(1, 1024), 11u);
}

/// Asymmetric: 2048x1.
TEST(ComputeTextureMipCount, Asymmetric_2048x1) {
    // max(2048,1) = 2048, log2(2048) + 1 = 12
    EXPECT_EQ(ComputeTextureMipCount(2048, 1), 12u);
}

/// Standard PBR texture size: 2048x2048 matches expected mip count.
TEST(ComputeTextureMipCount, StandardPBR_2048) {
    // Common PBR texture size — 12 mips
    EXPECT_EQ(ComputeTextureMipCount(2048, 2048), 12u);
}

// ─────────────────────────────────────────────────────────────────────────────
// TextureLoadResult — mipLevels field
// ─────────────────────────────────────────────────────────────────────────────

/// mipLevels assignment round-trips correctly.
TEST(TextureLoadResult, MipLevels_Assignment_RoundTrip) {
    TextureLoadResult r;
    r.mipLevels = 11;
    EXPECT_EQ(r.mipLevels, 11u);
}

/// Null-device Load returns zero mipLevels.
TEST(TextureLoader, Load_NullDevice_MipLevelsIsZero) {
    TextureLoadResult result = TextureLoader::Load(nullptr, nullptr, "test.png");
    EXPECT_EQ(result.mipLevels, 0u);
}

/// Null-device LoadFromMemory returns zero mipLevels.
TEST(TextureLoader, LoadFromMemory_NullDevice_MipLevelsIsZero) {
    const uint8_t kBytes[] = {0x01};
    TextureLoadResult result = TextureLoader::LoadFromMemory(
        nullptr, nullptr, kBytes, 1, "test");
    EXPECT_EQ(result.mipLevels, 0u);
}
