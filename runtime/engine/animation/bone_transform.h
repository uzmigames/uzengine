/// UzEngine BoneTransform — Per-bone local transform for skeletal animation.
///
/// Thin wrapper around uz::Transform providing animation-specific naming.
/// Stores position, rotation (Quat), and scale in bone-local space.
/// Used for bind poses, animation keyframes, and runtime pose evaluation.

#pragma once

#include "transform.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// BoneTransform — Bone-local TRS transform
// ─────────────────────────────────────────────────────────────────────────────

/// Per-bone local transform for skeletal animation.
///
/// Wraps the same data as `Transform` (position + rotation + scale) but lives in
/// the animation module to provide domain-specific factory methods and documentation.
/// The bind pose of each bone is stored as a BoneTransform. Animation keyframes
/// also produce BoneTransform values that override or blend with the bind pose.
///
/// # Memory Layout
/// `[position: Vec3(12B), rotation: Quat(16B), scale: Vec3(12B)]` — 40 bytes.
///
/// # Examples
/// ```cpp
/// BoneTransform bind = BoneTransform::Identity();
/// BoneTransform key0{Vec3{10, 0, 0}, Quat::FromAxisAngle(Vec3::Up(), 0.5f), Vec3::One()};
/// BoneTransform blended = BoneTransform::Interpolate(bind, key0, 0.25f);
/// Mat4 local = blended.ToMatrix();
/// ```
struct BoneTransform {
    /// Bone-local translation (offset from parent).
    Vec3 position{0.0f, 0.0f, 0.0f};
    /// Bone-local rotation as unit quaternion (identity = no rotation).
    Quat rotation{0.0f, 0.0f, 0.0f, 1.0f};
    /// Bone-local scale (uniform 1,1,1 = no scaling).
    Vec3 scale{1.0f, 1.0f, 1.0f};

    /// Default: identity transform (no translation, no rotation, unit scale).
    BoneTransform() = default;

    /// Constructs from explicit position, rotation, and scale.
    ///
    /// # Arguments
    /// * `pos` — Bone-local translation.
    /// * `rot` — Bone-local rotation quaternion (should be normalized).
    /// * `scl` — Bone-local scale.
    constexpr BoneTransform(const Vec3& pos, const Quat& rot, const Vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}

    /// Constructs from an existing Transform.
    ///
    /// # Arguments
    /// * `t` — Source transform to copy.
    explicit BoneTransform(const Transform& t)
        : position(t.position), rotation(t.rotation), scale(t.scale) {}

    /// Returns the identity bone transform (no translation, no rotation, unit scale).
    [[nodiscard]] static BoneTransform Identity() {
        return {};
    }

    /// Converts this bone-local transform to a 4x4 matrix (TRS order).
    ///
    /// Matrix = Translation(position) * Rotation(rotation) * Scale(scale).
    /// Delegates to Mat4::TRS for SIMD-accelerated construction.
    ///
    /// # Returns
    /// 4x4 transformation matrix representing this bone's local transform.
    [[nodiscard]] Mat4 ToMatrix() const {
        return Mat4::TRS(position, rotation, scale);
    }

    /// Converts to the engine's general-purpose Transform type.
    ///
    /// # Returns
    /// Transform with identical position, rotation, and scale.
    [[nodiscard]] Transform ToTransform() const {
        return Transform{position, rotation, scale};
    }

    /// Interpolates between two bone transforms.
    ///
    /// Position and scale are linearly interpolated. Rotation uses spherical linear
    /// interpolation (slerp) for constant angular velocity. Suitable for blending
    /// between animation keyframes or bind pose and animated pose.
    ///
    /// # Arguments
    /// * `a` — Start transform (returned when t=0).
    /// * `b` — End transform (returned when t=1).
    /// * `t` — Interpolation factor, clamped internally by slerp to [0, 1] semantics.
    ///
    /// # Returns
    /// Blended bone transform.
    [[nodiscard]] static BoneTransform Interpolate(const BoneTransform& a, const BoneTransform& b, float t) {
        BoneTransform result;
        result.position = Vec3::Lerp(a.position, b.position, t);
        result.rotation = a.rotation.slerp(b.rotation, t);
        result.scale = Vec3::Lerp(a.scale, b.scale, t);
        return result;
    }

    /// Composes this local transform with a parent transform.
    ///
    /// Produces the world-space (or parent-relative) transform by applying
    /// this bone's local TRS on top of the parent's world transform.
    /// Used when walking the bone hierarchy to compute world-space bone matrices.
    ///
    /// # Arguments
    /// * `parent` — Parent bone's world-space transform.
    ///
    /// # Returns
    /// World-space bone transform: position = parent * local.
    [[nodiscard]] BoneTransform ComposeWith(const BoneTransform& parent) const {
        BoneTransform result;
        result.scale = parent.scale * scale;
        result.rotation = (parent.rotation * rotation).normalize();
        result.position = parent.position + parent.rotation * (parent.scale * position);
        return result;
    }

    /// Exact equality comparison (no epsilon tolerance).
    bool operator==(const BoneTransform& other) const {
        return position == other.position && rotation == other.rotation && scale == other.scale;
    }

    /// Exact inequality comparison.
    bool operator!=(const BoneTransform& other) const {
        return !(*this == other);
    }
};

} // namespace uz
