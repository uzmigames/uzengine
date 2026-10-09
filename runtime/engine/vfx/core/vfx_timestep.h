#pragma once

/// Deterministic sub-stepped timestep accumulator for VFX simulation
/// (Niagara-parity FixedTickDeltaTime).
///
/// Frame-rate-dependent particle simulation drifts in behaviour across
/// hardware — a curl advection solver tuned at 60Hz behaves differently
/// at 30 or 144 Hz. Niagara supports a `FixedTickDeltaTime` that collapses
/// that variance by running the simulation at a fixed sub-step while the
/// frame time accumulates. UzEngine ships the same model via this class.
///
/// # Modes
///
/// | Mode       | Semantics                                                 |
/// |------------|-----------------------------------------------------------|
/// | `FrameRate`| One update CS dispatch per frame with `dt = frameDt`.     |
/// | `FixedRate`| N dispatches per frame, each using `dt = fixedDt`.        |
///
/// In FixedRate mode the accumulator stores the residual frame time; each
/// call to `Advance(frameDt)` returns the number of sub-steps to run this
/// frame and subtracts `numSteps * fixedDt` from the accumulator. The
/// residual carries over to the next frame so long-term average time
/// progression matches real time.
///
/// # Anti-spiral clamp
///
/// When the frame time spikes (e.g. hitch, debugger pause), the
/// accumulator could demand arbitrarily many sub-steps, which would then
/// make the frame even longer, spiralling out of control. `Advance`
/// clamps at `maxSubSteps` (default 8) and drops any remaining
/// accumulator. Simulation progresses slightly slower than real time
/// during the hitch but the spiral is broken immediately.
///
/// # Example
///
/// ```cpp
/// VfxTimestepAccumulator acc;
/// acc.SetFixedRate(1.0f / 60.0f, /*maxSubSteps=*/ 8);
///
/// // Per-frame update:
/// const uint32_t steps = acc.Advance(frameDt);
/// for (uint32_t i = 0; i < steps; ++i) {
///     DispatchParticleUpdate(acc.SubStepDt());
/// }
/// ```
///
/// @ref UE5 `UNiagaraEmitter::FixedTickDeltaTime`

#include <cstdint>

namespace uz {

/// Selects how the accumulator turns a frame's `dt` into simulation sub-steps.
enum class VfxTimestepMode : uint8_t {
    /// Default. One sub-step per frame with `subStepDt = frameDt`.
    /// Equivalent to pre-7.8.0 behaviour.
    FrameRate = 0,

    /// Deterministic sub-stepping. `subStepDt = fixedDt` regardless of
    /// `frameDt`. The number of sub-steps per frame varies with frame
    /// time, clamped at `maxSubSteps`.
    FixedRate = 1,
};

/// Deterministic sub-step accumulator for the VFX update pass.
///
/// Thread-safety: not thread-safe. Each emitter owns one accumulator and
/// `Advance` is called on the main simulation thread.
class VfxTimestepAccumulator {
public:
    VfxTimestepAccumulator() = default;

    /// Switches to FrameRate mode. `Advance` returns 1 sub-step per frame
    /// and `SubStepDt()` returns the last `frameDt` passed to `Advance`.
    void SetFrameRate() noexcept;

    /// Switches to FixedRate mode with the supplied sub-step size and
    /// anti-spiral clamp. `fixedDt` must be strictly positive; zero or
    /// negative values silently fall back to FrameRate mode.
    void SetFixedRate(float fixedDt, uint32_t maxSubSteps = 8u) noexcept;

    /// Consumes a real-time `frameDt` and returns the number of sub-steps
    /// the caller should run this frame.
    ///
    /// FrameRate mode: always returns 1 (or 0 if `frameDt <= 0`). Side
    /// effect: records `frameDt` so `SubStepDt()` mirrors it.
    ///
    /// FixedRate mode: accumulates `frameDt` and returns
    /// `min(floor(accumulator / fixedDt), maxSubSteps)`. The accumulator
    /// is decremented by `(returned * fixedDt)`; leftover residual
    /// carries to the next frame. When clamped at `maxSubSteps`, any
    /// excess accumulator is dropped (anti-spiral).
    ///
    /// Negative or non-finite `frameDt` is clamped to 0 — simulation does
    /// not rewind.
    uint32_t Advance(float frameDt) noexcept;

    /// The `dt` each sub-step should be dispatched with. In FrameRate
    /// mode this is the frame time recorded by the last `Advance` call;
    /// in FixedRate mode it is the configured `fixedDt`.
    [[nodiscard]] float SubStepDt() const noexcept { return m_subStepDt; }

    /// Current residual inside the accumulator (0 in FrameRate mode).
    /// Exposed for debugging / telemetry only.
    [[nodiscard]] float GetAccumulator() const noexcept { return m_accumulator; }

    /// Active mode.
    [[nodiscard]] VfxTimestepMode GetMode() const noexcept { return m_mode; }

    /// Configured fixed sub-step size (undefined in FrameRate mode).
    [[nodiscard]] float GetFixedDt() const noexcept { return m_fixedDt; }

    /// Anti-spiral clamp (undefined in FrameRate mode).
    [[nodiscard]] uint32_t GetMaxSubSteps() const noexcept { return m_maxSubSteps; }

    /// Resets the accumulator to zero. Typically called when an emitter
    /// is restarted (e.g. preset hot-reload) so residual sub-step debt
    /// from the old configuration is cleared.
    void Reset() noexcept;

private:
    VfxTimestepMode m_mode           = VfxTimestepMode::FrameRate;
    float           m_fixedDt        = 1.0f / 60.0f;
    uint32_t        m_maxSubSteps    = 8u;
    float           m_accumulator    = 0.0f;
    float           m_subStepDt      = 0.0f;
};

} // namespace uz
