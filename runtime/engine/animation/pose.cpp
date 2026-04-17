/// UzEngine Pose — Implementation.

#include "pose.h"
#include "skeleton.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Pose — Construction
// ─────────────────────────────────────────────────────────────────────────────

Pose Pose::FromBindPose(const Skeleton& skeleton) {
    Pose pose;
    const uint32_t count = skeleton.GetBoneCount();
    pose.localTransforms.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        pose.localTransforms.push_back(skeleton.GetBone(i).GetBindTransform());
    }
    return pose;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pose — Blending
// ─────────────────────────────────────────────────────────────────────────────

Pose Pose::Blend(const Pose& a, const Pose& b, float alpha) {
    const uint32_t count = std::min(a.GetBoneCount(), b.GetBoneCount());
    Pose result(count);
    for (uint32_t i = 0; i < count; ++i) {
        result.localTransforms[i] = BoneTransform::Interpolate(
            a.localTransforms[i], b.localTransforms[i], alpha);
    }
    return result;
}

void Pose::BlendInPlace(const Pose& other, float alpha) {
    const uint32_t count = std::min(GetBoneCount(), other.GetBoneCount());
    for (uint32_t i = 0; i < count; ++i) {
        localTransforms[i] = BoneTransform::Interpolate(
            localTransforms[i], other.localTransforms[i], alpha);
    }
}

void Pose::ResetToBindPose(const Skeleton& skeleton) {
    const uint32_t count = skeleton.GetBoneCount();
    if (GetBoneCount() != count) {
        localTransforms.resize(count);
    }
    for (uint32_t i = 0; i < count; ++i) {
        localTransforms[i] = skeleton.GetBone(i).GetBindTransform();
    }
}

} // namespace uz
