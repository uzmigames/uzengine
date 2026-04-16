/// UzEngine Pose — Per-frame array of local bone transforms for a skeleton.
///
/// A Pose is a snapshot of the skeleton's bone transforms at a given moment.
/// It stores local-space transforms (one per bone) plus utility operations for
/// blending and copying. A Pose is indexed by bone index (matching the skeleton's
/// topological order), not by bone name.
///
/// # Relationship to Skeleton
/// - Skeleton defines the immutable bind pose and hierarchy.
/// - Pose stores the current animated transforms at runtime.
/// - A Pose is valid only for the skeleton whose bind pose it was derived from.
///
/// # Lifecycle
/// ```cpp
/// Pose pose = Pose::FromBindPose(skeleton);   // Identity relative to bind
/// animClip.SamplePose(t, skeleton, pose);     // Fill from animation
/// Pose blended = Pose::Blend(poseA, poseB, 0.3f);
/// ```
///
/// # Thread Safety
/// A Pose has value semantics — each AnimationPlayer owns its own Pose. Safe to
/// copy across threads, but concurrent mutation of the same Pose is not allowed.

#pragma once

#include "bone_transform.h"

#include <cstdint>
#include <vector>

namespace uz {

class Skeleton;

// ─────────────────────────────────────────────────────────────────────────────
// Pose — Local bone transforms for a skeleton
// ─────────────────────────────────────────────────────────────────────────────

/// Array of local BoneTransforms indexed by bone index.
///
/// `localTransforms[i]` is the local-space transform of bone `i` relative to
/// its parent. To compute the world-space transform of a bone, compose its
/// local transform with the world transform of its parent recursively.
///
/// The size of `localTransforms` must equal the bone count of the skeleton
/// this pose was built for. Mismatched sizes will produce incorrect skinning.
struct Pose {
    /// Local bone transforms, indexed by bone index (topological order).
    std::vector<BoneTransform> localTransforms;

    // ─── Construction ────────────────────────────────────────────────────

    /// Default: empty pose. Use `FromBindPose()` or `Resize()` before sampling.
    Pose() = default;

    /// Constructs a pose of the given size with all transforms at identity.
    explicit Pose(uint32_t boneCount) : localTransforms(boneCount, BoneTransform::Identity()) {}

    /// Builds a pose from the bind pose of a skeleton.
    ///
    /// Each bone's local transform is set to its `BoneInfo::bindTranslation/Rotation/Scale`.
    /// This is the starting point for any animated pose — missing channels in an
    /// animation clip fall back to the bind pose.
    ///
    /// # Arguments
    /// * `skeleton` — Skeleton to sample bind pose from.
    ///
    /// # Returns
    /// A Pose with `localTransforms.size() == skeleton.GetBoneCount()`.
    [[nodiscard]] static Pose FromBindPose(const Skeleton& skeleton);

    // ─── Access ──────────────────────────────────────────────────────────

    /// Returns the number of bones in this pose.
    [[nodiscard]] uint32_t GetBoneCount() const {
        return static_cast<uint32_t>(localTransforms.size());
    }

    /// Returns true if the pose has no bones.
    [[nodiscard]] bool IsEmpty() const {
        return localTransforms.empty();
    }

    /// Resizes the pose to hold `boneCount` transforms, initialized to identity.
    void Resize(uint32_t boneCount) {
        localTransforms.assign(boneCount, BoneTransform::Identity());
    }

    /// Returns the local transform of bone `boneIdx`.
    [[nodiscard]] const BoneTransform& GetLocal(uint32_t boneIdx) const {
        return localTransforms[boneIdx];
    }

    /// Returns a mutable reference to the local transform of bone `boneIdx`.
    [[nodiscard]] BoneTransform& GetLocalMut(uint32_t boneIdx) {
        return localTransforms[boneIdx];
    }

    // ─── Blending ────────────────────────────────────────────────────────

    /// Linearly blends two poses bone-by-bone.
    ///
    /// For each bone: `result[i] = Interpolate(a[i], b[i], alpha)`. Position
    /// and scale use lerp, rotation uses slerp. Poses must have the same bone
    /// count; if they differ, the result uses the smaller size.
    ///
    /// # Arguments
    /// * `a` — Start pose (returned when alpha=0).
    /// * `b` — End pose (returned when alpha=1).
    /// * `alpha` — Blend factor, expected in [0, 1] but not clamped.
    ///
    /// # Returns
    /// Blended pose with `localTransforms.size() == min(a.size, b.size)`.
    [[nodiscard]] static Pose Blend(const Pose& a, const Pose& b, float alpha);

    /// Blends pose `b` into this pose in-place by `alpha`.
    ///
    /// More efficient than `Blend()` when the destination is already allocated.
    /// Requires `other.GetBoneCount() == GetBoneCount()`.
    void BlendInPlace(const Pose& other, float alpha);

    /// Replaces this pose with the identity bind pose of the skeleton.
    ///
    /// Equivalent to `*this = FromBindPose(skeleton)` but avoids reallocation
    /// when the pose already has the right size.
    void ResetToBindPose(const Skeleton& skeleton);
};

} // namespace uz
