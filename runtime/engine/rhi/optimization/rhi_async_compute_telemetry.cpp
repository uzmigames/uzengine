/// UzEngine Async Compute Overlap Telemetry — Implementation.
///
/// See rhi_async_compute_telemetry.h for design overview.

#include "rhi_async_compute_telemetry.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <cmath>
#include <numeric>

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

float AsyncComputeTelemetry::DurationMs(TimePoint start, TimePoint end) {
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return static_cast<float>(ns) / 1'000'000.0f;
}

float AsyncComputeTelemetry::ComputeOverlapMs(TimePoint s1, TimePoint e1,
                                               TimePoint s2, TimePoint e2) {
    // Overlap = max(0, min(e1,e2) - max(s1,s2))
    const auto overlapStart = std::max(s1, s2);
    const auto overlapEnd = std::min(e1, e2);
    if (overlapEnd <= overlapStart) {
        return 0.0f;
    }
    return DurationMs(overlapStart, overlapEnd);
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool AsyncComputeTelemetry::Init(const AsyncComputeTelemetryConfig& config) {
    if (m_initialized) {
        UZ_LOG(RHI, Warning, "AsyncComputeTelemetry::Init called while already initialized");
        return false;
    }

    m_config = config;
    m_overlapHistory.resize(config.historySize, 0.0f);
    m_historyIndex = 0;
    m_historyFull = false;
    m_initialized = true;

    UZ_LOG(RHI, Info, "AsyncComputeTelemetry initialized: historySize={}",
           config.historySize);
    return true;
}

void AsyncComputeTelemetry::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_overlapHistory.clear();
    m_lastGraphics = {};
    m_lastCompute = {};
    m_lastOverlap = 0.0f;
    m_historyIndex = 0;
    m_historyFull = false;
    m_graphicsActive = false;
    m_computeActive = false;
    m_initialized = false;

    UZ_LOG(RHI, Info, "AsyncComputeTelemetry shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame timing markers
// ─────────────────────────────────────────────────────────────────────────────

void AsyncComputeTelemetry::BeginGraphics() {
    m_graphicsBegin = Clock::now();
    m_graphicsActive = true;
}

void AsyncComputeTelemetry::EndGraphics() {
    if (!m_graphicsActive) {
        UZ_LOG(RHI, Warning, "AsyncComputeTelemetry::EndGraphics called without BeginGraphics");
        return;
    }
    m_graphicsEnd = Clock::now();
    m_graphicsActive = false;
}

void AsyncComputeTelemetry::BeginCompute() {
    m_computeBegin = Clock::now();
    m_computeActive = true;
}

void AsyncComputeTelemetry::EndCompute() {
    if (!m_computeActive) {
        UZ_LOG(RHI, Warning, "AsyncComputeTelemetry::EndCompute called without BeginCompute");
        return;
    }
    m_computeEnd = Clock::now();
    m_computeActive = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// End-of-frame processing
// ─────────────────────────────────────────────────────────────────────────────

void AsyncComputeTelemetry::EndFrame() {
    UZ_PROFILE_SCOPE("AsyncComputeTelemetry::EndFrame");

    if (!m_initialized) {
        return;
    }

    const float graphicsBusyMs = DurationMs(m_graphicsBegin, m_graphicsEnd);
    const float computeBusyMs = DurationMs(m_computeBegin, m_computeEnd);

    // Frame total is from earliest begin to latest end across both queues.
    const auto frameStart = std::min(m_graphicsBegin, m_computeBegin);
    const auto frameEnd = std::max(m_graphicsEnd, m_computeEnd);
    const float frameTotalMs = DurationMs(frameStart, frameEnd);

    // Idle time = frame total minus busy time for each queue.
    const float graphicsIdleMs = std::max(0.0f, frameTotalMs - graphicsBusyMs);
    const float computeIdleMs = std::max(0.0f, frameTotalMs - computeBusyMs);

    // Utilization per queue.
    auto computeUtil = [](float busy, float idle) -> float {
        const float total = busy + idle;
        return (total > 0.0f) ? std::clamp(busy / total, 0.0f, 1.0f) : 0.0f;
    };

    m_lastGraphics = {graphicsBusyMs, graphicsIdleMs, computeUtil(graphicsBusyMs, graphicsIdleMs)};
    m_lastCompute = {computeBusyMs, computeIdleMs, computeUtil(computeBusyMs, computeIdleMs)};

    // Overlap ratio: intersection of busy intervals / shorter busy interval.
    const float overlapMs = ComputeOverlapMs(m_graphicsBegin, m_graphicsEnd,
                                              m_computeBegin, m_computeEnd);
    const float shorterBusy = std::min(graphicsBusyMs, computeBusyMs);
    m_lastOverlap = (shorterBusy > 0.0f) ? std::clamp(overlapMs / shorterBusy, 0.0f, 1.0f) : 0.0f;

    // Update rolling history.
    if (!m_overlapHistory.empty()) {
        m_overlapHistory[m_historyIndex] = m_lastOverlap;
        m_historyIndex = (m_historyIndex + 1) % static_cast<uint32_t>(m_overlapHistory.size());
        if (m_historyIndex == 0) {
            m_historyFull = true;
        }
    }

    // Update stat counter (integer 0–100 scale).
    UZ_STAT_INCREMENT(RHI, AsyncComputeOverlap,
                      static_cast<int64_t>(m_lastOverlap * 100.0f) - UZ_STAT_GET(RHI, AsyncComputeOverlap));

    if (m_config.enableDetailedLogging) {
        UZ_LOG(RHI, Debug, "AsyncCompute: gfx={:.2f}ms compute={:.2f}ms overlap={:.1f}%",
               graphicsBusyMs, computeBusyMs, m_lastOverlap * 100.0f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

QueueTimingSnapshot AsyncComputeTelemetry::GetGraphicsStats() const {
    return m_lastGraphics;
}

QueueTimingSnapshot AsyncComputeTelemetry::GetComputeStats() const {
    return m_lastCompute;
}

float AsyncComputeTelemetry::GetOverlapRatio() const {
    return m_lastOverlap;
}

float AsyncComputeTelemetry::GetAvgOverlapRatio() const {
    if (m_overlapHistory.empty()) {
        return 0.0f;
    }

    const uint32_t count = m_historyFull
        ? static_cast<uint32_t>(m_overlapHistory.size())
        : m_historyIndex;

    if (count == 0) {
        return 0.0f;
    }

    const float sum = std::accumulate(m_overlapHistory.begin(),
                                       m_overlapHistory.begin() + count,
                                       0.0f);
    return sum / static_cast<float>(count);
}

void AsyncComputeTelemetry::LogSummary() const {
    UZ_LOG(RHI, Info,
           "AsyncComputeTelemetry: gfx[busy={:.2f}ms util={:.0f}%] "
           "compute[busy={:.2f}ms util={:.0f}%] overlap={:.1f}% avg={:.1f}%",
           m_lastGraphics.busyMs, m_lastGraphics.utilization * 100.0f,
           m_lastCompute.busyMs, m_lastCompute.utilization * 100.0f,
           m_lastOverlap * 100.0f, GetAvgOverlapRatio() * 100.0f);
}

} // namespace uz
