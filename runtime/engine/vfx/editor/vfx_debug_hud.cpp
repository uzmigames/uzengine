#include "vfx_debug_hud.h"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace uz {

const char* VfxDebugHudStageName(VfxDebugHudStage stage) noexcept {
    switch (stage) {
        case VfxDebugHudStage::Spawn:  return "Spawn";
        case VfxDebugHudStage::Update: return "Update";
        case VfxDebugHudStage::Render: return "Render";
    }
    return "Unknown";
}

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

void VfxDebugHudCollector::ReportModule(const VfxDebugHudModuleSample& sample) {
    m_moduleSamples.push_back(sample);
}

void VfxDebugHudCollector::ReportModule(std::string emitterName,
                                         std::string moduleName,
                                         VfxDebugHudStage stage,
                                         float gpuMs) {
    VfxDebugHudModuleSample s;
    s.emitterName = std::move(emitterName);
    s.moduleName  = std::move(moduleName);
    s.stage       = stage;
    s.gpuMs       = gpuMs;
    m_moduleSamples.push_back(std::move(s));
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

    // ── Module breakdown attach (phase 8.5.0) ────────────────────────────
    // Group module samples by (emitter, module, stage) accumulating gpuMs so
    // modules that dispatch multiple times per frame collapse into a single
    // entry. Then attach the sorted, percent-annotated breakdown onto the
    // matching row. Samples with no matching emitter row are discarded (the
    // renderer wrapped a module but forgot to ReportEmitter, or the emitter
    // was culled and skipped emitter reporting).
    //
    // Keyed by "emitter\0module\0stage" so collisions are impossible.
    struct Key {
        std::string   emitter;
        std::string   module;
        VfxDebugHudStage stage;
        bool operator==(const Key& o) const noexcept {
            return emitter == o.emitter && module == o.module && stage == o.stage;
        }
    };
    struct KeyHash {
        size_t operator()(const Key& k) const noexcept {
            // FNV-1a — cheap, collision-resistant enough for a single frame.
            size_t h = 1469598103934665603ull;
            auto mix = [&](const std::string& s) {
                for (char c : s) { h ^= static_cast<unsigned char>(c); h *= 1099511628211ull; }
                h ^= '\0'; h *= 1099511628211ull;
            };
            mix(k.emitter); mix(k.module);
            h ^= static_cast<uint8_t>(k.stage); h *= 1099511628211ull;
            return h;
        }
    };
    std::unordered_map<Key, float, KeyHash> grouped;
    grouped.reserve(m_moduleSamples.size());
    for (const auto& ms : m_moduleSamples) {
        grouped[Key{ms.emitterName, ms.moduleName, ms.stage}] += ms.gpuMs;
    }

    // Attach grouped entries to rows by emitter name.
    for (auto& kv : grouped) {
        const auto& key = kv.first;
        auto it = std::find_if(view.rows.begin(), view.rows.end(),
                               [&](const VfxDebugHudRow& r) {
                                   return r.emitterName == key.emitter;
                               });
        if (it == view.rows.end()) continue; // orphan module sample

        VfxDebugHudModuleEntry entry;
        entry.moduleName = key.module;
        entry.stage      = key.stage;
        entry.gpuMs      = kv.second;
        it->moduleBreakdown.entries.push_back(std::move(entry));
        it->moduleBreakdown.totalMs += kv.second;
    }

    // Sort each breakdown by gpuMs desc and compute per-entry percent of
    // the enclosing row's total GPU time (not of the breakdown total — the
    // user wants to see "this module is 40% of this emitter").
    for (auto& row : view.rows) {
        if (row.moduleBreakdown.entries.empty()) continue;
        std::sort(row.moduleBreakdown.entries.begin(),
                   row.moduleBreakdown.entries.end(),
                   [](const VfxDebugHudModuleEntry& a,
                       const VfxDebugHudModuleEntry& b) {
                       return a.gpuMs > b.gpuMs;
                   });
        if (row.totalMs > 0.0f) {
            for (auto& e : row.moduleBreakdown.entries) {
                e.percentOfEmitterTotal = 100.0f * e.gpuMs / row.totalMs;
            }
        }
    }

    // Clear the per-frame buffer — the collector is reused next frame.
    m_samples.clear();
    m_moduleSamples.clear();
    return view;
}

} // namespace uz
