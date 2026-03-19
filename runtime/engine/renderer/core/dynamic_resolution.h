/// UzEngine Dynamic Resolution Controller — GPU-driven render scale adjustment.
///
/// Adjusts screen percentage each frame based on GPU frame time to maintain
/// a target framerate. When the GPU is overloaded, resolution is reduced
/// immediately; when there is headroom, resolution increases conservatively
/// to avoid oscillation.
///
/// # Thread Safety
/// NOT thread-safe. Render thread only.
///
/// @source UE5 DynamicRenderScaling.h — FDynamicRenderScaling
/// @source Unity DynamicResolutionHandler — scale-up hysteresis (360 frames at 60Hz)

#pragma once

#include <cstdint>

namespace uz {

/// Dynamic resolution controller.
///
/// Call `Update()` once per frame at the start of the frame, BEFORE applying
/// the screen percentage to the render target allocation.
///
/// # Algorithm
///
/// Scale DOWN is immediate (1 frame delay) when the GPU exceeds the budget.
/// Scale UP is conservative — it requires `kScaleUpThreshold` consecutive
/// frames under budget before increasing resolution by one step.
///
/// A dead zone between the scale-up and scale-down thresholds prevents
/// oscillation when the GPU is near the target.
///
/// @source UE5 DynamicRenderScaling.h — FDynamicRenderScaling
class DynamicResolutionController {
public:
    DynamicResolutionController() = default;
    ~DynamicResolutionController() = default;

    DynamicResolutionController(const DynamicResolutionController&) = default;
    DynamicResolutionController& operator=(const DynamicResolutionController&) = default;
    DynamicResolutionController(DynamicResolutionController&&) noexcept = default;
    DynamicResolutionController& operator=(DynamicResolutionController&&) noexcept = default;

    // ─── Enable/Disable ────────────────────────────────────────────────

    /// Enables or disables dynamic resolution scaling.
    ///
    /// When disabled, `Update()` returns `m_maxPct` and resets internal state.
    void SetEnabled(bool enabled) { m_enabled = enabled; }

    /// Returns true if dynamic resolution is active.
    [[nodiscard]] bool IsEnabled() const { return m_enabled; }

    // ─── Target Configuration ──────────────────────────────────────────

    /// Sets the target frame time in seconds.
    ///
    /// @param seconds  Target GPU frame time. Must be positive.
    ///                 Default: 1/60 = 16.67ms.
    void SetTargetFrameTime(float seconds);

    /// Sets the target by frames per second. Convenience wrapper.
    ///
    /// @param fps  Target FPS. Must be positive.
    void SetTargetFPS(float fps);

    /// Returns the current target FPS (1 / targetFrameTime).
    [[nodiscard]] float GetTargetFPS() const { return 1.0f / m_targetFrameTime; }

    /// Returns the current target frame time in seconds.
    [[nodiscard]] float GetTargetFrameTime() const { return m_targetFrameTime; }

    // ─── Screen Percentage Range ───────────────────────────────────────

    /// Sets the minimum allowed screen percentage.
    ///
    /// @param pct  Minimum percentage (e.g. 50.0f for 50%). Clamped to [10, maxPct].
    void SetMinScreenPercentage(float pct);

    /// Sets the maximum allowed screen percentage.
    ///
    /// @param pct  Maximum percentage (e.g. 100.0f for 100%). Clamped to [minPct, 200].
    void SetMaxScreenPercentage(float pct);

    /// Returns the minimum screen percentage.
    [[nodiscard]] float GetMinScreenPercentage() const { return m_minPct; }

    /// Returns the maximum screen percentage.
    [[nodiscard]] float GetMaxScreenPercentage() const { return m_maxPct; }

    // ─── Per-Frame Update ──────────────────────────────────────────────

    /// Updates the controller with the latest GPU frame time and returns the
    /// recommended screen percentage for the next frame.
    ///
    /// # Algorithm
    /// @source UE5 DynamicRenderScaling.h — FDynamicRenderScaling::Update
    /// @source Unity DynamicResolutionHandler — scale-up hysteresis
    ///
    /// - **Scale DOWN**: If `gpuTime > targetTime * kScaleDownRatio (0.95)`,
    ///   reduce by `kScaleStep` (5%) immediately. Instant response prevents
    ///   frame drops from compounding.
    /// - **Scale UP**: If `gpuTime < targetTime * kScaleUpRatio (0.85)` for
    ///   `kScaleUpThreshold` (120) consecutive frames, increase by `kScaleStep`.
    ///   Conservative to avoid oscillation.
    /// - **Dead zone**: GPU time between 85% and 95% of budget — no change,
    ///   counter is reset to avoid partial scale-up after brief dips.
    /// - Result is clamped to `[m_minPct, m_maxPct]`.
    ///
    /// @param gpuFrameTimeSeconds  GPU frame time from the previous frame (seconds).
    ///                             If negative or zero, treated as zero (no scaling action).
    /// @return Recommended screen percentage in `[minPct, maxPct]`.
    [[nodiscard]] float Update(float gpuFrameTimeSeconds);

    /// Returns the current recommended screen percentage (last `Update()` result).
    [[nodiscard]] float GetCurrentScreenPercentage() const { return m_currentPct; }

    /// Returns the number of consecutive frames the GPU has been under budget.
    ///
    /// Useful for debug overlays. Resets to 0 on scale-down or dead zone.
    [[nodiscard]] uint32_t GetFramesUnderBudget() const { return m_framesUnderBudget; }

    /// Resets internal state to defaults (max percentage, zero counter).
    ///
    /// Does NOT change enabled state or configuration — only runtime state.
    void Reset();

private:
    bool  m_enabled = false;
    float m_targetFrameTime = 1.0f / 60.0f;
    float m_minPct = 50.0f;
    float m_maxPct = 100.0f;
    float m_currentPct = 100.0f;

    /// Counter for consecutive frames under budget (scale-up hysteresis).
    /// @source Unity DynamicResolutionHandler — requires ~360 frames at 60Hz
    uint32_t m_framesUnderBudget = 0;

    /// Number of consecutive under-budget frames required before scaling up.
    /// @source UE5 DynamicRenderScaling.h — UpperBound heuristic
    static constexpr uint32_t kScaleUpThreshold = 120;

    /// Scale-down fires when GPU exceeds 95% of the target frame time.
    /// @source UE5 DynamicRenderScaling.h — BudgetMs_ScaleDown
    static constexpr float kScaleDownRatio = 0.95f;

    /// Scale-up fires when GPU is under 85% of the target frame time.
    /// @source UE5 DynamicRenderScaling.h — BudgetMs_ScaleUp
    static constexpr float kScaleUpRatio = 0.85f;

    /// Resolution change per step (5 percentage points).
    static constexpr float kScaleStep = 5.0f;

    /// Absolute minimum screen percentage to prevent degenerate resolutions.
    static constexpr float kAbsoluteMinPct = 10.0f;

    /// Absolute maximum screen percentage (supersampling upper bound).
    static constexpr float kAbsoluteMaxPct = 200.0f;
};

} // namespace uz
