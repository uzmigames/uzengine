/// GpuMemoryTracker implementation.
///
/// Tracks per-category GPU memory allocations with budget enforcement,
/// pressure callbacks, and telemetry integration via UZ_STAT counters.

#include "gpu_memory_tracker.h"
#include "rhi.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(Renderer);

UZ_DEFINE_STAT(GPU, MemoryAllocations);
UZ_DEFINE_STAT(GPU, MemoryFrees);
UZ_DEFINE_STAT(GPU, MemoryPressureEvents);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// GpuMemoryCategoryName
// ─────────────────────────────────────────────────────────────────────────────

const char* GpuMemoryCategoryName(GpuMemoryCategory cat) {
    switch (cat) {
    case GpuMemoryCategory::Textures:     return "Textures";
    case GpuMemoryCategory::RenderTargets: return "RenderTargets";
    case GpuMemoryCategory::Buffers:      return "Buffers";
    case GpuMemoryCategory::Staging:      return "Staging";
    default:                              return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool GpuMemoryTracker::Init(IRHIDevice* device) {
    if (!device) {
        UZ_LOG(Renderer, Error, "GpuMemoryTracker::Init: device is null");
        return false;
    }

    m_device = device;

    for (auto& cat : m_categories) {
        cat.stats = {};
        cat.allocations.clear();
    }

    UZ_LOG(Renderer, Info, "GpuMemoryTracker initialized");
    return true;
}

void GpuMemoryTracker::Shutdown() {
    // Warn about leaked allocations
    for (size_t i = 0; i < static_cast<size_t>(GpuMemoryCategory::Count); ++i) {
        const auto& cat = m_categories[i];
        if (!cat.allocations.empty()) {
            const auto catEnum = static_cast<GpuMemoryCategory>(i);
            UZ_LOG(Renderer, Warning,
                   "GpuMemoryTracker::Shutdown: {} has {} leaked allocations ({} bytes)",
                   GpuMemoryCategoryName(catEnum),
                   cat.allocations.size(),
                   cat.stats.usedBytes);
        }
    }

    for (auto& cat : m_categories) {
        cat.stats = {};
        cat.allocations.clear();
    }

    m_pressureCallback = nullptr;
    m_device = nullptr;

    UZ_LOG(Renderer, Info, "GpuMemoryTracker shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Budget
// ─────────────────────────────────────────────────────────────────────────────

void GpuMemoryTracker::SetBudget(GpuMemoryCategory category, uint64_t budgetBytes) {
    const auto idx = static_cast<size_t>(category);
    if (idx >= static_cast<size_t>(GpuMemoryCategory::Count)) {
        UZ_LOG(Renderer, Error, "GpuMemoryTracker::SetBudget: invalid category {}", idx);
        return;
    }

    m_categories[idx].stats.budgetBytes = budgetBytes;

    UZ_LOG(Renderer, Info, "GpuMemoryTracker: {} budget set to {} MB",
           GpuMemoryCategoryName(category),
           budgetBytes / (1024 * 1024));
}

// ─────────────────────────────────────────────────────────────────────────────
// TrackAllocation / TrackFree
// ─────────────────────────────────────────────────────────────────────────────

void GpuMemoryTracker::TrackAllocation(GpuMemoryCategory category, uint64_t key,
                                       uint64_t sizeBytes) {
    const auto idx = static_cast<size_t>(category);
    if (idx >= static_cast<size_t>(GpuMemoryCategory::Count)) {
        UZ_LOG(Renderer, Error, "GpuMemoryTracker::TrackAllocation: invalid category {}", idx);
        return;
    }

    auto& cat = m_categories[idx];

    // Check for duplicate key
    if (cat.allocations.count(key) > 0) {
        UZ_LOG(Renderer, Warning,
               "GpuMemoryTracker::TrackAllocation: duplicate key 0x{:X} in {} ({} bytes)",
               key, GpuMemoryCategoryName(category), sizeBytes);
        return;
    }

    cat.allocations[key] = AllocationEntry{sizeBytes};

    auto& stats = cat.stats;
    stats.usedBytes += sizeBytes;
    stats.allocationCount++;
    stats.peakUsedBytes = std::max(stats.peakUsedBytes, stats.usedBytes);
    stats.totalAllocatedBytes += sizeBytes;

    UZ_STAT_INCREMENT(GPU, MemoryAllocations);
}

void GpuMemoryTracker::TrackFree(GpuMemoryCategory category, uint64_t key) {
    const auto idx = static_cast<size_t>(category);
    if (idx >= static_cast<size_t>(GpuMemoryCategory::Count)) {
        UZ_LOG(Renderer, Error, "GpuMemoryTracker::TrackFree: invalid category {}", idx);
        return;
    }

    auto& cat = m_categories[idx];
    const auto it = cat.allocations.find(key);

    if (it == cat.allocations.end()) {
        UZ_LOG(Renderer, Warning,
               "GpuMemoryTracker::TrackFree: key 0x{:X} not found in {}",
               key, GpuMemoryCategoryName(category));
        return;
    }

    const uint64_t sizeBytes = it->second.sizeBytes;
    cat.allocations.erase(it);

    auto& stats = cat.stats;
    stats.usedBytes -= sizeBytes;
    stats.allocationCount--;
    stats.totalFreedBytes += sizeBytes;

    UZ_STAT_INCREMENT(GPU, MemoryFrees);
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

GpuMemoryCategoryStats GpuMemoryTracker::GetCategoryStats(GpuMemoryCategory category) const {
    const auto idx = static_cast<size_t>(category);
    if (idx >= static_cast<size_t>(GpuMemoryCategory::Count)) {
        return {};
    }
    return m_categories[idx].stats;
}

GpuMemorySnapshot GpuMemoryTracker::TakeSnapshot() const {
    GpuMemorySnapshot snapshot = {};

    for (size_t i = 0; i < static_cast<size_t>(GpuMemoryCategory::Count); ++i) {
        snapshot.categories[i] = m_categories[i].stats;
        snapshot.totalUsedBytes += m_categories[i].stats.usedBytes;
    }

    if (m_device) {
        const RHIMemoryBudget budget = m_device->GetMemoryBudget();
        snapshot.deviceBudgetBytes = budget.budgetBytes;
        snapshot.deviceUsageBytes = budget.usageBytes;
        snapshot.budgetUtilization =
            (budget.budgetBytes > 0)
                ? static_cast<float>(budget.usageBytes) / static_cast<float>(budget.budgetBytes)
                : 0.0f;
    }

    return snapshot;
}

// ─────────────────────────────────────────────────────────────────────────────
// Pressure
// ─────────────────────────────────────────────────────────────────────────────

void GpuMemoryTracker::SetPressureCallback(GpuMemoryPressureCallback callback) {
    m_pressureCallback = std::move(callback);
}

void GpuMemoryTracker::CheckPressure() {
    if (!m_pressureCallback) {
        return;
    }

    for (size_t i = 0; i < static_cast<size_t>(GpuMemoryCategory::Count); ++i) {
        const auto& stats = m_categories[i].stats;
        if (stats.budgetBytes > 0 && stats.usedBytes > stats.budgetBytes) {
            const uint64_t overBudget = stats.usedBytes - stats.budgetBytes;
            const auto catEnum = static_cast<GpuMemoryCategory>(i);

            UZ_STAT_INCREMENT(GPU, MemoryPressureEvents);

            m_pressureCallback(catEnum, overBudget);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics
// ─────────────────────────────────────────────────────────────────────────────

void GpuMemoryTracker::LogSummary() const {
    constexpr uint64_t kMB = 1024 * 1024;

    UZ_LOG(Renderer, Info, "=== GPU Memory Summary ===");

    for (size_t i = 0; i < static_cast<size_t>(GpuMemoryCategory::Count); ++i) {
        const auto& stats = m_categories[i].stats;
        const auto catEnum = static_cast<GpuMemoryCategory>(i);
        const char* name = GpuMemoryCategoryName(catEnum);

        if (stats.budgetBytes > 0) {
            UZ_LOG(Renderer, Info,
                   "  {}: {:.1f}/{:.1f} MB ({} allocs, peak {:.1f} MB)",
                   name,
                   static_cast<double>(stats.usedBytes) / kMB,
                   static_cast<double>(stats.budgetBytes) / kMB,
                   stats.allocationCount,
                   static_cast<double>(stats.peakUsedBytes) / kMB);
        } else {
            UZ_LOG(Renderer, Info,
                   "  {}: {:.1f} MB / unlimited ({} allocs, peak {:.1f} MB)",
                   name,
                   static_cast<double>(stats.usedBytes) / kMB,
                   stats.allocationCount,
                   static_cast<double>(stats.peakUsedBytes) / kMB);
        }
    }

    if (m_device) {
        const RHIMemoryBudget budget = m_device->GetMemoryBudget();
        UZ_LOG(Renderer, Info,
               "  Device: {:.1f}/{:.1f} MB ({:.0f}% utilization)",
               static_cast<double>(budget.usageBytes) / kMB,
               static_cast<double>(budget.budgetBytes) / kMB,
               (budget.budgetBytes > 0)
                   ? 100.0 * static_cast<double>(budget.usageBytes) / static_cast<double>(budget.budgetBytes)
                   : 0.0);
    }
}

} // namespace uz
