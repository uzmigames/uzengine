// frame_pacer.cpp -- Frame pacing, VSync mode selection, and frame timing statistics.

#include "engine/core/frame_pacer.h"
#include "engine/core/platform.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstring>
#include <thread>

#ifdef UZ_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>    // Sleep
    #include <intrin.h>     // _mm_pause
#else
    #include <time.h>       // nanosleep
    #include <immintrin.h>  // _mm_pause (x86 Linux/Mac)
#endif

namespace uz {

// ============================================================================
// Construction
// ============================================================================

FramePacer::FramePacer() noexcept
    : time_source_(platform::get_time_seconds)
{
}

FramePacer::FramePacer(TimeSourceFn time_source) noexcept
    : time_source_(time_source)
{
    assert(time_source_ && "TimeSourceFn must not be null");
}

// ============================================================================
// VSync mode
// ============================================================================

void FramePacer::set_vsync_mode(VSyncMode mode) noexcept {
    vsync_mode_ = mode;
}

VSyncMode FramePacer::vsync_mode() const noexcept {
    return vsync_mode_;
}

// ============================================================================
// Frame rate limiter
// ============================================================================

void FramePacer::set_target_fps(float fps) noexcept {
    if (fps <= 0.0f) {
        target_fps_ = 0.0f;
        return;
    }
    // Clamp to valid range.
    target_fps_ = std::clamp(fps, kMinTargetFps, kMaxTargetFps);
}

float FramePacer::target_fps() const noexcept {
    return target_fps_;
}

bool FramePacer::is_limiter_active() const noexcept {
    if (target_fps_ <= 0.0f) {
        return false;
    }
    // The limiter only applies in Off and Adaptive modes.
    // In On and MailBox modes, the display timing controls pacing.
    return vsync_mode_ == VSyncMode::Off || vsync_mode_ == VSyncMode::Adaptive;
}

// ============================================================================
// Frame lifecycle
// ============================================================================

void FramePacer::begin_frame() noexcept {
    frame_start_sec_ = time_source_();
    frame_active_ = true;
}

void FramePacer::end_frame() noexcept {
    if (!frame_active_) {
        return;
    }
    frame_active_ = false;

    // Apply frame limiter if active.
    if (is_limiter_active()) {
        const double target_time_sec = 1.0 / static_cast<double>(target_fps_);
        const double target_end = frame_start_sec_ + target_time_sec;
        const double now = time_source_();
        const double remaining = target_end - now;

        if (remaining > kSpinWaitThresholdSec) {
            // Sleep for the bulk of the wait, leaving kSpinWaitThresholdSec for spin.
            os_sleep(remaining - kSpinWaitThresholdSec);
        }

        if (remaining > 0.0) {
            // Spin-wait for the final precision portion.
            spin_wait_until(target_end);
        }
    }

    // Record frame time.
    const double end_sec = time_source_();
    const auto frame_ms = static_cast<float>((end_sec - frame_start_sec_) * 1000.0);
    record_frame_time(frame_ms);
}

// ============================================================================
// Statistics
// ============================================================================

FrameStats FramePacer::stats() const noexcept {
    FrameStats s{};
    if (history_count_ == 0) {
        return s;
    }

    const uint32_t n = std::min(history_count_, kHistorySize);
    s.count = n;

    // Copy frame times into a temporary buffer for sorting (percentiles).
    // Stack allocation -- kHistorySize is 256, so 1KB on stack.
    float sorted[kHistorySize];
    float sum = 0.0f;
    float lo  = 1e30f;
    float hi  = -1e30f;

    for (uint32_t i = 0; i < n; ++i) {
        // Read from the ring buffer. When count < kHistorySize, entries are
        // at indices [0..count-1]. When count >= kHistorySize, the ring has
        // wrapped, and we read relative to head_.
        uint32_t idx;
        if (history_count_ <= kHistorySize) {
            idx = i;
        } else {
            idx = (history_head_ + i) % kHistorySize;
        }
        const float v = history_[idx];
        sorted[i] = v;
        sum += v;
        lo = std::min(lo, v);
        hi = std::max(hi, v);
    }

    s.avg_ms = sum / static_cast<float>(n);
    s.min_ms = lo;
    s.max_ms = hi;

    // Sort for percentiles.
    std::sort(sorted, sorted + n);

    // Percentile helper: index = floor((percentile / 100) * (n - 1))
    auto percentile = [&](float p) -> float {
        if (n == 1) return sorted[0];
        const float rank = (p / 100.0f) * static_cast<float>(n - 1);
        const auto lo_idx = static_cast<uint32_t>(rank);
        const auto hi_idx = std::min(lo_idx + 1, n - 1);
        const float frac = rank - static_cast<float>(lo_idx);
        return sorted[lo_idx] + frac * (sorted[hi_idx] - sorted[lo_idx]);
    };

    s.p50_ms = percentile(50.0f);
    s.p90_ms = percentile(90.0f);
    s.p95_ms = percentile(95.0f);
    s.p99_ms = percentile(99.0f);

    // FPS from average.
    if (s.avg_ms > 0.0f) {
        s.fps = 1000.0f / s.avg_ms;
    }

    return s;
}

void FramePacer::reset_stats() noexcept {
    std::memset(history_, 0, sizeof(history_));
    history_head_  = 0;
    history_count_ = 0;
}

float FramePacer::last_frame_ms() const noexcept {
    if (history_count_ == 0) {
        return 0.0f;
    }
    // Last written entry is at (head_ - 1) mod kHistorySize.
    const uint32_t idx = (history_head_ + kHistorySize - 1) % kHistorySize;
    return history_[idx];
}

uint32_t FramePacer::frame_count() const noexcept {
    return history_count_;
}

// ============================================================================
// Internal helpers
// ============================================================================

void FramePacer::os_sleep(double seconds) noexcept {
    if (seconds <= 0.0) {
        return;
    }

#ifdef UZ_PLATFORM_WINDOWS
    // Windows Sleep has ~1ms granularity (with timeBeginPeriod). We sleep
    // for 1ms less than requested to avoid overshooting.
    const auto ms = static_cast<DWORD>(seconds * 1000.0);
    if (ms > 0) {
        ::Sleep(ms);
    }
#else
    // POSIX nanosleep.
    const auto sec  = static_cast<time_t>(seconds);
    const auto nsec = static_cast<long>((seconds - static_cast<double>(sec)) * 1e9);
    struct timespec ts = { sec, nsec };
    while (nanosleep(&ts, &ts) == -1) {
        // Retry on EINTR.
    }
#endif
}

void FramePacer::spin_wait_until(double target_time) noexcept {
    while (time_source_() < target_time) {
#if defined(_MSC_VER) || defined(__SSE2__)
        _mm_pause();
#else
        // Fallback: yield to avoid burning the core on non-x86.
        std::this_thread::yield();
#endif
    }
}

void FramePacer::record_frame_time(float ms) noexcept {
    history_[history_head_] = ms;
    history_head_ = (history_head_ + 1) % kHistorySize;
    ++history_count_;
}

} // namespace uz
