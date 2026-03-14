#pragma once

/// UzEngine Async Compute Overlap Telemetry — GPU queue utilization measurement.
///
/// Measures graphics vs compute queue busy/idle times to quantify how much
/// async compute work runs concurrently with graphics. Higher overlap means
/// better hardware utilization and lower frame times.
///
/// # Usage
/// ```cpp
/// AsyncComputeTelemetry telemetry;
/// telemetry.Init({.historySize = 120});
///
/// // Each frame:
/// telemetry.BeginGraphics();
/// // ... submit graphics work ...
/// telemetry.EndGraphics();
///
/// telemetry.BeginCompute();
/// // ... submit compute work ...
/// telemetry.EndCompute();
///
/// telemetry.EndFrame();
/// float overlap = telemetry.GetOverlapRatio(); // 0.0–1.0
/// ```
///
/// # Thread Safety
/// All methods must be called from the main render thread. The timing
/// measurements are CPU-side approximations of GPU queue occupancy.

#include <chrono>
#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Data Types
// ─────────────────────────────────────────────────────────────────────────────

/// Per-frame timing snapshot for one GPU queue.
struct QueueTimingSnapshot {
    float busyMs = 0.0f;      ///< Time the queue was actively executing work.
    float idleMs = 0.0f;      ///< Time the queue was idle (waiting for fence/work).
    float utilization = 0.0f; ///< busyMs / (busyMs + idleMs), clamped to 0.0–1.0.
};

/// Configuration for async compute telemetry.
struct AsyncComputeTelemetryConfig {
    /// Number of frames in the rolling average window.
    uint32_t historySize = 120;

    /// When true, logs per-frame timing at Debug level.
    bool enableDetailedLogging = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// AsyncComputeTelemetry
// ─────────────────────────────────────────────────────────────────────────────

/// Tracks graphics and compute queue utilization to measure async overlap.
///
/// Call `BeginGraphics()`/`EndGraphics()` and `BeginCompute()`/`EndCompute()`
/// around queue submissions. `EndFrame()` computes per-frame stats and updates
/// the rolling average.
///
/// The overlap ratio measures how much compute work runs concurrently with
/// graphics — higher is better (1.0 = perfect overlap, 0.0 = fully sequential).
///
/// # Overlap Calculation
/// Overlap is computed as the intersection of the graphics and compute busy
/// intervals divided by the shorter interval's duration. This gives 1.0 when
/// both queues are fully overlapping and 0.0 when they run sequentially.
class AsyncComputeTelemetry {
public:
    AsyncComputeTelemetry() = default;
    ~AsyncComputeTelemetry() = default;

    AsyncComputeTelemetry(const AsyncComputeTelemetry&) = delete;
    AsyncComputeTelemetry& operator=(const AsyncComputeTelemetry&) = delete;
    AsyncComputeTelemetry(AsyncComputeTelemetry&&) = default;
    AsyncComputeTelemetry& operator=(AsyncComputeTelemetry&&) = default;

    /// Initializes the telemetry system with the given configuration.
    ///
    /// # Returns
    /// Always returns `true`. Provided for consistency with other RHI subsystems.
    bool Init(const AsyncComputeTelemetryConfig& config = {});

    /// Resets all state and clears history.
    void Shutdown();

    /// Marks the start of graphics queue work for this frame.
    void BeginGraphics();

    /// Marks the end of graphics queue work for this frame.
    void EndGraphics();

    /// Marks the start of compute queue work for this frame.
    void BeginCompute();

    /// Marks the end of compute queue work for this frame.
    void EndCompute();

    /// Finalizes the current frame's timing, computes overlap, and updates
    /// the rolling average. Must be called once per frame after all
    /// Begin/End pairs.
    void EndFrame();

    /// Returns the graphics queue timing for the most recent completed frame.
    [[nodiscard]] QueueTimingSnapshot GetGraphicsStats() const;

    /// Returns the compute queue timing for the most recent completed frame.
    [[nodiscard]] QueueTimingSnapshot GetComputeStats() const;

    /// Returns the overlap ratio for the most recent completed frame.
    ///
    /// # Returns
    /// 0.0 if queues ran sequentially, 1.0 if perfectly overlapped.
    [[nodiscard]] float GetOverlapRatio() const;

    /// Returns the rolling average overlap ratio over the configured window.
    [[nodiscard]] float GetAvgOverlapRatio() const;

    /// Returns true if the telemetry system has been initialized.
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    /// Logs a summary of current queue utilization and overlap at Info level.
    void LogSummary() const;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    /// Computes milliseconds between two time points.
    static float DurationMs(TimePoint start, TimePoint end);

    /// Computes overlap duration in ms between two intervals [s1,e1] and [s2,e2].
    static float ComputeOverlapMs(TimePoint s1, TimePoint e1,
                                  TimePoint s2, TimePoint e2);

    AsyncComputeTelemetryConfig m_config;
    bool m_initialized = false;

    // Per-frame timestamps
    TimePoint m_graphicsBegin{};
    TimePoint m_graphicsEnd{};
    TimePoint m_computeBegin{};
    TimePoint m_computeEnd{};
    bool m_graphicsActive = false;
    bool m_computeActive = false;

    // Last completed frame results
    QueueTimingSnapshot m_lastGraphics;
    QueueTimingSnapshot m_lastCompute;
    float m_lastOverlap = 0.0f;

    // Rolling history for averaging
    std::vector<float> m_overlapHistory;
    uint32_t m_historyIndex = 0;
    bool m_historyFull = false;
};

} // namespace uz
