/// UzEngine Smooth Normal Generator — Implementation
///
/// @ref Max 1999 "Weights for Computing Vertex Normals from Facet Normals"
/// @ref Thürey 2000 "Weighted Vertex Normals"

#include "normal_smoother.h"
#include "engine/asset/formats/uzmesh_format.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

struct Vec3f {
    float x, y, z;
    Vec3f() : x(0), y(0), z(0) {}
    Vec3f(float a, float b, float c) : x(a), y(b), z(c) {}
    Vec3f operator-(const Vec3f& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3f operator+(const Vec3f& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3f operator*(float s) const { return {x * s, y * s, z * s}; }
    float dot(const Vec3f& o) const { return x * o.x + y * o.y + z * o.z; }
    Vec3f cross(const Vec3f& o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    Vec3f normalized() const {
        float l = length();
        return l > 1e-8f ? Vec3f{x / l, y / l, z / l} : Vec3f{0, 0, 0};
    }
};

static Vec3f GetPos(const VertexPosNormUV& v) {
    return {v.position[0], v.position[1], v.position[2]};
}

/// Spatial hash key for vertex position merging (epsilon tolerance).
struct PosKey {
    int32_t x, y, z;
    bool operator==(const PosKey& o) const { return x == o.x && y == o.y && z == o.z; }
};

struct PosKeyHash {
    size_t operator()(const PosKey& k) const {
        size_t h = 0;
        h ^= std::hash<int32_t>{}(k.x) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>{}(k.y) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int32_t>{}(k.z) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

static PosKey MakePosKey(const Vec3f& p, float invCellSize) {
    return {
        static_cast<int32_t>(std::floor(p.x * invCellSize)),
        static_cast<int32_t>(std::floor(p.y * invCellSize)),
        static_cast<int32_t>(std::floor(p.z * invCellSize)),
    };
}

// ─────────────────────────────────────────────────────────────────────────────
// ComputeSmoothNormals
// ─────────────────────────────────────────────────────────────────────────────

void ComputeSmoothNormals(
    std::vector<VertexPosNormUV>& vertices,
    std::vector<uint32_t>& indices,
    float smoothingAngleDeg)
{
    if (vertices.empty() || indices.empty()) return;

    const size_t triCount = indices.size() / 3;
    const size_t vertCount = vertices.size();
    const float smoothingAngleRad = smoothingAngleDeg * 3.14159265359f / 180.0f;
    const float cosThreshold = std::cos(smoothingAngleRad);

    // ── Step 1: Compute face normals and areas ──────────────────────────
    struct FaceInfo {
        Vec3f normal;
        float area;
    };
    std::vector<FaceInfo> faces(triCount);

    for (size_t f = 0; f < triCount; ++f) {
        Vec3f p0 = GetPos(vertices[indices[f * 3 + 0]]);
        Vec3f p1 = GetPos(vertices[indices[f * 3 + 1]]);
        Vec3f p2 = GetPos(vertices[indices[f * 3 + 2]]);

        Vec3f e1 = p1 - p0;
        Vec3f e2 = p2 - p0;
        Vec3f faceNormal = e1.cross(e2);
        float area = faceNormal.length() * 0.5f;
        faces[f].normal = faceNormal.normalized();
        faces[f].area = area;
    }

    // ── Step 2: Build vertex → face adjacency via spatial hash ──────────
    // Group vertices by position (different indices may share same position).
    constexpr float kCellSize = 1e-5f;
    const float invCellSize = 1.0f / kCellSize;

    // Map: position key → list of (vertex index, face index, corner in face)
    struct VertFaceRef {
        uint32_t vertIdx;
        uint32_t faceIdx;
        uint32_t cornerInFace; // 0, 1, or 2
    };

    std::unordered_map<PosKey, std::vector<VertFaceRef>, PosKeyHash> posToFaces;
    posToFaces.reserve(vertCount);

    for (size_t f = 0; f < triCount; ++f) {
        for (uint32_t c = 0; c < 3; ++c) {
            uint32_t vi = indices[f * 3 + c];
            Vec3f pos = GetPos(vertices[vi]);
            PosKey key = MakePosKey(pos, invCellSize);
            posToFaces[key].push_back({vi, static_cast<uint32_t>(f), c});
        }
    }

    // ── Step 3: For each vertex, accumulate weighted normals ─────────────
    // Weight = triangle area × corner angle (UE5 pattern).
    // Only accumulate from faces whose normal is within smoothing threshold.
    std::vector<Vec3f> smoothNormals(vertCount, Vec3f{0, 0, 0});

    for (auto& [key, refs] : posToFaces) {
        // For each vertex at this position, accumulate normals from
        // adjacent faces that are within the smoothing angle.
        for (auto& ref : refs) {
            const auto& face = faces[ref.faceIdx];

            // Compute corner angle at this vertex in this triangle
            uint32_t i0 = indices[ref.faceIdx * 3 + ref.cornerInFace];
            uint32_t i1 = indices[ref.faceIdx * 3 + (ref.cornerInFace + 1) % 3];
            uint32_t i2 = indices[ref.faceIdx * 3 + (ref.cornerInFace + 2) % 3];

            Vec3f p0 = GetPos(vertices[i0]);
            Vec3f p1 = GetPos(vertices[i1]);
            Vec3f p2 = GetPos(vertices[i2]);

            Vec3f e1 = (p1 - p0).normalized();
            Vec3f e2 = (p2 - p0).normalized();
            float cornerAngle = std::acos(std::clamp(e1.dot(e2), -1.0f, 1.0f));

            // Weight = area × corner angle
            float weight = face.area * cornerAngle;

            // Accumulate this face's contribution to this vertex
            Vec3f contribution = face.normal * weight;

            // For all vertices at the same position, check if we should
            // smooth with this face (dihedral angle check).
            for (auto& otherRef : refs) {
                if (otherRef.vertIdx == ref.vertIdx && otherRef.faceIdx == ref.faceIdx)
                    continue; // Skip self

                // Check if the other face shares a smooth edge with this face
                float cosAngle = faces[ref.faceIdx].normal.dot(faces[otherRef.faceIdx].normal);
                if (cosAngle >= cosThreshold) {
                    // Smooth edge — already accumulated via this vertex's own face loop
                }
            }

            smoothNormals[ref.vertIdx] = smoothNormals[ref.vertIdx] + contribution;
        }
    }

    // Now for each vertex, also accumulate contributions from OTHER faces
    // at the same position that pass the smoothing angle test.
    for (auto& [key, refs] : posToFaces) {
        for (size_t i = 0; i < refs.size(); ++i) {
            for (size_t j = 0; j < refs.size(); ++j) {
                if (i == j) continue;
                if (refs[i].vertIdx == refs[j].vertIdx) continue;

                // Check if face normals are within smoothing threshold
                float cosAngle = faces[refs[i].faceIdx].normal.dot(faces[refs[j].faceIdx].normal);
                if (cosAngle >= cosThreshold) {
                    // Compute weight for the other face's contribution
                    const auto& otherFace = faces[refs[j].faceIdx];
                    uint32_t oi0 = indices[refs[j].faceIdx * 3 + refs[j].cornerInFace];
                    uint32_t oi1 = indices[refs[j].faceIdx * 3 + (refs[j].cornerInFace + 1) % 3];
                    uint32_t oi2 = indices[refs[j].faceIdx * 3 + (refs[j].cornerInFace + 2) % 3];

                    Vec3f op0 = GetPos(vertices[oi0]);
                    Vec3f op1 = GetPos(vertices[oi1]);
                    Vec3f op2 = GetPos(vertices[oi2]);

                    Vec3f oe1 = (op1 - op0).normalized();
                    Vec3f oe2 = (op2 - op0).normalized();
                    float otherCornerAngle = std::acos(std::clamp(oe1.dot(oe2), -1.0f, 1.0f));
                    float otherWeight = otherFace.area * otherCornerAngle;

                    smoothNormals[refs[i].vertIdx] = smoothNormals[refs[i].vertIdx] +
                        otherFace.normal * otherWeight;
                }
            }
        }
    }

    // ── Step 4: Normalize and write back ────────────────────────────────
    for (size_t v = 0; v < vertCount; ++v) {
        Vec3f n = smoothNormals[v].normalized();
        if (n.length() < 0.5f) {
            // Degenerate — keep original normal
            continue;
        }
        vertices[v].normal[0] = n.x;
        vertices[v].normal[1] = n.y;
        vertices[v].normal[2] = n.z;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Overload with per-face smoothing flags
// ─────────────────────────────────────────────────────────────────────────────

void ComputeSmoothNormals(
    std::vector<VertexPosNormUV>& vertices,
    std::vector<uint32_t>& indices,
    float smoothingAngleDeg,
    const std::vector<bool>& faceSmoothFlags)
{
    if (faceSmoothFlags.empty()) {
        // No smoothing flags — fall back to angle-only
        ComputeSmoothNormals(vertices, indices, smoothingAngleDeg);
        return;
    }

    if (vertices.empty() || indices.empty()) return;

    const size_t triCount = indices.size() / 3;
    const size_t vertCount = vertices.size();
    const float smoothingAngleRad = smoothingAngleDeg * 3.14159265359f / 180.0f;
    const float cosThreshold = std::cos(smoothingAngleRad);

    // Step 1: Compute face normals
    struct FaceInfo { Vec3f normal; float area; bool smooth; };
    std::vector<FaceInfo> faces(triCount);
    for (size_t f = 0; f < triCount; ++f) {
        Vec3f p0 = GetPos(vertices[indices[f * 3 + 0]]);
        Vec3f p1 = GetPos(vertices[indices[f * 3 + 1]]);
        Vec3f p2 = GetPos(vertices[indices[f * 3 + 2]]);
        Vec3f faceNormal = (p1 - p0).cross(p2 - p0);
        faces[f].area = faceNormal.length() * 0.5f;
        faces[f].normal = faceNormal.normalized();
        faces[f].smooth = (f < faceSmoothFlags.size()) ? faceSmoothFlags[f] : true;
    }

    // Step 2: Build adjacency
    constexpr float kCellSize = 1e-5f;
    const float invCellSize = 1.0f / kCellSize;

    struct VertFaceRef { uint32_t vertIdx; uint32_t faceIdx; uint32_t corner; };
    std::unordered_map<PosKey, std::vector<VertFaceRef>, PosKeyHash> posToFaces;
    posToFaces.reserve(vertCount);

    for (size_t f = 0; f < triCount; ++f) {
        for (uint32_t c = 0; c < 3; ++c) {
            uint32_t vi = indices[f * 3 + c];
            PosKey key = MakePosKey(GetPos(vertices[vi]), invCellSize);
            posToFaces[key].push_back({vi, static_cast<uint32_t>(f), c});
        }
    }

    // Step 3: Accumulate weighted normals with smoothing flag check
    std::vector<Vec3f> smoothNormals(vertCount, Vec3f{0, 0, 0});

    for (auto& [key, refs] : posToFaces) {
        for (size_t i = 0; i < refs.size(); ++i) {
            const auto& refI = refs[i];
            const auto& faceI = faces[refI.faceIdx];

            // Own face contribution (always added)
            uint32_t i0 = indices[refI.faceIdx * 3 + refI.corner];
            uint32_t i1 = indices[refI.faceIdx * 3 + (refI.corner + 1) % 3];
            uint32_t i2 = indices[refI.faceIdx * 3 + (refI.corner + 2) % 3];
            Vec3f e1 = (GetPos(vertices[i1]) - GetPos(vertices[i0])).normalized();
            Vec3f e2 = (GetPos(vertices[i2]) - GetPos(vertices[i0])).normalized();
            float cornerAngle = std::acos(std::clamp(e1.dot(e2), -1.0f, 1.0f));
            smoothNormals[refI.vertIdx] = smoothNormals[refI.vertIdx] + faceI.normal * (faceI.area * cornerAngle);

            // Neighbor contributions — only if BOTH faces are smooth AND within angle threshold
            for (size_t j = 0; j < refs.size(); ++j) {
                if (i == j) continue;
                const auto& refJ = refs[j];
                const auto& faceJ = faces[refJ.faceIdx];

                // Both faces must be marked smooth
                if (!faceI.smooth || !faceJ.smooth) continue;

                // Angle threshold check
                float cosAngle = faceI.normal.dot(faceJ.normal);
                if (cosAngle < cosThreshold) continue;

                // Compute neighbor's weight
                uint32_t j0 = indices[refJ.faceIdx * 3 + refJ.corner];
                uint32_t j1 = indices[refJ.faceIdx * 3 + (refJ.corner + 1) % 3];
                uint32_t j2 = indices[refJ.faceIdx * 3 + (refJ.corner + 2) % 3];
                Vec3f je1 = (GetPos(vertices[j1]) - GetPos(vertices[j0])).normalized();
                Vec3f je2 = (GetPos(vertices[j2]) - GetPos(vertices[j0])).normalized();
                float jAngle = std::acos(std::clamp(je1.dot(je2), -1.0f, 1.0f));

                smoothNormals[refI.vertIdx] = smoothNormals[refI.vertIdx] + faceJ.normal * (faceJ.area * jAngle);
            }
        }
    }

    // Step 4: Normalize and write back
    for (size_t v = 0; v < vertCount; ++v) {
        Vec3f n = smoothNormals[v].normalized();
        if (n.length() < 0.5f) continue;
        vertices[v].normal[0] = n.x;
        vertices[v].normal[1] = n.y;
        vertices[v].normal[2] = n.z;
    }
}

} // namespace uz
