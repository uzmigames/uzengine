/// UzEngine AnimationClip — Keyframe-based animation data for a skeleton.
///
/// An AnimationClip stores per-bone keyframe tracks (position, rotation, scale)
/// sampled from DCC tools (Maya, Blender, Mixamo). At runtime, `SamplePose()`
/// interpolates between keyframes to produce a Pose for a given time.
///
/// # Bone Matching
/// Clips store bone data by NAME rather than index. When sampled against a
/// Skeleton, the clip matches channels to skeleton bones via `FindBoneIndex()`.
/// Bones in the skeleton without a matching channel keep their bind pose.
/// This design allows one clip to be shared across multiple similar skeletons.
///
/// # Memory Layout
/// Keyframe arrays are stored separately per channel type (position, rotation,
/// scale) because different channels can have different key counts — for example,
/// a "look" bone may only have rotation keys.
///
/// # Thread Safety
/// Immutable after construction. Safe to share across threads.

#pragma once

#include "vector3.h"
#include "quaternion.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class Skeleton;
struct Pose;

// ─────────────────────────────────────────────────────────────────────────────
// Keyframe<T> — Timestamp + value
// ─────────────────────────────────────────────────────────────────────────────

/// A single animation keyframe: `(time, value)` pair.
///
/// Time is in seconds, relative to the clip's start (time 0 = clip beginning).
/// The value type `T` is one of `Vec3` (position, scale) or `Quat` (rotation).
template <typename T>
struct Keyframe {
    /// Time of this keyframe in seconds.
    float time = 0.0f;
    /// Sampled value at `time`.
    T value{};
};

using Vec3Key = Keyframe<Vec3>;
using QuatKey = Keyframe<Quat>;

// ─────────────────────────────────────────────────────────────────────────────
// BoneChannel — Per-bone keyframe tracks
// ─────────────────────────────────────────────────────────────────────────────

/// Keyframe tracks for a single bone.
///
/// Each channel type (position, rotation, scale) is an independent sorted array
/// of keyframes. Missing tracks produce a bind-pose fallback for that component.
struct BoneChannel {
    /// Bone name for matching against a Skeleton's `FindBoneIndex()`.
    std::string boneName;

    /// Position keyframes, sorted by time (ascending). May be empty.
    std::vector<Vec3Key> positionKeys;

    /// Rotation keyframes, sorted by time (ascending). May be empty.
    std::vector<QuatKey> rotationKeys;

    /// Scale keyframes, sorted by time (ascending). May be empty.
    std::vector<Vec3Key> scaleKeys;

    /// Returns true if this channel has no keyframes at all.
    [[nodiscard]] bool IsEmpty() const {
        return positionKeys.empty() && rotationKeys.empty() && scaleKeys.empty();
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// AnimationClip — Skeletal animation asset
// ─────────────────────────────────────────────────────────────────────────────

/// Keyframe-based skeletal animation clip.
///
/// Contains a collection of `BoneChannel` tracks plus clip-level metadata
/// (duration, sample rate, loop flag). Sampled at runtime by `AnimationPlayer`.
///
/// # Sampling Model
/// For each channel:
///   - Find the two keyframes bracketing the current time `t`.
///   - Linearly interpolate (lerp for position/scale, slerp for rotation).
///   - Fall back to the bind pose for missing channels.
///
/// # Loop Behavior
/// If `loop == true`, `SamplePose()` wraps the time via `fmod(t, duration)`.
/// Otherwise the time is clamped to `[0, duration]`.
class AnimationClip {
public:
    // ── Metadata ────────────────────────────────────────────────────────

    /// Human-readable clip name (e.g., "Walk", "Idle", "Attack01").
    std::string name;

    /// Clip duration in seconds. `0.0f` means a single-pose clip (no animation).
    float duration = 0.0f;

    /// Original DCC sample rate in frames per second (informational only).
    /// Runtime sampling is time-based, so this field does not affect playback —
    /// it is kept for tooling and debugging.
    float sampleRate = 30.0f;

    /// Whether this clip loops by default. `AnimationPlayer` may override this.
    bool loop = true;

    /// Name of the skeleton this clip was authored against. Used for validation.
    /// Runtime matching is by bone name, so mismatched skeletons will still sample
    /// — they just produce partial poses.
    std::string skeletonName;

    /// Per-bone keyframe tracks.
    std::vector<BoneChannel> channels;

    // ── Construction ────────────────────────────────────────────────────

    AnimationClip() = default;

    // Move-only to avoid accidental deep copies of large keyframe arrays.
    AnimationClip(AnimationClip&&) noexcept = default;
    AnimationClip& operator=(AnimationClip&&) noexcept = default;

    // Copy is allowed but explicit (e.g., for editor operations).
    AnimationClip(const AnimationClip&) = default;
    AnimationClip& operator=(const AnimationClip&) = default;

    // ── Queries ─────────────────────────────────────────────────────────

    /// Returns the number of bone channels in this clip.
    [[nodiscard]] uint32_t GetChannelCount() const {
        return static_cast<uint32_t>(channels.size());
    }

    /// Returns true if this clip has no channels and no duration.
    [[nodiscard]] bool IsEmpty() const {
        return channels.empty() && duration == 0.0f;
    }

    /// Finds a channel by bone name. Linear scan — avoid in hot loops.
    /// Returns a pointer into the `channels` array, or nullptr if not found.
    [[nodiscard]] const BoneChannel* FindChannel(const std::string& boneName) const;

    // ── Sampling ────────────────────────────────────────────────────────

    /// Samples the clip at the given time and fills `outPose`.
    ///
    /// For each bone in the skeleton:
    ///   - If the clip has a matching channel, interpolate its keyframes at `timeSec`.
    ///   - Otherwise, write the bone's bind transform.
    ///
    /// `outPose` is resized to `skeleton.GetBoneCount()` if needed.
    ///
    /// # Arguments
    /// * `timeSec` — Sample time in seconds. If `loop`, wrapped to [0, duration);
    ///   otherwise clamped to [0, duration].
    /// * `skeleton` — Skeleton to sample against (for bone matching + bind fallback).
    /// * `outPose` — Destination pose (resized + overwritten).
    void SamplePose(float timeSec, const Skeleton& skeleton, Pose& outPose) const;
};

// ─────────────────────────────────────────────────────────────────────────────
// Free functions — keyframe interpolation (exposed for testing)
// ─────────────────────────────────────────────────────────────────────────────

/// Samples a Vec3 track at time `t`. Empty arrays return `fallback`.
/// Out-of-range times clamp to the first/last keyframe.
[[nodiscard]] Vec3 SampleVec3Track(const std::vector<Vec3Key>& keys, float t, const Vec3& fallback);

/// Samples a Quat track at time `t`. Empty arrays return `fallback`.
/// Out-of-range times clamp to the first/last keyframe. Uses slerp.
[[nodiscard]] Quat SampleQuatTrack(const std::vector<QuatKey>& keys, float t, const Quat& fallback);

} // namespace uz
