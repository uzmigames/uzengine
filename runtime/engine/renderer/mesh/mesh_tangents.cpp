/// UzEngine Mesh Format — Tangent Generation (Lengyel Method)

#include "mesh.h"
#include "profiler.h"

#include <cmath>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GenerateTangents
// ─────────────────────────────────────────────────────────────────────────────

void GenerateTangents(std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices) {
    UZ_PROFILE_FUNCTION();

    if (vertices.empty() || indices.size() < 3) {
        return;
    }

    const size_t vertexCount = vertices.size();

    // Accumulated tangent and bitangent vectors per vertex (not yet normalized).
    // Using separate arrays avoids polluting the Vertex struct during accumulation.
    std::vector<Vec3> tangentAccum(vertexCount, Vec3{0.f, 0.f, 0.f});
    std::vector<Vec3> bitangentAccum(vertexCount, Vec3{0.f, 0.f, 0.f});

    // Accumulate tangent/bitangent contributions from each triangle.
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const uint32_t i0 = indices[i];
        const uint32_t i1 = indices[i + 1];
        const uint32_t i2 = indices[i + 2];

        // Bounds check — skip invalid indices rather than crashing.
        if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
            continue;
        }

        const Vec3& p0 = vertices[i0].position;
        const Vec3& p1 = vertices[i1].position;
        const Vec3& p2 = vertices[i2].position;

        const Vec2& uv0 = vertices[i0].texcoord;
        const Vec2& uv1 = vertices[i1].texcoord;
        const Vec2& uv2 = vertices[i2].texcoord;

        // Edge vectors in position space
        const float e1x = p1.x - p0.x;
        const float e1y = p1.y - p0.y;
        const float e1z = p1.z - p0.z;

        const float e2x = p2.x - p0.x;
        const float e2y = p2.y - p0.y;
        const float e2z = p2.z - p0.z;

        // UV deltas
        const float du1 = uv1.x - uv0.x;
        const float dv1 = uv1.y - uv0.y;
        const float du2 = uv2.x - uv0.x;
        const float dv2 = uv2.y - uv0.y;

        // Determinant of the UV matrix. If near-zero, the triangle has
        // degenerate UVs (zero-area in UV space) — skip it.
        const float det = du1 * dv2 - du2 * dv1;
        if (std::abs(det) < 1e-12f) {
            continue;
        }
        const float invDet = 1.0f / det;

        // Tangent: T = (e1 * dv2 - e2 * dv1) / det
        const Vec3 t = {
            (e1x * dv2 - e2x * dv1) * invDet,
            (e1y * dv2 - e2y * dv1) * invDet,
            (e1z * dv2 - e2z * dv1) * invDet,
        };

        // Bitangent: B = (e2 * du1 - e1 * du2) / det
        const Vec3 b = {
            (e2x * du1 - e1x * du2) * invDet,
            (e2y * du1 - e1y * du2) * invDet,
            (e2z * du1 - e1z * du2) * invDet,
        };

        // Accumulate for all three vertices of the triangle.
        // Weighting by triangle area is implicit in the unnormalized edge vectors.
        tangentAccum[i0].x += t.x; tangentAccum[i0].y += t.y; tangentAccum[i0].z += t.z;
        tangentAccum[i1].x += t.x; tangentAccum[i1].y += t.y; tangentAccum[i1].z += t.z;
        tangentAccum[i2].x += t.x; tangentAccum[i2].y += t.y; tangentAccum[i2].z += t.z;

        bitangentAccum[i0].x += b.x; bitangentAccum[i0].y += b.y; bitangentAccum[i0].z += b.z;
        bitangentAccum[i1].x += b.x; bitangentAccum[i1].y += b.y; bitangentAccum[i1].z += b.z;
        bitangentAccum[i2].x += b.x; bitangentAccum[i2].y += b.y; bitangentAccum[i2].z += b.z;
    }

    // Orthogonalize, normalize, and compute bitangent sign per vertex.
    for (size_t v = 0; v < vertexCount; ++v) {
        const Vec3& n = vertices[v].normal;
        Vec3 t = tangentAccum[v];
        const Vec3& b = bitangentAccum[v];

        // Gram-Schmidt orthogonalization: T' = T - N * dot(N, T)
        const float nDotT = n.x * t.x + n.y * t.y + n.z * t.z;
        t.x -= n.x * nDotT;
        t.y -= n.y * nDotT;
        t.z -= n.z * nDotT;

        // Normalize the tangent
        const float len = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
        if (len > 1e-8f) {
            const float invLen = 1.0f / len;
            t.x *= invLen;
            t.y *= invLen;
            t.z *= invLen;
        } else {
            // Fallback: generate an arbitrary tangent perpendicular to the normal.
            // Choose the axis least aligned with the normal to minimize numerical error.
            const float absX = std::abs(n.x);
            const float absY = std::abs(n.y);
            const float absZ = std::abs(n.z);

            Vec3 up;
            if (absX <= absY && absX <= absZ) {
                up = Vec3{1.f, 0.f, 0.f};
            } else if (absY <= absZ) {
                up = Vec3{0.f, 1.f, 0.f};
            } else {
                up = Vec3{0.f, 0.f, 1.f};
            }

            // t = normalize(up - n * dot(n, up))
            const float d = n.x * up.x + n.y * up.y + n.z * up.z;
            t.x = up.x - n.x * d;
            t.y = up.y - n.y * d;
            t.z = up.z - n.z * d;

            const float fallbackLen = std::sqrt(t.x * t.x + t.y * t.y + t.z * t.z);
            if (fallbackLen > 1e-8f) {
                const float invFallbackLen = 1.0f / fallbackLen;
                t.x *= invFallbackLen;
                t.y *= invFallbackLen;
                t.z *= invFallbackLen;
            } else {
                // Extremely degenerate case (zero normal) — use +X as tangent.
                t = Vec3{1.f, 0.f, 0.f};
            }
        }

        // Compute bitangent sign: w = sign(dot(cross(N, T), B))
        // cross(N, T):
        const float cx = n.y * t.z - n.z * t.y;
        const float cy = n.z * t.x - n.x * t.z;
        const float cz = n.x * t.y - n.y * t.x;
        // dot(cross(N, T), B):
        const float sign = cx * b.x + cy * b.y + cz * b.z;
        const float w = (sign < 0.0f) ? -1.0f : 1.0f;

        vertices[v].tangent = Vec4{t.x, t.y, t.z, w};
    }
}

} // namespace uz
