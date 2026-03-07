// test_pick_buffer.cpp -- Google Test coverage for pick_buffer.h free functions.
//
// Covers:
//   - pick_node_at_screen: null/empty data, zero dimensions, coordinate clamping,
//     correct ID decoding (R|G<<8|B<<16), out-of-bounds pixel index, quarter-res mapping
//   - box_pick_nodes: null/empty data, zero dimensions, single-pixel box,
//     multi-pixel box with deduplication, zero-ID filtering, coordinate clamping,
//     box exceeding buffer bounds

#include <gtest/gtest.h>
#include "engine/renderer/core/pick_buffer.h"

#include <cstring>

// ============================================================================
// Helpers
// ============================================================================

// Build a pick buffer with given dimensions. Each pixel is 4 bytes (RGBA).
// Caller fills pixels after construction.
static std::vector<uint8_t> make_buffer(uint32_t w, uint32_t h) {
    return std::vector<uint8_t>(w * h * 4, 0);
}

// Encode a node ID into RGB at a specific pixel position.
static void set_pixel(std::vector<uint8_t>& buf, uint32_t w, uint32_t px, uint32_t py, uint32_t id) {
    const size_t idx = (static_cast<size_t>(py) * w + px) * 4;
    buf[idx + 0] = static_cast<uint8_t>(id & 0xFF);
    buf[idx + 1] = static_cast<uint8_t>((id >> 8) & 0xFF);
    buf[idx + 2] = static_cast<uint8_t>((id >> 16) & 0xFF);
    buf[idx + 3] = 0xFF; // alpha (unused)
}

// ============================================================================
// pick_node_at_screen tests
// ============================================================================

class PickNodeAtScreenTest : public ::testing::Test {};

TEST_F(PickNodeAtScreenTest, NullData_ReturnsZero) {
    EXPECT_EQ(uz::pick_node_at_screen(nullptr, 100, 10, 10, 0, 0), 0u);
}

TEST_F(PickNodeAtScreenTest, EmptyData_ReturnsZero) {
    uint8_t dummy = 0;
    EXPECT_EQ(uz::pick_node_at_screen(&dummy, 0, 10, 10, 0, 0), 0u);
}

TEST_F(PickNodeAtScreenTest, ZeroWidth_ReturnsZero) {
    auto buf = make_buffer(1, 1);
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 0, 1, 0, 0), 0u);
}

TEST_F(PickNodeAtScreenTest, ZeroHeight_ReturnsZero) {
    auto buf = make_buffer(1, 1);
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 1, 0, 0, 0), 0u);
}

TEST_F(PickNodeAtScreenTest, SimpleId_ReturnsCorrectValue) {
    // 4x4 pick buffer = 16x16 screen space (quarter-res)
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 42);

    // Screen (0,0) maps to pick (0,0)
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 0, 0), 42u);
}

TEST_F(PickNodeAtScreenTest, QuarterResMapping) {
    // 4x4 pick buffer. Screen (7,7) should map to pick (1,1) since 7/4=1
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 1, 1, 99);

    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 7, 7), 99u);
    // Screen (4,4) also maps to pick (1,1)
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 4, 4), 99u);
}

TEST_F(PickNodeAtScreenTest, LargeId_24Bit) {
    // Max 24-bit ID: 0xFFFFFF = 16777215
    auto buf = make_buffer(2, 2);
    set_pixel(buf, 2, 0, 0, 0xFFFFFF);

    uint32_t id = uz::pick_node_at_screen(buf.data(), buf.size(), 2, 2, 0, 0);
    EXPECT_EQ(id, 0x00FFFFFFu);
}

TEST_F(PickNodeAtScreenTest, RGBEncoding_Correct) {
    // ID = R | (G<<8) | (B<<16)
    // Set R=1, G=2, B=3 → ID = 1 + 512 + 196608 = 197121 = 0x030201
    auto buf = make_buffer(1, 1);
    buf[0] = 1;  // R
    buf[1] = 2;  // G
    buf[2] = 3;  // B
    buf[3] = 0;  // A

    uint32_t id = uz::pick_node_at_screen(buf.data(), buf.size(), 1, 1, 0, 0);
    EXPECT_EQ(id, 0x00030201u);
}

TEST_F(PickNodeAtScreenTest, EmptyPixel_ReturnsZero) {
    auto buf = make_buffer(4, 4);
    // All zeros — no node
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 0, 0), 0u);
}

TEST_F(PickNodeAtScreenTest, NegativeCoords_ClampToZero) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 7);

    // Negative screen coords → clamped to pick (0,0)
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, -100, -100), 7u);
}

TEST_F(PickNodeAtScreenTest, LargeCoords_ClampToMax) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 3, 3, 55);

    // Screen (9999, 9999) → pick clamp to (3,3)
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 9999, 9999), 55u);
}

TEST_F(PickNodeAtScreenTest, DataSizeTooSmall_ReturnsZero) {
    // Buffer claims 4x4 but data is only 2 pixels worth
    auto buf = make_buffer(1, 1); // 4 bytes
    set_pixel(buf, 1, 0, 0, 10);

    // Access pixel (3,3) in a 4x4 grid would need index 15*4+2=62 >= 4
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 12, 12), 0u);
}

TEST_F(PickNodeAtScreenTest, MultiplePixels_CorrectLookup) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 1);
    set_pixel(buf, 4, 1, 0, 2);
    set_pixel(buf, 4, 2, 1, 3);
    set_pixel(buf, 4, 3, 3, 4);

    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 0, 0), 1u);
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 4, 0), 2u);
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 8, 4), 3u);
    EXPECT_EQ(uz::pick_node_at_screen(buf.data(), buf.size(), 4, 4, 12, 12), 4u);
}

// ============================================================================
// box_pick_nodes tests
// ============================================================================

class BoxPickNodesTest : public ::testing::Test {};

TEST_F(BoxPickNodesTest, NullData_ReturnsEmpty) {
    auto result = uz::box_pick_nodes(nullptr, 100, 10, 10, 0, 0, 40, 40);
    EXPECT_TRUE(result.empty());
}

TEST_F(BoxPickNodesTest, EmptyData_ReturnsEmpty) {
    uint8_t dummy = 0;
    auto result = uz::box_pick_nodes(&dummy, 0, 10, 10, 0, 0, 40, 40);
    EXPECT_TRUE(result.empty());
}

TEST_F(BoxPickNodesTest, ZeroWidth_ReturnsEmpty) {
    auto buf = make_buffer(1, 1);
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 0, 1, 0, 0, 4, 4);
    EXPECT_TRUE(result.empty());
}

TEST_F(BoxPickNodesTest, ZeroHeight_ReturnsEmpty) {
    auto buf = make_buffer(1, 1);
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 1, 0, 0, 0, 4, 4);
    EXPECT_TRUE(result.empty());
}

TEST_F(BoxPickNodesTest, SinglePixelBox_ReturnsOneNode) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 42);

    // Box covering screen (0,0)-(4,4) → pick pixel (0,0) only
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 4, 4);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 42u);
}

TEST_F(BoxPickNodesTest, MultipleNodes_Deduplicated) {
    auto buf = make_buffer(4, 4);
    // Fill a 2x2 region with the same ID
    set_pixel(buf, 4, 0, 0, 10);
    set_pixel(buf, 4, 1, 0, 10);
    set_pixel(buf, 4, 0, 1, 10);
    set_pixel(buf, 4, 1, 1, 10);

    // Box covers all four pick pixels
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 8, 8);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 10u);
}

TEST_F(BoxPickNodesTest, MultipleDistinctNodes) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 1);
    set_pixel(buf, 4, 1, 0, 2);
    set_pixel(buf, 4, 0, 1, 3);
    set_pixel(buf, 4, 1, 1, 1); // duplicate of node 1

    // Box covers 2x2 pick region
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 8, 8);
    ASSERT_EQ(result.size(), 3u);

    // Should contain 1, 2, 3 (deduplicated)
    auto has = [&](uint32_t id) {
        return std::find(result.begin(), result.end(), id) != result.end();
    };
    EXPECT_TRUE(has(1));
    EXPECT_TRUE(has(2));
    EXPECT_TRUE(has(3));
}

TEST_F(BoxPickNodesTest, ZeroIdPixels_Filtered) {
    auto buf = make_buffer(4, 4);
    // Only one non-zero pixel in the box area
    set_pixel(buf, 4, 1, 0, 5);

    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 8, 4);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 5u);
}

TEST_F(BoxPickNodesTest, AllZero_ReturnsEmpty) {
    auto buf = make_buffer(4, 4);
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 16, 16);
    EXPECT_TRUE(result.empty());
}

TEST_F(BoxPickNodesTest, NegativeCoords_ClampToZero) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 7);

    // Negative origin → clamped to (0,0) in pick space
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, -100, -100, 200, 200);
    // Should include all non-zero pixels
    ASSERT_GE(result.size(), 1u);
    EXPECT_EQ(result[0], 7u);
}

TEST_F(BoxPickNodesTest, BoxExceedsBounds_ClampedSafely) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 3, 3, 88);

    // Box from (0,0) with huge size → clamped to buffer bounds
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 9999, 9999);
    auto has = [&](uint32_t id) {
        return std::find(result.begin(), result.end(), id) != result.end();
    };
    EXPECT_TRUE(has(88));
}

TEST_F(BoxPickNodesTest, ZeroSizeBox_ReturnsEmpty) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 42);

    // Zero-width box
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 0, 4);
    EXPECT_TRUE(result.empty());

    // Zero-height box
    result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 4, 0);
    EXPECT_TRUE(result.empty());
}

TEST_F(BoxPickNodesTest, DataSizeTooSmall_SkipsOutOfBoundsPixels) {
    // 2 pixels of data but claim 4x4 buffer
    auto buf = make_buffer(2, 1); // 8 bytes, 2 pixels
    set_pixel(buf, 2, 0, 0, 15);
    set_pixel(buf, 2, 1, 0, 16);

    // Request a large box — pixels beyond data_size are skipped
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 16, 16);
    ASSERT_EQ(result.size(), 2u);
    auto has = [&](uint32_t id) {
        return std::find(result.begin(), result.end(), id) != result.end();
    };
    EXPECT_TRUE(has(15));
    EXPECT_TRUE(has(16));
}

TEST_F(BoxPickNodesTest, PartialBox_OnlyPicksInsideRegion) {
    auto buf = make_buffer(4, 4);
    set_pixel(buf, 4, 0, 0, 1);
    set_pixel(buf, 4, 3, 3, 2);

    // Small box covering only the top-left pixel
    auto result = uz::box_pick_nodes(buf.data(), buf.size(), 4, 4, 0, 0, 4, 4);
    ASSERT_EQ(result.size(), 1u);
    EXPECT_EQ(result[0], 1u);
}
