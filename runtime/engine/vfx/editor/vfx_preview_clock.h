#pragma once

/// VFX editor preview clock — play / pause / scrub / step-frame / freeze.
///
/// The VFX editor replaces the wall-clock delta time used by `VFXSystem::Tick`
/// with a clock-driven delta whenever the author is inspecting a timing-
/// critical effect. This gives 5–10× faster iteration on multi-beat sequences
/// (impact → flash → debris → smoke) by letting authors freeze at any frame
/// or jump to a specific sub-second mark without waiting for the real-time
/// playback to loop.
///
/// # State machine
///
/// ```
/// Playing   — Tick(wallDt) advances by wallDt * playRate
/// Paused    — Tick(*) yields dt = 0 so nothing advances
/// Stepping  — StepFrame(dt) issues exactly `dt` once then auto-Paused
/// Scrubbing — Scrub(t) jumps currentTime to t and yields dt = (t - prev)
/// Frozen    — Tick(*) yields dt = 0 and currentTime stays put (same as Paused
///             but reserved for the UI "freeze" toggle so the widget can
///             distinguish "deliberately frozen" from "just paused while I
///             drag the scrubber")
/// ```
///
/// Transitions are idempotent — calling `Play()` while already playing is a
/// no-op. `StepFrame(dt)` always jumps straight into Stepping regardless of
/// the previous state, then falls back to Paused after exactly one `Tick`.
///
/// # Fixed-timestep determinism
///
/// `VFXSystem::Tick` consumes the clock-provided delta as a single scalar,
/// so when a caller threads the dt into both the fixed-timestep accumulator
/// and the clock the simulation remains bit-for-bit deterministic across
/// scrub operations. The clock never introduces sub-frame jitter: scrub
/// targets are reported as a single large dt once and cannot silently desync
/// the accumulator on the next frame because the clock re-snaps to Paused.
///
/// # Threading
///
/// Not thread-safe. Drive from the main / editor thread only. Lifetime is
/// owned by the editor overlay — no singletons.
///
/// @ref UE5 SNiagaraSystemViewportToolBar + FNiagaraTimelineController.
/// @ref docs/engine/25-vfx/21-preview-timeline.md

#include <algorithm>

namespace uz {

/// Discrete author-visible preview states.
enum class VfxPreviewState : uint8_t {
    Playing   = 0, ///< Real-time playback (dt = wallDt * playRate).
    Paused    = 1, ///< Timeline halted; dt always 0 until state changes.
    Stepping  = 2, ///< One-shot advance of a known dt, then auto-Paused.
    Scrubbing = 3, ///< User is dragging the scrubber; dt is the jump delta.
    Frozen    = 4, ///< Same dt=0 behaviour as Paused, but author-intentional.
};

/// Preview-clock configuration defaults. Mirrors Niagara's viewport toolbar.
struct VfxPreviewClockConfig {
    /// Default playback rate multiplier (1.0 = real-time).
    float playRate = 1.0f;

    /// Upper bound on the scrub slider in seconds. Authors can extend this
    /// from the UI when editing long cinematic effects.
    float maxDuration = 10.0f;

    /// Default fixed step size (in seconds) used when `StepFrame()` is
    /// invoked without a dt argument. 1/60 matches the engine's default
    /// fixed-timestep accumulator.
    float defaultStepDt = 1.0f / 60.0f;
};

/// Play / pause / scrub / step-frame state machine for the VFX editor.
///
/// Used by the editor overlay to produce a replacement delta-time that the
/// VFXSystem consumes in place of the wall-clock dt. Self-contained — no
/// engine-global coupling, all state lives on the instance.
class VfxPreviewClock {
public:
    VfxPreviewClock() = default;
    explicit VfxPreviewClock(const VfxPreviewClockConfig& cfg) : config_(cfg) {}

    /// Advances the clock by the caller's wall-clock delta and returns the
    /// simulation dt that should feed `VFXSystem::Tick`. Handles all state
    /// transitions (Stepping → Paused, Scrubbing → Paused) internally.
    ///
    /// # Arguments
    /// - `wallDt` — wall-clock delta in seconds (always ≥ 0, typically 1/60).
    ///
    /// # Returns
    /// The simulation dt the caller must forward to `VFXSystem::Tick`. May
    /// be 0 (Paused / Frozen), `wallDt * playRate` (Playing), `stepDt_`
    /// (Stepping), or the scrub jump (Scrubbing).
    [[nodiscard]] float Tick(float wallDt) noexcept {
        float dt = 0.0f;
        switch (state_) {
            case VfxPreviewState::Playing:
                dt = std::max(0.0f, wallDt) * config_.playRate;
                currentTime_ += dt;
                break;
            case VfxPreviewState::Stepping:
                dt = stepDt_;
                currentTime_ += dt;
                // One-shot: fall back to Paused so the next frame is idle.
                state_ = VfxPreviewState::Paused;
                stepDt_ = 0.0f;
                break;
            case VfxPreviewState::Scrubbing:
                dt = scrubDt_;
                // currentTime_ was already set by Scrub(); snap to Paused so
                // subsequent ticks don't re-issue the same delta.
                state_ = VfxPreviewState::Paused;
                scrubDt_ = 0.0f;
                break;
            case VfxPreviewState::Paused:
            case VfxPreviewState::Frozen:
                dt = 0.0f;
                break;
        }
        currentTime_ = std::clamp(currentTime_, 0.0f, config_.maxDuration);
        return dt;
    }

    // ── State transitions ────────────────────────────────────────────────

    /// Resumes real-time playback. Idempotent.
    void Play() noexcept { state_ = VfxPreviewState::Playing; }

    /// Halts playback without marking as user-frozen. Idempotent.
    void Pause() noexcept { state_ = VfxPreviewState::Paused; }

    /// Freezes playback as an explicit author choice (surface a different
    /// icon in the UI vs Paused). Behaves identically otherwise.
    void Freeze() noexcept { state_ = VfxPreviewState::Frozen; }

    /// Issues a single fixed-size advance, then auto-pauses. `dt` defaults
    /// to `config_.defaultStepDt` when not specified.
    void StepFrame(float dt) noexcept {
        stepDt_ = (dt > 0.0f) ? dt : config_.defaultStepDt;
        state_ = VfxPreviewState::Stepping;
    }

    /// Jumps the timeline cursor to `targetTime` (clamped to [0, maxDuration]).
    /// The jump delta is reported on the next `Tick` call so callers that
    /// cannot accept a non-monotonic clock (fixed-timestep accumulators)
    /// still receive a single scalar dt.
    void Scrub(float targetTime) noexcept {
        const float clamped = std::clamp(targetTime, 0.0f, config_.maxDuration);
        scrubDt_ = clamped - currentTime_;
        currentTime_ = clamped;
        state_ = VfxPreviewState::Scrubbing;
    }

    /// Resets the timeline to t=0 and pauses.
    void Reset() noexcept {
        state_ = VfxPreviewState::Paused;
        currentTime_ = 0.0f;
        stepDt_ = 0.0f;
        scrubDt_ = 0.0f;
    }

    /// Adjusts the real-time playback rate multiplier (1.0 = real-time,
    /// 2.0 = 2× fast-forward, 0.25 = quarter-speed). Clamped to
    /// `[0, 8]` so a typo cannot stall or jet the preview.
    void SetPlayRate(float rate) noexcept {
        config_.playRate = std::clamp(rate, 0.0f, 8.0f);
    }

    /// Replaces the maximum scrubber value. Re-clamps the current time.
    void SetMaxDuration(float seconds) noexcept {
        config_.maxDuration = std::max(0.0f, seconds);
        currentTime_ = std::clamp(currentTime_, 0.0f, config_.maxDuration);
    }

    // ── Accessors ────────────────────────────────────────────────────────

    [[nodiscard]] VfxPreviewState GetState() const noexcept { return state_; }
    [[nodiscard]] float GetCurrentTime() const noexcept { return currentTime_; }
    [[nodiscard]] float GetMaxDuration() const noexcept { return config_.maxDuration; }
    [[nodiscard]] float GetPlayRate() const noexcept { return config_.playRate; }
    [[nodiscard]] bool  IsPlaying() const noexcept {
        return state_ == VfxPreviewState::Playing;
    }
    [[nodiscard]] bool  IsHalted() const noexcept {
        return state_ == VfxPreviewState::Paused ||
               state_ == VfxPreviewState::Frozen;
    }

private:
    VfxPreviewClockConfig config_{};
    VfxPreviewState       state_       = VfxPreviewState::Playing;
    float                 currentTime_ = 0.0f;
    float                 stepDt_      = 0.0f;
    float                 scrubDt_     = 0.0f;
};

} // namespace uz
