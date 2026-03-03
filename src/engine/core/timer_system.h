#pragma once

// timer_system.h -- Frame-driven timer and scheduler.
//
// All timers are driven by explicit tick(delta_seconds) calls -- no threads.
// Handles: one-shot after N seconds, repeating every N seconds, countdown.
//
// Design decisions:
//   - Linear scan of timers_ is intentional: kMaxTimers=1024 and the vector
//     is cache-friendly. A hash map would be slower for typical timer counts.
//   - Finished/Cancelled timers are erased at the END of tick() to allow
//     callbacks to safely cancel/schedule timers without invalidating iterators.
//   - remaining accumulates deficit (remaining += interval, not = interval)
//     so that multiple fires per tick are handled correctly for fast deltas.

#include <cstdint>
#include <functional>
#include <vector>

namespace uz {

using TimerHandle = uint32_t;
inline constexpr TimerHandle kTimerHandleInvalid = 0;

using TimerCallback = std::function<void()>;

enum class TimerState : uint8_t {
    Active    = 0,
    Paused    = 1,
    Finished  = 2,
    Cancelled = 3,
};

struct TimerInfo {
    TimerHandle   handle;
    float         interval;    // Seconds between fires
    float         remaining;   // Seconds until next fire
    float         elapsed;     // Total elapsed since creation
    uint32_t      fire_count;  // How many times it has fired
    uint32_t      max_fires;   // 0 = infinite repeat; N = fire N times then cancel
    TimerState    state;
    TimerCallback callback;
};

class TimerSystem {
public:
    static constexpr uint32_t kMaxTimers = 1024;

    TimerSystem();

    // Advance all active timers by delta_seconds. Fires callbacks for any
    // that expire. Removes timers that have reached max_fires.
    void tick(float delta_seconds);

    // Schedule a one-shot timer. Calls callback once after `delay` seconds.
    // Returns a handle to cancel it before it fires.
    [[nodiscard]] TimerHandle schedule_after(float delay, TimerCallback callback);

    // Schedule a repeating timer. Calls callback every `interval` seconds, forever.
    [[nodiscard]] TimerHandle schedule_every(float interval, TimerCallback callback);

    // Schedule a timer that fires `count` times then cancels itself.
    [[nodiscard]] TimerHandle schedule_repeat(float interval, uint32_t count,
                                              TimerCallback callback);

    // Cancel a timer. No-op for invalid handles or already-finished timers.
    void cancel(TimerHandle handle);

    // Pause / resume a timer.
    void pause(TimerHandle handle);
    void resume(TimerHandle handle);

    // Query timer state. Returns TimerState::Cancelled for invalid handles.
    [[nodiscard]] TimerState state(TimerHandle handle) const;

    // Query time remaining until next fire. Returns 0.0f for invalid/finished.
    [[nodiscard]] float remaining(TimerHandle handle) const;

    // Query how many times a timer has fired. Returns 0 for invalid handles.
    [[nodiscard]] uint32_t fire_count(TimerHandle handle) const;

    // Remove all timers.
    void clear();

    // Number of active (non-finished, non-cancelled) timers.
    [[nodiscard]] uint32_t active_count() const;

    // Total allocated timer slots (for debugging).
    [[nodiscard]] uint32_t total_count() const;

private:
    std::vector<TimerInfo> timers_;
    TimerHandle next_handle_ = 1; // Start at 1; 0 = invalid

    TimerInfo*       find(TimerHandle handle);
    const TimerInfo* find(TimerHandle handle) const;
};

} // namespace uz
