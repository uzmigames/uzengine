#pragma once

/// UzEngine Bounds — AABB, Plane, Frustum with intersection tests.

#include "uz_math_types.h"
#include "uz_mat4.h"

namespace uz {

// ── Plane ────────────────────────────────────────────────────────────────────

struct Plane {
    Vec4 coefficients; // (nx, ny, nz, d) where nx*x + ny*y + nz*z + d = 0

    Plane() : coefficients(Vec4::Zero()) {}
    Plane(float a, float b, float c, float d) : coefficients(a, b, c, d) {}
    Plane(Vec3 normal, float distance) : coefficients(normal.X(), normal.Y(), normal.Z(), distance) {}

    Vec3 Normal() const { return Vec3(coefficients.X(), coefficients.Y(), coefficients.Z()); }
    float Distance() const { return coefficients.W(); }

    Plane Normalized() const {
        XMVECTOR p = XMPlaneNormalize(coefficients.v);
        return Plane(XMVectorGetX(p), XMVectorGetY(p), XMVectorGetZ(p), XMVectorGetW(p));
    }

    float DistanceToPoint(Vec3 point) const {
        return XMVectorGetX(XMPlaneDotCoord(coefficients.v, point.v));
    }
};

// ── AABB (Axis-Aligned Bounding Box) ─────────────────────────────────────────

struct AABB {
    Vec3 min;
    Vec3 max;

    AABB() : min(Vec3(FLT_MAX)), max(Vec3(-FLT_MAX)) {}
    AABB(Vec3 min, Vec3 max) : min(min), max(max) {}

    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    Vec3 Size() const { return max - min; }

    bool IsValid() const {
        return min.X() <= max.X() && min.Y() <= max.Y() && min.Z() <= max.Z();
    }

    bool Contains(Vec3 point) const {
        return point.X() >= min.X() && point.X() <= max.X()
            && point.Y() >= min.Y() && point.Y() <= max.Y()
            && point.Z() >= min.Z() && point.Z() <= max.Z();
    }

    bool Intersects(AABB other) const {
        if (max.X() < other.min.X() || min.X() > other.max.X()) return false;
        if (max.Y() < other.min.Y() || min.Y() > other.max.Y()) return false;
        if (max.Z() < other.min.Z() || min.Z() > other.max.Z()) return false;
        return true;
    }

    AABB Merge(AABB other) const {
        return AABB(Vec3::Min(min, other.min), Vec3::Max(max, other.max));
    }

    void Expand(Vec3 point) {
        min = Vec3::Min(min, point);
        max = Vec3::Max(max, point);
    }

    static AABB FromCenterExtents(Vec3 center, Vec3 extents) {
        return AABB(center - extents, center + extents);
    }
};

// ── Frustum ──────────────────────────────────────────────────────────────────

enum class FrustumPlane : int {
    Left = 0, Right, Bottom, Top, Near, Far,
    Count
};

struct Frustum {
    Plane planes[6];

    static Frustum FromViewProj(const Mat4& viewProj) {
        Frustum f;
        // Extract planes from view-projection matrix (row-based extraction)
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, viewProj.m);

        // Left:   row3 + row0
        f.planes[0] = Plane(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41).Normalized();
        // Right:  row3 - row0
        f.planes[1] = Plane(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41).Normalized();
        // Bottom: row3 + row1
        f.planes[2] = Plane(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42).Normalized();
        // Top:    row3 - row1
        f.planes[3] = Plane(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42).Normalized();
        // Near:   row2
        f.planes[4] = Plane(m._13, m._23, m._33, m._43).Normalized();
        // Far:    row3 - row2
        f.planes[5] = Plane(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43).Normalized();

        return f;
    }

    bool TestAABB(AABB box) const {
        Vec3 center = box.Center();
        Vec3 extents = box.Extents();

        for (int i = 0; i < 6; ++i) {
            Vec3 normal = planes[i].Normal();
            // Compute the projection interval radius of box onto plane normal
            float r = extents.X() * std::abs(normal.X())
                    + extents.Y() * std::abs(normal.Y())
                    + extents.Z() * std::abs(normal.Z());
            float d = planes[i].DistanceToPoint(center);
            if (d + r < 0.0f) return false; // Entirely outside
        }
        return true; // Potentially visible
    }

    bool TestSphere(Vec3 center, float radius) const {
        for (int i = 0; i < 6; ++i) {
            if (planes[i].DistanceToPoint(center) + radius < 0.0f) {
                return false;
            }
        }
        return true;
    }

    bool TestPoint(Vec3 point) const {
        for (int i = 0; i < 6; ++i) {
            if (planes[i].DistanceToPoint(point) < 0.0f) {
                return false;
            }
        }
        return true;
    }
};

} // namespace uz
