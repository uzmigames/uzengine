#include "frame_pacer.h"

/// Frame timing controller and VRR (Variable Refresh Rate) integration.
///
/// Manages frame budget, timing history, dropped frame detection, and sync mode
/// selection (VSync, adaptive sync, uncapped).

#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>
#include <numeric>

UZ_DECLARE_LOG_CATEGORY(Renderer);
UZ_DEFINE_STAT(Rendering, DroppedFrames);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool FramePacer::Init(IRHIDevice* device, const FramePacerConfig& config)
{
    if (m_initialized) {
        UZ_LOG(Renderer, Warning, "FramePacer::Init called when already initialized");
        return false;
    }
    if (!device) {
        UZ_LOG(Renderer, Error, "FramePacer::Init: device is null");
        return false;
    }

    m_device = device;
    m_config = config;
    m_stats = {};
    m_historyIndex = 0;

    // Pre-allocate history buffer with zeros.
    const uint32_t size = std::max(config.historySize, 1u);
    m_frameTimeHistory.assign(size, 0.0f);

    // Query the RHI device for variable refresh rate (tearing) support.
    m_vrrSupported = m_device && m_device->SupportsVariableRefreshRate();

    m_initialized = true;
    UZ_LOG(Renderer, Info, "FramePacer initialized: targetMs={:.2f}, vrrMode={}, swapInterval={}, historySize={}",
           m_config.targetFrameTimeMs,
           static_cast<int>(m_config.vrrMode),
           m_config.swapInterval,
           size);
    return true;
}

void FramePacer::Shutdown()
{
    if (!m_initialized) {
        return;
    }

    LogSummary();

    m_frameTimeHistory.clear();
    m_device = nullptr;
    m_initialized = false;

    UZ_LOG(Renderer, Info, "FramePacer shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-frame timing
// ─────────────────────────────────────────────────────────────────────────────

void FramePacer::BeginFrame()
{
    m_frameStart = std::chrono::high_resolution_clock::now();
}

void FramePacer::EndFrame()
{
    const auto now = std::chrono::high_resolution_clock::now();
    const float elapsedMs = std::chrono::duration<float, std::milli>(now - m_frameStart).count();

    m_stats.cpuFrameTimeMs = elapsedMs;
    m_stats.totalFrames++;

    // Detect missed target.
    if (elapsedMs > m_config.targetFrameTimeMs) {
        m_stats.droppedFrames++;
        UZ_STAT_INCREMENT(Rendering, DroppedFrames);
    }

    UpdateRollingAverage(elapsedMs);

    // Compute FPS from rolling average, guarding against zero.
    if (m_stats.avgFrameTimeMs > 0.0f) {
        m_stats.fps = 1000.0f / m_stats.avgFrameTimeMs;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Present helpers
// ─────────────────────────────────────────────────────────────────────────────

uint32_t FramePacer::GetPresentFlags() const
{
    // DXGI_PRESENT_ALLOW_TEARING = 0x200.
    constexpr uint32_t kAllowTearing = 0x200u;

    if (m_config.vrrMode == VRRMode::AllowTearing && m_vrrSupported) {
        return kAllowTearing;
    }
    return 0;
}

uint32_t FramePacer::GetSyncInterval() const
{
    // VRR with tearing requires sync interval 0.
    if (m_config.vrrMode == VRRMode::AllowTearing && m_vrrSupported) {
        return 0;
    }
    return m_config.swapInterval;
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics
// ─────────────────────────────────────────────────────────────────────────────

void FramePacer::LogSummary() const
{
    UZ_LOG(Renderer, Info,
           "FramePacer summary: totalFrames={}, droppedFrames={}, avgMs={:.2f}, fps={:.1f}",
           m_stats.totalFrames,
           m_stats.droppedFrames,
           m_stats.avgFrameTimeMs,
           m_stats.fps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal
// ─────────────────────────────────────────────────────────────────────────────

void FramePacer::UpdateRollingAverage(float frameTimeMs)
{
    if (m_frameTimeHistory.empty()) {
        return;
    }

    m_frameTimeHistory[m_historyIndex] = frameTimeMs;
    m_historyIndex = (m_historyIndex + 1) % static_cast<uint32_t>(m_frameTimeHistory.size());

    // Compute average over the filled portion of the history.
    const uint32_t count = std::min(static_cast<uint32_t>(m_stats.totalFrames),
                                    static_cast<uint32_t>(m_frameTimeHistory.size()));
    const float sum = std::accumulate(m_frameTimeHistory.begin(),
                                      m_frameTimeHistory.begin() + count,
                                      0.0f);
    m_stats.avgFrameTimeMs = sum / static_cast<float>(count);
}

} // namespace uz
