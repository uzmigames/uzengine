#pragma once

// Transform — position/rotation/scale decomposed transform.
//
// A Transform represents a TRS (Translate-Rotate-Scale) decomposition that is
// more ergonomic for scene editing than raw 4x4 matrices. The engine stores
// both a local transform (relative to parent) and a cached world transform
// (computed by scene_update_world_transforms).
//
// Conventions:
//   - Position is in world units (meters).
//   - Rotation is a unit quaternion (x, y, z, w).
//   - Scale is non-uniform (per-axis).
//   - The combined TRS matrix is: T * R * S (scale first, then rotate, then translate).

#include "engine/core/math.h"

namespace uz {

// ============================================================================
// Transform — TRS decomposition
// ============================================================================

struct Transform {
    Vec3 position;  // Translation
    Quat rotation;  // Unit quaternion rotation
    Vec3 scale;     // Per-axis scale

    /// Returns the identity transform (origin, no rotation, unit scale).
    [[nodiscard]] static constexpr Transform identity() noexcept {
        return {
            {0.0f, 0.0f, 0.0f, 0.0f},   // position
            {0.0f, 0.0f, 0.0f, 1.0f},    // rotation (identity quat)
            {1.0f, 1.0f, 1.0f, 0.0f}     // scale
        };
    }
};

// ============================================================================
// Transform functions
// ============================================================================

/// Convert a Transform to a 4x4 matrix: M = T * R * S.
/// Scale is applied first, then rotation, then translation.
[[nodiscard]] inline Mat4 transform_to_mat4(const Transform& t) noexcept {
    // Build the combined TRS matrix.
    // M = T * R * S
    const Mat4 mt = Mat4::translate(t.position);
    const Mat4 mr = Mat4::rotate(t.rotation);
    const Mat4 ms = Mat4::scale(t.scale);
    return mt * mr * ms;
}

/// Combine two transforms: parent * child.
/// The result represents applying child first, then parent.
/// This is equivalent to matrix multiplication of their TRS matrices,
/// but decomposed back into TRS form.
[[nodiscard]] inline Transform transform_combine(const Transform& parent, const Transform& child) noexcept {
    Transform result;

    // Scale: component-wise multiply
    result.scale = {
        parent.scale.x * child.scale.x,
        parent.scale.y * child.scale.y,
        parent.scale.z * child.scale.z,
        0.0f
    };

    // Rotation: quaternion multiply (parent * child)
    result.rotation = quat_mul(parent.rotation, child.rotation);

    // Position: parent.position + parent.rotation * (parent.scale * child.position)
    Vec3 scaled_child_pos = {
        parent.scale.x * child.position.x,
        parent.scale.y * child.position.y,
        parent.scale.z * child.position.z,
        0.0f
    };
    Vec3 rotated = quat_rotate_vec3(parent.rotation, scaled_child_pos);
    result.position = parent.position + rotated;

    return result;
}

/// Compute the inverse of a transform.
/// Assumes rotation is a unit quaternion and scale components are non-zero.
[[nodiscard]] inline Transform transform_inverse(const Transform& t) noexcept {
    Transform result;

    // Inverse scale: 1/s
    result.scale = {
        1.0f / t.scale.x,
        1.0f / t.scale.y,
        1.0f / t.scale.z,
        0.0f
    };

    // Inverse rotation: conjugate of unit quaternion
    result.rotation = {-t.rotation.x, -t.rotation.y, -t.rotation.z, t.rotation.w};

    // Inverse position: -inv_rot * (inv_scale * pos)
    Vec3 scaled_pos = {
        result.scale.x * t.position.x,
        result.scale.y * t.position.y,
        result.scale.z * t.position.z,
        0.0f
    };
    Vec3 rotated = quat_rotate_vec3(result.rotation, scaled_pos);
    result.position = -rotated;

    return result;
}

/// Check if two transforms are approximately equal within tolerance.
[[nodiscard]] inline bool transform_approx_equal(const Transform& a, const Transform& b, float tol = kEpsilon) noexcept {
    auto approx = [tol](float x, float y) { return std::abs(x - y) <= tol; };

    return approx(a.position.x, b.position.x)
        && approx(a.position.y, b.position.y)
        && approx(a.position.z, b.position.z)
        && approx(a.rotation.x, b.rotation.x)
        && approx(a.rotation.y, b.rotation.y)
        && approx(a.rotation.z, b.rotation.z)
        && approx(a.rotation.w, b.rotation.w)
        && approx(a.scale.x, b.scale.x)
        && approx(a.scale.y, b.scale.y)
        && approx(a.scale.z, b.scale.z);
}

} // namespace uz
