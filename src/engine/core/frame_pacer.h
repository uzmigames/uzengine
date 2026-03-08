#pragma once

// frame_pacer.h -- Frame pacing, VSync mode selection, and frame timing statistics.
//
// FramePacer is a standalone, backend-agnostic module that controls when a frame
// should be presented. The renderer queries it for:
//   - Which VSync mode is active (determines Present() parameters)
//   - Frame rate limiting (sleep + spin-wait to hit a target FPS)
//   - Frame timing statistics (avg, min, max, percentiles)
//
// Design decisions:
//   - No DX12/Vulkan/Metal headers -- this is pure C++ logic.
//   - Time source is injectable (function pointer) for deterministic testing.
//   - Frame time history uses a fixed-size ring buffer (no heap allocation per frame).
//   - Statistics are computed on demand from the ring buffer, not incrementally,
//     because the ring is small (256 entries) and cache-friendly.

#include <cstdint>

namespace uz {

// ============================================================================
// VSyncMode -- maps to different Present() strategies in the renderer.
// ============================================================================

enum class VSyncMode : uint8_t {
    Off      = 0,  // Uncapped; Present(0, ALLOW_TEARING) or Present(0, 0)
    On       = 1,  // VSync ON; WaitForVBlank + Present(1, 0)
    Adaptive = 2,  // Mailbox; Present(0, 0) with frame limiter
    MailBox  = 3,  // VRR; frame latency waitable + Present(0, ALLOW_TEARING)
};

// ============================================================================
// FrameStats -- snapshot of frame timing statistics.
// ============================================================================

struct FrameStats {
    float avg_ms    = 0.0f;  // Average frame time in milliseconds
    float min_ms    = 0.0f;  // Minimum frame time in milliseconds
    float max_ms    = 0.0f;  // Maximum frame time in milliseconds
    float p50_ms    = 0.0f;  // 50th percentile (median)
    float p90_ms    = 0.0f;  // 90th percentile
    float p95_ms    = 0.0f;  // 95th percentile
    float p99_ms    = 0.0f;  // 99th percentile
    float fps       = 0.0f;  // Frames per second (1000 / avg_ms)
    uint32_t count  = 0;     // Number of frames in the sample window
};

// ============================================================================
// TimeSourceFn -- injectable time source for testing.
// ============================================================================

// Returns elapsed time in seconds (monotonic). Same contract as
// uz::platform::get_time_seconds().
using TimeSourceFn = double(*)();

// ============================================================================
// FramePacer -- frame pacing controller.
// ============================================================================

class FramePacer {
public:
    // Ring buffer size for frame time history. Power of 2 for fast modulo.
    static constexpr uint32_t kHistorySize = 256;

    // Maximum target FPS. Prevents nonsensical values.
    static constexpr float kMaxTargetFps = 1000.0f;

    // Minimum target FPS. Below this, the limiter is effectively disabled.
    static constexpr float kMinTargetFps = 1.0f;

    // Spin-wait threshold in seconds. We sleep until this much time remains,
    // then spin-wait for precision.
    static constexpr double kSpinWaitThresholdSec = 0.002; // 2ms

    // -- Construction --------------------------------------------------------

    // Construct with default platform time source.
    FramePacer() noexcept;

    // Construct with an injectable time source (for testing).
    explicit FramePacer(TimeSourceFn time_source) noexcept;

    // -- VSync mode ----------------------------------------------------------

    // Set the active VSync mode.
    void set_vsync_mode(VSyncMode mode) noexcept;

    // Get the active VSync mode.
    [[nodiscard]] VSyncMode vsync_mode() const noexcept;

    // -- Frame rate limiter --------------------------------------------------

    // Set the target FPS. 0 = unlimited. Clamped to [kMinTargetFps, kMaxTargetFps]
    // when non-zero.
    void set_target_fps(float fps) noexcept;

    // Get the current target FPS (0 = unlimited).
    [[nodiscard]] float target_fps() const noexcept;

    // Returns true if the frame limiter is active for the current vsync mode.
    // The limiter only applies in Off and Adaptive modes.
    [[nodiscard]] bool is_limiter_active() const noexcept;

    // -- Frame lifecycle -----------------------------------------------------

    // Call at the start of each frame. Records the frame start time.
    void begin_frame() noexcept;

    // Call after all rendering work is submitted, before Present().
    // If the frame limiter is active, this blocks (sleep + spin-wait) until
    // the target frame time has elapsed. Records the frame time into history.
    void end_frame() noexcept;

    // -- Statistics ----------------------------------------------------------

    // Compute frame timing statistics from the history ring buffer.
    // Returns zeroed stats if no frames have been recorded.
    [[nodiscard]] FrameStats stats() const noexcept;

    // Reset the frame time history and statistics.
    void reset_stats() noexcept;

    // Get the last frame time in milliseconds. Returns 0 if no frames recorded.
    [[nodiscard]] float last_frame_ms() const noexcept;

    // Get the number of frames recorded in the history buffer.
    [[nodiscard]] uint32_t frame_count() const noexcept;

private:
    // Injectable time source.
    TimeSourceFn time_source_;

    // Current settings.
    VSyncMode vsync_mode_  = VSyncMode::On;
    float     target_fps_  = 0.0f;  // 0 = unlimited

    // Frame timing state.
    double frame_start_sec_ = 0.0;
    bool   frame_active_    = false;

    // Ring buffer for frame times (in milliseconds).
    float    history_[kHistorySize] = {};
    uint32_t history_head_  = 0;  // Next write index
    uint32_t history_count_ = 0;  // Total frames recorded (saturates at kHistorySize)

    // -- Internal helpers ----------------------------------------------------

    // Sleep for approximately `seconds` using OS sleep. May overshoot.
    void os_sleep(double seconds) noexcept;

    // Spin-wait until `target_time` (in seconds from time source epoch).
    void spin_wait_until(double target_time) noexcept;

    // Record a frame time (in milliseconds) into the ring buffer.
    void record_frame_time(float ms) noexcept;
};

} // namespace uz
