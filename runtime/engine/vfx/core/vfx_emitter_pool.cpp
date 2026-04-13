/// VFX Emitter Pool implementation -- pre-allocated GPU buffer slot recycling.

#include "vfx_emitter_pool.h"

#include "engine/logging/log.h"
#include "engine/rhi/rhi.h"

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Default tier distribution
// ─────────────────────────────────────────────────────────────────────────────

/// Default pool layout: 32 slots across 6 capacity tiers.
/// Covers common emitter sizes from small ambient particles (1K) to
/// large hero effects (64K).
static constexpr std::pair<uint32_t, uint32_t> kDefaultTiers[] = {
    {  1024, 8 },  // 8 small slots
    {  4096, 8 },  // 8 medium slots
    {  8192, 8 },  // 8 standard slots
    { 16384, 4 },  // 4 large slots
    { 32768, 2 },  // 2 very large slots
    { 65536, 2 },  // 2 max-size slots
};

// ─────────────────────────────────────────────────────────────────────────────
// Create / Destroy
// ─────────────────────────────────────────────────────────────────────────────

bool VFXEmitterPool::Create(IRHIDevice* device) {
    std::vector<std::pair<uint32_t, uint32_t>> tiers(
        std::begin(kDefaultTiers), std::end(kDefaultTiers));
    return CreateCustom(device, tiers);
}

bool VFXEmitterPool::CreateCustom(
    IRHIDevice* device,
    const std::vector<std::pair<uint32_t, uint32_t>>& tiers) {

    if (!device) {
        UZ_LOG(VFX, Error, "VFXEmitterPool::Create: device is null");
        return false;
    }

    // Count total slots.
    uint32_t totalSlots = 0;
    for (const auto& [capacity, count] : tiers) {
        totalSlots += count;
    }

    slots_.reserve(totalSlots);
    freeSlots_.reserve(totalSlots);

    for (const auto& [capacity, count] : tiers) {
        for (uint32_t i = 0; i < count; ++i) {
            PoolSlot slot;
            slot.capacity = capacity;
            slot.inUse = false;

            if (!slot.buffer.Create(device, capacity)) {
                UZ_LOG(VFX, Error,
                       "VFXEmitterPool: failed to allocate slot {} "
                       "(capacity={})",
                       slots_.size(), capacity);
                // Clean up all previously allocated slots.
                for (auto& s : slots_) {
                    s.buffer.Destroy(device);
                }
                slots_.clear();
                freeSlots_.clear();
                return false;
            }

            const uint32_t slotIndex = static_cast<uint32_t>(slots_.size());
            slots_.push_back(std::move(slot));
            freeSlots_.push_back(slotIndex);
        }
    }

    stats_ = {};
    stats_.totalSlots = totalSlots;

    UZ_LOG(VFX, Info,
           "VFXEmitterPool created: {} slots across {} tiers",
           totalSlots, tiers.size());
    return true;
}

void VFXEmitterPool::Destroy(IRHIDevice* device) {
    if (!device) { return; }

    for (auto& slot : slots_) {
        slot.buffer.Destroy(device);
    }
    slots_.clear();
    freeSlots_.clear();
    stats_ = {};
}

// ─────────────────────────────────────────────────────────────────────────────
// AcquireSlot
// ─────────────────────────────────────────────────────────────────────────────

GPUParticleBuffer* VFXEmitterPool::AcquireSlot(uint32_t requestedCapacity) {
    // Find the smallest free slot that fits the request.
    // Linear scan over the free list -- with 32 slots this is negligible.
    uint32_t bestFreeIdx = UINT32_MAX;
    uint32_t bestCapacity = UINT32_MAX;

    for (uint32_t i = 0; i < freeSlots_.size(); ++i) {
        const uint32_t slotIdx = freeSlots_[i];
        const uint32_t cap = slots_[slotIdx].capacity;

        if (cap >= requestedCapacity && cap < bestCapacity) {
            bestFreeIdx = i;
            bestCapacity = cap;

            // Exact match -- no point searching further.
            if (cap == requestedCapacity) { break; }
        }
    }

    if (bestFreeIdx == UINT32_MAX) {
        // No free slot large enough -- dynamic fallback.
        stats_.dynamicOverflowCount++;
        UZ_LOG(VFX, Warning,
               "VFXEmitterPool: no free slot for capacity {}; "
               "falling back to dynamic allocation (overflow #{})",
               requestedCapacity, stats_.dynamicOverflowCount);
        return nullptr;
    }

    // Remove from free list (swap with last element for O(1) removal).
    const uint32_t slotIdx = freeSlots_[bestFreeIdx];
    freeSlots_[bestFreeIdx] = freeSlots_.back();
    freeSlots_.pop_back();

    slots_[slotIdx].inUse = true;
    stats_.inUseSlots++;
    if (stats_.inUseSlots > stats_.peakInUse) {
        stats_.peakInUse = stats_.inUseSlots;
    }

    // Warn when pool is > 80% full.
    if (stats_.totalSlots > 0 &&
        stats_.inUseSlots > (stats_.totalSlots * 4 / 5)) {
        UZ_LOG(VFX, Warning,
               "VFXEmitterPool: pool is {}/{}  (>80%% full)",
               stats_.inUseSlots, stats_.totalSlots);
    }

    return &slots_[slotIdx].buffer;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReleaseSlot
// ─────────────────────────────────────────────────────────────────────────────

void VFXEmitterPool::ReleaseSlot(GPUParticleBuffer* buffer) {
    if (!buffer) { return; }

    // Find the slot that owns this buffer.
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        if (&slots_[i].buffer == buffer) {
            if (!slots_[i].inUse) {
                UZ_LOG(VFX, Warning,
                       "VFXEmitterPool::ReleaseSlot: slot {} already free",
                       i);
                return;
            }

            slots_[i].inUse = false;
            freeSlots_.push_back(i);
            stats_.inUseSlots--;

            // Reset the buffer state for reuse without freeing GPU memory.
            // The compute shader will re-initialize via UploadInitialState()
            // when the slot is next acquired. Mark for re-initialization.
            buffer->needsInitialization = true;
            buffer->hasCompletedCycle = false;

            return;
        }
    }

    UZ_LOG(VFX, Warning,
           "VFXEmitterPool::ReleaseSlot: buffer not found in pool "
           "(possible dynamic allocation)");
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats
// ─────────────────────────────────────────────────────────────────────────────

PoolStats VFXEmitterPool::GetStats() const noexcept {
    return stats_;
}

} // namespace uz
