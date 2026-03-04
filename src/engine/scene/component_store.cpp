// component_store.cpp — Implementation of the generic component storage.

#include "engine/scene/component_store.h"

#include <cstdlib>   // malloc, free
#include <cstring>   // memset

namespace uz {

// ============================================================================
// Lifecycle
// ============================================================================

void ComponentStore::init() {
    std::memset(masks, 0, sizeof(masks));
    type_count = 0;
    for (auto& pool : pools) {
        pool.stride = 0;
        pool.data   = nullptr;
    }
}

void ComponentStore::shutdown() {
    for (auto& pool : pools) {
        if (pool.data) {
            std::free(pool.data);
            pool.data = nullptr;
        }
        pool.stride = 0;
    }
    type_count = 0;
    std::memset(masks, 0, sizeof(masks));
}

// ============================================================================
// Type registration
// ============================================================================

ComponentTypeId ComponentStore::register_type(uint32_t stride) {
    if (type_count >= kMaxComponentTypes) {
        return kComponentTypeInvalid;
    }

    const ComponentTypeId id = type_count;

    // Allocate the pool. Zero-initialize for clean default state.
    const size_t pool_size = static_cast<size_t>(stride) * kComponentPoolCapacity;
    auto* data = static_cast<uint8_t*>(std::malloc(pool_size));
    if (!data) {
        return kComponentTypeInvalid;
    }
    std::memset(data, 0, pool_size);

    pools[id].stride = stride;
    pools[id].data   = data;
    ++type_count;

    return id;
}

// ============================================================================
// Per-node operations
// ============================================================================

void* ComponentStore::add(uint32_t node_id, ComponentTypeId type_id) {
    if (type_id >= type_count || node_id == 0 || node_id >= kComponentPoolCapacity) {
        return nullptr;
    }

    const Pool& pool = pools[type_id];
    if (!pool.data) return nullptr;

    // Set the bit in the node's mask.
    masks[node_id] |= (1ULL << type_id);

    // Return pointer to the component slot.
    return pool.data + static_cast<size_t>(pool.stride) * node_id;
}

void* ComponentStore::get(uint32_t node_id, ComponentTypeId type_id) {
    if (type_id >= type_count || node_id == 0 || node_id >= kComponentPoolCapacity) {
        return nullptr;
    }
    if (!(masks[node_id] & (1ULL << type_id))) {
        return nullptr;
    }
    return pools[type_id].data + static_cast<size_t>(pools[type_id].stride) * node_id;
}

const void* ComponentStore::get(uint32_t node_id, ComponentTypeId type_id) const {
    if (type_id >= type_count || node_id == 0 || node_id >= kComponentPoolCapacity) {
        return nullptr;
    }
    if (!(masks[node_id] & (1ULL << type_id))) {
        return nullptr;
    }
    return pools[type_id].data + static_cast<size_t>(pools[type_id].stride) * node_id;
}

bool ComponentStore::has(uint32_t node_id, ComponentTypeId type_id) const {
    if (type_id >= type_count || node_id == 0 || node_id >= kComponentPoolCapacity) {
        return false;
    }
    return (masks[node_id] & (1ULL << type_id)) != 0;
}

void ComponentStore::remove(uint32_t node_id, ComponentTypeId type_id) {
    if (type_id >= type_count || node_id == 0 || node_id >= kComponentPoolCapacity) {
        return;
    }

    // Clear the bit.
    masks[node_id] &= ~(1ULL << type_id);

    // Zero out the component data so it's clean if re-added.
    const Pool& pool = pools[type_id];
    if (pool.data) {
        std::memset(pool.data + static_cast<size_t>(pool.stride) * node_id,
                    0, pool.stride);
    }
}

void ComponentStore::remove_all(uint32_t node_id) {
    if (node_id == 0 || node_id >= kComponentPoolCapacity) {
        return;
    }

    const uint64_t mask = masks[node_id];
    if (mask == 0) return;

    // Zero out each component's data.
    for (uint16_t t = 0; t < type_count; ++t) {
        if (mask & (1ULL << t)) {
            const Pool& pool = pools[t];
            if (pool.data) {
                std::memset(pool.data + static_cast<size_t>(pool.stride) * node_id,
                            0, pool.stride);
            }
        }
    }

    masks[node_id] = 0;
}

} // namespace uz
