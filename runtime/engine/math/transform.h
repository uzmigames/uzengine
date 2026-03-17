/// UzEngine Transform — Decomposed TRS (Position + Rotation + Scale).
///
/// Stores position, rotation (Quat), and scale separately for efficient manipulation,
/// interpolation, and scene-graph hierarchy composition. Converts to/from Mat4 on demand.

#pragma once

#include "quaternion.h"
#include "mat4.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Transform — Position + Rotation + Scale
// ─────────────────────────────────────────────────────────────────────────────

/// Decomposed 3D transform: position + rotation + scale (TRS).
///
/// Stores transform components separately for efficient manipulation and interpolation.
/// Conversion to/from Mat4 available via `toMatrix()` / `FromMatrix()`.
/// Supports hierarchy composition for scene graphs.
///
/// # TRS Order
/// Matrix = Scale * Rotate * Translate (applied right-to-left to vertices).
///
/// # Examples
/// ```cpp
/// Transform t;
/// t.position = {10, 0, 0};
/// t.rotation = Quat::FromAxisAngle(Vec3::Up(), XM_PIDIV2); // 90 deg around Z (world up)
/// t.scale = {2, 2, 2};
/// Mat4 matrix = t.toMatrix();
/// Vec3 worldPos = t.TransformPoint({1, 0, 0});
/// ```
struct Transform {
    /// World position (translation).
    Vec3 position = {0, 0, 0};
    /// Orientation as unit quaternion.
    Quat rotation = {0, 0, 0, 1};
    /// Non-uniform scale per axis.
    Vec3 scale = {1, 1, 1};

    /// Default: identity transform (origin, no rotation, unit scale).
    Transform() = default;
    /// Constructs from explicit position, rotation, and scale.
    Transform(const Vec3& pos, const Quat& rot, const Vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}
    /// Decomposes a matrix into position, rotation, and scale via `XMMatrixDecompose`.
    explicit Transform(const Mat4& m);

    /// Builds a 4x4 matrix from this transform (TRS order).
    Mat4 toMatrix() const;
    /// Uppercase alias for `toMatrix()`.
    Mat4 ToMatrix() const { return toMatrix(); }

    /// Decomposes a matrix into a Transform.
    static Transform FromMatrix(const Mat4& m);

    /// Composes this local transform with a parent to produce world-space transform.
    ///
    /// # Arguments
    /// * `parent` - The parent (world-space) transform.
    ///
    /// # Returns
    /// World-space transform: `world = local * parent`.
    Transform compose(const Transform& parent) const;
    /// Uppercase alias for `compose()`.
    Transform Compose(const Transform& parent) const { return compose(parent); }

    /// Returns the inverse transform (undo this transform).
    Transform inverse() const;
    /// Uppercase alias for `inverse()`.
    Transform Inverse() const { return inverse(); }

    /// Transforms a 3D point from local space to world space.
    ///
    /// Applies scale, then rotation, then translation: `pos + rot * (scale * p)`.
    ///
    /// # Arguments
    /// * `p` — Point in local space.
    ///
    /// # Returns
    /// Point in world space after the full TRS transform.
    Vec3 TransformPoint(const Vec3& p) const { return position + rotation * (scale * p); }
    /// Transforms a 3D direction vector from local space to world space.
    ///
    /// Applies scale and rotation only (no translation), since directions
    /// are displacement vectors without a fixed origin.
    ///
    /// # Arguments
    /// * `d` — Direction in local space.
    ///
    /// # Returns
    /// Direction in world space: `rot * (scale * d)`.
    Vec3 TransformDirection(const Vec3& d) const { return rotation * (scale * d); }

    /// Returns a copy with position offset by `delta`.
    Transform translate(const Vec3& delta) const;
    /// Uppercase alias for `translate()`.
    Transform Translate(const Vec3& delta) const { return translate(delta); }
    /// Returns a copy with rotation composed by `rot`.
    Transform rotate(const Quat& rot) const;
    /// Uppercase alias for `rotate()`.
    Transform Rotate(const Quat& rot) const { return rotate(rot); }
    /// Returns a copy with scale multiplied component-wise by `s`.
    Transform scaleBy(const Vec3& s) const;
    /// Uppercase alias for `scaleBy()`.
    Transform ScaleBy(const Vec3& s) const { return scaleBy(s); }

    /// Interpolates between two transforms (position lerp, rotation slerp, scale lerp).
    ///
    /// # Arguments
    /// * `a` - Start transform.
    /// * `b` - End transform.
    /// * `t` - Interpolation factor in [0, 1].
    static Transform Lerp(const Transform& a, const Transform& b, float t) {
        Transform result;
        result.position = a.position + (b.position - a.position) * t;
        result.rotation = a.rotation.slerp(b.rotation, t);
        result.scale = a.scale + (b.scale - a.scale) * t;
        return result;
    }
};

} // namespace uz
