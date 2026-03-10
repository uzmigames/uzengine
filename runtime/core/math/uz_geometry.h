/// UzEngine Geometry Library
///
/// AABB (Axis-Aligned Bounding Box) and Frustum for frustum culling.
/// All types work with DirectXMath vectors.

#pragma once

#include <algorithm>

#include "uz_math.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Plane — defines half-space (ax + by + cz + d = 0)
// ─────────────────────────────────────────────────────────────────────────────

/// Plane equation (ax + by + cz + d = 0) — defines half-space for frustum culling.
/// Normal vector is (a, b, c), and signed distance from point is the plane equation result.
struct Plane {
    float a, b, c, d;

    /// Constructors
    Plane() = default;
    /// Create plane from coefficients (a, b, c, d) where normal = (a, b, c).
    Plane(float a, float b, float c, float d) : a(a), b(b), c(c), d(d) {}
    /// Create plane from XMVECTOR (packed as XMFLOAT4 coefficients).
    explicit Plane(const XMVECTOR& xm) { XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm); }

    /// Conversion to XMVECTOR for SIMD operations.
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Signed distance from point to plane. Positive = front (on normal side), negative = behind.
    float distance(const Vec3& p) const { return a * p.x + b * p.y + c * p.z + d; }
    /// Uppercase alias for distance() for test compatibility.
    float Distance(const Vec3& p) const { return distance(p); }
    /// Uppercase alias for distance() for clarity.
    float DistanceToPoint(const Vec3& p) const { return distance(p); }
};

// ─────────────────────────────────────────────────────────────────────────────
// AABB — Axis-Aligned Bounding Box
// ─────────────────────────────────────────────────────────────────────────────

/// Axis-Aligned Bounding Box for collision testing and frustum culling.
/// Stores min/max corners; invalid by default (min > max) to safely expand incrementally.
struct AABB {
    Vec3 min, max;

    /// Default constructor creates invalid AABB (min=+inf, max=-inf) for safe expansion.
    AABB() : min(FLT_MAX, FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX, -FLT_MAX) {}
    /// Create AABB from explicit min/max corners.
    AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}

    /// Test if point is strictly inside AABB (including boundary).
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x && p.y >= min.y && p.y <= max.y && p.z >= min.z &&
               p.z <= max.z;
    }
    /// Uppercase alias for test compatibility.
    bool Contains(const Vec3& p) const { return contains(p); }

    /// Test if another AABB overlaps this one (inclusive).
    bool intersects(const AABB& other) const {
        return !(max.x < other.min.x || min.x > other.max.x || max.y < other.min.y ||
                 min.y > other.max.y || max.z < other.min.z || min.z > other.max.z);
    }
    /// Uppercase alias for test compatibility.
    bool Intersects(const AABB& other) const { return intersects(other); }

    /// Test if sphere (center + radius) overlaps this AABB.
    bool intersectsSphere(const Vec3& center, float radius) const {
        Vec3 closest;
        closest.x = std::max(min.x, std::min(center.x, max.x));
        closest.y = std::max(min.y, std::min(center.y, max.y));
        closest.z = std::max(min.z, std::min(center.z, max.z));

        float dx = center.x - closest.x;
        float dy = center.y - closest.y;
        float dz = center.z - closest.z;
        return (dx * dx + dy * dy + dz * dz) <= (radius * radius);
    }
    /// Uppercase alias for test compatibility.
    bool IntersectsSphere(const Vec3& center, float radius) const {
        return intersectsSphere(center, radius);
    }

    /// Expand AABB to include point (returns new AABB, non-mutating).
    AABB expand(const Vec3& p) const {
        AABB result = *this;
        result.min = result.min.min(p);
        result.max = result.max.max(p);
        return result;
    }
    /// Expand this AABB to include point (mutating).
    void Expand(const Vec3& p) {
        min = min.min(p);
        max = max.max(p);
    }

    /// Merge with another AABB (returns new AABB, non-mutating).
    AABB merge(const AABB& other) const {
        AABB result = *this;
        result.min = result.min.min(other.min);
        result.max = result.max.max(other.max);
        return result;
    }
    /// Uppercase alias for merge() for consistency.
    AABB Merge(const AABB& other) const { return merge(other); }

    /// Center point of the AABB.
    Vec3 center() const { return (min + max) * 0.5f; }
    /// Uppercase alias for consistency.
    Vec3 Center() const { return center(); }
    /// Half-extents from center to corners.
    Vec3 extents() const { return (max - min) * 0.5f; }
    /// Uppercase alias for consistency.
    Vec3 Extents() const { return extents(); }
    /// Full size (max - min).
    Vec3 size() const { return max - min; }
    /// Uppercase alias for consistency.
    Vec3 Size() const { return size(); }

    /// Volume of the bounding box (useful for tree sorting/heuristics).
    float volume() const {
        Vec3 s = size();
        return s.x * s.y * s.z;
    }
    /// Uppercase alias for consistency.
    float Volume() const { return volume(); }

    /// Check if AABB is valid (min <= max in all dimensions).
    bool isValid() const { return min.x <= max.x && min.y <= max.y && min.z <= max.z; }
    /// Uppercase alias for consistency.
    bool IsValid() const { return isValid(); }

    /// Construct AABB from center and half-extents.
    static AABB FromCenterExtents(const Vec3& center, const Vec3& extents) {
        return {center - extents, center + extents};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — View frustum with 6 planes (near, far, left, right, top, bottom)
// ─────────────────────────────────────────────────────────────────────────────

/// View frustum with 6 clipping planes for frustum culling.
/// Planes are indexed: 0=near, 1=far, 2=left, 3=right, 4=top, 5=bottom.
/// Each plane has inward-facing normal (negative distance means inside frustum).
struct Frustum {
    Plane planes[6]; // 0=near, 1=far, 2=left, 3=right, 4=top, 5=bottom

    /// Default constructor — creates empty frustum.
    Frustum() = default;
    /// Construct frustum from 6 explicit clipping planes.
    Frustum(const Plane& near_p, const Plane& far_p, const Plane& left_p, const Plane& right_p,
            const Plane& top_p, const Plane& bottom_p) {
        planes[0] = near_p;
        planes[1] = far_p;
        planes[2] = left_p;
        planes[3] = right_p;
        planes[4] = top_p;
        planes[5] = bottom_p;
    }

    /// Test if point is strictly inside frustum (all plane distances <= 0).
    bool contains(const Vec3& p) const {
        for (int i = 0; i < 6; i++) {
            if (planes[i].distance(p) > 0)
                return false;
        }
        return true;
    }
    /// Uppercase alias for consistency.
    bool Contains(const Vec3& p) const { return contains(p); }
    /// Alias for clarity.
    bool TestPoint(const Vec3& p) const { return contains(p); }

    /// Test if AABB is potentially visible (conservative: returns true if uncertain).
    /// Returns false only if AABB is completely outside at least one plane.
    bool intersects(const AABB& box) const {
        for (int i = 0; i < 6; i++) {
            // Find vertex of box closest to plane (in direction opposite to normal)
            Vec3 closest = box.max;
            if (planes[i].a > 0)
                closest.x = box.min.x;
            if (planes[i].b > 0)
                closest.y = box.min.y;
            if (planes[i].c > 0)
                closest.z = box.min.z;

            // If closest point is outside plane, box is completely culled
            if (planes[i].distance(closest) > 0)
                return false;
        }
        return true;
    }
    /// Uppercase alias for consistency.
    bool Intersects(const AABB& box) const { return intersects(box); }
    /// Alias for clarity.
    bool TestAABB(const AABB& box) const { return intersects(box); }

    /// Test if sphere is potentially visible (center + radius vs all planes).
    bool intersectsSphere(const Vec3& center, float radius) const {
        for (int i = 0; i < 6; i++) {
            if (planes[i].distance(center) > radius)
                return false;
        }
        return true;
    }
    /// Uppercase alias for consistency.
    bool IntersectsSphere(const Vec3& center, float radius) const {
        return intersectsSphere(center, radius);
    }
    /// Alias for clarity.
    bool TestSphere(const Vec3& center, float radius) const {
        return intersectsSphere(center, radius);
    }

    /// Extract view frustum from combined view-projection matrix (proj * view).
    /// Extracts the 6 clipping planes from the viewproj matrix rows.
    static Frustum FromViewProj(const Mat4& viewproj) {
        XMMATRIX m = viewproj.toXM();
        XMVECTOR planes_xm[6];

        // Extract 6 planes from matrix rows (without normalization to preserve magnitudes)
        // Left: (r[3] + r[0])
        // Right: (r[3] - r[0])
        // Bottom: (r[3] + r[1])
        // Top: (r[3] - r[1])
        // Near: r[2]
        // Far: (r[3] - r[2])

        planes_xm[2] = XMVectorAdd(m.r[3], m.r[0]);      // left
        planes_xm[3] = XMVectorSubtract(m.r[3], m.r[0]); // right
        planes_xm[5] = XMVectorAdd(m.r[3], m.r[1]);      // bottom
        planes_xm[4] = XMVectorSubtract(m.r[3], m.r[1]); // top
        planes_xm[0] = XMVectorNegate(m.r[2]);           // near (negated for inward normal)
        planes_xm[1] = XMVectorSubtract(m.r[3], m.r[2]); // far

        Frustum f;
        for (int i = 0; i < 6; i++) {
            f.planes[i] = Plane(planes_xm[i]);
        }
        return f;
    }
};

} // namespace uz
