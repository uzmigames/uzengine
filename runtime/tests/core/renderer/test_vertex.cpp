/// Unit tests for vertex layout definitions.
///
/// Validates struct sizes, alignment, and field offsets for GPU compatibility.
/// Both Vertex (48 bytes) and SimpleVertex (36 bytes) must match the HLSL
/// StructuredBuffer layout exactly.

#include "vertex.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Vertex — full PBR mesh vertex (48 bytes)
// ─────────────────────────────────────────────────────────────────────────────

TEST(Vertex, SizeIs48Bytes) {
    EXPECT_EQ(sizeof(Vertex), 48u);
}

TEST(Vertex, FieldOffsets) {
    EXPECT_EQ(offsetof(Vertex, position), 0u);
    EXPECT_EQ(offsetof(Vertex, normal), 12u);
    EXPECT_EQ(offsetof(Vertex, texcoord), 24u);
    EXPECT_EQ(offsetof(Vertex, tangent), 32u);
}

TEST(Vertex, DefaultInitialization) {
    Vertex v{};
    EXPECT_FLOAT_EQ(v.position.x, 0.0f);
    EXPECT_FLOAT_EQ(v.position.y, 0.0f);
    EXPECT_FLOAT_EQ(v.position.z, 0.0f);
    EXPECT_FLOAT_EQ(v.normal.x, 0.0f);
    EXPECT_FLOAT_EQ(v.texcoord.x, 0.0f);
    EXPECT_FLOAT_EQ(v.texcoord.y, 0.0f);
    EXPECT_FLOAT_EQ(v.tangent.x, 0.0f);
    EXPECT_FLOAT_EQ(v.tangent.w, 0.0f);
}

TEST(Vertex, AggregateInitialization) {
    Vertex v = {
        {1.0f, 2.0f, 3.0f},      // position
        {0.0f, 1.0f, 0.0f},      // normal
        {0.5f, 0.5f},            // texcoord
        {1.0f, 0.0f, 0.0f, 1.0f} // tangent + handedness
    };
    EXPECT_FLOAT_EQ(v.position.x, 1.0f);
    EXPECT_FLOAT_EQ(v.position.z, 3.0f);
    EXPECT_FLOAT_EQ(v.normal.y, 1.0f);
    EXPECT_FLOAT_EQ(v.texcoord.x, 0.5f);
    EXPECT_FLOAT_EQ(v.tangent.w, 1.0f);
}

TEST(Vertex, BinaryLayoutMatchesHLSL) {
    // Verify the struct is tightly packed as expected by HLSL StructuredBuffer
    Vertex v = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}, {7.0f, 8.0f}, {9.0f, 10.0f, 11.0f, 12.0f}};

    const auto* raw = reinterpret_cast<const float*>(&v);
    EXPECT_FLOAT_EQ(raw[0], 1.0f);   // position.x
    EXPECT_FLOAT_EQ(raw[1], 2.0f);   // position.y
    EXPECT_FLOAT_EQ(raw[2], 3.0f);   // position.z
    EXPECT_FLOAT_EQ(raw[3], 4.0f);   // normal.x
    EXPECT_FLOAT_EQ(raw[4], 5.0f);   // normal.y
    EXPECT_FLOAT_EQ(raw[5], 6.0f);   // normal.z
    EXPECT_FLOAT_EQ(raw[6], 7.0f);   // texcoord.x
    EXPECT_FLOAT_EQ(raw[7], 8.0f);   // texcoord.y
    EXPECT_FLOAT_EQ(raw[8], 9.0f);   // tangent.x
    EXPECT_FLOAT_EQ(raw[9], 10.0f);  // tangent.y
    EXPECT_FLOAT_EQ(raw[10], 11.0f); // tangent.z
    EXPECT_FLOAT_EQ(raw[11], 12.0f); // tangent.w
}

TEST(Vertex, ArrayStride) {
    Vertex verts[3] = {};
    auto ptrDiff = reinterpret_cast<uintptr_t>(&verts[1]) - reinterpret_cast<uintptr_t>(&verts[0]);
    EXPECT_EQ(ptrDiff, 48u);
}

// ─────────────────────────────────────────────────────────────────────────────
// SimpleVertex — debug/UI vertex (36 bytes)
// ─────────────────────────────────────────────────────────────────────────────

TEST(SimpleVertex, SizeIs36Bytes) {
    EXPECT_EQ(sizeof(SimpleVertex), 36u);
}

TEST(SimpleVertex, FieldOffsets) {
    EXPECT_EQ(offsetof(SimpleVertex, position), 0u);
    EXPECT_EQ(offsetof(SimpleVertex, color), 12u);
    EXPECT_EQ(offsetof(SimpleVertex, texcoord), 28u);
}

TEST(SimpleVertex, DefaultInitialization) {
    SimpleVertex v{};
    EXPECT_FLOAT_EQ(v.position.x, 0.0f);
    EXPECT_FLOAT_EQ(v.color.x, 0.0f);
    EXPECT_FLOAT_EQ(v.color.w, 0.0f);
    EXPECT_FLOAT_EQ(v.texcoord.x, 0.0f);
}

TEST(SimpleVertex, AggregateInitialization) {
    SimpleVertex v = {
        {0.0f, 0.5f, 0.0f},         // position
        {1.0f, 0.0f, 0.0f, 1.0f},   // color (red)
        {0.5f, 0.0f}                 // texcoord
    };
    EXPECT_FLOAT_EQ(v.position.y, 0.5f);
    EXPECT_FLOAT_EQ(v.color.x, 1.0f);
    EXPECT_FLOAT_EQ(v.color.w, 1.0f);
    EXPECT_FLOAT_EQ(v.texcoord.x, 0.5f);
}

TEST(SimpleVertex, BinaryLayoutMatchesHLSL) {
    SimpleVertex v = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f, 7.0f}, {8.0f, 9.0f}};

    const auto* raw = reinterpret_cast<const float*>(&v);
    EXPECT_FLOAT_EQ(raw[0], 1.0f);  // position.x
    EXPECT_FLOAT_EQ(raw[1], 2.0f);  // position.y
    EXPECT_FLOAT_EQ(raw[2], 3.0f);  // position.z
    EXPECT_FLOAT_EQ(raw[3], 4.0f);  // color.x
    EXPECT_FLOAT_EQ(raw[4], 5.0f);  // color.y
    EXPECT_FLOAT_EQ(raw[5], 6.0f);  // color.z
    EXPECT_FLOAT_EQ(raw[6], 7.0f);  // color.w
    EXPECT_FLOAT_EQ(raw[7], 8.0f);  // texcoord.x
    EXPECT_FLOAT_EQ(raw[8], 9.0f);  // texcoord.y
}

TEST(SimpleVertex, ArrayStride) {
    SimpleVertex verts[3] = {};
    auto ptrDiff = reinterpret_cast<uintptr_t>(&verts[1]) - reinterpret_cast<uintptr_t>(&verts[0]);
    EXPECT_EQ(ptrDiff, 36u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Triangle vertex data consistency
// ─────────────────────────────────────────────────────────────────────────────

TEST(SimpleVertex, TriangleVerticesAreValid) {
    SimpleVertex triangle[3] = {
        {{ 0.0f,  0.5f, 0.0f}, {1, 0, 0, 1}, {0.5f, 0.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0, 1, 0, 1}, {1.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0, 0, 1, 1}, {0.0f, 1.0f}},
    };

    // All positions in NDC range
    for (const auto& v : triangle) {
        EXPECT_GE(v.position.x, -1.0f);
        EXPECT_LE(v.position.x, 1.0f);
        EXPECT_GE(v.position.y, -1.0f);
        EXPECT_LE(v.position.y, 1.0f);
    }

    // All colors have full alpha
    for (const auto& v : triangle) {
        EXPECT_FLOAT_EQ(v.color.w, 1.0f);
    }

    // UVs in [0, 1]
    for (const auto& v : triangle) {
        EXPECT_GE(v.texcoord.x, 0.0f);
        EXPECT_LE(v.texcoord.x, 1.0f);
        EXPECT_GE(v.texcoord.y, 0.0f);
        EXPECT_LE(v.texcoord.y, 1.0f);
    }
}
