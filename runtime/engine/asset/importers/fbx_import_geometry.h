#pragma once

#include "engine/asset/mesh/normal_smoother.h"
#include "engine/asset/mesh/mikktspace_tangents.h"

#include <algorithm>
#include <cstddef>

namespace uz {

// Keep geometric normals in source (CCW-outward) orientation. The Maya/Max
// remaps have det=+1; only the render indices need conversion to CW-front.
inline void PrepareFbxGeometry(
    std::vector<VertexPosNormUV>& vertices,
    std::vector<uint32_t>& indices,
    bool recomputeNormals,
    float smoothingAngle,
    const std::vector<bool>& smoothFlags,
    bool reverseWinding) {
    if (recomputeNormals) {
        ComputeSmoothNormals(vertices, indices, smoothingAngle, smoothFlags);
    }
    if (reverseWinding) {
        for (size_t t = 0; t + 2 < indices.size(); t += 3) {
            std::swap(indices[t + 1], indices[t + 2]);
        }
    }
}

inline void ComputeFbxMikkTSpaceTangents(
    const std::vector<VertexPosNormUV>& vertices,
    const std::vector<uint32_t>& indices,
    std::vector<float>& tangents,
    bool reversedWinding) {
    ComputeMikkTSpaceTangents(vertices, indices, tangents);
    // Mikk's sign follows UV triangle orientation. The CW conversion reverses
    // that orientation but not N or the UV derivatives: restore B = w * (N x T).
    if (reversedWinding) {
        for (size_t v = 3; v < tangents.size(); v += 4) {
            tangents[v] = -tangents[v];
        }
    }
}

} // namespace uz
