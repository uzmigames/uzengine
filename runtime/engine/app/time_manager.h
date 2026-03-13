#pragma once

/// UzEngine Time Manager
///
/// Tracks frame timing, provides scaled delta time, and implements a fixed-timestep
/// accumulator for deterministic physics/logic updates.
///
/// # Design
/// - Updated directly by the engine loop via `BeginFrame()`, not via tick groups.
/// - Provides both variable (`deltaTime`) and fixed (`fixedDeltaTime`) timesteps.
/// - `timeScale` allows slow-motion or pause without affecting the fixed-step accumulator.
/// - Delta time is clamped to `kMaxDeltaTime` to prevent spiral-of-death after hitches.
///
/// # Example
/// ```cpp
/// TimeManager time;
/// time.Init();
/// // ... each frame:
/// time.BeginFrame();
/// float dt = time.GetDeltaTime();
/// int fixedTicks = time.ConsumeFixedTicks();
/// for (int i = 0; i < fixedTicks; ++i) {
///     PhysicsStep(time.GetFixedDeltaTime());
/// }
/// ```

#include "subsystem.h"

#include <chrono>

namespace uz {

/// Maximum delta time in seconds. Frames slower than this are clamped
/// to prevent physics explosions after long hitches (e.g., debugger pause).
inline constexpr float kMaxDeltaTime = 0.25f;

/// Default fixed timestep in seconds (1/60 = ~16.67ms).
inline constexpr float kDefaultFixedDeltaTime = 1.0f / 60.0f;

/// Manages frame timing and fixed-timestep accumulation.
///
/// Implements `ISubsystem` for lifecycle integration, but uses `TickGroup::None`
/// because it is driven directly by the engine loop (must run before any tick group).
///
/// # Thread Safety
/// Not thread-safe. All methods must be called from the main thread.
class TimeManager final : public ISubsystem {
public:
    // ── ISubsystem interface ────────────────────────────────────────────────

    /// Returns "TimeManager".
    [[nodiscard]] const char* GetName() const override { return "TimeManager"; }

    /// Returns `InitPhase::PreInit` — time tracking has no GPU dependencies.
    [[nodiscard]] InitPhase GetInitPhase() const override { return InitPhase::PreInit; }

    /// Returns `TickGroup::None` — updated by engine loop, not tick groups.
    [[nodiscard]] TickGroup GetTickGroup() const override { return TickGroup::None; }

    /// Resets all timing state to initial values.
    ///
    /// # Returns
    /// Always returns `true`.
    [[nodiscard]] bool Init() override;

    /// No-op. TimeManager holds no external resources.
    void Shutdown() override {}

    // ── Time API ────────────────────────────────────────────────────────────

    /// Call at the start of each frame to compute delta time.
    ///
    /// # Behavior
    /// - Measures wall-clock time since last `BeginFrame()` call.
    /// - Clamps raw delta to `kMaxDeltaTime` to prevent spiral-of-death.
    /// - Applies `timeScale` to produce the scaled delta time.
    /// - Increments `frameCount`.
    /// - Adds scaled delta to the fixed-step accumulator.
    void BeginFrame();

    /// Returns the number of fixed-timestep ticks that should execute this frame.
    ///
    /// Drains the internal accumulator by `fixedDeltaTime` for each tick returned.
    /// Call this once per frame, then loop that many times for physics/fixed-rate logic.
    ///
    /// # Returns
    /// Number of fixed ticks to execute (0 if accumulator hasn't reached one full step).
    [[nodiscard]] int ConsumeFixedTicks();

    /// Scaled delta time for this frame, in seconds.
    ///
    /// This is `clamp(rawDelta, 0, kMaxDeltaTime) * timeScale`.
    [[nodiscard]] float GetDeltaTime() const { return deltaTime_; }

    /// Unscaled (wall-clock) delta time for this frame, in seconds.
    [[nodiscard]] float GetRawDeltaTime() const { return rawDeltaTime_; }

    /// Total elapsed scaled time since Init(), in seconds.
    [[nodiscard]] float GetTotalTime() const { return totalTime_; }

    /// Fixed timestep interval in seconds.
    [[nodiscard]] float GetFixedDeltaTime() const { return fixedDeltaTime_; }

    /// Sets the fixed timestep interval.
    ///
    /// # Arguments
    /// * `dt` - New fixed delta time in seconds. Must be > 0.
    void SetFixedDeltaTime(float dt);

    /// Number of frames since Init().
    [[nodiscard]] uint64_t GetFrameCount() const { return frameCount_; }

    /// Time scale multiplier. 1.0 = normal, 0.0 = paused, 0.5 = half speed.
    [[nodiscard]] float GetTimeScale() const { return timeScale_; }

    /// Sets the time scale multiplier.
    ///
    /// # Arguments
    /// * `scale` - New time scale. Clamped to [0.0, 10.0].
    void SetTimeScale(float scale);

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    float deltaTime_ = 0.0f;        ///< Scaled delta time (seconds).
    float rawDeltaTime_ = 0.0f;     ///< Unscaled wall-clock delta (seconds).
    float totalTime_ = 0.0f;        ///< Accumulated scaled time (seconds).
    float fixedDeltaTime_ = kDefaultFixedDeltaTime; ///< Fixed step interval (seconds).
    float fixedAccumulator_ = 0.0f; ///< Accumulated time for fixed-step ticks.
    float timeScale_ = 1.0f;        ///< Time scale multiplier.
    uint64_t frameCount_ = 0;       ///< Frames since Init().
    TimePoint lastFrameTime_ = {};  ///< Timestamp of previous BeginFrame() call.
    bool firstFrame_ = true;        ///< True until first BeginFrame() completes.
};

} // namespace uz
