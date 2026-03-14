/// UzEngine Frame Pacer — Implementation.
///
/// See rhi_frame_pacer.h for design overview.

#include "rhi_frame_pacer.h"

#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <numeric>

UZ_DECLARE_LOG_CATEGORY(RHI);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool FramePacer::Init(const FramePacerConfig& config) {
    if (m_initialized) {
        UZ_LOG(RHI, Warning, "FramePacer::Init called while already initialized");
        return false;
    }

    if (config.minSwapChainBuffers > config.maxSwapChainBuffers) {
        UZ_LOG(RHI, Error, "FramePacer::Init: minSwapChainBuffers ({}) > maxSwapChainBuffers ({})",
               config.minSwapChainBuffers, config.maxSwapChainBuffers);
        return false;
    }

    if (config.latencyThresholdMs >= config.throughputThresholdMs) {
        UZ_LOG(RHI, Error, "FramePacer::Init: latencyThresholdMs ({:.1f}) must be < "
               "throughputThresholdMs ({:.1f})",
               config.latencyThresholdMs, config.throughputThresholdMs);
        return false;
    }

    if (config.historySize == 0) {
        UZ_LOG(RHI, Error, "FramePacer::Init: historySize must be > 0");
        return false;
    }

    m_config = config;
    m_frameTimeHistory.resize(config.historySize, 0.0f);
    m_historyIndex = 0;
    m_historyFull = false;
    m_droppedFrames = 0;
    m_currentBufferCount = config.minSwapChainBuffers;
    m_recommendedBufferCount = config.minSwapChainBuffers;
    m_consecutiveAboveThreshold = 0;
    m_consecutiveBelowThreshold = 0;
    m_initialized = true;

    UZ_LOG(RHI, Info, "FramePacer initialized: target={:.2f}ms buffers={}-{} autoTune={}",
           config.targetFrameTimeMs, config.minSwapChainBuffers,
           config.maxSwapChainBuffers, config.autoTuneBufferCount);
    return true;
}

void FramePacer::Shutdown() {
    if (!m_initialized) {
        return;
    }

    m_frameTimeHistory.clear();
    m_initialized = false;

    UZ_LOG(RHI, Info, "FramePacer shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame timing
// ─────────────────────────────────────────────────────────────────────────────

void FramePacer::BeginFrame() {
    m_frameBegin = Clock::now();
}

void FramePacer::EndFrame() {
    UZ_PROFILE_SCOPE("FramePacer::EndFrame");

    if (!m_initialized) {
        return;
    }

    const auto frameEnd = Clock::now();
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(frameEnd - m_frameBegin).count();
    m_lastFrameTimeMs = static_cast<float>(ns) / 1'000'000.0f;

    // Update rolling history.
    m_frameTimeHistory[m_historyIndex] = m_lastFrameTimeMs;
    m_historyIndex = (m_historyIndex + 1) % static_cast<uint32_t>(m_frameTimeHistory.size());
    if (m_historyIndex == 0) {
        m_historyFull = true;
    }

    // Dropped frame detection.
    if (m_lastFrameTimeMs > m_config.targetFrameTimeMs) {
        ++m_droppedFrames;
        UZ_STAT_INCREMENT(Rendering, DroppedFrames);
    }

    // Buffer count auto-tuning with hysteresis.
    if (!m_config.autoTuneBufferCount) {
        return;
    }

    const float avgMs = GetAvgFrameTimeMs();

    if (avgMs > m_config.throughputThresholdMs) {
        ++m_consecutiveAboveThreshold;
        m_consecutiveBelowThreshold = 0;
    } else if (avgMs < m_config.latencyThresholdMs) {
        ++m_consecutiveBelowThreshold;
        m_consecutiveAboveThreshold = 0;
    } else {
        // In the dead zone — don't change.
        m_consecutiveAboveThreshold = 0;
        m_consecutiveBelowThreshold = 0;
    }

    // Only change recommendation after sustained threshold crossing.
    if (m_consecutiveAboveThreshold >= m_config.hysteresisFrames &&
        m_recommendedBufferCount < m_config.maxSwapChainBuffers) {
        m_recommendedBufferCount = m_config.maxSwapChainBuffers;
        m_consecutiveAboveThreshold = 0;
        UZ_STAT_INCREMENT(Rendering, SwapChainResizes);
        UZ_LOG(RHI, Info, "FramePacer: recommending {} buffers (avg={:.2f}ms > {:.1f}ms)",
               m_recommendedBufferCount, avgMs, m_config.throughputThresholdMs);
    } else if (m_consecutiveBelowThreshold >= m_config.hysteresisFrames &&
               m_recommendedBufferCount > m_config.minSwapChainBuffers) {
        m_recommendedBufferCount = m_config.minSwapChainBuffers;
        m_consecutiveBelowThreshold = 0;
        UZ_STAT_INCREMENT(Rendering, SwapChainResizes);
        UZ_LOG(RHI, Info, "FramePacer: recommending {} buffers (avg={:.2f}ms < {:.1f}ms)",
               m_recommendedBufferCount, avgMs, m_config.latencyThresholdMs);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

float FramePacer::GetLastFrameTimeMs() const {
    return m_lastFrameTimeMs;
}

float FramePacer::GetAvgFrameTimeMs() const {
    if (m_frameTimeHistory.empty()) {
        return 0.0f;
    }

    const uint32_t count = m_historyFull
        ? static_cast<uint32_t>(m_frameTimeHistory.size())
        : m_historyIndex;

    if (count == 0) {
        return 0.0f;
    }

    const float sum = std::accumulate(m_frameTimeHistory.begin(),
                                       m_frameTimeHistory.begin() + count,
                                       0.0f);
    return sum / static_cast<float>(count);
}

uint32_t FramePacer::GetDroppedFrameCount() const {
    return m_droppedFrames;
}

uint32_t FramePacer::GetRecommendedBufferCount() const {
    return m_recommendedBufferCount;
}

bool FramePacer::ShouldResizeSwapChain() const {
    return m_recommendedBufferCount != m_currentBufferCount;
}

void FramePacer::SetCurrentBufferCount(uint32_t count) {
    m_currentBufferCount = count;
    UZ_LOG(RHI, Debug, "FramePacer: current buffer count set to {}", count);
}

void FramePacer::LogSummary() const {
    UZ_LOG(RHI, Info,
           "FramePacer: last={:.2f}ms avg={:.2f}ms dropped={} buffers={}/{} recommended={}",
           m_lastFrameTimeMs, GetAvgFrameTimeMs(), m_droppedFrames,
           m_currentBufferCount, m_config.maxSwapChainBuffers, m_recommendedBufferCount);
}

} // namespace uz
