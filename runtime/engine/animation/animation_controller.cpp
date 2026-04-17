/// UzEngine AnimationController — Implementation.

#include "animation_controller.h"
#include "skeleton.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Animation);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Clip library
// ─────────────────────────────────────────────────────────────────────────────

void AnimationController::RegisterClip(const std::string& name, AnimationClip clip) {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto owned = std::make_unique<AnimationClip>(std::move(clip));
    owned->name = name; // ensure the lookup key matches the stored name
    m_clips[name] = std::move(owned);
}

bool AnimationController::HasClip(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clips.find(name) != m_clips.end();
}

std::size_t AnimationController::GetClipCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clips.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// Playback control
// ─────────────────────────────────────────────────────────────────────────────

void AnimationController::PlayLocked(const std::string& clipName, bool loop, float speed, bool resetTime) {
    if (m_clips.find(clipName) == m_clips.end()) {
        UZ_LOG(Animation, Warning, "AnimationController::Play: clip '{}' not found", clipName);
        return;
    }
    m_currentClip  = clipName;
    m_currentSpeed = speed;
    m_currentLoop  = loop;
    m_isPlaying    = true;
    if (resetTime) m_currentTime = 0.f;

    m_crossfadeActive = false;
    m_fadeFromClip.clear();
    m_fadeElapsed = 0.f;
    m_fadeDuration = 0.f;
}

void AnimationController::CrossfadeLocked(const std::string& clipName, float blendTime, bool loop, float speed) {
    if (m_clips.find(clipName) == m_clips.end()) {
        UZ_LOG(Animation, Warning, "AnimationController::Crossfade: clip '{}' not found", clipName);
        return;
    }
    if (!m_isPlaying || m_currentClip.empty() || blendTime <= 0.f) {
        PlayLocked(clipName, loop, speed, /*resetTime=*/true);
        return;
    }
    if (m_currentClip == clipName) {
        // Already playing — nothing to fade to.
        return;
    }

    // Snapshot outgoing clip state.
    m_fadeFromClip  = m_currentClip;
    m_fadeFromTime  = m_currentTime;
    m_fadeFromSpeed = m_currentSpeed;
    m_fadeFromLoop  = m_currentLoop;

    // Incoming clip begins at t=0.
    m_currentClip  = clipName;
    m_currentTime  = 0.f;
    m_currentSpeed = speed;
    m_currentLoop  = loop;
    m_isPlaying    = true;

    m_crossfadeActive = true;
    m_fadeDuration    = blendTime;
    m_fadeElapsed     = 0.f;
}

void AnimationController::Play(const std::string& clipName, bool loop, float speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    PlayLocked(clipName, loop, speed, /*resetTime=*/true);
}

void AnimationController::Crossfade(const std::string& clipName, float blendTime, bool loop, float speed) {
    std::lock_guard<std::mutex> lock(m_mutex);
    CrossfadeLocked(clipName, blendTime, loop, speed);
}

void AnimationController::Stop() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_isPlaying = false;
    m_crossfadeActive = false;
    m_fadeFromClip.clear();
}

std::string AnimationController::GetCurrentClipName() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isPlaying ? m_currentClip : std::string{};
}

float AnimationController::GetCurrentTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentTime;
}

// ─────────────────────────────────────────────────────────────────────────────
// State machine
// ─────────────────────────────────────────────────────────────────────────────

const AnimState* AnimationController::FindStateLocked(const std::string& name) const {
    for (const auto& s : m_states) {
        if (s.name == name) return &s;
    }
    return nullptr;
}

void AnimationController::AddState(AnimState state) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_states.push_back(std::move(state));
}

void AnimationController::AddTransition(AnimTransition transition) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_transitions.push_back(std::move(transition));
}

void AnimationController::SetEntryState(const std::string& stateName) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entryState = stateName;
}

void AnimationController::Start() {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_entryState.empty()) return;
    const AnimState* s = FindStateLocked(m_entryState);
    if (!s) {
        UZ_LOG(Animation, Warning, "AnimationController::Start: entry state '{}' not found", m_entryState);
        return;
    }
    m_currentState = s->name;
    PlayLocked(s->clipName, s->loop, s->speed, /*resetTime=*/true);
}

std::string AnimationController::GetCurrentState() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentState;
}

void AnimationController::EvaluateTransitionsLocked() {
    if (m_currentState.empty()) return;

    for (auto& t : m_transitions) {
        // Match transitions from current state OR wildcard (fromState empty = any).
        if (!t.fromState.empty() && t.fromState != m_currentState) continue;
        if (!t.condition || !t.condition(m_params)) continue;

        // Transition fires — look up destination clip from the state.
        const AnimState* dest = FindStateLocked(t.toState);
        if (!dest) continue;

        CrossfadeLocked(dest->clipName, t.blendTime, dest->loop, dest->speed);
        m_currentState = dest->name;

        // Consume any triggers that matched. Cheap sweep: reset every trigger
        // so one-shot edges don't fire again next frame.
        for (auto& [k, v] : m_params.triggers) v = false;

        // Only fire one transition per Update to avoid ping-pong.
        return;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Parameters
// ─────────────────────────────────────────────────────────────────────────────

void AnimationController::SetBool(const std::string& name, bool value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params.bools[name] = value;
}

void AnimationController::SetFloat(const std::string& name, float value) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params.floats[name] = value;
}

void AnimationController::SetTrigger(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_params.triggers[name] = true;
}

bool AnimationController::GetBool(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_params.bools.find(name);
    return (it != m_params.bools.end()) ? it->second : false;
}

float AnimationController::GetFloat(const std::string& name) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_params.floats.find(name);
    return (it != m_params.floats.end()) ? it->second : 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update + sampling
// ─────────────────────────────────────────────────────────────────────────────

void AnimationController::Update(float dt) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_isPlaying) return;

    m_currentTime += dt * m_currentSpeed;

    if (m_crossfadeActive) {
        m_fadeFromTime += dt * m_fadeFromSpeed;
        m_fadeElapsed  += dt;
        if (m_fadeElapsed >= m_fadeDuration) {
            // Crossfade complete.
            m_crossfadeActive = false;
            m_fadeFromClip.clear();
            m_fadeElapsed = 0.f;
            m_fadeDuration = 0.f;
        }
    }

    EvaluateTransitionsLocked();
}

void AnimationController::SamplePose(const Skeleton& skeleton, Pose& outPose) const {
    // Snapshot state under the lock; run the sampling outside the lock.
    bool playing = false;
    std::string currentClip, fadeFromClip;
    float currentTime = 0.f, fadeFromTime = 0.f;
    bool crossfadeActive = false;
    float fadeAlpha = 0.f;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        playing = m_isPlaying;
        if (!playing) return;
        currentClip     = m_currentClip;
        currentTime     = m_currentTime;
        crossfadeActive = m_crossfadeActive;
        if (crossfadeActive) {
            fadeFromClip = m_fadeFromClip;
            fadeFromTime = m_fadeFromTime;
            fadeAlpha    = (m_fadeDuration > 0.f) ? (m_fadeElapsed / m_fadeDuration) : 1.f;
            if (fadeAlpha > 1.f) fadeAlpha = 1.f;
        }
    }

    // Clips are immutable after insertion, so pointers stay valid without a lock.
    const AnimationClip* current = nullptr;
    const AnimationClip* fadeFrom = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto itCur = m_clips.find(currentClip);
        if (itCur != m_clips.end()) current = itCur->second.get();
        if (!fadeFromClip.empty()) {
            auto itFrom = m_clips.find(fadeFromClip);
            if (itFrom != m_clips.end()) fadeFrom = itFrom->second.get();
        }
    }

    if (!current) {
        outPose = Pose::FromBindPose(skeleton);
        return;
    }

    if (!crossfadeActive || !fadeFrom) {
        current->SamplePose(currentTime, skeleton, outPose);
        return;
    }

    // Crossfade: sample both, blend linearly.
    Pose poseA;
    Pose poseB;
    fadeFrom->SamplePose(fadeFromTime, skeleton, poseA);
    current->SamplePose(currentTime,   skeleton, poseB);
    outPose = Pose::Blend(poseA, poseB, fadeAlpha);
}

} // namespace uz
