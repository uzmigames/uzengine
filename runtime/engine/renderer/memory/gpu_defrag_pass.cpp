/// GPU Defragmentation Pass implementation.
///
/// Currently monitoring-only — logs fragmentation state when threshold is
/// exceeded. Actual GPU resource relocation will be added when the RHI
/// exposes resource aliasing and copy queue integration.

#include "gpu_defrag_pass.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <utility>

namespace uz {

UZ_DEFINE_STAT(GPU, DefragPassRuns);

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool GpuDefragPass::Init(IRHIDevice* device, const GpuDefragConfig& config) {
    UZ_PROFILE_FUNCTION();

    if (!device) {
        UZ_LOG(Renderer, Error, "GpuDefragPass::Init: device is null");
        return false;
    }

    m_device = device;
    m_config = config;
    m_lastStats = {};
    m_totalAllocated = 0;
    m_totalUsed = 0;
    m_freeGapCount = 0;

    UZ_LOG(Renderer, Info,
           "GpuDefragPass initialized: maxBytesPerFrame={:.1f} MB, threshold={:.0f}%, enabled={}",
           static_cast<double>(m_config.maxBytesPerFrame) / (1024.0 * 1024.0),
           static_cast<double>(m_config.fragmentationThreshold) * 100.0,
           m_config.enabled);

    return true;
}

void GpuDefragPass::Shutdown() {
    UZ_PROFILE_FUNCTION();

    m_device = nullptr;
    m_config = {};
    m_lastStats = {};
    m_relocateCallback = nullptr;
    m_totalAllocated = 0;
    m_totalUsed = 0;
    m_freeGapCount = 0;

    UZ_LOG(Renderer, Info, "GpuDefragPass shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Configuration
// ─────────────────────────────────────────────────────────────────────────────

void GpuDefragPass::SetRelocateCallback(DefragRelocateCallback callback) {
    m_relocateCallback = std::move(callback);
}

// ─────────────────────────────────────────────────────────────────────────────
// Fragmentation Tracking
// ─────────────────────────────────────────────────────────────────────────────

void GpuDefragPass::UpdateFragmentationState(uint64_t totalAllocated, uint64_t totalUsed,
                                              uint32_t freeGapCount) {
    m_totalAllocated = totalAllocated;
    m_totalUsed = totalUsed;
    m_freeGapCount = freeGapCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// Execute
// ─────────────────────────────────────────────────────────────────────────────

GpuDefragStats GpuDefragPass::Execute() {
    UZ_PROFILE_FUNCTION();

    GpuDefragStats stats{};

    // Compute fragmentation ratio: wasted / total (0.0 = perfect, 1.0 = all waste)
    if (m_totalAllocated > 0) {
        const uint64_t wastedBytes = m_totalAllocated - m_totalUsed;
        stats.fragmentationRatio = static_cast<float>(
            static_cast<double>(wastedBytes) / static_cast<double>(m_totalAllocated));
    }

    if (!m_config.enabled) {
        stats.didRun = false;
        m_lastStats = stats;
        return stats;
    }

    if (stats.fragmentationRatio < m_config.fragmentationThreshold) {
        stats.didRun = false;
        m_lastStats = stats;
        return stats;
    }

    // Fragmentation exceeds threshold — log and mark as run.
    // Actual resource relocation is not yet implemented; this pass currently
    // serves as a monitoring probe that surfaces fragmentation metrics.
    stats.didRun = true;
    stats.bytesRelocated = 0;
    stats.resourcesRelocated = 0;

    UZ_STAT_INCREMENT(GPU, DefragPassRuns);

    UZ_LOG(Renderer, Warning,
           "GpuDefragPass: fragmentation {:.1f}% exceeds threshold {:.0f}% "
           "(allocated={:.1f} MB, used={:.1f} MB, gaps={})",
           static_cast<double>(stats.fragmentationRatio) * 100.0,
           static_cast<double>(m_config.fragmentationThreshold) * 100.0,
           static_cast<double>(m_totalAllocated) / (1024.0 * 1024.0),
           static_cast<double>(m_totalUsed) / (1024.0 * 1024.0),
           m_freeGapCount);

    m_lastStats = stats;
    return stats;
}

} // namespace uz
