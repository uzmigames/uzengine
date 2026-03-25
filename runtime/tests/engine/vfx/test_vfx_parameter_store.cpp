/// Unit tests for VFXParameterStore — named parameter storage and GPU packing.
///
/// Covers: SetFloat/GetFloat round-trip, SetVec3/GetVec3 round-trip,
/// SetVec4/GetVec4 round-trip, defaults when missing, Has() correctness,
/// Remove() correctness, Clear() empties store, GetCount() accuracy,
/// GetNames() insertion order, GetUploadSize() formula, PackToBuffer
/// content and order, PackToBuffer insufficient-buffer guard, and
/// overwriting a parameter preserves insertion order.
///
/// No GPU hardware required — all tests are CPU-only.

#include <gtest/gtest.h>

#include "parameters/vfx_parameter_store.h"

#include <cstring>
#include <string>
#include <vector>

using namespace uz;

// =============================================================================
// SetFloat / GetFloat round-trip
// =============================================================================

TEST(VFXParameterStore, SetFloat_GetFloat_RoundTrip) {
    VFXParameterStore store;
    store.SetFloat("speed", 7.5f);

    EXPECT_FLOAT_EQ(store.GetFloat("speed"), 7.5f);
}

TEST(VFXParameterStore, SetFloat_GetFloat_NegativeValue) {
    VFXParameterStore store;
    store.SetFloat("gravity", -9.81f);

    EXPECT_FLOAT_EQ(store.GetFloat("gravity"), -9.81f);
}

TEST(VFXParameterStore, SetFloat_GetFloat_ZeroValue) {
    VFXParameterStore store;
    store.SetFloat("zero", 0.0f);

    EXPECT_FLOAT_EQ(store.GetFloat("zero"), 0.0f);
}

// =============================================================================
// SetVec3 / GetVec3 round-trip
// =============================================================================

TEST(VFXParameterStore, SetVec3_GetVec3_RoundTrip) {
    VFXParameterStore store;
    const Vec3 wind{1.0f, 2.0f, 3.0f};
    store.SetVec3("wind", wind);

    const Vec3 got = store.GetVec3("wind");
    EXPECT_FLOAT_EQ(got.x, 1.0f);
    EXPECT_FLOAT_EQ(got.y, 2.0f);
    EXPECT_FLOAT_EQ(got.z, 3.0f);
}

TEST(VFXParameterStore, SetVec3_GetVec3_NegativeComponents) {
    VFXParameterStore store;
    store.SetVec3("dir", Vec3{-1.0f, -2.5f, 0.5f});

    const Vec3 got = store.GetVec3("dir");
    EXPECT_FLOAT_EQ(got.x, -1.0f);
    EXPECT_FLOAT_EQ(got.y, -2.5f);
    EXPECT_FLOAT_EQ(got.z, 0.5f);
}

/// GetVec3 only reads xyz; the w component stored internally must be 0.
TEST(VFXParameterStore, SetVec3_StoredWComponentIsZero) {
    VFXParameterStore store;
    store.SetVec3("pos", Vec3{4.0f, 5.0f, 6.0f});

    // Read back as Vec4 to inspect the internally stored w
    const Vec4 raw = store.GetVec4("pos");
    EXPECT_FLOAT_EQ(raw.x, 4.0f);
    EXPECT_FLOAT_EQ(raw.y, 5.0f);
    EXPECT_FLOAT_EQ(raw.z, 6.0f);
    EXPECT_FLOAT_EQ(raw.w, 0.0f) << "SetVec3 must zero-pad the w component";
}

// =============================================================================
// SetVec4 / GetVec4 round-trip
// =============================================================================

TEST(VFXParameterStore, SetVec4_GetVec4_RoundTrip) {
    VFXParameterStore store;
    const Vec4 color{0.1f, 0.2f, 0.3f, 0.9f};
    store.SetVec4("color", color);

    const Vec4 got = store.GetVec4("color");
    EXPECT_FLOAT_EQ(got.x, 0.1f);
    EXPECT_FLOAT_EQ(got.y, 0.2f);
    EXPECT_FLOAT_EQ(got.z, 0.3f);
    EXPECT_FLOAT_EQ(got.w, 0.9f);
}

TEST(VFXParameterStore, SetVec4_GetVec4_AllZero) {
    VFXParameterStore store;
    store.SetVec4("null", Vec4{0.0f, 0.0f, 0.0f, 0.0f});

    const Vec4 got = store.GetVec4("null");
    EXPECT_FLOAT_EQ(got.x, 0.0f);
    EXPECT_FLOAT_EQ(got.y, 0.0f);
    EXPECT_FLOAT_EQ(got.z, 0.0f);
    EXPECT_FLOAT_EQ(got.w, 0.0f);
}

// =============================================================================
// Default values when parameter is missing
// =============================================================================

TEST(VFXParameterStore, GetFloat_MissingParam_ReturnsDefaultZero) {
    VFXParameterStore store;
    EXPECT_FLOAT_EQ(store.GetFloat("missing"), 0.0f);
}

TEST(VFXParameterStore, GetFloat_MissingParam_ReturnsSuppliedDefault) {
    VFXParameterStore store;
    EXPECT_FLOAT_EQ(store.GetFloat("missing", 42.0f), 42.0f);
}

TEST(VFXParameterStore, GetVec3_MissingParam_ReturnsDefaultZeroVec) {
    VFXParameterStore store;
    const Vec3 got = store.GetVec3("missing");
    EXPECT_FLOAT_EQ(got.x, 0.0f);
    EXPECT_FLOAT_EQ(got.y, 0.0f);
    EXPECT_FLOAT_EQ(got.z, 0.0f);
}

TEST(VFXParameterStore, GetVec3_MissingParam_ReturnsSuppliedDefault) {
    VFXParameterStore store;
    const Vec3 def{7.0f, 8.0f, 9.0f};
    const Vec3 got = store.GetVec3("missing", def);
    EXPECT_FLOAT_EQ(got.x, 7.0f);
    EXPECT_FLOAT_EQ(got.y, 8.0f);
    EXPECT_FLOAT_EQ(got.z, 9.0f);
}

TEST(VFXParameterStore, GetVec4_MissingParam_ReturnsDefaultZeroVec) {
    VFXParameterStore store;
    const Vec4 got = store.GetVec4("missing");
    EXPECT_FLOAT_EQ(got.x, 0.0f);
    EXPECT_FLOAT_EQ(got.y, 0.0f);
    EXPECT_FLOAT_EQ(got.z, 0.0f);
    EXPECT_FLOAT_EQ(got.w, 0.0f);
}

TEST(VFXParameterStore, GetVec4_MissingParam_ReturnsSuppliedDefault) {
    VFXParameterStore store;
    const Vec4 def{1.0f, 2.0f, 3.0f, 4.0f};
    const Vec4 got = store.GetVec4("missing", def);
    EXPECT_FLOAT_EQ(got.x, 1.0f);
    EXPECT_FLOAT_EQ(got.y, 2.0f);
    EXPECT_FLOAT_EQ(got.z, 3.0f);
    EXPECT_FLOAT_EQ(got.w, 4.0f);
}

// =============================================================================
// Has()
// =============================================================================

TEST(VFXParameterStore, Has_ExistingParam_ReturnsTrue) {
    VFXParameterStore store;
    store.SetFloat("x", 1.0f);
    EXPECT_TRUE(store.Has("x"));
}

TEST(VFXParameterStore, Has_MissingParam_ReturnsFalse) {
    VFXParameterStore store;
    EXPECT_FALSE(store.Has("x"));
}

TEST(VFXParameterStore, Has_AfterVec3Set_ReturnsTrue) {
    VFXParameterStore store;
    store.SetVec3("dir", Vec3{1.0f, 0.0f, 0.0f});
    EXPECT_TRUE(store.Has("dir"));
}

TEST(VFXParameterStore, Has_AfterVec4Set_ReturnsTrue) {
    VFXParameterStore store;
    store.SetVec4("col", Vec4{});
    EXPECT_TRUE(store.Has("col"));
}

// =============================================================================
// Remove()
// =============================================================================

TEST(VFXParameterStore, Remove_ExistingParam_ReturnsTrueAndRemovesIt) {
    VFXParameterStore store;
    store.SetFloat("speed", 5.0f);

    const bool removed = store.Remove("speed");

    EXPECT_TRUE(removed);
    EXPECT_FALSE(store.Has("speed"))
        << "Parameter must not exist after Remove()";
}

TEST(VFXParameterStore, Remove_MissingParam_ReturnsFalse) {
    VFXParameterStore store;
    EXPECT_FALSE(store.Remove("nonexistent"));
}

TEST(VFXParameterStore, Remove_ExistingParam_DecreasesCount) {
    VFXParameterStore store;
    store.SetFloat("a", 1.0f);
    store.SetFloat("b", 2.0f);
    ASSERT_EQ(store.GetCount(), 2u);

    store.Remove("a");

    EXPECT_EQ(store.GetCount(), 1u);
}

TEST(VFXParameterStore, Remove_AllParams_CountBecomesZero) {
    VFXParameterStore store;
    store.SetFloat("x", 1.0f);
    store.Remove("x");
    EXPECT_EQ(store.GetCount(), 0u);
}

// =============================================================================
// Clear()
// =============================================================================

TEST(VFXParameterStore, Clear_EmptiesAllParams) {
    VFXParameterStore store;
    store.SetFloat("a", 1.0f);
    store.SetVec3("b", Vec3{1.0f, 2.0f, 3.0f});
    store.SetVec4("c", Vec4{1.0f, 2.0f, 3.0f, 4.0f});
    ASSERT_EQ(store.GetCount(), 3u);

    store.Clear();

    EXPECT_EQ(store.GetCount(), 0u);
    EXPECT_FALSE(store.Has("a"));
    EXPECT_FALSE(store.Has("b"));
    EXPECT_FALSE(store.Has("c"));
}

TEST(VFXParameterStore, Clear_OnEmptyStore_DoesNotCrash) {
    VFXParameterStore store;
    store.Clear(); // Must not crash on empty store
    EXPECT_EQ(store.GetCount(), 0u);
}

// =============================================================================
// GetCount()
// =============================================================================

TEST(VFXParameterStore, GetCount_EmptyStore_ReturnsZero) {
    VFXParameterStore store;
    EXPECT_EQ(store.GetCount(), 0u);
}

TEST(VFXParameterStore, GetCount_AfterThreeInserts_ReturnsThree) {
    VFXParameterStore store;
    store.SetFloat("a", 1.0f);
    store.SetFloat("b", 2.0f);
    store.SetFloat("c", 3.0f);
    EXPECT_EQ(store.GetCount(), 3u);
}

TEST(VFXParameterStore, GetCount_AfterOverwrite_Unchanged) {
    VFXParameterStore store;
    store.SetFloat("x", 1.0f);
    store.SetFloat("x", 99.0f); // overwrite
    EXPECT_EQ(store.GetCount(), 1u)
        << "Overwriting a key must not increase count";
}

// =============================================================================
// GetNames() — insertion order
// =============================================================================

TEST(VFXParameterStore, GetNames_EmptyStore_ReturnsEmptyVector) {
    VFXParameterStore store;
    EXPECT_TRUE(store.GetNames().empty());
}

TEST(VFXParameterStore, GetNames_ThreeParams_ReturnsInsertionOrder) {
    VFXParameterStore store;
    store.SetFloat("first", 1.0f);
    store.SetVec3("second", Vec3{});
    store.SetVec4("third", Vec4{});

    const auto names = store.GetNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "first");
    EXPECT_EQ(names[1], "second");
    EXPECT_EQ(names[2], "third");
}

TEST(VFXParameterStore, GetNames_AfterRemoveMiddle_OrderPreserved) {
    VFXParameterStore store;
    store.SetFloat("a", 1.0f);
    store.SetFloat("b", 2.0f);
    store.SetFloat("c", 3.0f);
    store.Remove("b");

    const auto names = store.GetNames();
    ASSERT_EQ(names.size(), 2u);
    EXPECT_EQ(names[0], "a");
    EXPECT_EQ(names[1], "c");
}

// =============================================================================
// Overwriting preserves insertion order
// =============================================================================

/// When an existing parameter is overwritten, its position in insertion order
/// must not change, and GetNames() must still return the original order.
TEST(VFXParameterStore, Overwrite_PreservesInsertionOrder) {
    VFXParameterStore store;
    store.SetFloat("first", 1.0f);
    store.SetFloat("second", 2.0f);
    store.SetFloat("third", 3.0f);

    // Overwrite "second" with a new value
    store.SetFloat("second", 99.0f);

    // Count must be unchanged
    EXPECT_EQ(store.GetCount(), 3u);

    // Order must be unchanged
    const auto names = store.GetNames();
    ASSERT_EQ(names.size(), 3u);
    EXPECT_EQ(names[0], "first");
    EXPECT_EQ(names[1], "second");
    EXPECT_EQ(names[2], "third");

    // Value must be updated
    EXPECT_FLOAT_EQ(store.GetFloat("second"), 99.0f);
}

// =============================================================================
// GetUploadSize()
// =============================================================================

TEST(VFXParameterStore, GetUploadSize_EmptyStore_ReturnsZero) {
    VFXParameterStore store;
    EXPECT_EQ(store.GetUploadSize(), 0u);
}

TEST(VFXParameterStore, GetUploadSize_OneParam_Returns16) {
    VFXParameterStore store;
    store.SetFloat("x", 1.0f);
    EXPECT_EQ(store.GetUploadSize(), 16u)
        << "Each parameter occupies exactly 16 bytes (one Vec4)";
}

TEST(VFXParameterStore, GetUploadSize_ThreeParams_Returns48) {
    VFXParameterStore store;
    store.SetFloat("a", 1.0f);
    store.SetFloat("b", 2.0f);
    store.SetFloat("c", 3.0f);
    EXPECT_EQ(store.GetUploadSize(), 48u);
}

TEST(VFXParameterStore, GetUploadSize_EqualToCountTimes16) {
    VFXParameterStore store;
    store.SetVec4("p0", Vec4{1.0f, 2.0f, 3.0f, 4.0f});
    store.SetVec4("p1", Vec4{5.0f, 6.0f, 7.0f, 8.0f});

    const uint32_t expectedSize = static_cast<uint32_t>(store.GetCount()) * 16u;
    EXPECT_EQ(store.GetUploadSize(), expectedSize);
}

// =============================================================================
// PackToBuffer — correct content and insertion order
// =============================================================================

/// Single float parameter packs as Vec4{value, 0, 0, 0} in 16 bytes.
TEST(VFXParameterStore, PackToBuffer_SingleFloat_CorrectBytes) {
    VFXParameterStore store;
    store.SetFloat("speed", 5.0f);

    const uint32_t size = store.GetUploadSize();
    ASSERT_EQ(size, 16u);

    std::vector<uint8_t> buf(size, 0xAA);
    const uint32_t written = store.PackToBuffer(buf.data(), size);
    EXPECT_EQ(written, 16u);

    float values[4] = {};
    std::memcpy(values, buf.data(), 16);
    EXPECT_FLOAT_EQ(values[0], 5.0f) << "x must hold the float value";
    EXPECT_FLOAT_EQ(values[1], 0.0f) << "y must be zero-padded";
    EXPECT_FLOAT_EQ(values[2], 0.0f) << "z must be zero-padded";
    EXPECT_FLOAT_EQ(values[3], 0.0f) << "w must be zero-padded";
}

/// Two Vec4 parameters pack in insertion order.
TEST(VFXParameterStore, PackToBuffer_TwoVec4s_PackedInInsertionOrder) {
    VFXParameterStore store;
    store.SetVec4("first",  Vec4{1.0f, 2.0f, 3.0f, 4.0f});
    store.SetVec4("second", Vec4{5.0f, 6.0f, 7.0f, 8.0f});

    const uint32_t size = store.GetUploadSize();
    ASSERT_EQ(size, 32u);

    std::vector<uint8_t> buf(size, 0);
    store.PackToBuffer(buf.data(), size);

    float v[8] = {};
    std::memcpy(v, buf.data(), 32);

    // First 16 bytes = "first"
    EXPECT_FLOAT_EQ(v[0], 1.0f);
    EXPECT_FLOAT_EQ(v[1], 2.0f);
    EXPECT_FLOAT_EQ(v[2], 3.0f);
    EXPECT_FLOAT_EQ(v[3], 4.0f);

    // Next 16 bytes = "second"
    EXPECT_FLOAT_EQ(v[4], 5.0f);
    EXPECT_FLOAT_EQ(v[5], 6.0f);
    EXPECT_FLOAT_EQ(v[6], 7.0f);
    EXPECT_FLOAT_EQ(v[7], 8.0f);
}

/// Three parameters pack in the order they were inserted (float, vec3, vec4).
TEST(VFXParameterStore, PackToBuffer_MixedTypes_InsertionOrderPreserved) {
    VFXParameterStore store;
    store.SetFloat("alpha", 0.5f);
    store.SetVec3("pos",    Vec3{10.0f, 20.0f, 30.0f});
    store.SetVec4("color",  Vec4{1.0f, 0.0f, 0.0f, 1.0f});

    const uint32_t size = store.GetUploadSize();
    ASSERT_EQ(size, 48u);

    std::vector<uint8_t> buf(size, 0);
    const uint32_t written = store.PackToBuffer(buf.data(), size);
    ASSERT_EQ(written, 48u);

    float v[12] = {};
    std::memcpy(v, buf.data(), 48);

    // Slot 0 (alpha): {0.5, 0, 0, 0}
    EXPECT_FLOAT_EQ(v[0], 0.5f);
    EXPECT_FLOAT_EQ(v[1], 0.0f);
    EXPECT_FLOAT_EQ(v[2], 0.0f);
    EXPECT_FLOAT_EQ(v[3], 0.0f);

    // Slot 1 (pos): {10, 20, 30, 0}
    EXPECT_FLOAT_EQ(v[4], 10.0f);
    EXPECT_FLOAT_EQ(v[5], 20.0f);
    EXPECT_FLOAT_EQ(v[6], 30.0f);
    EXPECT_FLOAT_EQ(v[7], 0.0f);

    // Slot 2 (color): {1, 0, 0, 1}
    EXPECT_FLOAT_EQ(v[8],  1.0f);
    EXPECT_FLOAT_EQ(v[9],  0.0f);
    EXPECT_FLOAT_EQ(v[10], 0.0f);
    EXPECT_FLOAT_EQ(v[11], 1.0f);
}

// =============================================================================
// PackToBuffer — insufficient buffer guard
// =============================================================================

/// PackToBuffer returns 0 when the provided buffer is too small.
TEST(VFXParameterStore, PackToBuffer_BufferTooSmall_ReturnsZero) {
    VFXParameterStore store;
    store.SetFloat("x", 1.0f);
    store.SetFloat("y", 2.0f); // requires 32 bytes

    std::vector<uint8_t> tinyBuf(15, 0xBB); // only 15 bytes
    const uint32_t written = store.PackToBuffer(tinyBuf.data(), 15u);
    EXPECT_EQ(written, 0u)
        << "PackToBuffer must return 0 when buffer is smaller than GetUploadSize()";
}

/// PackToBuffer returns 0 when outBuffer is nullptr (size > 0 but null pointer).
TEST(VFXParameterStore, PackToBuffer_NullBuffer_ReturnsZero) {
    VFXParameterStore store;
    store.SetFloat("x", 1.0f);

    const uint32_t written = store.PackToBuffer(nullptr, 16u);
    EXPECT_EQ(written, 0u);
}

/// PackToBuffer returns 0 for an empty store.
TEST(VFXParameterStore, PackToBuffer_EmptyStore_ReturnsZero) {
    VFXParameterStore store;
    uint8_t dummy[16] = {};
    const uint32_t written = store.PackToBuffer(dummy, 16u);
    EXPECT_EQ(written, 0u);
}
