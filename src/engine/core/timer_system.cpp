// timer_system.cpp -- Implementation of the frame-driven timer system.

#include "engine/core/timer_system.h"

#include <algorithm>
#include <cassert>

namespace uz {

TimerSystem::TimerSystem() {
    timers_.reserve(64); // Reasonable initial capacity to avoid early reallocs
}

void TimerSystem::tick(float delta_seconds) {
    // Clamp negative deltas to zero -- negative time makes no physical sense
    // and would cause timers to fire backwards.
    if (delta_seconds < 0.0f) {
        delta_seconds = 0.0f;
    }

    // Phase 1: advance all active timers and fire callbacks.
    // We iterate by index because callbacks may push new timers (growing the
    // vector), but we only process timers that existed at the start of tick().
    const auto count = timers_.size();
    for (size_t i = 0; i < count; ++i) {
        auto& timer = timers_[i];
        if (timer.state != TimerState::Active) {
            continue;
        }

        timer.elapsed   += delta_seconds;
        timer.remaining -= delta_seconds;

        while (timer.remaining <= 0.0f) {
            // Fire the callback. The callback may cancel this timer or schedule
            // new ones, so we re-check state after each fire.
            if (timer.callback) {
                timer.callback();
            }

            timer.fire_count++;

            // Check if this timer has been cancelled inside its own callback.
            if (timer.state != TimerState::Active) {
                break;
            }

            if (timer.max_fires > 0 && timer.fire_count >= timer.max_fires) {
                timer.state = TimerState::Finished;
                break;
            }

            // Accumulate remainder rather than resetting to interval, so that
            // multiple fires are correctly handled when delta >> interval.
            timer.remaining += timer.interval;
        }
    }

    // Phase 2: erase finished and cancelled timers.
    // Using erase-remove idiom to compact in one pass.
    timers_.erase(
        std::remove_if(timers_.begin(), timers_.end(),
                       [](const TimerInfo& t) {
                           return t.state == TimerState::Finished ||
                                  t.state == TimerState::Cancelled;
                       }),
        timers_.end());
}

TimerHandle TimerSystem::schedule_after(float delay, TimerCallback callback) {
    assert(timers_.size() < kMaxTimers && "Timer pool exhausted");

    const TimerHandle handle = next_handle_++;
    timers_.push_back(TimerInfo{
        .handle     = handle,
        .interval   = delay,
        .remaining  = delay,
        .elapsed    = 0.0f,
        .fire_count = 0,
        .max_fires  = 1,
        .state      = TimerState::Active,
        .callback   = std::move(callback),
    });
    return handle;
}

TimerHandle TimerSystem::schedule_every(float interval, TimerCallback callback) {
    assert(timers_.size() < kMaxTimers && "Timer pool exhausted");

    const TimerHandle handle = next_handle_++;
    timers_.push_back(TimerInfo{
        .handle     = handle,
        .interval   = interval,
        .remaining  = interval,
        .elapsed    = 0.0f,
        .fire_count = 0,
        .max_fires  = 0, // 0 = infinite
        .state      = TimerState::Active,
        .callback   = std::move(callback),
    });
    return handle;
}

TimerHandle TimerSystem::schedule_repeat(float interval, uint32_t count,
                                         TimerCallback callback) {
    assert(timers_.size() < kMaxTimers && "Timer pool exhausted");

    const TimerHandle handle = next_handle_++;
    timers_.push_back(TimerInfo{
        .handle     = handle,
        .interval   = interval,
        .remaining  = interval,
        .elapsed    = 0.0f,
        .fire_count = 0,
        .max_fires  = count,
        .state      = TimerState::Active,
        .callback   = std::move(callback),
    });
    return handle;
}

void TimerSystem::cancel(TimerHandle handle) {
    if (handle == kTimerHandleInvalid) return;

    TimerInfo* timer = find(handle);
    if (!timer) return;

    // Only cancel if still active or paused -- finished timers are already done.
    if (timer->state == TimerState::Active || timer->state == TimerState::Paused) {
        timer->state = TimerState::Cancelled;
    }
}

void TimerSystem::pause(TimerHandle handle) {
    if (handle == kTimerHandleInvalid) return;

    TimerInfo* timer = find(handle);
    if (!timer) return;

    if (timer->state == TimerState::Active) {
        timer->state = TimerState::Paused;
    }
}

void TimerSystem::resume(TimerHandle handle) {
    if (handle == kTimerHandleInvalid) return;

    TimerInfo* timer = find(handle);
    if (!timer) return;

    if (timer->state == TimerState::Paused) {
        timer->state = TimerState::Active;
    }
}

TimerState TimerSystem::state(TimerHandle handle) const {
    if (handle == kTimerHandleInvalid) return TimerState::Cancelled;

    const TimerInfo* timer = find(handle);
    if (!timer) return TimerState::Cancelled;

    return timer->state;
}

float TimerSystem::remaining(TimerHandle handle) const {
    if (handle == kTimerHandleInvalid) return 0.0f;

    const TimerInfo* timer = find(handle);
    if (!timer) return 0.0f;

    if (timer->state == TimerState::Finished ||
        timer->state == TimerState::Cancelled) {
        return 0.0f;
    }

    return timer->remaining;
}

uint32_t TimerSystem::fire_count(TimerHandle handle) const {
    if (handle == kTimerHandleInvalid) return 0;

    const TimerInfo* timer = find(handle);
    if (!timer) return 0;

    return timer->fire_count;
}

void TimerSystem::clear() {
    timers_.clear();
}

uint32_t TimerSystem::active_count() const {
    uint32_t count = 0;
    for (const auto& timer : timers_) {
        if (timer.state == TimerState::Active ||
            timer.state == TimerState::Paused) {
            ++count;
        }
    }
    return count;
}

uint32_t TimerSystem::total_count() const {
    return static_cast<uint32_t>(timers_.size());
}

TimerInfo* TimerSystem::find(TimerHandle handle) {
    for (auto& timer : timers_) {
        if (timer.handle == handle) {
            return &timer;
        }
    }
    return nullptr;
}

const TimerInfo* TimerSystem::find(TimerHandle handle) const {
    for (const auto& timer : timers_) {
        if (timer.handle == handle) {
            return &timer;
        }
    }
    return nullptr;
}

} // namespace uz
