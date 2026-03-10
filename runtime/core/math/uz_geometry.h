/// UzEngine Geometry Library
///
/// AABB (Axis-Aligned Bounding Box) and Frustum for frustum culling.
/// All types work with DirectXMath vectors.

#pragma once

#include "uz_math.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Plane — defines half-space (ax + by + cz + d = 0)
// ─────────────────────────────────────────────────────────────────────────────

struct Plane {
    float a, b, c, d;

    /// Constructors
    Plane() = default;
    Plane(float a, float b, float c, float d) : a(a), b(b), c(c), d(d) {}
    explicit Plane(const XMVECTOR& xm) {
        XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm);
    }

    /// Conversion to XMVECTOR
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Signed distance from point to plane (positive = front)
    float distance(const Vec3& p) const { return a * p.x + b * p.y + c * p.z + d; }
    float Distance(const Vec3& p) const { return distance(p); }
    float DistanceToPoint(const Vec3& p) const { return distance(p); }
};

// ─────────────────────────────────────────────────────────────────────────────
// AABB — Axis-Aligned Bounding Box
// ─────────────────────────────────────────────────────────────────────────────

struct AABB {
    Vec3 min, max;

    /// Constructors
    AABB() : min(FLT_MAX, FLT_MAX, FLT_MAX), max(-FLT_MAX, -FLT_MAX, -FLT_MAX) {}
    AABB(const Vec3& mn, const Vec3& mx) : min(mn), max(mx) {}

    /// Test point containment
    bool contains(const Vec3& p) const {
        return p.x >= min.x && p.x <= max.x &&
               p.y >= min.y && p.y <= max.y &&
               p.z >= min.z && p.z <= max.z;
    }
    bool Contains(const Vec3& p) const { return contains(p); }

    /// Test AABB intersection
    bool intersects(const AABB& other) const {
        return !(max.x < other.min.x || min.x > other.max.x ||
                 max.y < other.min.y || min.y > other.max.y ||
                 max.z < other.min.z || min.z > other.max.z);
    }
    bool Intersects(const AABB& other) const { return intersects(other); }

    /// Test sphere intersection (center + radius)
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
    bool IntersectsSphere(const Vec3& center, float radius) const { return intersectsSphere(center, radius); }

    /// Merge with point
    AABB expand(const Vec3& p) const {
        AABB result = *this;
        result.min = result.min.min(p);
        result.max = result.max.max(p);
        return result;
    }
    void Expand(const Vec3& p) {
        min = min.min(p);
        max = max.max(p);
    }

    /// Merge with another AABB
    AABB merge(const AABB& other) const {
        AABB result = *this;
        result.min = result.min.min(other.min);
        result.max = result.max.max(other.max);
        return result;
    }
    AABB Merge(const AABB& other) const { return merge(other); }

    /// Center and extents
    Vec3 center() const { return (min + max) * 0.5f; }
    Vec3 Center() const { return center(); }
    Vec3 extents() const { return (max - min) * 0.5f; }
    Vec3 Extents() const { return extents(); }
    Vec3 size() const { return max - min; }
    Vec3 Size() const { return size(); }

    /// Volume (for sorting/priority)
    float volume() const {
        Vec3 s = size();
        return s.x * s.y * s.z;
    }
    float Volume() const { return volume(); }

    /// Check if valid (min < max)
    bool isValid() const {
        return min.x <= max.x && min.y <= max.y && min.z <= max.z;
    }
    bool IsValid() const { return isValid(); }

    /// Static constructors
    static AABB FromCenterExtents(const Vec3& center, const Vec3& extents) {
        return {center - extents, center + extents};
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Frustum — View frustum with 6 planes (near, far, left, right, top, bottom)
// ─────────────────────────────────────────────────────────────────────────────

struct Frustum {
    Plane planes[6];  // 0=near, 1=far, 2=left, 3=right, 4=top, 5=bottom

    /// Constructors
    Frustum() = default;
    Frustum(const Plane& near_p, const Plane& far_p, const Plane& left_p,
            const Plane& right_p, const Plane& top_p, const Plane& bottom_p) {
        planes[0] = near_p;
        planes[1] = far_p;
        planes[2] = left_p;
        planes[3] = right_p;
        planes[4] = top_p;
        planes[5] = bottom_p;
    }

    /// Test point containment
    bool contains(const Vec3& p) const {
        for (int i = 0; i < 6; i++) {
            if (planes[i].distance(p) > 0) return false;
        }
        return true;
    }
    bool Contains(const Vec3& p) const { return contains(p); }
    bool TestPoint(const Vec3& p) const { return contains(p); }

    /// Test AABB intersection (conservative: returns true if possibly visible)
    bool intersects(const AABB& box) const {
        for (int i = 0; i < 6; i++) {
            // Find vertex of box closest to plane (in direction opposite to normal)
            Vec3 closest = box.max;
            if (planes[i].a > 0) closest.x = box.min.x;
            if (planes[i].b > 0) closest.y = box.min.y;
            if (planes[i].c > 0) closest.z = box.min.z;

            // If closest point is behind plane, box is completely culled
            if (planes[i].distance(closest) > 0) return false;
        }
        return true;
    }
    bool Intersects(const AABB& box) const { return intersects(box); }
    bool TestAABB(const AABB& box) const { return intersects(box); }

    /// Test sphere intersection
    bool intersectsSphere(const Vec3& center, float radius) const {
        for (int i = 0; i < 6; i++) {
            if (planes[i].distance(center) > radius) return false;
        }
        return true;
    }
    bool IntersectsSphere(const Vec3& center, float radius) const { return intersectsSphere(center, radius); }
    bool TestSphere(const Vec3& center, float radius) const { return intersectsSphere(center, radius); }

    /// Extract frustum from projection matrix (ViewProj)
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

}  // namespace uz
