/// Tests for TextureLoadResult struct defaults and TextureLoader null-safety.
///
/// These are pure CPU tests — no GPU device or command list is allocated.
/// They verify struct field defaults and that null-pointer arguments produce
/// safe, well-defined results (empty/invalid output, no crash).

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

/// Two default-constructed TextureLoadResult values must compare identically.
TEST(TextureLoadResult, TwoDefaults_AreEqual) {
    TextureLoadResult a;
    TextureLoadResult b;
    EXPECT_EQ(a.handle.index,      b.handle.index);
    EXPECT_EQ(a.handle.generation, b.handle.generation);
    EXPECT_EQ(a.srvIndex,          b.srvIndex);
    EXPECT_EQ(a.width,             b.width);
    EXPECT_EQ(a.height,            b.height);
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
