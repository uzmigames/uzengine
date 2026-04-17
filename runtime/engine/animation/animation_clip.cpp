/// UzEngine AnimationClip — Implementation.

#include "animation_clip.h"
#include "pose.h"
#include "skeleton.h"

#include <algorithm>
#include <cmath>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — binary search for bracketing keyframes
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Finds the index of the first keyframe with `time >= t`.
///
/// Returns `keys.size()` if all keyframes come before `t`.
template <typename Key>
uint32_t UpperBoundKey(const std::vector<Key>& keys, float t) {
    // Linear scan is fine for typical keyframe counts (dozens per channel).
    // Binary search would help only for tracks with hundreds of keys.
    const uint32_t n = static_cast<uint32_t>(keys.size());
    for (uint32_t i = 0; i < n; ++i) {
        if (keys[i].time >= t) return i;
    }
    return n;
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Free functions — keyframe interpolation
// ─────────────────────────────────────────────────────────────────────────────

Vec3 SampleVec3Track(const std::vector<Vec3Key>& keys, float t, const Vec3& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    // Clamp before first key → first value. Clamp after last key → last value.
    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    const uint32_t upper = UpperBoundKey(keys, t);
    // `upper` is guaranteed > 0 because t > first.time.
    const Vec3Key& k1 = keys[upper];
    const Vec3Key& k0 = keys[upper - 1];

    const float span = k1.time - k0.time;
    const float alpha = (span > 0.0f) ? (t - k0.time) / span : 0.0f;
    return Vec3::Lerp(k0.value, k1.value, alpha);
}

Quat SampleQuatTrack(const std::vector<QuatKey>& keys, float t, const Quat& fallback) {
    if (keys.empty()) return fallback;
    if (keys.size() == 1) return keys[0].value;

    if (t <= keys.front().time) return keys.front().value;
    if (t >= keys.back().time) return keys.back().value;

    const uint32_t upper = UpperBoundKey(keys, t);
    const QuatKey& k1 = keys[upper];
    const QuatKey& k0 = keys[upper - 1];

    const float span = k1.time - k0.time;
    const float alpha = (span > 0.0f) ? (t - k0.time) / span : 0.0f;
    return k0.value.slerp(k1.value, alpha);
}

// ─────────────────────────────────────────────────────────────────────────────
// AnimationClip — Queries
// ─────────────────────────────────────────────────────────────────────────────

const BoneChannel* AnimationClip::FindChannel(const std::string& boneName) const {
    for (const auto& ch : channels) {
        if (ch.boneName == boneName) return &ch;
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// AnimationClip — Sampling
// ─────────────────────────────────────────────────────────────────────────────

void AnimationClip::SamplePose(float timeSec, const Skeleton& skeleton, Pose& outPose) const {
    const uint32_t boneCount = skeleton.GetBoneCount();
    if (outPose.GetBoneCount() != boneCount) {
        outPose.Resize(boneCount);
    }

    // Step 1 — seed with bind pose so bones without animation channels stay in
    //          bind (common for ornamental bones and roots of sub-hierarchies).
    for (uint32_t i = 0; i < boneCount; ++i) {
        outPose.localTransforms[i] = skeleton.GetBone(i).GetBindTransform();
    }

    // Step 2 — time wrapping. Clamp-or-loop to [0, duration].
    float t = timeSec;
    if (duration > 0.0f) {
        if (loop) {
            t = std::fmod(t, duration);
            if (t < 0.0f) t += duration;
        } else {
            t = std::clamp(t, 0.0f, duration);
        }
    } else {
        t = 0.0f;
    }

    // Step 3 — for each channel, find the skeleton bone and override the
    //          components that have keyframes.
    for (const auto& ch : channels) {
        const int32_t boneIdx = skeleton.FindBoneIndex(ch.boneName);
        if (boneIdx < 0) continue;

        BoneTransform& out = outPose.localTransforms[boneIdx];

        if (!ch.positionKeys.empty()) {
            out.position = SampleVec3Track(ch.positionKeys, t, out.position);
        }
        if (!ch.rotationKeys.empty()) {
            out.rotation = SampleQuatTrack(ch.rotationKeys, t, out.rotation);
        }
        if (!ch.scaleKeys.empty()) {
            out.scale = SampleVec3Track(ch.scaleKeys, t, out.scale);
        }
    }
}

} // namespace uz
