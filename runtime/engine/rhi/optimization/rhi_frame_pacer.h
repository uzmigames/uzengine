#pragma once

/// UzEngine Frame Pacer — Adaptive frame timing with swap chain buffer tuning.
///
/// Monitors frame times and recommends optimal swap chain buffer counts to
/// balance latency vs throughput. With auto-tuning enabled, recommends 2
/// buffers (low latency) when frame times are consistently fast, and 3 buffers
/// (higher throughput) when the GPU is struggling.
///
/// # Usage
/// ```cpp
/// AdaptiveFramePacer pacer;
/// pacer.Init({.autoTuneBufferCount = true, .targetFrameTimeMs = 16.67f});
///
/// // Each frame:
/// pacer.BeginFrame();
/// // ... render ...
/// pacer.EndFrame();
///
/// if (pacer.ShouldResizeSwapChain()) {
///     swapChain.Resize(pacer.GetRecommendedBufferCount());
/// }
/// ```
///
/// # Thread Safety
/// All methods must be called from the main render thread only.

#include <chrono>
#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

/// Configuration for the frame pacer.
struct AdaptiveFramePacerConfig {
    /// Target frame time in milliseconds. Used for dropped-frame detection.
    float targetFrameTimeMs = 16.667f;

    /// Number of frames in the rolling average window.
    uint32_t historySize = 120;

    /// Minimum swap chain buffer count (low latency mode).
    uint32_t minSwapChainBuffers = 2;

    /// Maximum swap chain buffer count (high throughput mode).
    uint32_t maxSwapChainBuffers = 3;

    /// Automatically recommend buffer count changes based on frame timing.
    bool autoTuneBufferCount = false;

    /// Switch to `minSwapChainBuffers` when avg frame time is below this.
    float latencyThresholdMs = 20.0f;

    /// Switch to `maxSwapChainBuffers` when avg frame time exceeds this.
    float throughputThresholdMs = 25.0f;

    /// Number of consecutive frames above/below threshold before recommending
    /// a buffer count change. Prevents oscillation.
    uint32_t hysteresisFrames = 60;
};

// ─────────────────────────────────────────────────────────────────────────────
// AdaptiveFramePacer
// ─────────────────────────────────────────────────────────────────────────────

/// Monitors frame timing and recommends swap chain buffer count adjustments.
///
/// Tracks per-frame CPU time between `BeginFrame()` and `EndFrame()`, maintains
/// a rolling average, detects dropped frames, and optionally recommends buffer
/// count changes for latency/throughput trade-offs.
class AdaptiveFramePacer {
public:
    AdaptiveFramePacer() = default;
    ~AdaptiveFramePacer() = default;

    AdaptiveFramePacer(const AdaptiveFramePacer&) = delete;
    AdaptiveFramePacer& operator=(const AdaptiveFramePacer&) = delete;
    AdaptiveFramePacer(AdaptiveFramePacer&&) = default;
    AdaptiveFramePacer& operator=(AdaptiveFramePacer&&) = default;

    /// Initializes the frame pacer with the given configuration.
    ///
    /// # Returns
    /// `true` on success, `false` if configuration is invalid (e.g., min > max buffers).
    bool Init(const AdaptiveFramePacerConfig& config = {});

    /// Resets all state and clears history.
    void Shutdown();

    /// Marks the start of a new frame. Call at the top of the render loop.
    void BeginFrame();

    /// Marks the end of the current frame. Computes frame time, updates
    /// rolling average, and evaluates buffer count recommendation.
    void EndFrame();

    /// Returns the most recent frame time in milliseconds.
    [[nodiscard]] float GetLastFrameTimeMs() const;

    /// Returns the rolling average frame time in milliseconds.
    [[nodiscard]] float GetAvgFrameTimeMs() const;

    /// Returns the total number of dropped frames (exceeded target) since init.
    [[nodiscard]] uint32_t GetDroppedFrameCount() const;

    /// Returns the recommended swap chain buffer count based on current timing.
    ///
    /// Only meaningful when `autoTuneBufferCount` is enabled. Otherwise returns
    /// the current buffer count.
    [[nodiscard]] uint32_t GetRecommendedBufferCount() const;

    /// Returns `true` if the recommended buffer count differs from the current.
    ///
    /// The caller should resize the swap chain when this returns `true`.
    [[nodiscard]] bool ShouldResizeSwapChain() const;

    /// Sets the current buffer count (call after swap chain resize).
    void SetCurrentBufferCount(uint32_t count);

    /// Returns true if the frame pacer has been initialized.
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

    /// Logs a summary of frame timing and buffer count state at Info level.
    void LogSummary() const;

private:
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = Clock::time_point;

    AdaptiveFramePacerConfig m_config;
    bool m_initialized = false;

    TimePoint m_frameBegin{};
    float m_lastFrameTimeMs = 0.0f;

    // Rolling history
    std::vector<float> m_frameTimeHistory;
    uint32_t m_historyIndex = 0;
    bool m_historyFull = false;

    uint32_t m_droppedFrames = 0;

    // Buffer count tuning
    uint32_t m_currentBufferCount = 2;
    uint32_t m_recommendedBufferCount = 2;
    uint32_t m_consecutiveAboveThreshold = 0;   ///< Frames above throughputThresholdMs.
    uint32_t m_consecutiveBelowThreshold = 0;   ///< Frames below latencyThresholdMs.
};

} // namespace uz
