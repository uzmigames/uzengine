#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace uz {

inline constexpr uint32_t kMeshSdfBvhFlag = 0x80000000u;
inline constexpr uint32_t kMeshSdfBvhLeafSize = 4;

struct MeshSdfGpuTriangle {
    float v0[3]; uint32_t originalIndex;
    float v1[3]; uint32_t padding1;
    float v2[3]; uint32_t padding2;
    float normal[3]; uint32_t padding3;
};

// Nodes follow the reordered triangles in the same 64-byte structured buffer.
// Escape indices address the node array; firstTriangle addresses the triangle array.
struct MeshSdfGpuBvhNode {
    float boundsMin[3]; uint32_t escapeIndex;
    float boundsMax[3]; uint32_t firstTriangle;
    uint32_t triangleCount;
    uint32_t padding[7] = {};
};

static_assert(sizeof(MeshSdfGpuTriangle) == 64);
static_assert(sizeof(MeshSdfGpuBvhNode) == 64);
static_assert(offsetof(MeshSdfGpuTriangle, originalIndex) == 12);
static_assert(offsetof(MeshSdfGpuBvhNode, escapeIndex) == 12);
static_assert(offsetof(MeshSdfGpuBvhNode, firstTriangle) == 28);
static_assert(offsetof(MeshSdfGpuBvhNode, triangleCount) == 32);

namespace sdf_bvh_detail {

inline double Centroid(const MeshSdfGpuTriangle& triangle, uint32_t axis) {
    return (static_cast<double>(triangle.v0[axis]) + triangle.v1[axis] + triangle.v2[axis]) / 3.0;
}

/// @ref Pharr, Jakob & Humphreys 2023 "Physically Based Rendering", 4th ed., section 7.3.
/// Equal-count median splits bound recursion depth even for coincident centroids.
inline void BuildNode(std::vector<MeshSdfGpuTriangle>& triangles,
                      std::vector<MeshSdfGpuBvhNode>& nodes, uint32_t begin, uint32_t end) {
    MeshSdfGpuBvhNode node{};
    double centroidMin[3], centroidMax[3];
    for (uint32_t axis = 0; axis < 3; ++axis) {
        node.boundsMin[axis] = std::numeric_limits<float>::max();
        node.boundsMax[axis] = std::numeric_limits<float>::lowest();
        centroidMin[axis] = std::numeric_limits<double>::max();
        centroidMax[axis] = std::numeric_limits<double>::lowest();
    }
    for (uint32_t i = begin; i < end; ++i) {
        const auto& triangle = triangles[i];
        for (uint32_t axis = 0; axis < 3; ++axis) {
            node.boundsMin[axis] = std::min({node.boundsMin[axis], triangle.v0[axis], triangle.v1[axis], triangle.v2[axis]});
            node.boundsMax[axis] = std::max({node.boundsMax[axis], triangle.v0[axis], triangle.v1[axis], triangle.v2[axis]});
            const double centroid = Centroid(triangle, axis);
            centroidMin[axis] = std::min(centroidMin[axis], centroid);
            centroidMax[axis] = std::max(centroidMax[axis], centroid);
        }
    }
    const auto nodeIndex = static_cast<uint32_t>(nodes.size());
    nodes.push_back(node);
    const uint32_t count = end - begin;
    if (count <= kMeshSdfBvhLeafSize) {
        nodes[nodeIndex].firstTriangle = begin;
        nodes[nodeIndex].triangleCount = count;
    } else {
        uint32_t axis = 0;
        for (uint32_t candidate = 1; candidate < 3; ++candidate) {
            if (centroidMax[candidate] - centroidMin[candidate] > centroidMax[axis] - centroidMin[axis])
                axis = candidate;
        }
        const uint32_t middle = begin + count / 2;
        std::nth_element(triangles.begin() + begin, triangles.begin() + middle, triangles.begin() + end,
            [axis](const MeshSdfGpuTriangle& a, const MeshSdfGpuTriangle& b) {
                const double ca = Centroid(a, axis), cb = Centroid(b, axis);
                return ca == cb ? a.originalIndex < b.originalIndex : ca < cb;
            });
        BuildNode(triangles, nodes, begin, middle);
        BuildNode(triangles, nodes, middle, end);
    }
    nodes[nodeIndex].escapeIndex = static_cast<uint32_t>(nodes.size());
}

} // namespace sdf_bvh_detail

/// Reorders triangles in place and emits preorder nodes with stackless escape links.
/// Rejects non-finite positions and sizes that cannot fit GPU element/host byte counts.
inline bool BuildMeshSdfBvh(std::vector<MeshSdfGpuTriangle>& triangles,
                           std::vector<MeshSdfGpuBvhNode>& nodes) {
    nodes.clear();
    const size_t count = triangles.size();
    if (count == 0 || count > (UINT32_MAX - 1u) / 3u
        || count > std::numeric_limits<size_t>::max() / (3u * sizeof(MeshSdfGpuTriangle))) return false;
    for (size_t i = 0; i < count; ++i) {
        auto& triangle = triangles[i];
        for (uint32_t axis = 0; axis < 3; ++axis) {
            if (!std::isfinite(triangle.v0[axis]) || !std::isfinite(triangle.v1[axis])
                || !std::isfinite(triangle.v2[axis])) return false;
        }
        triangle.originalIndex = static_cast<uint32_t>(i);
    }
    nodes.reserve(count);
    sdf_bvh_detail::BuildNode(triangles, nodes, 0, static_cast<uint32_t>(count));
    return true;
}

} // namespace uz
