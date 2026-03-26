/// DecalPool implementation -- object pool for DecalComponent instances.

#include "engine/vfx/decals/decal_pool.h"

#include "engine/logging/log.h"

#include <algorithm>
#include <cstddef>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

DecalPool::DecalPool(uint32_t poolSize)
    : poolSize_(poolSize) {
    if (poolSize == 0) {
        UZ_LOG(VFX, Warning, "DecalPool created with zero capacity");
        return;
    }

    // Pre-allocate the backing store with default-constructed components.
    backingStore_.resize(poolSize);

    // Build the free list with all indices. We push in reverse order so that
    // AcquireDecal() pops index 0 first (natural ascending order).
    freeList_.reserve(poolSize);
    for (uint32_t i = poolSize; i > 0; --i) {
        freeList_.push_back(i - 1);
    }

    // Reserve active list to avoid reallocation during gameplay.
    activeDecals_.reserve(poolSize);

    UZ_LOG(VFX, Info, "DecalPool created: capacity={}", poolSize);
}

// ─────────────────────────────────────────────────────────────────────────────
// Move semantics
// ─────────────────────────────────────────────────────────────────────────────

DecalPool::DecalPool(DecalPool&& other) noexcept
    : backingStore_(std::move(other.backingStore_)),
      freeList_(std::move(other.freeList_)),
      activeDecals_(std::move(other.activeDecals_)),
      poolSize_(other.poolSize_) {
    other.poolSize_ = 0;
}

DecalPool& DecalPool::operator=(DecalPool&& other) noexcept {
    if (this != &other) {
        backingStore_ = std::move(other.backingStore_);
        freeList_ = std::move(other.freeList_);
        activeDecals_ = std::move(other.activeDecals_);
        poolSize_ = other.poolSize_;
        other.poolSize_ = 0;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// AcquireDecal -- pop from free list, reset, add to active list
// ─────────────────────────────────────────────────────────────────────────────

DecalComponent* DecalPool::AcquireDecal() {
    if (freeList_.empty()) {
        UZ_LOG(VFX, Warning,
               "DecalPool exhausted: all {} slots in use, cannot acquire",
               poolSize_);
        return nullptr;
    }

    const uint32_t index = freeList_.back();
    freeList_.pop_back();

    // Reset to a clean default state so no stale data leaks from prior use.
    backingStore_[index] = DecalComponent{};

    DecalComponent* decal = &backingStore_[index];
    activeDecals_.push_back(decal);

    return decal;
}

// ─────────────────────────────────────────────────────────────────────────────
// ReturnDecal -- validate, remove from active, push to free list
// ─────────────────────────────────────────────────────────────────────────────

void DecalPool::ReturnDecal(DecalComponent* decal) {
    if (decal == nullptr) {
        UZ_LOG(VFX, Error, "DecalPool::ReturnDecal called with nullptr");
        return;
    }

    // Validate that the pointer falls within our backing store's memory range.
    const DecalComponent* storeBegin = backingStore_.data();
    const DecalComponent* storeEnd = storeBegin + backingStore_.size();

    if (decal < storeBegin || decal >= storeEnd) {
        UZ_LOG(VFX, Error,
               "DecalPool::ReturnDecal: pointer does not belong to this pool "
               "(ptr={}, range=[{}, {}))",
               static_cast<const void*>(decal),
               static_cast<const void*>(storeBegin),
               static_cast<const void*>(storeEnd));
        return;
    }

    // Compute the index from the pointer offset.
    const auto index = static_cast<uint32_t>(decal - storeBegin);

    // Remove from the active list. Use swap-and-pop for O(1) removal since
    // the active list order is unspecified.
    auto it = std::find(activeDecals_.begin(), activeDecals_.end(), decal);
    if (it == activeDecals_.end()) {
        UZ_LOG(VFX, Warning,
               "DecalPool::ReturnDecal: decal at index {} is not in the active "
               "list (possible double-return)",
               index);
        return;
    }

    // Swap-and-pop: move the last element into the erased slot.
    *it = activeDecals_.back();
    activeDecals_.pop_back();

    // Reset the component to defaults before returning to the free list.
    backingStore_[index] = DecalComponent{};

    freeList_.push_back(index);
}

// ─────────────────────────────────────────────────────────────────────────────
// Clear -- return all active decals to the free list
// ─────────────────────────────────────────────────────────────────────────────

void DecalPool::Clear() {
    const uint32_t returned = static_cast<uint32_t>(activeDecals_.size());

    activeDecals_.clear();

    // Reset all components to defaults and rebuild the full free list.
    freeList_.clear();
    for (uint32_t i = poolSize_; i > 0; --i) {
        backingStore_[i - 1] = DecalComponent{};
        freeList_.push_back(i - 1);
    }

    if (returned > 0) {
        UZ_LOG(VFX, Info, "DecalPool::Clear: returned {} decals to pool", returned);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

uint32_t DecalPool::GetActiveCount() const noexcept {
    return static_cast<uint32_t>(activeDecals_.size());
}

uint32_t DecalPool::GetAvailableCount() const noexcept {
    return static_cast<uint32_t>(freeList_.size());
}

uint32_t DecalPool::GetPoolSize() const noexcept {
    return poolSize_;
}

const std::vector<DecalComponent*>& DecalPool::GetActiveDecals() const noexcept {
    return activeDecals_;
}

} // namespace uz
