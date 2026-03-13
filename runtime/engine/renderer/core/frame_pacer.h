#pragma once

/// UzEngine Frame Pacer — Measures present-to-display latency and manages VRR/tearing.
///
/// Tracks frame timing, measures present latency, and provides VRR (Variable
/// Refresh Rate) support flags for FreeSync/G-Sync displays.
///
/// # Thread Safety
/// NOT thread-safe. Render thread only.

#include <chrono>
#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;

/// VRR (Variable Refresh Rate) mode.
enum class VRRMode : uint8_t {
    Off,          ///< Fixed refresh, no tearing.
    AllowTearing, ///< Allow tearing for lowest latency (DXGI_PRESENT_ALLOW_TEARING).
    Adaptive,     ///< VRR via FreeSync/G-Sync (no tearing, variable refresh).
};

/// Frame timing statistics snapshot.
///
/// Updated each frame by `FramePacer::EndFrame()`. All times are in milliseconds.
struct FrameTimingStats {
    float cpuFrameTimeMs   = 0.0f; ///< CPU frame time (ms).
    float gpuFrameTimeMs   = 0.0f; ///< GPU frame time (ms, if available).
    float presentLatencyMs = 0.0f; ///< Present-to-display latency estimate (ms).
    float avgFrameTimeMs   = 0.0f; ///< Rolling average frame time (ms).
    float fps              = 0.0f; ///< Current FPS (1000 / avgFrameTimeMs).
    uint32_t droppedFrames = 0;    ///< Frames that missed target (cumulative).
    uint64_t totalFrames   = 0;    ///< Total frames presented.
};

/// Configuration for the frame pacer.
struct FramePacerConfig {
    float targetFrameTimeMs = 16.667f;    ///< Target frame time (60 FPS default).
    VRRMode vrrMode = VRRMode::Off;       ///< VRR/tearing mode.
    uint32_t swapInterval = 1;            ///< Swap interval (0=no vsync, 1=vsync, 2=half rate).
    uint32_t historySize = 120;           ///< Number of frames for rolling average.
    bool enableLatencyMeasurement = true; ///< Measure present-to-display latency.
};

/// Manages frame pacing, timing, and VRR support.
///
/// # Usage
/// ```cpp
/// FramePacer pacer;
/// pacer.Init(device, config);
///
/// // In render loop:
/// pacer.BeginFrame();
/// // ... render ...
/// swapChain->Present(pacer.GetSyncInterval(), pacer.GetPresentFlags());
/// pacer.EndFrame();
/// ```
///
/// # Ownership
/// Does NOT own the `IRHIDevice` pointer — caller must ensure the device
/// outlives the pacer.
class FramePacer {
public:
    FramePacer() = default;
    ~FramePacer() = default;

    FramePacer(const FramePacer&) = delete;
    FramePacer& operator=(const FramePacer&) = delete;
    FramePacer(FramePacer&&) = delete;
    FramePacer& operator=(FramePacer&&) = delete;

    /// Initializes the frame pacer with the given device and configuration.
    ///
    /// # Parameters
    /// - `device` — Non-null RHI device pointer. Used for future VRR detection.
    /// - `config` — Pacing configuration. Defaults to 60 FPS, no VRR.
    ///
    /// # Returns
    /// `true` on success, `false` if device is null or already initialized.
    bool Init(IRHIDevice* device, const FramePacerConfig& config = {});

    /// Shuts down the frame pacer and releases internal state.
    void Shutdown();

    /// Called at the start of each frame to record the begin timestamp.
    void BeginFrame();

    /// Called after Present() to record end timestamp and update stats.
    void EndFrame();

    /// Returns current timing statistics (read-only snapshot).
    [[nodiscard]] const FrameTimingStats& GetStats() const { return m_stats; }

    /// Returns the mutable config for runtime tweaking.
    FramePacerConfig& GetConfig() { return m_config; }

    /// Returns the immutable config.
    [[nodiscard]] const FramePacerConfig& GetConfig() const { return m_config; }

    /// Returns the DXGI present flags based on current VRR mode.
    ///
    /// # Returns
    /// `DXGI_PRESENT_ALLOW_TEARING` (0x200) if VRR AllowTearing mode is active
    /// and VRR is supported. Otherwise 0.
    [[nodiscard]] uint32_t GetPresentFlags() const;

    /// Returns the sync interval for Present().
    ///
    /// # Returns
    /// 0 if VRR AllowTearing is active, otherwise `config.swapInterval`.
    [[nodiscard]] uint32_t GetSyncInterval() const;

    /// Returns true if VRR/tearing is supported by the display adapter.
    [[nodiscard]] bool IsVRRSupported() const { return m_vrrSupported; }

    /// Logs a summary of timing statistics via UZ_LOG(Renderer, Info).
    void LogSummary() const;

    /// Returns true if Init() has been called successfully.
    [[nodiscard]] bool IsInitialized() const { return m_initialized; }

private:
    /// Updates the rolling average with a new frame time sample.
    void UpdateRollingAverage(float frameTimeMs);

    IRHIDevice* m_device = nullptr;
    FramePacerConfig m_config;
    FrameTimingStats m_stats;
    bool m_initialized = false;
    bool m_vrrSupported = false;

    std::chrono::high_resolution_clock::time_point m_frameStart;
    std::vector<float> m_frameTimeHistory;
    uint32_t m_historyIndex = 0;
};

} // namespace uz
