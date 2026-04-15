#include "vfx_static_mesh_di.h"

#include <algorithm>
#include <cmath>

namespace uz {

namespace {

Vec3 VecSub(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{a.x - b.x, a.y - b.y, a.z - b.z};
}

/// Cross product for triangle area computation.
Vec3 VecCross(const Vec3& a, const Vec3& b) noexcept {
    return Vec3{
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x,
    };
}

float VecLength(const Vec3& v) noexcept {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

/// Triangle area from three vertices.
float TriangleArea(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
    return 0.5f * VecLength(VecCross(VecSub(b, a), VecSub(c, a)));
}

float Clamp01(float v) noexcept {
    if (!(v >= 0.0f)) return 0.0f;          // catches NaN too
    if (v >= 1.0f)    return 0.999999f;     // strict [0, 1)
    return v;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VfxUniformBarycentric — Turk 1990 distribution
// ─────────────────────────────────────────────────────────────────────────────
//
// Uniform points on a triangle: given two uniform [0,1) inputs, produce
// barycentrics (a, b, c) with a + b + c == 1 and uniform area density.

VfxBarycentric VfxUniformBarycentric(float u, float v) noexcept {
    u = Clamp01(u);
    v = Clamp01(v);
    const float sqrtU = std::sqrt(u);
    VfxBarycentric b;
    b.a = 1.0f - sqrtU;
    b.b = sqrtU * (1.0f - v);
    b.c = sqrtU * v;
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxStaticMeshDI::BuildAreaCdf
// ─────────────────────────────────────────────────────────────────────────────
//
// Walks every triangle, sums areas, and stores a normalised cumulative
// prefix-sum so inverse-CDF sampling can pick a triangle weighted by area
// via a single binary search.

void VfxStaticMeshDI::BuildAreaCdf() const {
    const uint32_t triCount = GetTriangleCount();
    triangleAreaCdf.assign(triCount, 0.0f);
    totalArea = 0.0f;

    if (triCount == 0) return;

    // First pass: accumulate raw areas into the table.
    for (uint32_t i = 0; i < triCount; ++i) {
        const uint32_t i0 = indices[i * 3 + 0];
        const uint32_t i1 = indices[i * 3 + 1];
        const uint32_t i2 = indices[i * 3 + 2];
        // Defensive: out-of-range indices produce zero-area triangles.
        const bool safe = (i0 < positions.size()) &&
                          (i1 < positions.size()) &&
                          (i2 < positions.size());
        const float area = safe
            ? TriangleArea(positions[i0], positions[i1], positions[i2])
            : 0.0f;
        totalArea += area;
        triangleAreaCdf[i] = totalArea;
    }

    // Second pass: normalise to [0, 1]. If totalArea is zero (degenerate
    // mesh) leave the table zeroed — SampleSurface checks and returns empty.
    if (!(totalArea > 0.0f)) return;
    const float invTotal = 1.0f / totalArea;
    for (uint32_t i = 0; i < triCount; ++i) {
        triangleAreaCdf[i] *= invTotal;
    }
    // Guarantee the last entry is exactly 1.0 so the binary search always
    // resolves even with floating-point summation error.
    triangleAreaCdf.back() = 1.0f;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxStaticMeshDI::SampleSurface
// ─────────────────────────────────────────────────────────────────────────────

VfxMeshSurfaceSample VfxStaticMeshDI::SampleSurface(float rng0, float rng1,
                                                    float rng2) const {
    VfxMeshSurfaceSample out;
    const uint32_t triCount = GetTriangleCount();
    if (triCount == 0 || triangleAreaCdf.empty() || !(totalArea > 0.0f)) {
        return out;
    }

    const float u = Clamp01(rng0);

    // Binary search the CDF for the first entry >= u.
    uint32_t lo = 0;
    uint32_t hi = triCount - 1;
    while (lo < hi) {
        const uint32_t mid = (lo + hi) / 2;
        if (triangleAreaCdf[mid] < u) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    const uint32_t triIndex = lo;

    const uint32_t i0 = indices[triIndex * 3 + 0];
    const uint32_t i1 = indices[triIndex * 3 + 1];
    const uint32_t i2 = indices[triIndex * 3 + 2];

    // Defensive: clamp out-of-range vertex indices to the first vertex.
    const uint32_t n = static_cast<uint32_t>(positions.size());
    const uint32_t v0 = (i0 < n) ? i0 : 0u;
    const uint32_t v1 = (i1 < n) ? i1 : 0u;
    const uint32_t v2 = (i2 < n) ? i2 : 0u;

    const VfxBarycentric bary = VfxUniformBarycentric(rng1, rng2);

    // Position interpolation.
    const Vec3 p0 = positions[v0];
    const Vec3 p1 = positions[v1];
    const Vec3 p2 = positions[v2];
    out.position = Vec3{
        p0.x * bary.a + p1.x * bary.b + p2.x * bary.c,
        p0.y * bary.a + p1.y * bary.b + p2.y * bary.c,
        p0.z * bary.a + p1.z * bary.b + p2.z * bary.c,
    };

    // Normal interpolation + re-normalisation.
    if (v0 < normals.size() && v1 < normals.size() && v2 < normals.size()) {
        const Vec3 n0 = normals[v0];
        const Vec3 n1 = normals[v1];
        const Vec3 n2 = normals[v2];
        const Vec3 rawN = Vec3{
            n0.x * bary.a + n1.x * bary.b + n2.x * bary.c,
            n0.y * bary.a + n1.y * bary.b + n2.y * bary.c,
            n0.z * bary.a + n1.z * bary.b + n2.z * bary.c,
        };
        const float nLen = VecLength(rawN);
        if (nLen > 0.0f) {
            const float inv = 1.0f / nLen;
            out.normal = Vec3{rawN.x * inv, rawN.y * inv, rawN.z * inv};
        } else {
            out.normal = Vec3{0.0f, 0.0f, 1.0f};  // defensive default
        }
    } else {
        out.normal = Vec3{0.0f, 0.0f, 1.0f};
    }

    // UV interpolation (no re-normalisation).
    if (v0 < uvs.size() && v1 < uvs.size() && v2 < uvs.size()) {
        const Vec2 u0 = uvs[v0];
        const Vec2 u1 = uvs[v1];
        const Vec2 u2 = uvs[v2];
        out.uv = Vec2{
            u0.x * bary.a + u1.x * bary.b + u2.x * bary.c,
            u0.y * bary.a + u1.y * bary.b + u2.y * bary.c,
        };
    }

    out.triangleIndex = triIndex;
    return out;
}

} // namespace uz
