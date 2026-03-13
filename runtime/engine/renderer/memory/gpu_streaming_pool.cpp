/// GpuStreamingPool implementation.
///
/// Manages mip-level streaming with LRU eviction for GPU textures.
/// Tracks per-mip residency state, processes load requests, and enforces
/// a configurable memory budget by evicting least-recently-used fine mips.

#include "gpu_streaming_pool.h"
#include "log.h"
#include "profiler.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(Renderer);

UZ_DEFINE_STAT(GPU, StreamingMipLoads);
UZ_DEFINE_STAT(GPU, StreamingMipEvictions);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MipResidencyStateName
// ─────────────────────────────────────────────────────────────────────────────

const char* MipResidencyStateName(MipResidencyState state) {
    switch (state) {
    case MipResidencyState::NotLoaded: return "NotLoaded";
    case MipResidencyState::Loading:   return "Loading";
    case MipResidencyState::Resident:  return "Resident";
    case MipResidencyState::Evicting:  return "Evicting";
    default:                           return "Unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool GpuStreamingPool::Init(IRHIDevice* device, uint64_t budgetBytes) {
    if (!device) {
        UZ_LOG(Renderer, Error, "GpuStreamingPool::Init: device is null");
        return false;
    }

    if (budgetBytes == 0) {
        UZ_LOG(Renderer, Error, "GpuStreamingPool::Init: budget must be > 0");
        return false;
    }

    m_device = device;
    m_budgetBytes = budgetBytes;
    m_residentBytes = 0;
    m_nextId = 0;
    m_textures.clear();

    UZ_LOG(Renderer, Info, "GpuStreamingPool initialized: budget={:.1f} MB",
           static_cast<double>(budgetBytes) / (1024.0 * 1024.0));
    return true;
}

void GpuStreamingPool::Shutdown() {
    if (!m_textures.empty()) {
        UZ_LOG(Renderer, Warning,
               "GpuStreamingPool::Shutdown: {} textures still registered ({:.1f} MB resident)",
               m_textures.size(),
               static_cast<double>(m_residentBytes) / (1024.0 * 1024.0));
    }

    m_textures.clear();
    m_residentBytes = 0;
    m_budgetBytes = 0;
    m_nextId = 0;
    m_device = nullptr;

    UZ_LOG(Renderer, Info, "GpuStreamingPool shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Register / Unregister
// ─────────────────────────────────────────────────────────────────────────────

StreamingTextureId GpuStreamingPool::Register(RHITextureHandle texture,
                                              uint32_t totalMipCount,
                                              const uint64_t* mipSizes,
                                              const char* debugName) {
    if (!texture.IsValid()) {
        UZ_LOG(Renderer, Error, "GpuStreamingPool::Register: invalid texture handle");
        return kInvalidStreamingTexture;
    }

    if (totalMipCount == 0) {
        UZ_LOG(Renderer, Error, "GpuStreamingPool::Register: totalMipCount must be >= 1");
        return kInvalidStreamingTexture;
    }

    if (!mipSizes) {
        UZ_LOG(Renderer, Error, "GpuStreamingPool::Register: mipSizes is null");
        return kInvalidStreamingTexture;
    }

    const StreamingTextureId id = m_nextId++;

    StreamingTextureEntry entry;
    entry.texture = texture;
    entry.debugName = debugName ? debugName : "";
    entry.totalMipCount = totalMipCount;
    entry.residentMipCount = 0;
    entry.desiredMipCount = 1; // At minimum, the coarsest mip should be desired.
    entry.residentSizeBytes = 0;
    entry.lastUsedFrame = 0;

    entry.mips.resize(totalMipCount);
    uint64_t totalSize = 0;
    for (uint32_t i = 0; i < totalMipCount; ++i) {
        entry.mips[i].state = MipResidencyState::NotLoaded;
        entry.mips[i].sizeBytes = mipSizes[i];
        entry.mips[i].lastUsedFrame = 0;
        totalSize += mipSizes[i];
    }
    entry.totalSizeBytes = totalSize;

    m_textures.emplace(id, std::move(entry));

    UZ_LOG(Renderer, Info,
           "GpuStreamingPool::Register: id={} name='{}' mips={} totalSize={:.1f} MB",
           id, debugName ? debugName : "<unnamed>", totalMipCount,
           static_cast<double>(totalSize) / (1024.0 * 1024.0));

    return id;
}

void GpuStreamingPool::Unregister(StreamingTextureId id) {
    const auto it = m_textures.find(id);
    if (it == m_textures.end()) {
        UZ_LOG(Renderer, Warning,
               "GpuStreamingPool::Unregister: id={} not found", id);
        return;
    }

    const auto& entry = it->second;

    // Subtract resident bytes before removal.
    if (entry.residentSizeBytes > m_residentBytes) {
        UZ_LOG(Renderer, Error,
               "GpuStreamingPool::Unregister: residentSizeBytes ({}) > pool total ({})",
               entry.residentSizeBytes, m_residentBytes);
        m_residentBytes = 0;
    } else {
        m_residentBytes -= entry.residentSizeBytes;
    }

    UZ_LOG(Renderer, Info,
           "GpuStreamingPool::Unregister: id={} name='{}' freed {:.1f} MB",
           id, entry.debugName,
           static_cast<double>(entry.residentSizeBytes) / (1024.0 * 1024.0));

    m_textures.erase(it);
}

// ─────────────────────────────────────────────────────────────────────────────
// SetDesiredMips / Touch / NotifyMipLoaded
// ─────────────────────────────────────────────────────────────────────────────

void GpuStreamingPool::SetDesiredMips(StreamingTextureId id, uint32_t desiredMipCount) {
    const auto it = m_textures.find(id);
    if (it == m_textures.end()) {
        return;
    }

    auto& entry = it->second;
    // Clamp to [1, totalMipCount]. At least the coarsest mip must be desired.
    entry.desiredMipCount = std::clamp(desiredMipCount, 1u, entry.totalMipCount);
}

void GpuStreamingPool::Touch(StreamingTextureId id, uint64_t frameNumber) {
    const auto it = m_textures.find(id);
    if (it == m_textures.end()) {
        return;
    }

    auto& entry = it->second;
    entry.lastUsedFrame = frameNumber;

    // Update per-mip timestamps for resident mips.
    for (auto& mip : entry.mips) {
        if (mip.state == MipResidencyState::Resident) {
            mip.lastUsedFrame = frameNumber;
        }
    }
}

void GpuStreamingPool::NotifyMipLoaded(StreamingTextureId id, uint32_t mipLevel) {
    const auto it = m_textures.find(id);
    if (it == m_textures.end()) {
        UZ_LOG(Renderer, Warning,
               "GpuStreamingPool::NotifyMipLoaded: id={} not found", id);
        return;
    }

    auto& entry = it->second;

    if (mipLevel >= entry.totalMipCount) {
        UZ_LOG(Renderer, Error,
               "GpuStreamingPool::NotifyMipLoaded: mip {} out of range (max {})",
               mipLevel, entry.totalMipCount - 1);
        return;
    }

    auto& mip = entry.mips[mipLevel];

    if (mip.state != MipResidencyState::Loading) {
        UZ_LOG(Renderer, Warning,
               "GpuStreamingPool::NotifyMipLoaded: mip {} of '{}' in state {} (expected Loading)",
               mipLevel, entry.debugName, MipResidencyStateName(mip.state));
        return;
    }

    mip.state = MipResidencyState::Resident;
    entry.residentSizeBytes += mip.sizeBytes;
    m_residentBytes += mip.sizeBytes;

    // Recount resident mips from coarsest.
    uint32_t count = 0;
    for (uint32_t i = entry.totalMipCount; i > 0; --i) {
        if (entry.mips[i - 1].state == MipResidencyState::Resident) {
            ++count;
        } else {
            break;
        }
    }
    entry.residentMipCount = count;

    UZ_STAT_INCREMENT(GPU, StreamingMipLoads);
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — per-frame streaming logic
// ─────────────────────────────────────────────────────────────────────────────

void GpuStreamingPool::Update(uint64_t frameNumber, uint32_t maxUploadsPerFrame) {
    uint32_t uploadsThisFrame = 0;

    // Load pass: for each texture needing more mips, mark the next mip as Loading.
    // We load from coarsest to finest (high index to low index).
    for (auto& [id, entry] : m_textures) {
        if (uploadsThisFrame >= maxUploadsPerFrame) {
            break;
        }

        if (entry.desiredMipCount <= entry.residentMipCount) {
            continue; // Already has enough mips loaded.
        }

        // Find the next mip to load: the coarsest NotLoaded mip.
        // Mips load from coarsest (totalMipCount-1) toward finest (0).
        for (uint32_t i = entry.totalMipCount; i > 0 && uploadsThisFrame < maxUploadsPerFrame; --i) {
            const uint32_t mipIdx = i - 1;
            auto& mip = entry.mips[mipIdx];

            if (mip.state == MipResidencyState::NotLoaded) {
                mip.state = MipResidencyState::Loading;
                ++uploadsThisFrame;

                UZ_LOG(Renderer, Trace,
                       "GpuStreamingPool: loading mip {} of '{}' ({} bytes)",
                       mipIdx, entry.debugName, mip.sizeBytes);
            }
        }
    }

    // Eviction pass: if over budget, evict LRU mips.
    if (m_residentBytes > m_budgetBytes) {
        const uint64_t excess = m_residentBytes - m_budgetBytes;
        const uint64_t freed = EvictLRU(excess);

        UZ_LOG(Renderer, Info,
               "GpuStreamingPool::Update: over budget by {:.1f} MB, freed {:.1f} MB",
               static_cast<double>(excess) / (1024.0 * 1024.0),
               static_cast<double>(freed) / (1024.0 * 1024.0));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// EvictLRU — evict finest mips of least-recently-used textures
// ─────────────────────────────────────────────────────────────────────────────

uint64_t GpuStreamingPool::EvictLRU(uint64_t bytesToFree) {
    if (bytesToFree == 0) {
        return 0;
    }

    const auto sorted = GetLRUSortedTextures();
    uint64_t totalFreed = 0;

    for (const auto texId : sorted) {
        if (totalFreed >= bytesToFree) {
            break;
        }

        auto it = m_textures.find(texId);
        if (it == m_textures.end()) {
            continue;
        }

        auto& entry = it->second;

        // Evict finest resident mips first (low index = fine).
        // Never evict the coarsest mip (index totalMipCount-1) so textures
        // always have at least a tiny fallback.
        for (uint32_t mipIdx = 0; mipIdx < entry.totalMipCount - 1; ++mipIdx) {
            if (totalFreed >= bytesToFree) {
                break;
            }

            auto& mip = entry.mips[mipIdx];
            if (mip.state != MipResidencyState::Resident) {
                continue;
            }

            mip.state = MipResidencyState::NotLoaded;
            const uint64_t freed = mip.sizeBytes;

            if (freed > entry.residentSizeBytes) {
                entry.residentSizeBytes = 0;
            } else {
                entry.residentSizeBytes -= freed;
            }

            if (freed > m_residentBytes) {
                m_residentBytes = 0;
            } else {
                m_residentBytes -= freed;
            }

            totalFreed += freed;

            // Recount resident mips from coarsest.
            uint32_t count = 0;
            for (uint32_t j = entry.totalMipCount; j > 0; --j) {
                if (entry.mips[j - 1].state == MipResidencyState::Resident) {
                    ++count;
                } else {
                    break;
                }
            }
            entry.residentMipCount = count;

            UZ_STAT_INCREMENT(GPU, StreamingMipEvictions);

            UZ_LOG(Renderer, Trace,
                   "GpuStreamingPool: evicted mip {} of '{}' ({} bytes)",
                   mipIdx, entry.debugName, freed);
        }
    }

    return totalFreed;
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

const StreamingTextureEntry* GpuStreamingPool::GetEntry(StreamingTextureId id) const {
    const auto it = m_textures.find(id);
    if (it == m_textures.end()) {
        return nullptr;
    }
    return &it->second;
}

std::vector<StreamingTextureId> GpuStreamingPool::GetLRUSortedTextures() const {
    std::vector<StreamingTextureId> ids;
    ids.reserve(m_textures.size());

    for (const auto& [id, entry] : m_textures) {
        ids.push_back(id);
    }

    std::sort(ids.begin(), ids.end(), [this](StreamingTextureId a, StreamingTextureId b) {
        const auto itA = m_textures.find(a);
        const auto itB = m_textures.find(b);
        return itA->second.lastUsedFrame < itB->second.lastUsedFrame;
    });

    return ids;
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics
// ─────────────────────────────────────────────────────────────────────────────

void GpuStreamingPool::LogSummary() const {
    constexpr double kMB = 1024.0 * 1024.0;

    UZ_LOG(Renderer, Info, "=== GPU Streaming Pool Summary ===");
    UZ_LOG(Renderer, Info, "  Budget: {:.1f} MB", static_cast<double>(m_budgetBytes) / kMB);
    UZ_LOG(Renderer, Info, "  Resident: {:.1f} MB ({:.0f}%)",
           static_cast<double>(m_residentBytes) / kMB,
           (m_budgetBytes > 0)
               ? 100.0 * static_cast<double>(m_residentBytes) / static_cast<double>(m_budgetBytes)
               : 0.0);
    UZ_LOG(Renderer, Info, "  Textures: {}", m_textures.size());

    for (const auto& [id, entry] : m_textures) {
        UZ_LOG(Renderer, Info,
               "    [{}] '{}': {}/{} mips resident, {:.1f}/{:.1f} MB",
               id, entry.debugName,
               entry.residentMipCount, entry.totalMipCount,
               static_cast<double>(entry.residentSizeBytes) / kMB,
               static_cast<double>(entry.totalSizeBytes) / kMB);
    }
}

} // namespace uz
