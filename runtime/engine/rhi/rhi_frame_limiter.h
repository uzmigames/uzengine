#pragma once

/// Frame Limiter — hybrid sleep/spin-wait frame pacing for consistent frame times.
///
/// Uses a two-phase approach inspired by UE5's frame limiter:
/// 1. Sleep phase: `std::this_thread::sleep_for` for the bulk of remaining time (minus 1ms margin)
/// 2. Spin phase: `std::this_thread::yield()` busy-loop for sub-millisecond precision
///
/// On Windows, calls `timeBeginPeriod(1)` at construction and `timeEndPeriod(1)` at destruction
/// to raise the system timer resolution from ~15.6ms to ~1ms, making sleep_for accurate.
///
/// # Usage
/// ```cpp
/// FrameLimiter limiter;  // default 500 FPS cap
/// while (running) {
///     // ... render frame ...
///     swapChain->Present(vsync);
///     limiter.WaitForTargetFrameTime();  // blocks until frame budget elapsed
/// }
/// ```
///
/// # Thread Safety
/// NOT thread-safe. Intended to be used from a single render thread.
/// `SetMaxFPS()` may be called from any thread if externally synchronized.

#include <chrono>
#include <cstdint>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <timeapi.h>
#endif

#include "log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// FrameLimiter
// ─────────────────────────────────────────────────────────────────────────────

/// Hybrid sleep/spin-wait frame limiter for consistent frame pacing.
///
/// Default cap is 500 FPS (2ms per frame). Call `SetMaxFPS(0)` to disable.
/// The limiter tracks the end of the previous `WaitForTargetFrameTime()` call
/// as the start of the next frame, so no separate `BeginFrame()` is needed.
class FrameLimiter {
public:
    /// Constructs the frame limiter and raises system timer resolution on Windows.
    ///
    /// # Arguments
    /// * `maxFps` — Maximum frames per second. 0 disables the limiter.
    explicit FrameLimiter(uint32_t maxFps = kDefaultMaxFps)
        : m_maxFps(maxFps) {
#ifdef _WIN32
        // Raise Windows timer resolution to ~1ms for accurate sleep_for
        timeBeginPeriod(1);
#endif
        UpdateTargetFrameTime();
        m_lastFrameEnd = Clock::now();
    }

    ~FrameLimiter() {
#ifdef _WIN32
        timeEndPeriod(1);
#endif
    }

    // Non-copyable, non-movable — tied to timeBeginPeriod/timeEndPeriod lifetime
    FrameLimiter(const FrameLimiter&) = delete;
    FrameLimiter& operator=(const FrameLimiter&) = delete;
    FrameLimiter(FrameLimiter&&) = delete;
    FrameLimiter& operator=(FrameLimiter&&) = delete;

    /// Changes the FPS cap at runtime. 0 disables the limiter (unlimited FPS).
    ///
    /// # Arguments
    /// * `fps` — New maximum FPS. 0 = unlimited.
    void SetMaxFPS(uint32_t fps) {
        m_maxFps = fps;
        UpdateTargetFrameTime();
    }

    /// Returns the current FPS cap. 0 means unlimited.
    [[nodiscard]] uint32_t GetMaxFPS() const noexcept { return m_maxFps; }

    /// Blocks until the target frame time has elapsed since the last call.
    ///
    /// Uses hybrid sleep/spin-wait: sleeps for (remaining - kSleepMargin), then
    /// spin-waits with `std::this_thread::yield()` for the remainder.
    /// If the frame already took longer than the budget, returns immediately.
    void WaitForTargetFrameTime() {
        if (m_maxFps == 0) {
            m_lastFrameEnd = Clock::now();
            return;
        }

        const auto targetEnd = m_lastFrameEnd + m_targetFrameTime;
        const auto now = Clock::now();

        if (now < targetEnd) {
            // Phase 1: Sleep for the bulk of remaining time (minus margin for spin precision)
            const auto remaining = targetEnd - now;
            if (remaining > kSleepMargin) {
                std::this_thread::sleep_for(remaining - kSleepMargin);
            }

            // Phase 2: Spin-wait for sub-millisecond precision
            while (Clock::now() < targetEnd) {
                std::this_thread::yield();
            }
        }

        m_lastFrameEnd = Clock::now();
    }

    /// Logs the current frame limiter configuration. Call once after initialization.
    void LogConfig() const {
        if (m_maxFps == 0) {
            UZ_LOG(RHI, Info, "Frame limiter: disabled (unlimited FPS)");
        } else {
            const double frameTimeMs = std::chrono::duration<double, std::milli>(m_targetFrameTime).count();
            UZ_LOG(RHI, Info, "Frame limiter: {} FPS cap ({:.2f}ms/frame)", m_maxFps, frameTimeMs);
        }
    }

private:
    using Clock = std::chrono::high_resolution_clock;
    using Duration = Clock::duration;
    using TimePoint = Clock::time_point;

    /// Default FPS cap — 500 FPS (2ms per frame).
    static constexpr uint32_t kDefaultMaxFps = 500;

    /// Safety margin subtracted from sleep duration to avoid oversleeping.
    /// The spin-wait phase covers this last millisecond with high precision.
    static constexpr Duration kSleepMargin = std::chrono::microseconds(1000);

    /// Recalculates target frame duration from current m_maxFps.
    void UpdateTargetFrameTime() {
        if (m_maxFps > 0) {
            m_targetFrameTime = std::chrono::duration_cast<Duration>(
                std::chrono::duration<double>(1.0 / static_cast<double>(m_maxFps)));
        } else {
            m_targetFrameTime = Duration::zero();
        }
    }

    uint32_t  m_maxFps = kDefaultMaxFps;
    Duration  m_targetFrameTime = Duration::zero();
    TimePoint m_lastFrameEnd = {};
};

} // namespace uz
