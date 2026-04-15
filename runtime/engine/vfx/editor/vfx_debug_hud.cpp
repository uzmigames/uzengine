#include "vfx_debug_hud.h"

#include <algorithm>
#include <utility>

namespace uz {

void VfxDebugHudCollector::ReportEmitter(const VfxDebugHudEmitterSample& sample) {
    m_samples.push_back(sample);
}

void VfxDebugHudCollector::ReportEmitter(std::string emitterName,
                                          uint32_t aliveCount,
                                          uint32_t maxParticles,
                                          float simulationMs,
                                          float renderMs,
                                          uint32_t lodLevel,
                                          bool culled) {
    VfxDebugHudEmitterSample s;
    s.emitterName  = std::move(emitterName);
    s.aliveCount   = aliveCount;
    s.maxParticles = maxParticles;
    s.simulationMs = simulationMs;
    s.renderMs     = renderMs;
    s.lodLevel     = lodLevel;
    s.culled       = culled;
    m_samples.push_back(std::move(s));
}

VfxDebugHudView VfxDebugHudCollector::Aggregate(uint32_t topNCount) {
    VfxDebugHudView view;
    view.rows.reserve(m_samples.size());

    // Build rows + reduce totals.
    for (const auto& s : m_samples) {
        VfxDebugHudRow row;
        row.emitterName  = s.emitterName;
        row.aliveCount   = s.aliveCount;
        row.maxParticles = s.maxParticles;
        row.simulationMs = s.simulationMs;
        row.renderMs     = s.renderMs;
        row.totalMs      = s.simulationMs + s.renderMs;
        row.budgetPercent =
            (s.maxParticles > 0u)
                ? 100.0f * static_cast<float>(s.aliveCount) /
                      static_cast<float>(s.maxParticles)
                : 0.0f;
        row.lodLevel = s.lodLevel;
        row.culled   = s.culled;

        view.totalAliveParticles += s.aliveCount;
        view.totalBudget         += s.maxParticles;
        view.totalGpuMs          += row.totalMs;
        if (s.culled) ++view.culledCount;

        view.rows.push_back(std::move(row));
    }

    // Sort by total GPU cost descending.
    std::sort(view.rows.begin(), view.rows.end(),
               [](const VfxDebugHudRow& a, const VfxDebugHudRow& b) {
                   return a.totalMs > b.totalMs;
               });

    // Mark the top-N rows. Skip when topNCount is 0 (highlight disabled).
    const uint32_t topCount =
        std::min<uint32_t>(topNCount, static_cast<uint32_t>(view.rows.size()));
    for (uint32_t i = 0; i < topCount; ++i) {
        view.rows[i].isTopN = true;
    }

    // Clear the per-frame buffer — the collector is reused next frame.
    m_samples.clear();
    return view;
}

} // namespace uz
