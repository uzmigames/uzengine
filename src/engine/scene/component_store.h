#pragma once

// component_store.h — Generic, extensible component storage for the scene graph.
//
// Decouples component data from SceneNode. Each component type is registered
// at startup and gets a flat array (pool) indexed by NodeId. Nodes carry a
// 64-bit bitmask indicating which components are attached.
//
// Usage:
//   // At startup — register component types (order defines the bit index).
//   ComponentTypeId cid = component_store_register<CameraData>(&store);
//
//   // Attach a component to a node.
//   CameraData* cam = component_add<CameraData>(&store, nodeId);
//   cam->fov_y = 60.0f;
//
//   // Query a component.
//   CameraData* cam = component_get<CameraData>(&store, nodeId);
//
//   // Remove a component.
//   component_remove<CameraData>(&store, nodeId);
//
// Design:
//   - Flat arrays per-type → cache-friendly iteration.
//   - 64-bit mask → up to 64 component types (more than enough).
//   - Type-erased pools → no hardcoded component types in core headers.
//   - Type-safe templates for public API.

#include <cstdint>
#include <cstring>
#include <new>       // placement new

namespace uz {

using ComponentTypeId = uint16_t;

inline constexpr uint16_t kMaxComponentTypes = 64;
inline constexpr uint16_t kComponentTypeInvalid = 0xFFFF;

// Forward declaration — kSceneMaxNodes is defined in scene.h. We need it for pool sizing.
// To avoid circular deps, the pool allocation is deferred to init().
inline constexpr uint32_t kComponentPoolCapacity = 65536; // Must match kSceneMaxNodes.

// ============================================================================
// ComponentStore — type-erased pool storage
// ============================================================================

struct ComponentStore {
    /// Per-type storage pool: a flat array of `stride * capacity` bytes.
    struct Pool {
        uint32_t stride    = 0;       // sizeof(ComponentT)
        uint8_t* data      = nullptr; // heap-allocated: stride * kComponentPoolCapacity
    };

    Pool     pools[kMaxComponentTypes];
    uint64_t masks[kComponentPoolCapacity]; // Per-node bitmask of attached components
    uint16_t type_count = 0;

    // -- Lifecycle --------------------------------------------------------

    void init();
    void shutdown();

    // -- Type registration ------------------------------------------------

    /// Register a new component type. Returns the type ID (bit index).
    /// `stride` is sizeof(ComponentT). Returns kComponentTypeInvalid if full.
    ComponentTypeId register_type(uint32_t stride);

    // -- Per-node operations ----------------------------------------------

    /// Attach a component to a node. Returns pointer to the component data.
    /// The data is zero-initialized. Returns nullptr on invalid inputs.
    void* add(uint32_t node_id, ComponentTypeId type_id);

    /// Get a component pointer. Returns nullptr if not attached.
    void* get(uint32_t node_id, ComponentTypeId type_id);
    const void* get(uint32_t node_id, ComponentTypeId type_id) const;

    /// Check if a node has a component.
    bool has(uint32_t node_id, ComponentTypeId type_id) const;

    /// Remove a component from a node.
    void remove(uint32_t node_id, ComponentTypeId type_id);

    /// Remove ALL components from a node (called when destroying a node).
    void remove_all(uint32_t node_id);
};

// ============================================================================
// Static type ID registry (one unique ID per C++ type, across all stores)
// ============================================================================

namespace detail {

inline ComponentTypeId next_component_type_id() {
    static ComponentTypeId counter = 0;
    return counter++;
}

template<typename T>
ComponentTypeId component_type_id() {
    static const ComponentTypeId id = next_component_type_id();
    return id;
}

} // namespace detail

// ============================================================================
// Type-safe public API (templates)
// ============================================================================

/// Register a component type T with the store. Call once at startup per type.
/// Returns the type ID.
template<typename T>
ComponentTypeId component_store_register(ComponentStore* store) {
    const ComponentTypeId id = detail::component_type_id<T>();
    // Only register if not already done (idempotent).
    if (id < store->type_count && store->pools[id].stride > 0) {
        return id;
    }
    store->register_type(sizeof(T));
    return id;
}

/// Add a component of type T to a node. Returns a pointer to the component
/// (zero-initialized if new). Returns nullptr on failure.
template<typename T>
T* component_add(ComponentStore* store, uint32_t node_id) {
    void* raw = store->add(node_id, detail::component_type_id<T>());
    if (!raw) return nullptr;
    // Construct in-place with default values.
    return new (raw) T{};
}

/// Get a component of type T from a node. Returns nullptr if not attached.
template<typename T>
T* component_get(ComponentStore* store, uint32_t node_id) {
    return static_cast<T*>(store->get(node_id, detail::component_type_id<T>()));
}

/// Get a const component of type T.
template<typename T>
const T* component_get(const ComponentStore* store, uint32_t node_id) {
    return static_cast<const T*>(store->get(node_id, detail::component_type_id<T>()));
}

/// Check if a node has a component of type T.
template<typename T>
bool component_has(const ComponentStore* store, uint32_t node_id) {
    return store->has(node_id, detail::component_type_id<T>());
}

/// Remove a component of type T from a node.
template<typename T>
void component_remove(ComponentStore* store, uint32_t node_id) {
    store->remove(node_id, detail::component_type_id<T>());
}

} // namespace uz
