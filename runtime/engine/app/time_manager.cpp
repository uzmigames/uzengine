/// UzEngine Time Manager — Implementation

#include "time_manager.h"
#include "subsystem_registry.h"

#include "../assertions/engine_assert.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// ISubsystem
// ─────────────────────────────────────────────────────────────────────────────

bool TimeManager::Init() {
    deltaTime_ = 0.0f;
    rawDeltaTime_ = 0.0f;
    totalTime_ = 0.0f;
    fixedDeltaTime_ = kDefaultFixedDeltaTime;
    fixedAccumulator_ = 0.0f;
    timeScale_ = 1.0f;
    frameCount_ = 0;
    lastFrameTime_ = {};
    firstFrame_ = true;

    UZ_LOG(Lifecycle, Info, "TimeManager initialized: fixedDt={:.4f}s, timeScale={:.2f}",
           fixedDeltaTime_, timeScale_);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Frame timing
// ─────────────────────────────────────────────────────────────────────────────

void TimeManager::BeginFrame() {
    const TimePoint now = Clock::now();

    if (firstFrame_) {
        // First frame has no previous timestamp — report zero delta.
        lastFrameTime_ = now;
        firstFrame_ = false;
        rawDeltaTime_ = 0.0f;
        deltaTime_ = 0.0f;
        return;
    }

    const auto elapsed = std::chrono::duration<float>(now - lastFrameTime_);
    lastFrameTime_ = now;

    rawDeltaTime_ = elapsed.count();

    // Clamp to prevent spiral-of-death after long hitches.
    const float clampedDelta = std::min(rawDeltaTime_, kMaxDeltaTime);
    deltaTime_ = clampedDelta * timeScale_;

    totalTime_ += deltaTime_;
    fixedAccumulator_ += deltaTime_;
    ++frameCount_;
}

int TimeManager::ConsumeFixedTicks() {
    int ticks = 0;
    while (fixedAccumulator_ >= fixedDeltaTime_) {
        fixedAccumulator_ -= fixedDeltaTime_;
        ++ticks;
    }
    return ticks;
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void TimeManager::SetFixedDeltaTime(float dt) {
    UZ_CHECK(dt > 0.0f);
    fixedDeltaTime_ = dt;
}

void TimeManager::SetTimeScale(float scale) {
    timeScale_ = std::clamp(scale, 0.0f, 10.0f);
}

} // namespace uz
