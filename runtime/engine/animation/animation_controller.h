/// UzEngine AnimationController — Thread-safe per-entity animation driver.
///
/// Owns a library of AnimationClip instances keyed by name, tracks a current
/// playback state (current clip + time + speed + looping), and supports linear
/// crossfades between clips. A simple parameter + state-machine layer lets
/// gameplay code drive transitions declaratively (set a bool, the machine
/// swaps clips on the next tick).
///
/// # Thread safety
/// Every public method takes `m_mutex` briefly. This design assumes:
///   - Gameplay code (input handlers, Lua callbacks) calls `Play/Crossfade/
///     SetBool/SetFloat` from the main thread.
///   - The AnimationSubsystem calls `Update(dt)` + `SamplePose(skeleton, out)`
///     from worker threads (one call per entity, no two workers touch the
///     same controller concurrently).
///
/// Since `Update()` and `SamplePose()` never run simultaneously on the same
/// controller, the mutex is mostly uncontended. It's held only while reading
/// or mutating the state — actual keyframe interpolation in `SamplePose()`
/// runs outside the lock on the sampled clip (which is immutable after load).
///
/// # State machine
/// States are declared via `AddState(name, clipName, loop)`. Transitions via
/// `AddTransition(fromState, toState, blendTime, condition)` where `condition`
/// is a callback evaluated against the current parameter set.
/// `EvaluateTransitions()` is invoked inside `Update()` automatically.

#pragma once

#include "animation_clip.h"
#include "pose.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

class Skeleton;

// ─────────────────────────────────────────────────────────────────────────────
// AnimState / AnimTransition — state-machine building blocks
// ─────────────────────────────────────────────────────────────────────────────

struct AnimState {
    std::string name;        ///< Unique state name.
    std::string clipName;    ///< Name of the clip to play while in this state.
    bool        loop = true; ///< Whether the clip loops while in this state.
    float       speed = 1.f; ///< Playback speed multiplier.
};

/// Condition callback signature — receives a const reference to the parameter
/// map and returns true if the transition should fire.
struct AnimParameters {
    std::unordered_map<std::string, bool>  bools;
    std::unordered_map<std::string, float> floats;
    std::unordered_map<std::string, bool>  triggers; ///< Set to true then reset once consumed.
};

using AnimTransitionCondition = std::function<bool(const AnimParameters&)>;

struct AnimTransition {
    std::string fromState;
    std::string toState;
    float       blendTime = 0.2f;
    AnimTransitionCondition condition;
};

// ─────────────────────────────────────────────────────────────────────────────
// AnimationController
// ─────────────────────────────────────────────────────────────────────────────

class AnimationController {
public:
    AnimationController() = default;
    ~AnimationController() = default;

    AnimationController(const AnimationController&) = delete;
    AnimationController& operator=(const AnimationController&) = delete;
    AnimationController(AnimationController&&) = delete;
    AnimationController& operator=(AnimationController&&) = delete;

    // ── Clip library ────────────────────────────────────────────────────

    /// Registers an AnimationClip under `name`. Takes ownership via move.
    /// Thread-safe.
    void RegisterClip(const std::string& name, AnimationClip clip);

    /// Returns true if the controller has a clip registered under `name`.
    [[nodiscard]] bool HasClip(const std::string& name) const;

    /// Returns the number of registered clips.
    [[nodiscard]] std::size_t GetClipCount() const;

    // ── Playback control ────────────────────────────────────────────────

    /// Begins playback of `clipName` immediately (no crossfade). If the clip is
    /// unknown, the call is silently ignored (a warning is logged).
    void Play(const std::string& clipName, bool loop = true, float speed = 1.f);

    /// Begins a linear crossfade from the current clip to `clipName` over
    /// `blendTime` seconds. If `clipName` is unknown, acts as a no-op.
    void Crossfade(const std::string& clipName, float blendTime, bool loop = true, float speed = 1.f);

    /// Stops playback. The output pose reverts to the skeleton bind pose on
    /// the next `SamplePose()` call.
    void Stop();

    /// Returns the name of the currently playing clip (or the crossfade target
    /// if one is active). Empty string if stopped.
    [[nodiscard]] std::string GetCurrentClipName() const;

    /// Returns the current playback time in seconds.
    [[nodiscard]] float GetCurrentTime() const;

    // ── State machine ───────────────────────────────────────────────────

    /// Registers a state. Must correspond to a clip already registered.
    void AddState(AnimState state);

    /// Registers a transition between two states. Condition is evaluated in
    /// `Update()` and, if true, fires a crossfade to `toState`.
    void AddTransition(AnimTransition transition);

    /// Sets the entry state — played when `Start()` is invoked.
    void SetEntryState(const std::string& stateName);

    /// Activates the state machine: jumps to the entry state and plays its clip.
    void Start();

    /// Returns the name of the current state (empty if no state machine).
    [[nodiscard]] std::string GetCurrentState() const;

    // ── Parameters (driven by gameplay / input) ─────────────────────────

    void SetBool(const std::string& name, bool value);
    void SetFloat(const std::string& name, float value);
    void SetTrigger(const std::string& name); ///< One-shot; reset after consumption.
    [[nodiscard]] bool  GetBool(const std::string& name) const;
    [[nodiscard]] float GetFloat(const std::string& name) const;

    // ── Per-frame update (worker thread) ────────────────────────────────

    /// Advances playback time by `dt`, evaluates state-machine transitions,
    /// and progresses any active crossfade. Must be called before `SamplePose()`.
    void Update(float dt);

    /// Samples the current pose (with crossfade blend if active) into `outPose`.
    /// Uses the skeleton for bone name matching and bind-pose fallback.
    void SamplePose(const Skeleton& skeleton, Pose& outPose) const;

private:
    mutable std::mutex m_mutex;

    // Clip library — owned.
    std::unordered_map<std::string, std::unique_ptr<AnimationClip>> m_clips;

    // Current playback state.
    std::string m_currentClip;
    float       m_currentTime = 0.f;
    float       m_currentSpeed = 1.f;
    bool        m_currentLoop  = true;
    bool        m_isPlaying    = false;

    // Crossfade state.
    bool        m_crossfadeActive = false;
    std::string m_fadeFromClip;        ///< Clip being faded OUT (was current before Crossfade).
    float       m_fadeFromTime = 0.f;  ///< Frozen playback time of outgoing clip.
    float       m_fadeFromSpeed = 1.f;
    bool        m_fadeFromLoop  = true;
    float       m_fadeDuration  = 0.f;
    float       m_fadeElapsed   = 0.f;

    // State machine — optional.
    std::vector<AnimState>      m_states;
    std::vector<AnimTransition> m_transitions;
    std::string                 m_entryState;
    std::string                 m_currentState;

    // Parameters.
    AnimParameters m_params;

    // ── Internal helpers (assume m_mutex held) ──────────────────────────

    void PlayLocked(const std::string& clipName, bool loop, float speed, bool resetTime);
    void CrossfadeLocked(const std::string& clipName, float blendTime, bool loop, float speed);
    const AnimState* FindStateLocked(const std::string& name) const;
    void EvaluateTransitionsLocked();
};

} // namespace uz
