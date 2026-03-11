/// UzEngine Geometry Library — Plane, AABB, and Frustum primitives.
///
/// Provides geometric primitives for spatial queries: frustum culling, collision detection,
/// and bounding volume tests. All types are value types with zero dynamic allocation.
///
/// # Types
/// - `Plane` — Half-space defined by `ax + by + cz + d = 0`
/// - `AABB` — Axis-Aligned Bounding Box (min/max corners)
/// - `Frustum` — 6-plane view frustum extracted from view-projection matrix
///
/// # Coordinate System
/// Left-handed (DirectX convention). Plane normals point toward the positive half-space.
/// Frustum planes have inward-facing normals (negative distance = inside frustum).
///
/// # Thread Safety
/// All types are trivially copyable and have no mutable shared state. Thread-safe by value.
///
/// # Examples
/// ```cpp
/// // Frustum culling
/// auto frustum = Frustum::FromViewProj(viewProj);
/// for (auto& obj : objects) {
///     if (frustum.intersects(obj.bounds)) {
///         drawList.push_back(&obj);
///     }
/// }
/// ```

#pragma once

#include <algorithm>

#include "engine_math.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Plane — defines half-space (ax + by + cz + d = 0)
// ─────────────────────────────────────────────────────────────────────────────

/// Half-space defined by the plane equation `ax + by + cz + d = 0`.
///
/// Normal vector is `(a, b, c)`. The signed distance from a point P to the plane
/// is `a*Px + b*Py + c*Pz + d`. Positive distance = point is on the normal side (front),
/// negative = behind the plane.
///
/// # Memory Layout
/// `[a: f32, b: f32, c: f32, d: f32]` — 16 bytes, same as XMFLOAT4.
///
/// # Examples
/// ```cpp
/// // Ground plane (y = 0, normal pointing up)
/// Plane ground{0.f, 1.f, 0.f, 0.f};
/// float dist = ground.distance(Vec3{0, 5, 0}); // +5.0 (above)
/// float dist2 = ground.distance(Vec3{0, -3, 0}); // -3.0 (below)
/// ```
struct Plane {
    /// Plane normal X component.
    float a;
    /// Plane normal Y component.
    float b;
    /// Plane normal Z component.
    float c;
    /// Signed distance from origin along normal.
    float d;

    /// Constructors
    Plane() = default;
    /// Create plane from coefficients (a, b, c, d) where normal = (a, b, c).
    Plane(float a, float b, float c, float d) : a(a), b(b), c(c), d(d) {}
    /// Create plane from XMVECTOR (packed as XMFLOAT4 coefficients).
    explicit Plane(const XMVECTOR& xm) { XMStoreFloat4(reinterpret_cast<XMFLOAT4*>(this), xm); }

    /// Conversion to XMVECTOR for SIMD operations.
    XMVECTOR toXM() const { return XMLoadFloat4(reinterpret_cast<const XMFLOAT4*>(this)); }

    /// Signed distance from point to plane.
    ///
    /// # Arguments
    /// * `p` - Point to test
    ///
    /// # Returns
    /// Positive if `p` is on the normal side (front), negative if behind, zero if exactly on plane.
    /// NOT normalized — if normal is not unit-length, result is scaled by `|normal|`.
    float distance(const Vec3& p) const { return a * p.x + b * p.y + c * p.z + d; }
    /// Alias for `distance()`.
    float Distance(const Vec3& p) const { return distance(p); }
    /// Alias for `distance()`.
    float DistanceToPoint(const Vec3& p) const { return distance(p); }
};

// ─────────────────────────────────────────────────────────────────────────────
// AABB — Axis-Aligned Bounding Box
// ─────────────────────────────────────────────────────────────────────────────

/// Axis-Aligned Bounding Box for collision testing and frustum culling.
///
/// Stores min/max corners. Default-constructed as **invalid** (`min=+INF, max=-INF`)
/// so that calling `Expand()` on a fresh AABB correctly builds the bounding box incrementally.
///
/// # Memory Layout
/// `[min: Vec3, max: Vec3]` — 24 bytes, no padding.
///
/// # Examples
/// ```cpp
/// AABB box;  // invalid — ready for expansion
/// for (auto& v : vertices) box.Expand(v.position);
/// assert(box.IsValid());
///
/// if (frustum.intersects(box)) { /* visible */ }
/// ```
struct AABB {
    /// Minimum corner (most negative X, Y, Z).
    Vec3 min;
    /// Maximum corner (most positive X, Y, Z).
    Vec3 max;

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
    ///
    /// # Arguments
    /// * `center` - Sphere center position
    /// * `radius` - Sphere radius (must be >= 0)
    ///
    /// # Returns
    /// `true` if any part of the sphere intersects the AABB.
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
///
/// Planes are indexed: `[0]=near, [1]=far, [2]=left, [3]=right, [4]=top, [5]=bottom`.
/// Each plane has **inward-facing normal** — negative signed distance means the point
/// is inside the frustum. This is the standard convention for Gribb-Hartmann extraction.
///
/// # Construction
/// Use `Frustum::FromViewProj(viewProjMatrix)` to extract planes from a combined
/// view-projection matrix. The extraction works for both perspective and orthographic projections.
///
/// # Culling Strategy
/// `intersects(AABB)` is **conservative**: it returns `true` for any AABB that *might* be visible.
/// False positives are possible near frustum corners, but false negatives never occur.
///
/// # Examples
/// ```cpp
/// Mat4 viewProj = proj * view;
/// Frustum frustum = Frustum::FromViewProj(viewProj);
///
/// for (auto& obj : scene) {
///     if (frustum.intersects(obj.worldAABB)) {
///         visibleObjects.push_back(&obj);
///     }
/// }
/// ```
struct Frustum {
    /// The 6 clipping planes: [0]=near, [1]=far, [2]=left, [3]=right, [4]=top, [5]=bottom.
    Plane planes[6];

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

    /// Test if point is inside the frustum (all 6 plane distances <= 0).
    ///
    /// # Arguments
    /// * `p` - World-space point to test
    ///
    /// # Returns
    /// `true` if point is on the negative side (inside) of all 6 planes.
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

    /// Conservative frustum-AABB intersection test.
    ///
    /// Returns `false` **only** if the AABB is completely outside at least one plane.
    /// May return `true` for AABBs near frustum corners that are actually outside
    /// (false positive), but **never** returns `false` for visible AABBs (no false negatives).
    ///
    /// # Arguments
    /// * `box` - World-space AABB to test
    ///
    /// # Returns
    /// `true` if the AABB might be visible, `false` if definitely culled.
    ///
    /// # Algorithm
    /// Tests the P-vertex (vertex most aligned with plane normal) against each plane.
    /// O(6) plane tests with early-out on first definite cull.
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

    /// Conservative frustum-sphere intersection test.
    ///
    /// # Arguments
    /// * `center` - Sphere center in world space
    /// * `radius` - Sphere radius (must be >= 0)
    ///
    /// # Returns
    /// `true` if the sphere might be visible, `false` if definitely culled.
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

    /// Extract view frustum planes from a combined view-projection matrix.
    ///
    /// Uses the Gribb-Hartmann method: extracts 6 planes directly from matrix rows.
    /// Works with both perspective and orthographic projections.
    ///
    /// # Arguments
    /// * `viewproj` - Combined view-projection matrix (`projection * view`)
    ///
    /// # Returns
    /// Frustum with 6 planes. Planes are NOT normalized — `distance()` results are
    /// scaled by the plane normal length. This is fine for culling (sign is preserved).
    ///
    /// # Examples
    /// ```cpp
    /// Mat4 view = Mat4::LookAt(eye, target, Vec3{0,1,0});
    /// Mat4 proj = Mat4::Perspective(fovY, aspect, nearZ, farZ);
    /// Frustum frustum = Frustum::FromViewProj(proj * view);
    /// ```
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
